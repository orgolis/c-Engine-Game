#pragma once
#include <cmath>
// ============================================================================
// ImportedSkinnedActor — a rigged character loaded from a .gltf/.glb and driven
// through the SAME proven path as SkinnedAnimDemo (skeleton pose → GPU compute
// skin → DrawItem into the normal G-buffer/shadow/cull path), but from a real
// asset instead of a procedural rig. Layer 1 of "Path B" (rigged-asset import).
//
// It draws with the asset's own material (base colour, roughness/metal and
// normal textures and their factors), and plays clips the way a character
// needs them:
//   - by name ("Walk" finds "01_Walk"), looping or once (a once clip holds
//     its last frame: a death stays down);
//   - crossfaded: a new clip blends in over a fraction of a second instead of
//     snapping;
//   - layered: an upper-body clip (aim, fire, reload) on the bones under one
//     bone (the spine) while the legs walk or run.
// Bones can be found by name (the loader appends "__<node>" to each, so a
// name matches its prefix) and placed in the world -- for a gun in a hand.
// ============================================================================
#include "vulkan/skinned_gltf_loader.h"
#include "vulkan/vulkan_skinned_mesh.h"
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/vulkan_texture.h"
#include "vulkan/vulkan_texture_manager.h"
#include "vulkan/vulkan_render_graph.h"   // DrawItem

#include "skeleton.h"
#include "two_bone_ik.h"
#include "foot_placement.h"
#include "pose_layers.h"
#include "ragdoll_rig.h"
#include "locomotion_warp.h"
#include "anim_graph_runtime.h"
#include "anim_hooks.h"   // inertialization and clip events (no GPU: animhooks_check)
#include "animation.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <sstream>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::editor {

/// One rigged file, parsed and its textures uploaded ONCE for every actor that
/// shows it: twenty soldiers of one kind parse one file, not twenty (each
/// takes a fifth of a second and its own copy of the textures). Each actor
/// keeps its own skeleton (its pose) and its own skinned GPU mesh.
struct SharedSkinnedAsset {
    gws::renderer::gpu::LoadedSkinnedModel model;
    std::unique_ptr<gws::renderer::gpu::Texture> albedo, mr, normal, emissive;
    /// Clip events (animation notifies), by clip index: what happens at which
    /// second of a clip -- a footstep, the jaws closing, a bolt leaving its port.
    using ClipEvent = anim::ClipEvent;
    anim::ClipEvents events;
    // Each clip measured as locomotion (foot plants, speed, way), once, by the first
    // actor that asks (on the main thread).
    std::vector<schizo::renderer::LocoClipInfo> loco;
    std::vector<char> loco_done;
    // Each clip's sync markers (its feet's plants, if it is locomotion), for graphs.
    std::vector<schizo::renderer::animgraph::SyncMarkers> sync;
    bool sync_done = false;
};

/// The events of a model's clips, from the text file beside it (the model's path
/// with `.events` for its extension). A line is `clip seconds name [param]`
/// (`#` starts a comment); the clip is found by name as play() finds it. Each is
/// published on the gameplay event bus as `anim.<name>` when its clip passes it,
/// with the entity as instigator, the clip in `param` (and the event's own param
/// after a `|`) and the time as `value`: scripts and logic graphs hear it like any
/// other gameplay event. glTF has no place for them, hence the file.
inline void load_clip_events(SharedSkinnedAsset& sa, const std::string& model_path,
                             const std::vector<std::shared_ptr<schizo::renderer::AnimationClip>>& clips) {
    std::filesystem::path path(model_path);
    path.replace_extension(".events");
    std::ifstream in(path);
    if (!in) return;
    std::vector<std::string> names;
    for (const auto& c : clips) names.push_back(c ? c->GetName() : std::string());
    std::vector<std::string> warnings;
    const int count = anim::parse_clip_events(in, [&](const std::string& clip) { return anim::find_clip(names, clip); },
                                              sa.events, &warnings);
    for (const auto& w : warnings) spdlog::warn("[anim] {}: {}", path.string(), w);
    if (count) spdlog::info("[anim] {} clip events for '{}'", count, model_path);
}
inline std::unordered_map<std::string, std::shared_ptr<SharedSkinnedAsset>>& skinned_asset_cache() {
    static std::unordered_map<std::string, std::shared_ptr<SharedSkinnedAsset>> cache;
    return cache;
}
/// Drop the shared files and their textures -- before the device goes, after
/// every actor using them.
inline void clear_skinned_asset_cache() { skinned_asset_cache().clear(); }

class ImportedSkinnedActor {
public:
    static std::unique_ptr<ImportedSkinnedActor> create(
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout mat_layout, VkDescriptorPool mat_pool,
        gws::renderer::gpu::TextureManager* textures,
        const std::string& gltf_path,
        const glm::vec3& world_pos,
        std::string* out_error = nullptr) {
        auto a = std::unique_ptr<ImportedSkinnedActor>(new ImportedSkinnedActor());
        if (!a->build(device, mat_layout, mat_pool, textures, gltf_path, world_pos, out_error))
            return nullptr;
        return a;
    }

    // ---- clips -------------------------------------------------------------
    size_t clip_count() const { return clips_.size(); }
    size_t clip_index() const { return static_cast<size_t>(std::max(base_.clip, 0)); }
    const std::string& clip_name(size_t i) const { static const std::string none; return i < clips_.size() ? clips_[i]->GetName() : none; }
    float clip_duration(size_t i) const { return i < clips_.size() ? clips_[i]->GetDuration() : 0.0f; }

    /// A clip by name: exact, else the first whose name contains it (any
    /// case) -- "walk" finds "01_Walk". -1 when there is none.
    int find_clip(const std::string& name) const {
        if (name.empty()) return -1;
        for (size_t i = 0; i < clips_.size(); ++i)
            if (clips_[i]->GetName() == name) return static_cast<int>(i);
        const std::string want = lower(name);
        for (size_t i = 0; i < clips_.size(); ++i)
            if (lower(clips_[i]->GetName()).find(want) != std::string::npos) return static_cast<int>(i);
        return -1;
    }

