#pragma once
// ============================================================================
// spring_bones.h -- secondary motion: chains of bones that hang, sway and
// settle on their own (an antenna, a strap, a ponytail, a tail, a pouch), on
// top of whatever the clips and the other layers posed.
//
// Each bone of a chain has a tip moved in the WORLD by Verlet integration, so
// the body's own motion -- a start, a stop, a turn -- leaves the chain behind
// and it catches up: it keeps its momentum (less `damping`), is pulled toward
// where the clip has it (`stiffness`), falls with `gravity`, keeps its length,
// and is pushed out of sphere colliders (a head, a back). The bone is then
// turned so its tip points there; its children follow. Fixed 60 Hz substeps:
// the same swing at any frame rate.
//
// Chains come from a model's `<model>.springs` file (parse_spring_file):
//
//   # first bone      last bone       stiffness damping gravity [radius]
//   chain antenna_01  antenna_04      0.08      0.12    0.6     0.01   [max angle]
//   collider chest    0 0.05 0.12     0.2       # bone, centre in its space, radius
//
// Pure math on a Skeleton: checked by tools/springbones_check.
// ============================================================================
#include "skeleton.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace schizo::renderer {

struct SpringChainDef {
    std::string first, last;
    float stiffness = 0.08f;   // 0..1 a step: how strongly it returns to the clip's pose
    float damping = 0.12f;     // 0..1 a step: how much of its motion it loses
    float gravity = 0.5f;      // fraction of 9.81 m/s^2 pulling its tips down
    float radius = 0.01f;      // its tips' radius against the colliders (m)
    float max_angle = 70.0f;   // degrees a segment may bend from where it rests (no flipping over)
};
struct SpringColliderDef {
    std::string bone;
    glm::vec3 centre{0.0f};    // in the bone's space
    float radius = 0.1f;       // (m, model units scaled by the model)
};
struct SpringFile {
    std::vector<SpringChainDef> chains;
    std::vector<SpringColliderDef> colliders;
};

/// Reads a `.springs` file's text (see the top of this file). Unknown lines are skipped.
inline SpringFile parse_spring_file(const std::string& text) {
    SpringFile f;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream l(line);
        std::string word;
        if (!(l >> word)) continue;
        if (word == "chain") {
            SpringChainDef c;
            if (!(l >> c.first >> c.last)) continue;
            l >> c.stiffness >> c.damping >> c.gravity >> c.radius >> c.max_angle;
            c.stiffness = std::clamp(c.stiffness, 0.0f, 1.0f);
            c.damping = std::clamp(c.damping, 0.0f, 1.0f);
            f.chains.push_back(c);
        } else if (word == "collider") {
            SpringColliderDef c;
            if (!(l >> c.bone >> c.centre.x >> c.centre.y >> c.centre.z >> c.radius)) continue;
            f.colliders.push_back(c);
        }
    }
    return f;
}

/// One character's spring chains, resolved on its skeleton and simulated.
class SpringBones {
public:
    /// Resolves the file's chains (a chain: `last` and its parents up to `first`)
    /// and colliders on a skeleton by bone names (`find_bone`).
    template <class FindBone>
    void init(const Skeleton& sk, const SpringFile& file, FindBone&& find_bone) {
        chains_.clear();
        colliders_.clear();
        for (const auto& d : file.chains) {
            const int first = find_bone(d.first), last = find_bone(d.last);
            if (first < 0 || last < 0) continue;
            Chain c;
            c.def = d;
            for (int b = last; b >= 0; b = sk.GetBone(static_cast<uint16_t>(b))->parent_id) {
                c.bones.insert(c.bones.begin(), b);
                if (b == first) break;
            }
            if (c.bones.empty() || c.bones.front() != first) continue;   // (last is not under first)
            // Each bone's tip: its child's head in its own space (the last: its own length,
            // taken as its parent's tip offset scaled, else 5 cm along its Y).
            for (size_t i = 0; i < c.bones.size(); ++i) {
                glm::vec3 tip;
                if (i + 1 < c.bones.size()) tip = sk.GetBone(static_cast<uint16_t>(c.bones[i + 1]))->local_transform.position;
                else tip = i > 0 ? c.tip_local.back() : glm::vec3(0.0f, 0.05f, 0.0f);
                c.tip_local.push_back(tip);
            }
            c.p.assign(c.bones.size(), glm::vec3(0.0f));
            c.prev.assign(c.bones.size(), glm::vec3(0.0f));
            c.shown.assign(c.bones.size(), glm::vec3(0.0f));
            chains_.push_back(std::move(c));
        }
        for (const auto& d : file.colliders) {
            const int b = find_bone(d.bone);
            if (b >= 0) colliders_.push_back({b, d.centre, d.radius});
        }
        fresh_ = true;
    }
    bool empty() const { return chains_.empty(); }
    /// Starts again from the pose (a teleport, a respawn): no motion carried.
    void reset() { fresh_ = true; }

