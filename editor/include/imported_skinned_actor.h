#pragma once
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
#include "animation.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
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
    std::unique_ptr<gws::renderer::gpu::Texture> albedo, mr, normal;
};
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
    void play(int i, bool loop, float blend = 0.2f, float speed = 1.0f, bool restart = false) {
        if (i < 0 || static_cast<size_t>(i) >= clips_.size()) return;
        base_.speed = speed;
        if (!restart && i == base_.clip && loop == base_.loop) return;
        if (blend > 0.0f && base_.clip >= 0) { prev_ = base_; fade_ = 0.0f; fade_len_ = blend; }
        else                                  { prev_.clip = -1; }
        base_.clip = i;
        base_.time = 0.0f;
        base_.loop = loop;
    }

    /// The upper body (the bones under `set_upper_body`'s bone) plays clip `i`
    /// over the base clip; -1 lets it go back to the base clip. Blended either way.
    void play_upper(int i, bool loop, float blend = 0.15f, bool restart = false) {
        upper_blend_ = std::max(blend, 1e-3f);
        if (i < 0 || static_cast<size_t>(i) >= clips_.size() || upper_mask_.empty()) { upper_target_ = 0.0f; return; }
        upper_target_ = 1.0f;
        if (!restart && i == upper_.clip && loop == upper_.loop && upper_w_ > 0.0f) return;
        upper_.clip = i;
        upper_.time = 0.0f;
        upper_.loop = loop;
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
        base_.time += dt * base_.speed;
        prev_.time += dt * base_.speed;
        upper_.time += dt;
        fade_ += dt;
        const float step = dt / upper_blend_;
        upper_w_ = upper_target_ > upper_w_ ? std::min(upper_target_, upper_w_ + step) : std::max(upper_target_, upper_w_ - step);
        pose();
    }

    /// Pose the skeleton WITHOUT advancing time. A paused actor still has to be
    /// posed on the first frame, or it renders in bind pose until you press
    /// play -- which reads as "the model is broken", not "it is paused".
    void refresh_pose() { pose(); }

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
    /// A bone's place in the world (the actor's model x the posed bone).
    glm::mat4 bone_world(int bone) const {
        if (!skeleton_ || bone < 0 || static_cast<uint32_t>(bone) >= skeleton_->GetBoneCount()) return model();
        return model() * skeleton_->GetWorldMatrix(static_cast<uint16_t>(bone));
    }

    /// GPU: record the skinning dispatch from the current pose. Call right after
    /// the frame command buffer begins (before shadow/geometry read the vbo).
    void record_skin(VkCommandBuffer cmd) {
        if (!mesh_ || !skeleton_) return;
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
    };

    static std::string lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Sample one clip from the bind pose (so bones it does not animate are not
    // left where another clip put them) and copy the locals out.
    void sample(const Track& tr, std::vector<schizo::renderer::BoneTransform>& out) {
        const auto& bones = skeleton_->GetAllBones();
        for (size_t b = 0; b < bones.size() && b < bind_.size(); ++b)
            skeleton_->SetBoneLocalTransform(static_cast<uint16_t>(b), bind_[b]);
        const auto& clip = clips_[static_cast<size_t>(tr.clip)];
        const float t = tr.loop ? tr.time : std::min(tr.time, std::max(0.0f, clip->GetDuration() - 1e-4f));
        clip->Sample(*skeleton_, t, tr.loop);
        out.resize(bones.size());
        for (size_t b = 0; b < bones.size(); ++b) out[b] = skeleton_->GetAllBones()[b].local_transform;
    }

    void pose() {
        if (!skeleton_ || clips_.empty()) return;
        if (base_.clip < 0) base_.clip = 0;
        using schizo::renderer::BoneTransform;
        sample(base_, pose_a_);
        if (prev_.clip >= 0 && fade_ < fade_len_) {
            sample(prev_, pose_b_);
            const float w = fade_ / std::max(fade_len_, 1e-4f);
            for (size_t b = 0; b < pose_a_.size(); ++b) pose_a_[b] = BoneTransform::Slerp(pose_b_[b], pose_a_[b], w);
        } else {
            prev_.clip = -1;
        }
        if (upper_.clip >= 0 && upper_w_ > 0.0f && !upper_mask_.empty()) {
            sample(upper_, pose_b_);
            for (size_t b = 0; b < pose_a_.size() && b < upper_mask_.size(); ++b)
                if (upper_mask_[b]) pose_a_[b] = BoneTransform::Slerp(pose_a_[b], pose_b_[b], upper_w_);
        }
        for (size_t b = 0; b < pose_a_.size(); ++b) skeleton_->SetBoneLocalTransform(static_cast<uint16_t>(b), pose_a_[b]);
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
            it = cache.emplace(gltf_path, std::move(sa)).first;
        }
        shared_ = it->second;
        const auto& loaded = shared_->model;

        skeleton_  = std::shared_ptr<schizo::renderer::Skeleton>(loaded.skeleton->Clone().release());   // its own pose
        clips_     = loaded.animations;                                                                     // read only: shared
        world_pos_ = world_pos;
        name_      = std::filesystem::path(gltf_path).filename().string();
        for (const auto& b : skeleton_->GetAllBones()) bind_.push_back(b.local_transform);

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
        }
        const gws::renderer::gpu::Texture* dw = textures ? textures->white()  : nullptr;
        const gws::renderer::gpu::Texture* dn = textures ? textures->normal() : nullptr;
        const gws::renderer::gpu::Texture* db = textures ? textures->black()  : nullptr;
        mat_ = gws::renderer::gpu::Material::create(device, mat_layout, mat_pool, mu,
                                                    shared_->albedo.get(), shared_->normal.get(), shared_->mr.get(), nullptr, nullptr,
                                                    dw, dn, db);
        return mat_ != nullptr;
    }

    std::shared_ptr<schizo::renderer::Skeleton> skeleton_;
    std::vector<std::shared_ptr<schizo::renderer::AnimationClip>> clips_;
    std::unique_ptr<gws::renderer::gpu::SkinnedMesh> mesh_;
    std::shared_ptr<SharedSkinnedAsset>              shared_;   // the file and its textures (shared)
    std::unique_ptr<gws::renderer::gpu::Material>    mat_;
    std::vector<schizo::renderer::BoneTransform>     bind_, pose_a_, pose_b_;

    Track base_, prev_, upper_;
    float fade_ = 0.0f, fade_len_ = 0.0f;
    float upper_w_ = 0.0f, upper_target_ = 0.0f, upper_blend_ = 0.15f;
    std::vector<uint8_t> upper_mask_;

    glm::mat4 model_{1.0f};
    bool      use_model_ = false;   // set once an entity Transform drives this
    glm::vec3 world_pos_{0.0f};
    float     scale_ = 1.0f;
    std::string name_;
};

} // namespace schizo::editor