    /// Play clip `i` (looping, or once and holding its last frame), blending
    /// in over `blend` seconds. The clip already playing the same way just
    /// goes on; `restart` starts it again from the beginning (a second shot).
    ///
    /// The change is smoothed by inertialization (Bollo, Gears of War, GDC 2018):
    /// the new clip takes over at once, and the difference between the pose that
    /// was showing and the new one -- and the way it was moving -- is carried
    /// along and decays to nothing (a critically damped spring whose half-life
    /// is a quarter of `blend`). Unlike a crossfade from the old clip, a change
    /// in the middle of another change never jumps, and the body keeps its
    /// momentum into the new motion. `blend` 0 cuts.
    void play(int i, bool loop, float blend = 0.2f, float speed = 1.0f, bool restart = false) {
        if (i < 0 || static_cast<size_t>(i) >= clips_.size()) return;
        base_.speed = speed;
        if (!restart && i == base_.clip && loop == base_.loop) return;
        if (base_.clip >= 0) transition(blend);
        base_.clip = i;
        base_.time = 0.0f;
        base_.loop = loop;
        base_.fresh = true;
    }

    /// The upper body (the bones under `set_upper_body`'s bone) plays clip `i`
    /// over the base clip; -1 lets it go back to the base clip. Blended either way.
    void play_upper(int i, bool loop, float blend = 0.15f, bool restart = false) {
        upper_blend_ = std::max(blend, 1e-3f);
        if (i < 0 || static_cast<size_t>(i) >= clips_.size() || upper_mask_.empty()) { upper_target_ = 0.0f; return; }
        upper_target_ = 1.0f;
        if (!restart && i == upper_.clip && loop == upper_.loop && upper_w_ > 0.0f) return;
        if (upper_w_ > 0.0f) transition(blend);   // a clip already showing gives way smoothly (a second shot, aim -> fire)
        upper_.clip = i;
        upper_.time = 0.0f;
        upper_.loop = loop;
        upper_.fresh = true;
    }
    /// Which bone's subtree is the upper body (a name, matched as a prefix).
    void set_upper_body(const std::string& bone) {
        upper_mask_.clear();
        const int root = find_bone(bone);
        if (root < 0 || !skeleton_) return;
        const auto& bones = skeleton_->GetAllBones();
        upper_mask_.assign(bones.size(), 0);
        for (size_t b = 0; b < bones.size(); ++b)
            for (int p = static_cast<int>(b); p >= 0; p = bones[static_cast<size_t>(p)].parent_id)
                if (p == root) { upper_mask_[b] = 1; break; }
    }

    /// A once clip has played to its end (the base, or the upper layer).
    bool finished() const { return base_.clip >= 0 && !base_.loop && base_.time >= clip_duration(static_cast<size_t>(base_.clip)); }
    bool upper_finished() const {
        return upper_.clip < 0 || upper_target_ <= 0.0f ||
               (!upper_.loop && upper_.time >= clip_duration(static_cast<size_t>(upper_.clip)));
    }
    int  upper_clip() const { return upper_target_ > 0.0f ? upper_.clip : -1; }

    /// CPU tick: advance the clips and pose the skeleton. Call once a frame.
    void advance(float dt) {
        if (!skeleton_ || clips_.empty()) return;
        if (graph_.ready()) {
            // A graph: its parameters (set before) choose; it reports what played
            // (events) and what changed (smoothed away on the bones it touched).
            graph_.update(dt, graph_src_);
            if (dt > 0.0f)
                for (const auto& a : graph_.active()) fire_window(a.clip, a.was, a.now, a.loop);
            for (const auto& c : graph_.take_changes()) transition(c.blend, c.mask);
            pose(dt);
            return;
        }
        if (base_.clip < 0) base_.clip = 0;
        const float base_was = base_.fresh ? -1e-4f : base_.time;
        const float upper_was = upper_.fresh ? -1e-4f : upper_.time;
        base_.time += dt * base_.speed;
        upper_.time += dt;
        if (dt > 0.0f) {
            fire_events(base_, base_was);
            if (upper_.clip >= 0 && upper_target_ > 0.0f) fire_events(upper_, upper_was);
            base_.fresh = upper_.fresh = false;
        }
        const float step = dt / upper_blend_;
        upper_w_ = upper_target_ > upper_w_ ? std::min(upper_target_, upper_w_ + step) : std::max(upper_target_, upper_w_ - step);
        pose(dt);
    }

    /// A clip's event, as it was passed (`<model>.events`, see load_events).
    struct FiredEvent {
        std::string clip, name, param;
        float time = 0.0f;
    };
    /// The events the clips passed since the last call (the caller publishes them).
    std::vector<FiredEvent> take_events() { std::vector<FiredEvent> out; out.swap(fired_); return out; }
    /// Whether any clip of this model carries events: such an actor is animated
    /// every frame (its events are gameplay; they must not wait for a level of detail).
    bool has_events() const { return shared_ && !shared_->events.empty(); }
    /// Where the base clip is: its name and seconds into it.
    std::string clip_name() const {
        return base_.clip >= 0 && static_cast<size_t>(base_.clip) < clips_.size() ? clips_[static_cast<size_t>(base_.clip)]->GetName() : std::string();
    }
    float clip_time() const { return base_.time; }

    /// Pose the skeleton WITHOUT advancing time. A paused actor still has to be
    /// posed on the first frame, or it renders in bind pose until you press
    /// play -- which reads as "the model is broken", not "it is paused".
    void refresh_pose() { pose(0.0f); }

    /// Absolute additive offset from the sampled clip, never accumulated.
    bool set_bone_rotation(const std::string& name, const glm::vec3& degrees) {
        const int b = find_bone(name);
        if (b < 0 || static_cast<size_t>(b) >= out_.size()) return false;
        if (procedural_.size() != out_.size()) procedural_.resize(out_.size(), glm::quat(1,0,0,0));
        procedural_[b] = glm::quat(glm::radians(degrees));
        auto tf = out_[b];
        tf.rotation = tf.rotation * procedural_[b];
        skeleton_->SetBoneLocalTransform(static_cast<uint16_t>(b), tf);
        skeleton_->UpdateWorldTransforms();
        return true;
    }

    /// Place the actor with a full transform, so an entity's Transform (with
    /// rotation and non-uniform scale) drives it rather than the position+scale
    /// pair this class used when there was only ever one actor.
    void set_model(const glm::mat4& m) { model_ = m; use_model_ = true; }
    glm::mat4 model() const {
        return use_model_ ? model_
                          : glm::translate(glm::mat4(1.0f), world_pos_) * glm::scale(glm::mat4(1.0f), glm::vec3(scale_));
    }