    /// After the pose is final (the clips and the other layers): moves the tips
    /// on `dt` (in 60 Hz substeps) with the model placed at `model`, and turns
    /// the chains' bones toward them. `weight` 0..1 blends from the clip's pose.
    void update(Skeleton& sk, const glm::mat4& model, float dt, float weight = 1.0f) {
        if (chains_.empty() || dt <= 0.0f) return;
        const float unit = glm::length(glm::vec3(model[1]));
        if (unit < 1e-6f) return;
        const glm::mat4 to_model = glm::inverse(model);
        glm::mat4 at = model;   // the model where this substep has it
        auto world = [&](int b) { return at * sk.GetWorldMatrix(static_cast<uint16_t>(b)); };
        struct Sphere { glm::vec3 c; float r; };
        std::vector<Sphere> spheres;
        if (!fresh_ && glm::length(glm::vec3(model[3]) - glm::vec3(last_model_[3])) > 3.0f * unit) fresh_ = true;   // a teleport
        if (fresh_) {
            for (auto& ch : chains_)
                for (size_t i = 0; i < ch.bones.size(); ++i) {
                    ch.p[i] = ch.prev[i] = glm::vec3(world(ch.bones[i]) * glm::vec4(ch.tip_local[i], 1.0f));
                }
            for (auto& ch : chains_) ch.base_prev = glm::vec3(world(ch.bones[0])[3]);
            fresh_ = false;
            acc_ = 0.0f;
            last_model_ = model;
        }
        const float before = acc_;
        acc_ = std::min(acc_ + dt, 0.25f);   // (a long hitch is not simulated step by step)
        const float h = 1.0f / 60.0f;
        int steps = 0;
        while (acc_ >= h && steps < 15) { acc_ -= h; ++steps; }
        // Each substep sees the model where it was at that moment of the frame
        // (between where it was last frame and now): the same motion at any frame rate.
        const float span = std::max(1e-6f, std::min(dt, 0.25f));
        for (int s = 0; s < steps; ++s) {
            const float when = std::clamp((h * static_cast<float>(s + 1) - before) / span, 0.0f, 1.0f);
            at = blend_model(last_model_, model, when);
            spheres.clear();
            for (const auto& c : colliders_) spheres.push_back({glm::vec3(world(c.bone) * glm::vec4(c.centre, 1.0f)), c.radius * unit});
            for (auto& ch : chains_) {
                const size_t n = ch.bones.size();
                // How the segment above has turned from the clip's pose (a rod bends
                // between neighbours: each segment rests along its parent as it is now).
                glm::quat parent_turn(1.0f, 0.0f, 0.0f, 0.0f);
                // Damping takes away motion relative to the body only: carried along at a
                // steady speed a chain feels no drag (it lags only while the body speeds up).
                const glm::vec3 base_now(world(ch.bones[0])[3]);
                const glm::vec3 body_v = base_now - ch.base_prev;
                ch.base_prev = base_now;
                for (size_t i = 0; i < n; ++i) {
                    const glm::vec3 head = i == 0 ? glm::vec3(world(ch.bones[0])[3]) : ch.p[i - 1];
                    // Where the clip has the tip: its direction, turned as its parent is.
                    const glm::vec3 anim_tip = glm::vec3(world(ch.bones[i]) * glm::vec4(ch.tip_local[i], 1.0f));
                    const glm::vec3 anim_dir = anim_tip - glm::vec3(world(ch.bones[i])[3]);
                    const glm::vec3 target = head + parent_turn * anim_dir;
                    const float len = glm::length(anim_dir);
                    glm::vec3 rel = ((ch.p[i] - ch.prev[i]) - body_v) * (1.0f - ch.def.damping);
                    if (glm::length(rel) > len) rel = glm::normalize(rel) * len;   // (a violent jolt does not throw it over)
                    const glm::vec3 v = body_v + rel;
                    ch.prev[i] = ch.p[i];
                    // Its momentum first, then pulled from there toward where it rests (the
                    // rest point has moved with the body this step: pulling from where the tip
                    // was would drive it ahead of the body at any steady speed).
                    const glm::vec3 carried = ch.p[i] + v;
                    glm::vec3 next = carried + (target - carried) * ch.def.stiffness + glm::vec3(0.0f, -9.81f * ch.def.gravity * unit, 0.0f) * (h * h);
                    // Bent at most max_angle from where it rests.
                    {
                        const glm::vec3 rest = glm::normalize(target - head);
                        glm::vec3 dir = next - head;
                        if (glm::length(dir) > 1e-7f) {
                            dir = glm::normalize(dir);
                            const float limit = glm::radians(std::clamp(ch.def.max_angle, 1.0f, 179.0f));
                            const float angle = std::acos(std::clamp(glm::dot(dir, rest), -1.0f, 1.0f));
                            if (angle > limit) {
                                const glm::quat q = rotation_between_(rest, dir);
                                dir = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), q, limit / angle) * rest;
                                next = head + dir * len;
                            }
                        }
                    }
                    // Its length kept, and out of the colliders -- the colliders last (where
                    // both cannot hold, a tip a hair long beats one inside a head; the bone
                    // only takes its tip's direction).
                    for (int pass = 0; pass < 2; ++pass) {
                        const glm::vec3 d = next - head;
                        if (glm::length(d) > 1e-7f) next = head + glm::normalize(d) * len;
                        for (const auto& sp : spheres) {
                            const glm::vec3 o = next - sp.c;
                            const float r = sp.r + ch.def.radius * unit;
                            if (glm::length(o) < r && glm::length(o) > 1e-7f) next = sp.c + glm::normalize(o) * r;
                        }
                    }
                    ch.p[i] = next;
                    if (glm::length(anim_dir) > 1e-7f && glm::length(next - head) > 1e-7f)
                        parent_turn = rotation_between_(glm::normalize(anim_dir), glm::normalize(next - head));
                }
            }
        }
        last_model_ = model;
        at = model;
        for (auto& ch : chains_) {
            const size_t n = ch.bones.size();
            // The bones turned toward their tips (from the root down; children follow).
            for (size_t i = 0; i < n; ++i) {
                const int b = ch.bones[i];
                const glm::mat4 wm = world(b);
                const glm::vec3 head(wm[3]);
                const glm::vec3 anim_tip = glm::vec3(wm * glm::vec4(ch.tip_local[i], 1.0f));
                const glm::vec3 from = glm::vec3(to_model * glm::vec4(anim_tip - head, 0.0f));
                // Shown between the last two substeps by the time left over (a fixed step
                // drawn in between: what is seen does not hang on where a frame falls).
                const glm::vec3 shown = glm::mix(ch.prev[i], ch.p[i], std::clamp(acc_ / h, 0.0f, 1.0f));
                ch.shown[i] = shown;
                const glm::vec3 to = glm::vec3(to_model * glm::vec4(shown - head, 0.0f));
                if (glm::length(from) < 1e-7f || glm::length(to) < 1e-7f) continue;
                glm::quat turn = rotation_between_(glm::normalize(from), glm::normalize(to));
                if (weight < 1.0f) turn = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), turn, std::clamp(weight, 0.0f, 1.0f));
                // The turn is in the model; into the bone's local rotation through its parent.
                const int parent = sk.GetBone(static_cast<uint16_t>(b))->parent_id;
                const glm::quat parent_rot = parent >= 0 ? rot_of(sk.GetWorldMatrix(static_cast<uint16_t>(parent))) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                const glm::quat bone_rot = rot_of(sk.GetWorldMatrix(static_cast<uint16_t>(b)));
                BoneTransform tf = sk.GetBone(static_cast<uint16_t>(b))->local_transform;
                tf.rotation = glm::normalize(glm::inverse(parent_rot) * turn * bone_rot);
                sk.SetBoneLocalTransform(static_cast<uint16_t>(b), tf);
                sk.UpdateWorldTransforms();
            }
        }
    }

    /// A chain's simulated tips in the world (for checks and debugging).
    const std::vector<glm::vec3>& tips(size_t chain) const { return chains_[chain].p; }
    /// As shown this frame (between the last two substeps).
    const std::vector<glm::vec3>& shown_tips(size_t chain) const { return chains_[chain].shown; }
    size_t chain_count() const { return chains_.size(); }
    const std::vector<int>& chain_bones(size_t chain) const { return chains_[chain].bones; }