    // ---- bones -------------------------------------------------------------
    /// A bone by name: exact, else the first whose name starts with it (the
    /// loader names bones "<name>__<node>"). -1 when there is none.
    int find_bone(const std::string& name) const {
        if (!skeleton_ || name.empty()) return -1;
        const auto& bones = skeleton_->GetAllBones();
        for (size_t b = 0; b < bones.size(); ++b) if (bones[b].name == name) return static_cast<int>(b);
        for (size_t b = 0; b < bones.size(); ++b) if (bones[b].name.rfind(name + "__", 0) == 0) return static_cast<int>(b);
        for (size_t b = 0; b < bones.size(); ++b) if (bones[b].name.rfind(name, 0) == 0) return static_cast<int>(b);
        return -1;
    }
    /// How the mesh around a bone is moved from where the model has it (the
    /// bone's skinning transform, in the world): a held thing follows this.
    glm::mat4 bone_skin(int bone) const {
        if (!skeleton_ || !shared_ || bone < 0 || static_cast<uint32_t>(bone) >= skeleton_->GetBoneCount() ||
            static_cast<size_t>(bone) >= shared_->model.inverse_bind.size())
            return model();
        return model() * skeleton_->GetWorldMatrix(static_cast<uint16_t>(bone)) * shared_->model.inverse_bind[static_cast<size_t>(bone)];
    }
    int bone_count() const { return skeleton_ ? static_cast<int>(skeleton_->GetBoneCount()) : 0; }
    /// A bone's parent (-1 for a root or none) and its name.
    int bone_parent(int bone) const {
        if (!skeleton_ || bone < 0 || static_cast<uint32_t>(bone) >= skeleton_->GetBoneCount()) return -1;
        return skeleton_->GetBone(static_cast<uint16_t>(bone))->parent_id;
    }
    std::string bone_name(int bone) const {
        if (!skeleton_ || bone < 0 || static_cast<uint32_t>(bone) >= skeleton_->GetBoneCount()) return {};
        std::string n = skeleton_->GetBone(static_cast<uint16_t>(bone))->name;
        if (const size_t at = n.find("__"); at != std::string::npos) n.resize(at);   // the loader's "<name>__<node>"
        return n;
    }
    /// A bone's place in the world (the actor's model x the posed bone).
    glm::mat4 bone_world(int bone) const {
        if (!skeleton_ || bone < 0 || static_cast<uint32_t>(bone) >= skeleton_->GetBoneCount()) return model();
        return model() * skeleton_->GetWorldMatrix(static_cast<uint16_t>(bone));
    }

    /// GPU: record the skinning dispatch from the current pose. Call right after
    /// the frame command buffer begins (before shadow/geometry read the vbo).
    // ---- first person ------------------------------------------------------
    /// An arm to bend: its three bones (upper arm, forearm, hand), where the
    /// hand is to be (world, with its orientation), and which way the elbow
    /// points (a world direction).
    struct ArmTarget {
        int       upper = -1, fore = -1, hand = -1;
        glm::mat4 target{1.0f};
        glm::vec3 pole{0.0f, -1.0f, 0.0f};
    };
    /// After advance(): fold `fold` bones away (scaled to nothing -- the head
    /// the eye sits in) and bend each arm so its hand is at its target. The
    /// skinning then draws this pose. Model first (set_model).
    void pose_first_person(const std::vector<int>& fold, const ArmTarget* arms, int count) {
        if (!skeleton_) return;
        const auto n = static_cast<int>(skeleton_->GetBoneCount());
        for (int b : fold)
            if (b >= 0 && b < n) {
                schizo::renderer::BoneTransform tf = skeleton_->GetBone(static_cast<uint16_t>(b))->local_transform;
                tf.scale = glm::vec3(1e-3f);
                skeleton_->SetBoneLocalTransform(static_cast<uint16_t>(b), tf);
            }
        skeleton_->UpdateWorldTransforms();
        const glm::mat4 inv = glm::inverse(model());
        auto rot = [](const glm::mat4& m) {
            return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                                                           glm::normalize(glm::vec3(m[2])))));
        };
        auto set_rot = [&](int b, const glm::quat& q) {
            schizo::renderer::BoneTransform tf = skeleton_->GetBone(static_cast<uint16_t>(b))->local_transform;
            tf.rotation = glm::normalize(q);
            skeleton_->SetBoneLocalTransform(static_cast<uint16_t>(b), tf);
        };
        for (int k = 0; k < count; ++k) {
            const ArmTarget& a = arms[k];
            if (a.upper < 0 || a.fore < 0 || a.hand < 0 || a.upper >= n || a.fore >= n || a.hand >= n) continue;
            const glm::mat4 gu = skeleton_->GetWorldMatrix(static_cast<uint16_t>(a.upper));
            const glm::mat4 gf = skeleton_->GetWorldMatrix(static_cast<uint16_t>(a.fore));
            const glm::mat4 gh = skeleton_->GetWorldMatrix(static_cast<uint16_t>(a.hand));
            const glm::mat4 tgt = inv * a.target;   // in the model's space, as the bones are
            const auto ik = schizo::renderer::solve_two_bone_ik(glm::vec3(gu[3]), glm::vec3(gf[3]), glm::vec3(gh[3]),
                                                                glm::vec3(tgt[3]), glm::mat3(inv) * a.pole);
            const glm::quat ru = ik.upper * rot(gu);
            const glm::quat rf = ik.fore * ik.upper * rot(gf);
            const glm::quat rh = rot(tgt);
            const int parent = skeleton_->GetBone(static_cast<uint16_t>(a.upper))->parent_id;
            const glm::quat rp = parent >= 0 ? rot(skeleton_->GetWorldMatrix(static_cast<uint16_t>(parent))) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            set_rot(a.upper, glm::inverse(rp) * ru);
            set_rot(a.fore, glm::inverse(ru) * rf);
            set_rot(a.hand, glm::inverse(rf) * rh);
            skeleton_->UpdateWorldTransforms();
        }
    }

    // ---- feet on the ground (foot IK: engine/renderer/include/foot_placement.h) ----
    using GroundFn = schizo::renderer::FootGroundFn;
    /// Whether the rig has two legs it can place: a foot bone on each side (by
    /// name: `foot.L` / `LeftFoot` / `l_foot`...), its parent the knee, that one's
    /// the hip.
    bool has_feet() {
        if (!feet_resolved_ && skeleton_) {
            feet_resolved_ = true;
            rig_ = schizo::renderer::find_foot_rig(*skeleton_, bind_);
            static const bool log_feet = std::getenv("GWS_TEST_LOG_ANIM") != nullptr;
            if (log_feet && rig_.valid())
                spdlog::info("[anim] feet: {} > {} > {} and {} > {} > {} (standing {:.3f} / {:.3f} m), pelvis {}", bone_name(rig_.legs[0].hip),
                             bone_name(rig_.legs[0].knee), bone_name(rig_.legs[0].ankle), bone_name(rig_.legs[1].hip),
                             bone_name(rig_.legs[1].knee), bone_name(rig_.legs[1].ankle), rig_.legs[0].rest_height,
                             rig_.legs[1].rest_height, bone_name(rig_.pelvis));
        }
        return rig_.valid();
    }
    /// After advance(): the feet meet the ground they stand on (foot_placement.h:
    /// the ground under each foot, the pelvis down for the lower one, two-bone IK,
    /// a planted foot laid on the slope; springs on everything). `weight` 0..1 (0
    /// in the air or dead), eased. `lock`: a planted foot is held where it came down
    /// (foot locking). Model first (set_model). Any thread if `ground`.
    void place_feet(const GroundFn& ground, float weight, float dt, bool lock = true) {
        if (!has_feet() || !skeleton_ || dt < 0.0f) return;
        const glm::mat4 M = model();
        schizo::renderer::place_feet(*skeleton_, rig_, M, ground, weight, dt, lock);
        const float unit = glm::length(glm::vec3(M[1]));
        if (unit > 1e-6f) report_feet(ground, M, glm::vec3(M[1]) / unit, unit, dt);
    }
    // GWS_TEST_LOG_FEET=1: for the player's body (else one character standing on a
    // slope: the ground under a foot 6 cm or more off its own), twice a second, each
    // foot's height over the ground under it against its standing height (0: on it),
    // whether it is held (foot locking), how fast the model goes and how fast its
    // planted feet slide over the ground.
    void report_feet(const GroundFn& ground, const glm::mat4& M, const glm::vec3& up_w, float unit, float dt) {
        static const bool log_feet = std::getenv("GWS_TEST_LOG_FEET") != nullptr;
        if (!log_feet) return;
        // The character marked to report (set_report_feet: the player's body), else the
        // first one standing on a slope.
        static std::atomic<const void*> logged{nullptr};
        const bool sloped = std::abs(rig_.legs[0].ground) > 0.06f || std::abs(rig_.legs[1].ground) > 0.06f;
        const void* expected = nullptr;
        if (report_feet_) logged.store(this);
        if (!(logged.load() == this || (sloped && logged.compare_exchange_strong(expected, this)))) return;
        // How fast a foot that is down slides over the ground (world, horizontal).
        for (int k = 0; k < 2; ++k) {
            const glm::vec3 a(M * skeleton_->GetWorldMatrix(static_cast<uint16_t>(rig_.legs[k].ankle))[3]);
            const glm::vec3 am(skeleton_->GetWorldMatrix(static_cast<uint16_t>(rig_.legs[k].ankle))[3]);
            const bool down = rig_.legs[k].plant > 0.95f;
            if (down && slide_down_[k] && dt > 1e-4f) {
                const glm::vec3 d = (a - slide_prev_[k]) / dt;
                slide_sum_ += glm::length(d - up_w * glm::dot(d, up_w));
                const glm::vec3 dm = (am - slide_prev_model_[k]) / dt;
                model_sum_ += glm::length(glm::vec2(dm.x, dm.z));
                ++slide_n_;
            }
            slide_prev_[k] = a;
            slide_prev_model_[k] = am;
            slide_down_[k] = down;
        }
        {   // how fast the model itself goes
            const glm::vec3 o(M[3]);
            if (dt > 1e-4f && body_known_) {
                const glm::vec3 d = (o - body_prev_) / dt;
                body_sum_ += glm::length(d - up_w * glm::dot(d, up_w));
                ++body_n_;
            }
            body_prev_ = o;
            body_known_ = true;
        }
        log_tick_ += dt;
        if (log_tick_ < 0.5f) return;
        log_tick_ = 0.0f;
        float gap[2] = {0.0f, 0.0f};
        for (int k = 0; k < 2; ++k) {
            const glm::vec3 a(M * skeleton_->GetWorldMatrix(static_cast<uint16_t>(rig_.legs[k].ankle))[3]);
            glm::vec3 hit, n;
            gap[k] = ground(a + up_w * (0.6f * unit), -up_w, 2.0f * unit, hit, n) ? glm::dot(a - hit, up_w) / unit - rig_.legs[k].rest_height : 9.0f;
        }
        const float slide = slide_n_ > 0 ? slide_sum_ / static_cast<float>(slide_n_) : 0.0f;
        const float in_model = slide_n_ > 0 ? model_sum_ / static_cast<float>(slide_n_) : 0.0f;
        const float body = body_n_ > 0 ? body_sum_ / static_cast<float>(body_n_) : 0.0f;
        body_sum_ = 0.0f;
        body_n_ = 0;
        spdlog::info("[test] feet: left {:+.3f} m ({}{}), right {:+.3f} m ({}{}), pelvis {:+.3f} m, ground under them {:+.3f} / {:+.3f}; the model goes {:.2f} m/s, planted feet slide {:.2f} m/s (move {:.2f} m/s in the model)",
                     gap[0], rig_.legs[0].plant > 0.5f ? "down" : "lifted", rig_.legs[0].locked ? ", held" : "",
                     gap[1], rig_.legs[1].plant > 0.5f ? "down" : "lifted", rig_.legs[1].locked ? ", held" : "",
                     rig_.pelvis_off, rig_.legs[0].ground, rig_.legs[1].ground, body, slide, in_model);
        slide_sum_ = 0.0f;
        model_sum_ = 0.0f;
        slide_n_ = 0;
    }
    /// GWS_TEST_LOG_FEET reports this character's feet.
    void set_report_feet(bool on) { report_feet_ = on; }
    /// A landing: the pelvis is pushed down (m/s) and springs back.
    void pelvis_impulse(float down_speed) { rig_.land_v -= down_speed; }

    // ---- an animation graph (engine/renderer/include/anim_graph_runtime.h) -------------------
    /// Runs `def` from now on instead of play()/play_upper(): set its parameters
    /// (graph()) each frame before advance(). Measures the clips' sync markers
    /// once per model (main thread). nullptr: back to play().
    void set_graph(std::shared_ptr<const schizo::renderer::animgraph::GraphDef> def) {
        if (!def || !skeleton_) { graph_ = {}; return; }
        prepare_sync();
        std::vector<int> parent;
        for (const auto& b : skeleton_->GetAllBones()) parent.push_back(b.parent_id);
        graph_src_.a = this;
        graph_.init(std::move(def), parent, [&](const std::string& n) { return find_bone(n); });
        graph_def_tag_ = nullptr;
    }
    bool has_graph() const { return graph_.ready(); }
    schizo::renderer::animgraph::GraphInstance& graph() { return graph_; }
    const schizo::renderer::animgraph::GraphInstance& graph() const { return graph_; }
    /// Who built the running graph (the caller's tag: rebuilds only on a change).
    const void* graph_tag() const { return graph_def_tag_; }
    void set_graph_tag(const void* t) { graph_def_tag_ = t; }
    /// A one-shot over the graph's layers on slot `slot`'s bones (a shot, a reload,
    /// a hit) -- without a graph, the clip plays on the whole body (play()).
    void play_montage(int clip, const std::string& slot, float rate = 1.0f, float blend_in = 0.1f, float blend_out = 0.2f, bool loop = false) {
        if (clip < 0 || static_cast<size_t>(clip) >= clips_.size()) return;
        if (!graph_.ready()) { play(clip, loop, blend_in, rate, true); return; }
        graph_.play_montage(graph_.def().slot(slot), clip, rate, blend_in, blend_out, loop);
    }
    void stop_montage(const std::string& slot, float blend_out = -1.0f) {
        if (graph_.ready()) graph_.stop_montage(graph_.def().slot(slot), blend_out);
    }
    /// A clip's sync markers (its feet's plants; none unless it is locomotion).
    const schizo::renderer::animgraph::SyncMarkers& sync_markers(int clip) const {
        static const schizo::renderer::animgraph::SyncMarkers none;
        if (!shared_ || clip < 0 || static_cast<size_t>(clip) >= shared_->sync.size()) return none;
        return shared_->sync[static_cast<size_t>(clip)];
    }

    // ---- locomotion (engine/renderer/include/locomotion_warp.h) ---------------------------
    /// A clip measured as locomotion: where each foot comes down, the ground speed
    /// it was made for, the way its body goes (model x, z). Measured once per model.
    const schizo::renderer::LocoClipInfo& loco_info(int clip) {
        static const schizo::renderer::LocoClipInfo none;
        if (!shared_ || clip < 0 || static_cast<size_t>(clip) >= clips_.size() || !has_feet()) return none;
        auto& sa = *shared_;
        if (sa.loco.size() != clips_.size()) { sa.loco.assign(clips_.size(), {}); sa.loco_done.assign(clips_.size(), 0); }
        if (!sa.loco_done[static_cast<size_t>(clip)]) {
            sa.loco_done[static_cast<size_t>(clip)] = 1;
            sa.loco[static_cast<size_t>(clip)] = schizo::renderer::measure_loco_clip(*skeleton_, bind_, rig_, *clips_[static_cast<size_t>(clip)]);
            static const bool log_loco = std::getenv("GWS_TEST_LOG_ANIM") != nullptr;
            const auto& li = sa.loco[static_cast<size_t>(clip)];
            if (log_loco && !li.valid && li.duration > 0.0f && !li.contact[0].empty() && !li.contact[1].empty())
                spdlog::info("[anim] not locomotion '{}': feet down {} / {} times, {} planted frames, ankle heights {:.3f}..{:.3f} / {:.3f}..{:.3f}",
                             clips_[static_cast<size_t>(clip)]->GetName(), li.contact[0].size(), li.contact[1].size(), li.planted_frames,
                             li.low[0], li.high[0], li.low[1], li.high[1]);
            if (log_loco && li.valid)
                spdlog::info("[anim] locomotion '{}': {:.2f} m/s toward ({:.2f}, {:.2f}), feet down at {} / {} s", clips_[static_cast<size_t>(clip)]->GetName(),
                             li.speed, li.dir.x, li.dir.y, li.contact[0].empty() ? -1.0f : li.contact[0][0], li.contact[1].empty() ? -1.0f : li.contact[1][0]);
        }
        return sa.loco[static_cast<size_t>(clip)];
    }
    /// Plays looping locomotion clip `i` in step with the one playing: from the same
    /// point of the same foot's step (falls back to play() when either cannot say).
    void play_synced(int i, float blend, float speed) {
        if (i == base_.clip || base_.clip < 0 || !base_.loop) { play(i, true, blend, speed); return; }
        const auto& from = loco_info(base_.clip);
        const auto& to = loco_info(i);
        if (!from.valid || !to.valid) { play(i, true, blend, speed); return; }
        const float t = schizo::renderer::loco_synced_time(from, base_.time, to);
        play(i, true, blend, speed);
        base_.time = t;
        base_.fresh = false;   // the step goes on from there: its events before it are not heard again
    }
    /// After advance(), before place_feet: the legs turned toward where the body
    /// really goes (`turn` radians about up, the upper body turned back) and the
    /// stride scaled (`stride`) along the clip's way `clip_dir` (model x, z). Springs
    /// ease both (a stop or a turn does not snap the legs).
    void warp_locomotion(const glm::vec2& clip_dir, float stride, float turn, float dt) {
        if (!has_feet() || !skeleton_ || dt < 0.0f) return;
        schizo::renderer::spring_to(loco_warp_.turn, loco_warp_.turn_v, turn, 0.1f, dt);
        schizo::renderer::spring_to(loco_warp_.stride, loco_warp_.stride_v, stride, 0.1f, dt);
        if (std::abs(loco_warp_.turn) < 1e-4f && std::abs(loco_warp_.stride - 1.0f) < 1e-3f) return;
        schizo::renderer::warp_locomotion(*skeleton_, rig_, look_chain_.empty() ? -1 : look_chain_.front(), clip_dir, loco_warp_.stride, loco_warp_.turn);
    }

    // ---- going limp (engine/renderer/include/ragdoll_rig.h) ------------------------------
    /// Whether the rig can become a ragdoll (legs, spine, head and arms found by name).
    bool has_ragdoll_rig() {
        if (!ragdoll_resolved_ && skeleton_) {
            ragdoll_resolved_ = true;
            ragdoll_rig_ = schizo::renderer::find_ragdoll_rig(*skeleton_, bind_);
        }
        return ragdoll_rig_.valid();
    }
    const schizo::renderer::RagdollRig& ragdoll_rig() const { return ragdoll_rig_; }
    /// Its parts where the pose has them now (model first: set_model).
    std::vector<schizo::renderer::RagdollPartStart> ragdoll_start() const {
        return skeleton_ ? schizo::renderer::ragdoll_start(*skeleton_, ragdoll_rig_, model()) : std::vector<schizo::renderer::RagdollPartStart>{};
    }
    /// While limp: the bones where the bodies are (model first).
    void apply_ragdoll(const std::vector<glm::vec3>& positions, const std::vector<glm::quat>& rotations) {
        if (skeleton_) schizo::renderer::apply_ragdoll_pose(*skeleton_, ragdoll_rig_, model(), positions, rotations);
    }

    // ---- leaning and looking (engine/renderer/include/pose_layers.h) -------------------
    /// After place_feet, before look_at: the body leans into its acceleration
    /// (world, m/s^2), `strength` of the angle that balances it against gravity,
    /// at most 12 degrees, over the lower half of the look chain. Model first.
    void lean_into(const glm::vec3& accel_world, float strength, float dt) {
        if (skeleton_) schizo::renderer::lean_into(*skeleton_, look_chain_, lean_state_, model(), accel_world, strength, dt);
    }
    /// A blow knocks the upper body along `world_dir` (the way the hit pushes);
    /// it comes back on the lean's spring (lean_into must run each frame).
    void flinch(const glm::vec3& world_dir, float strength = 5.5f) {
        schizo::renderer::flinch(lean_state_, model(), world_dir, strength);
    }
    /// The bones that turn toward what the character looks at: from `base` (the
    /// spine) up through the chain to `head`. Empty when `head` is not under `base`.
    void set_look_chain(const std::string& base, const std::string& head) {
        look_chain_.clear();
        const int h = find_bone(head), b = find_bone(base);
        if (h >= 0 && b >= 0 && skeleton_) look_chain_ = schizo::renderer::pose_chain(*skeleton_, b, h);
    }
    bool has_look_chain() const { return !look_chain_.empty(); }
    /// After advance(): turn the chain toward `world_point`, by `weight` (0..1).
    /// `aim` 0 is a glance (the head leads), 1 an aim (the spine and chest take it
    /// all: a rifle at the chest points there). Model first (set_model).
    void look_at(const glm::vec3& world_point, float weight, float aim, float dt) {
        if (skeleton_) schizo::renderer::look_at(*skeleton_, look_chain_, look_state_, model(), world_point, weight, aim, dt);
    }

    /// Skinned at least once (its vertex buffer holds a pose, not the bind pose).
    bool skinned_once() const { return skinned_once_; }
    /// Every bone's matrix finite and inside 1e5 m (a pose gone wrong -- NaN
    /// from a zero-length rotation, a scale exploding -- would skin vertices to
    /// nowhere). `bad` names the first offender. (GWS_TEST_RT_AUDIT)
    bool pose_sane(std::string* bad = nullptr) const {
        if (!skeleton_) return true;
        const uint32_t n = skeleton_->GetBoneCount();
        for (uint32_t i = 0; i < n; ++i) {
            const glm::mat4 m = skeleton_->GetWorldMatrix(static_cast<uint16_t>(i));
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (!std::isfinite(m[c][r]) || std::fabs(m[c][r]) > 1.0e5f) {
                        if (bad) *bad = bone_name(static_cast<int>(i)) + " (" + std::to_string(m[c][r]) + ")";
                        return false;
                    }
        }
        return true;
    }
    void record_skin(VkCommandBuffer cmd) {
        if (!mesh_ || !skeleton_) return;
        skinned_once_ = true;
        const uint32_t n = skeleton_->GetBoneCount();
        std::vector<glm::mat4> globals(n);
        for (uint32_t i = 0; i < n; ++i)
            globals[i] = skeleton_->GetWorldMatrix(static_cast<uint16_t>(i));
        mesh_->skin(cmd, globals);
    }

    gws::renderer::gpu::DrawItem draw_item() const {
        gws::renderer::gpu::DrawItem d{};
        d.mesh          = mesh_->output_mesh();
        d.material      = mat_.get();
        d.model         = model();
        d.submesh_index = 0;
        d.is_blend      = false;
        d.is_terrain    = false;
        return d;
    }

    const std::string& name() const { return name_; }
    /// Legacy: show clip `i`, looping, from its start, with no blend.
    void   set_clip(size_t i) { play(static_cast<int>(i), true, 0.0f, base_.speed, true); }
    void   set_world_pos(const glm::vec3& p) { world_pos_ = p; }
    glm::vec3 world_pos() const { return world_pos_; }
    void   set_scale(float s) { scale_ = s; }
    float  scale() const { return scale_; }