private:
    struct Chain {
        SpringChainDef def;
        std::vector<int> bones;
        std::vector<glm::vec3> tip_local;   // each bone's tip in its own space
        std::vector<glm::vec3> p, prev;     // the tips, simulated (world): this substep and the one before
        std::vector<glm::vec3> shown;       // as drawn (between them)
        glm::vec3 base_prev{0.0f};          // its first bone's head at the last substep (the body's motion)
    };
    struct Collider { int bone; glm::vec3 centre; float radius; };
    static glm::quat rot_of(const glm::mat4& m) {
        return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])))));
    }
    static glm::quat rotation_between_(const glm::vec3& a, const glm::vec3& b) {
        const float d = glm::dot(a, b);
        if (d > 0.999999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (d < -0.999999f) {
            glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), a);
            if (glm::length(axis) < 1e-4f) axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), a);
            return glm::angleAxis(3.14159265f, glm::normalize(axis));
        }
        const glm::vec3 c = glm::cross(a, b);
        return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
    }
    /// Between two placements of the model: position and scale mixed, turn slerped.
    static glm::mat4 blend_model(const glm::mat4& a, const glm::mat4& b, float t) {
        if (t >= 1.0f) return b;
        if (t <= 0.0f) return a;
        const glm::vec3 sa(glm::length(glm::vec3(a[0])), glm::length(glm::vec3(a[1])), glm::length(glm::vec3(a[2])));
        const glm::vec3 sb(glm::length(glm::vec3(b[0])), glm::length(glm::vec3(b[1])), glm::length(glm::vec3(b[2])));
        const glm::quat q = glm::slerp(rot_of(a), rot_of(b), t);
        const glm::vec3 sc = glm::mix(sa, sb, t);
        glm::mat4 m = glm::mat4_cast(q);
        m[0] *= sc.x;
        m[1] *= sc.y;
        m[2] *= sc.z;
        m[3] = glm::vec4(glm::mix(glm::vec3(a[3]), glm::vec3(b[3]), t), 1.0f);
        return m;
    }
    std::vector<Chain> chains_;
    std::vector<Collider> colliders_;
    glm::mat4 last_model_{1.0f};
    float acc_ = 0.0f;
    bool fresh_ = true;
};

}  // namespace schizo::renderer