private:
    ImportedSkinnedActor() = default;

    struct Track {
        int   clip = -1;
        float time = 0.0f;
        bool  loop = true;
        float speed = 1.0f;
        bool  fresh = true;   // just started: an event at its first instant still fires
    };

    static std::string lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Sample one clip from the bind pose (so bones it does not animate are not
    // left where another clip put them) and copy the locals out.
    void sample(const Track& tr, std::vector<schizo::renderer::BoneTransform>& out) {
        // Straight into the pose: the skeleton is not touched until the pose is set.
        const size_t n = skeleton_->GetBoneCount();
        out.assign(bind_.begin(), bind_.begin() + static_cast<std::ptrdiff_t>(std::min(n, bind_.size())));
        out.resize(n);
        const auto& clip = clips_[static_cast<size_t>(tr.clip)];
        const float t = tr.loop ? tr.time : std::min(tr.time, std::max(0.0f, clip->GetDuration() - 1e-4f));
        clip->SampleLocal(t, tr.loop, out);
    }

    // ---- inertialization (anim_hooks.h) ------------------------------------------------
    static glm::vec3 to_scaled_axis(const glm::quat& q) { return anim::to_scaled_axis(q); }
    static glm::quat from_scaled_axis(const glm::vec3& a) { return anim::from_scaled_axis(a); }
    static void decay(glm::vec3& x, glm::vec3& v, float halflife, float dt) { anim::decay(x, v, halflife, dt); }
    /// A change to smooth away over `blend` seconds on the bones of `mask` (all:
    /// nullptr); 0 cuts (nothing carried over there).
    void transition(float blend, const std::vector<uint8_t>* mask = nullptr) {
        if (blend <= 0.0f && !mask) {   // a cut of everything
            inert_live_ = false;
            inert_reqs_.clear();
            return;
        }
        inert_reqs_.push_back({blend <= 0.0f ? 0.0f : anim::halflife_for_blend(blend), mask ? *mask : std::vector<uint8_t>{}});
    }

    // ---- events ----------------------------------------------------------------------
    void fire_events(const Track& tr, float was) { fire_window(tr.clip, was, tr.time, tr.loop); }
    void fire_window(int c, float was, float now, bool loop) {
        if (!shared_ || c < 0 || static_cast<size_t>(c) >= clips_.size()) return;
        auto it = shared_->events.find(c);
        if (it == shared_->events.end() || it->second.empty()) return;
        const float dur = clips_[static_cast<size_t>(c)]->GetDuration();
        if (now <= was) return;
        const std::string& clip = clips_[static_cast<size_t>(c)]->GetName();
        passed_.clear();
        anim::events_passed(it->second, was, now, dur, loop, passed_);
        for (size_t i : passed_) fired_.push_back({clip, it->second[i].name, it->second[i].param, it->second[i].time});
    }

    // The graph's view of this model's clips.
    struct GraphClips final : schizo::renderer::animgraph::ClipSource {
        const ImportedSkinnedActor* a = nullptr;
        int clip_count() const override { return static_cast<int>(a->clips_.size()); }
        float duration(int c) const override { return a->clip_duration(static_cast<size_t>(c)); }
        void sample(int c, float t, bool loop, schizo::renderer::animgraph::Pose& pose) const override {
            if (c >= 0 && static_cast<size_t>(c) < a->clips_.size()) a->clips_[static_cast<size_t>(c)]->SampleLocal(t, loop, pose);
        }
        const schizo::renderer::animgraph::SyncMarkers& markers(int c) const override { return a->sync_markers(c); }
        const schizo::renderer::animgraph::Pose& bind() const override { return a->bind_; }
    };
    // Every clip's sync markers, measured once per model (on the main thread).
    void prepare_sync() {
        if (!shared_ || shared_->sync_done) return;
        shared_->sync_done = true;
        shared_->sync.assign(clips_.size(), {});
        if (!has_feet()) return;
        for (size_t c = 0; c < clips_.size(); ++c) {
            const auto& li = loco_info(static_cast<int>(c));
            if (li.valid) shared_->sync[c] = schizo::renderer::animgraph::SyncMarkers::from_contacts(li.duration, li.contact[0], li.contact[1]);
        }
    }

    void pose(float dt) {
        if (!skeleton_ || clips_.empty()) return;
        if (base_.clip < 0) base_.clip = 0;
        using schizo::renderer::BoneTransform;
        if (graph_.ready()) {
            graph_.evaluate(graph_src_, pose_a_);
        } else {
            sample(base_, pose_a_);
            if (upper_.clip >= 0 && upper_w_ > 0.0f && !upper_mask_.empty()) {
                sample(upper_, pose_b_);
                for (size_t b = 0; b < pose_a_.size() && b < upper_mask_.size(); ++b)
                    if (upper_mask_[b]) pose_a_[b] = BoneTransform::Slerp(pose_a_[b], pose_b_[b], upper_w_);
            }
        }
        const size_t n = pose_a_.size();
        // A change: on the bones it touches, what was showing minus what the clips
        // say now, and how it moved; the other bones keep what they carry.
        if (!inert_reqs_.empty() && out_.size() == n) {
            if (!inert_live_ || off_p_.size() != n) {
                off_p_.assign(n, glm::vec3(0.0f));
                off_vp_.assign(n, glm::vec3(0.0f));
                off_r_.assign(n, glm::vec3(0.0f));
                off_vr_.assign(n, glm::vec3(0.0f));
                off_half_.assign(n, 0.05f);
            }
            inert_live_ = true;
            inert_age_ = 0.0f;
            const bool moving = out_prev_.size() == n && out_dt_ > 1e-4f && out_dt_ < 0.1f;
            for (const auto& rq : inert_reqs_)
                for (size_t b = 0; b < n; ++b) {
                    if (!rq.mask.empty() && (b >= rq.mask.size() || !rq.mask[b])) continue;
                    if (rq.half <= 0.0f) {   // a cut there
                        off_p_[b] = off_vp_[b] = off_r_[b] = off_vr_[b] = glm::vec3(0.0f);
                        continue;
                    }
                    off_half_[b] = rq.half;
                    off_p_[b] = out_[b].position - pose_a_[b].position;
                    off_r_[b] = to_scaled_axis(out_[b].rotation * glm::inverse(pose_a_[b].rotation));
                    off_vp_[b] = off_vr_[b] = glm::vec3(0.0f);
                    if (moving) {
                        off_vp_[b] = (out_[b].position - out_prev_[b].position) / out_dt_;
                        off_vr_[b] = to_scaled_axis(out_[b].rotation * glm::inverse(out_prev_[b].rotation)) / out_dt_;
                        // (a sudden velocity from a hiccup in the frame time would throw the pose)
                        if (glm::length(off_vr_[b]) > 30.0f) off_vr_[b] = glm::normalize(off_vr_[b]) * 30.0f;
                        if (glm::length(off_vp_[b]) > 20.0f) off_vp_[b] = glm::normalize(off_vp_[b]) * 20.0f;
                    }
                }
            inert_half_ = 0.0f;
            for (float h : off_half_) inert_half_ = std::max(inert_half_, h);
        }
        inert_reqs_.clear();   // (nothing showing yet: nothing to carry)
        if (inert_live_ && off_p_.size() == n) {
            if (dt > 0.0f) {
                for (size_t b = 0; b < n; ++b) {
                    decay(off_p_[b], off_vp_[b], off_half_[b], dt);
                    decay(off_r_[b], off_vr_[b], off_half_[b], dt);
                }
                inert_age_ += dt;
                if (inert_age_ > inert_half_ * 12.0f) inert_live_ = false;   // under 0.03% left
            }
            for (size_t b = 0; b < n; ++b) {
                pose_a_[b].position += off_p_[b];
                pose_a_[b].rotation = glm::normalize(from_scaled_axis(off_r_[b]) * pose_a_[b].rotation);
            }
        }
        if (dt > 0.0f) {
            out_prev_.swap(out_);
            out_dt_ = dt;
        }
        out_ = pose_a_;
        for (size_t b = 0; b < n; ++b) {
            auto tf = pose_a_[b];
            if (b < procedural_.size()) tf.rotation = tf.rotation * procedural_[b];
            skeleton_->SetBoneLocalTransform(static_cast<uint16_t>(b), tf);
        }
        skeleton_->UpdateWorldTransforms();
    }

    bool build(gws::renderer::gpu::VulkanDevice* device,
               VkDescriptorSetLayout mat_layout, VkDescriptorPool mat_pool,
               gws::renderer::gpu::TextureManager* textures,
               const std::string& gltf_path, const glm::vec3& world_pos,
               std::string* out_error) {
        auto& cache = skinned_asset_cache();
        auto it = cache.find(gltf_path);
        if (it == cache.end()) {
            auto sa = std::make_shared<SharedSkinnedAsset>();
            sa->model = gws::renderer::gpu::load_skinned_gltf(gltf_path);
            if (!sa->model.valid) { if (out_error) *out_error = sa->model.error; return false; }
            const auto& lm0 = sa->model.material;
            const VkSampler s = textures ? textures->sampler(gws::renderer::gpu::SamplerDesc{}) : VK_NULL_HANDLE;
            auto upload = [&](const gws::renderer::gpu::LoadedImage& im, bool srgb) {
                return im.empty() ? std::unique_ptr<gws::renderer::gpu::Texture>()
                                  : gws::renderer::gpu::Texture::create_from_pixels(device, im.rgba.data(), im.width, im.height,
                                                                                    srgb, /*gen_mips=*/true, s);
            };
            sa->albedo = upload(lm0.base_color, true);
            sa->mr     = upload(lm0.metallic_roughness, false);
            sa->normal = upload(lm0.normal, false);
            sa->emissive = upload(lm0.emissive, true);
            load_clip_events(*sa, gltf_path, sa->model.animations);
            it = cache.emplace(gltf_path, std::move(sa)).first;
        }
        shared_ = it->second;
        const auto& loaded = shared_->model;

        skeleton_  = std::shared_ptr<schizo::renderer::Skeleton>(loaded.skeleton->Clone().release());   // its own pose
        clips_     = loaded.animations;                                                                     // read only: shared
        world_pos_ = world_pos;
        name_      = std::filesystem::path(gltf_path).filename().string();
        // The bind pose is the loaded skeleton's (Clone resets its copy to identity):
        // a bone a clip does not animate stays here, not at identity.
        for (const auto& b : loaded.skeleton->GetAllBones()) bind_.push_back(b.local_transform);
        for (size_t b = 0; b < bind_.size() && b < skeleton_->GetBoneCount(); ++b)
            skeleton_->SetBoneLocalTransform(static_cast<uint16_t>(b), bind_[b]);
        skeleton_->UpdateWorldTransforms();

        mesh_ = gws::renderer::gpu::SkinnedMesh::create(
            device, loaded.vertices, loaded.indices, loaded.submeshes, loaded.inverse_bind);
        if (!mesh_) { if (out_error) *out_error = "GPU SkinnedMesh creation failed"; return false; }

        // The asset's own material when it has one; a neutral grey otherwise.
        gws::renderer::gpu::MaterialUniforms mu{};
        mu.base_color_factor = glm::vec4(0.80f, 0.80f, 0.82f, 1.0f);
        mu.metallic_factor   = 0.0f;
        mu.roughness_factor  = 0.7f;
        const auto& lm = loaded.material;
        if (lm.present) {
            mu.base_color_factor = lm.base_color_factor;
            mu.metallic_factor   = lm.metallic_factor;
            mu.roughness_factor  = lm.roughness_factor;
            mu.emissive_factor = glm::vec4(lm.emissive_factor, 0.0f);
            mu.parallax.w = lm.emissive.empty() ? 0.0f : 1.0f;
        }
        const gws::renderer::gpu::Texture* dw = textures ? textures->white()  : nullptr;
        const gws::renderer::gpu::Texture* dn = textures ? textures->normal() : nullptr;
        const gws::renderer::gpu::Texture* db = textures ? textures->black()  : nullptr;
        mat_ = gws::renderer::gpu::Material::create(device, mat_layout, mat_pool, mu,
                                                    shared_->albedo.get(), shared_->normal.get(), shared_->mr.get(), nullptr, shared_->emissive.get(),
                                                    dw, dn, db);
        return mat_ != nullptr;
    }

    std::shared_ptr<schizo::renderer::Skeleton> skeleton_;
    std::vector<std::shared_ptr<schizo::renderer::AnimationClip>> clips_;
    std::unique_ptr<gws::renderer::gpu::SkinnedMesh> mesh_;
    std::shared_ptr<SharedSkinnedAsset>              shared_;   // the file and its textures (shared)
    std::unique_ptr<gws::renderer::gpu::Material>    mat_;
    std::vector<schizo::renderer::BoneTransform>     bind_, pose_a_, pose_b_;
    std::vector<glm::quat> procedural_;
    // place_feet: the legs, the pelvis and their springs.
    schizo::renderer::FootRig rig_;
    schizo::renderer::RagdollRig ragdoll_rig_;
    schizo::renderer::LocoWarpState loco_warp_;
    bool ragdoll_resolved_ = false;
    bool feet_resolved_ = false;
    bool report_feet_ = false;
    float log_tick_ = 0.0f, slide_sum_ = 0.0f, model_sum_ = 0.0f;
    glm::vec3 slide_prev_model_[2] = {glm::vec3(0.0f), glm::vec3(0.0f)}, body_prev_{0.0f};
    float body_sum_ = 0.0f;
    int body_n_ = 0;
    bool body_known_ = false;
    int slide_n_ = 0;
    glm::vec3 slide_prev_[2] = {glm::vec3(0.0f), glm::vec3(0.0f)};
    bool slide_down_[2] = {false, false};
    schizo::renderer::LeanState lean_state_;
    schizo::renderer::LookState look_state_;
    // look_at: the chain, and the springs of its yaw, pitch and aim (radians, 0..1).
    std::vector<int> look_chain_;
    // The pose shown (before the procedural offsets), this frame and the one before.
    std::vector<schizo::renderer::BoneTransform>     out_, out_prev_;
    float out_dt_ = 0.0f;
    // Inertialization: per bone, the offset still to decay and its velocity.
    std::vector<glm::vec3> off_p_, off_vp_, off_r_, off_vr_;
    std::vector<float> off_half_;   // each bone's half-life (the change that set it)
    struct InertReq { float half; std::vector<uint8_t> mask; };
    std::vector<InertReq> inert_reqs_;
    schizo::renderer::animgraph::GraphInstance graph_;
    GraphClips graph_src_;
    const void* graph_def_tag_ = nullptr;
    bool  inert_live_ = false;
    float inert_half_ = 0.05f, inert_age_ = 0.0f;
    std::vector<size_t> passed_;   // fire_events scratch
    std::vector<FiredEvent> fired_;

    Track base_, upper_;
    float upper_w_ = 0.0f, upper_target_ = 0.0f, upper_blend_ = 0.15f;
    std::vector<uint8_t> upper_mask_;

    glm::mat4 model_{1.0f};
    bool skinned_once_ = false;
    bool      use_model_ = false;   // set once an entity Transform drives this
    glm::vec3 world_pos_{0.0f};
    float     scale_ = 1.0f;
    std::string name_;
};

} // namespace schizo::editor
