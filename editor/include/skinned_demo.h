#pragma once

// ============================================================================
// SkinnedAnimDemo — a procedural, code-built skinned biped that exercises the
// WHOLE animation stack end-to-end (Animation Stage 5, "test rig" path):
//   Skeleton (9 bones) → AnimationStateMachine (idle<->walk) → root motion →
//   foot IK (per-leg, wavy ground) → GPU compute skinning → a DrawItem that
//   goes through the normal G-buffer/shadow/cull path.
//
// Bridges the two namespaces: schizo::renderer (skeleton/anim/IK, CPU) and
// gws::renderer::gpu (SkinnedMesh/Material/DrawItem, GPU). Self-contained so
// the pipeline is visible without importing a real rigged asset.
// ============================================================================

#include "skeleton.h"
#include "animation.h"
#include "animation_state_machine.h"
#include "ik.h"
#include "anim_pose.h"

#include "ai/navmesh.h"
#include "ai/nav_builder.h"
#include "ai/path_follow.h"

#include "vulkan/vulkan_skinned_mesh.h"
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/vulkan_texture_manager.h"
#include "vulkan/vulkan_render_graph.h"   // DrawItem

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <memory>
#include <vector>
#include <cstdint>

namespace schizo::editor {

class SkinnedAnimDemo {
public:
    static std::unique_ptr<SkinnedAnimDemo> create(
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout mat_layout, VkDescriptorPool mat_pool,
        gws::renderer::gpu::TextureManager* textures,
        const glm::vec3& world_pos) {
        auto d = std::unique_ptr<SkinnedAnimDemo>(new SkinnedAnimDemo());
        if (!d->build(device, mat_layout, mat_pool, textures, world_pos)) return nullptr;
        return d;
    }

    void set_enabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }

    /// Follow a floating-origin rebase. The demo carries its own navmesh,
    /// waypoints and follower, so none of it is reached by the scene-entity
    /// rebase loop -- it would keep walking its old route while the level moved
    /// out from under it.
    void apply_origin_shift(const glm::vec3& d) {
        if (d == glm::vec3(0.0f)) return;
        world_pos_ += d;
        for (glm::vec3& w : waypoints_) w += d;
        nav_.translate(d);
        follower_.translate(d);
    }

    /// CPU tick: navmesh path-follow a patrol loop → feed speed to the state
    /// machine, face the movement direction, foot IK. Call once per frame.
    void advance(float dt) {
        if (!enabled_) return;

        // Patrol: steer toward the current waypoint; on arrival, path-find to
        // the next one. Movement comes from steering (not root motion), so the
        // biped navigates the navmesh around the obstacle.
        auto st = follower_.steer(world_pos_, walk_speed_, facing_yaw_);
        if (st.arrived || !follower_.active()) {
            wp_ = (wp_ + 1) % waypoints_.size();
            std::vector<glm::vec3> path;
            if (nav_.find_path(world_pos_, waypoints_[wp_], path))
                follower_.set_path(path);
            st = follower_.steer(world_pos_, walk_speed_, facing_yaw_);
        }
        world_pos_ += st.velocity * dt;
        if (st.moving) facing_yaw_ = st.yaw;

        sm_->SetFloat("speed", st.speed);
        sm_->Update(dt);
        sm_->ConsumeRootMotionDelta();   // discard: steering drives position

        // Foot IK against a gentle wavy ground (shows per-foot adaptation).
        schizo::renderer::GroundQueryFn ground =
            [](const glm::vec3& o, const glm::vec3& dir, float md,
               glm::vec3& hp, glm::vec3& hn) {
                if (dir.y >= 0.0f) return false;
                const float gy = 0.08f * std::sin(o.x * 3.0f) * std::cos(o.z * 3.0f);
                const float t = (o.y - gy) / -dir.y;
                if (t < 0.0f || t > md) return false;
                hp = o + dir * t; hp.y = gy;
                hn = glm::normalize(glm::vec3(
                    -0.24f * std::cos(o.x * 3.0f) * std::cos(o.z * 3.0f), 1.0f,
                     0.24f * std::sin(o.x * 3.0f) * std::sin(o.z * 3.0f)));
                return true;
            };
        schizo::renderer::FootIkParams fp; fp.weight = 1.0f;
        schizo::renderer::LegChain L{lhip_, lknee_, lankle_, 0.11f};
        schizo::renderer::LegChain R{rhip_, rknee_, rankle_, 0.11f};
        schizo::renderer::SolveFootIK(*skel_, pelvis_, L, R, ground, fp);
    }

    /// GPU: record the skinning dispatch from the current pose. Call right
    /// after the frame command buffer begins (before RT/shadow/geometry read
    /// the skinned vertex buffer).
    void record_skin(VkCommandBuffer cmd) {
        if (!enabled_) return;
        const uint32_t n = skel_->GetBoneCount();
        std::vector<glm::mat4> globals(n);
        for (uint32_t i = 0; i < n; ++i) globals[i] = skel_->GetWorldMatrix(static_cast<uint16_t>(i));
        mesh_->skin(cmd, globals);
    }

    gws::renderer::gpu::DrawItem draw_item() const {
        gws::renderer::gpu::DrawItem d{};
        d.mesh          = mesh_->output_mesh();
        d.material      = mat_.get();
        // Face the movement direction (mesh forward is +Z; walk swings legs in Z).
        d.model = glm::translate(glm::mat4(1.0f), world_pos_) *
                  glm::rotate(glm::mat4(1.0f), facing_yaw_, glm::vec3(0, 1, 0));
        d.submesh_index = 0;
        d.is_blend      = false;
        d.is_terrain    = false;
        return d;
    }

private:
    SkinnedAnimDemo() = default;

    // --- box geometry: a limb segment from p0->p1, all verts weighted to `bone` ---
    void add_segment(std::vector<gws::renderer::gpu::SkinnedVertex>& verts,
                     std::vector<uint32_t>& idx,
                     const glm::vec3& p0, const glm::vec3& p1, float r, uint32_t bone) {
        glm::vec3 axis = p1 - p0;
        float len = glm::length(axis);
        glm::vec3 dir = len > 1e-4f ? axis / len : glm::vec3(0, 1, 0);
        if (len < 1e-4f) len = 2.0f * r;
        glm::vec3 up = std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 side = glm::normalize(glm::cross(dir, up));
        glm::vec3 up2  = glm::normalize(glm::cross(side, dir));
        const glm::vec3 center = (p0 + p1) * 0.5f;
        const uint32_t base = static_cast<uint32_t>(verts.size());
        for (int e = 0; e < 2; ++e)
            for (int s = -1; s <= 1; s += 2)
                for (int u = -1; u <= 1; u += 2) {
                    glm::vec3 pos = p0 + dir * (e ? len : 0.0f)
                                       + side * (r * s) + up2 * (r * u);
                    gws::renderer::gpu::SkinnedVertex v;
                    v.position = pos;
                    v.normal   = glm::normalize(pos - center);
                    v.uv       = glm::vec2(0.0f);
                    v.tangent  = glm::vec4(side, 1.0f);
                    v.joints   = glm::uvec4(bone, 0, 0, 0);
                    v.weights  = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                    verts.push_back(v);
                }
        // 8 corners ordered [e][s][u]; standard box faces.
        auto C = [base](int e, int s, int u) {
            return base + static_cast<uint32_t>((e << 2) | ((s > 0) << 1) | (u > 0));
        };
        const int quads[6][4] = {
            {C(0,-1,-1),C(0,1,-1),C(0,1,1),C(0,-1,1)},   // p0 cap
            {C(1,-1,-1),C(1,1,-1),C(1,1,1),C(1,-1,1)},   // p1 cap
            {C(0,-1,-1),C(1,-1,-1),C(1,-1,1),C(0,-1,1)}, // -side
            {C(0,1,-1), C(1,1,-1), C(1,1,1), C(0,1,1)},  // +side
            {C(0,-1,-1),C(1,-1,-1),C(1,1,-1),C(0,1,-1)}, // -up
            {C(0,-1,1), C(1,-1,1), C(1,1,1), C(0,1,1)},  // +up
        };
        for (auto& q : quads) {
            idx.insert(idx.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
        }
    }

    bool build(gws::renderer::gpu::VulkanDevice* device,
               VkDescriptorSetLayout mat_layout, VkDescriptorPool mat_pool,
               gws::renderer::gpu::TextureManager* textures,
               const glm::vec3& world_pos) {
        using namespace schizo::renderer;
        world_pos_ = world_pos;

        // --- patrol navmesh: a flat floor grid around the demo, with a square
        //     obstacle hole in the middle so the path-follow visibly detours ---
        {
            std::vector<glm::vec3> nverts;
            std::vector<uint32_t>  nidx;
            const int   R = 8;            // cells from -R..R (2m cells)
            const float C = 1.0f;         // cell size
            auto in_hole = [&](int x, int z) {   // 2x2-cell obstacle near centre
                return x >= 0 && x <= 1 && z >= 0 && z <= 1;
            };
            for (int z = -R; z < R; ++z)
                for (int x = -R; x < R; ++x) {
                    if (in_hole(x, z)) continue;
                    const uint32_t b = static_cast<uint32_t>(nverts.size());
                    const float x0 = world_pos.x + x * C, z0 = world_pos.z + z * C;
                    nverts.push_back({x0,     world_pos.y, z0});
                    nverts.push_back({x0 + C, world_pos.y, z0});
                    nverts.push_back({x0 + C, world_pos.y, z0 + C});
                    nverts.push_back({x0,     world_pos.y, z0 + C});
                    nidx.insert(nidx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
                }
            schizo::ai::NavMeshBuilder nb;
            nb.add_indexed(nverts, nidx, glm::mat4(1.0f));
            nb.build(nav_);
        }
        // Patrol corners in a bowtie so two of the four legs cross the centre
        // and must DETOUR around the obstacle (visible pathfinding), while the
        // other two run along clear edges.
        waypoints_ = {
            world_pos + glm::vec3(-4.0f, 0, -4.0f),
            world_pos + glm::vec3( 4.0f, 0,  4.0f),   // crosses centre → detour
            world_pos + glm::vec3( 4.0f, 0, -4.0f),
            world_pos + glm::vec3(-4.0f, 0,  4.0f),   // crosses centre → detour
        };
        world_pos_ = waypoints_[0];
        wp_ = 1;
        { std::vector<glm::vec3> path;
          if (nav_.find_path(world_pos_, waypoints_[wp_], path)) follower_.set_path(path); }

        // --- skeleton (bind pose, local transforms relative to parent) ---
        skel_ = std::make_shared<Skeleton>();
        auto B = [&](const char* n, const char* p, glm::vec3 lp) {
            BoneTransform t; t.position = lp; return skel_->AddBone(n, p, t);
        };
        pelvis_ = B("pelvis", "",       {0, 1.1f, 0});
        spine_  = B("spine",  "pelvis", {0, 0.45f, 0});
        head_   = B("head",   "spine",  {0, 0.35f, 0});
        lhip_   = B("lhip",   "pelvis", {-0.16f, 0, 0});
        lknee_  = B("lknee",  "lhip",   {0, -0.5f, 0});
        lankle_ = B("lankle", "lknee",  {0, -0.5f, 0});
        rhip_   = B("rhip",   "pelvis", { 0.16f, 0, 0});
        rknee_  = B("rknee",  "rhip",   {0, -0.5f, 0});
        rankle_ = B("rankle", "rknee",  {0, -0.5f, 0});
        skel_->UpdateWorldTransforms();

        // --- inverse-bind matrices from the bind world matrices ---
        const uint32_t nb = skel_->GetBoneCount();
        std::vector<glm::mat4> inv_bind(nb);
        std::vector<glm::vec3> bindpos(nb);
        for (uint32_t i = 0; i < nb; ++i) {
            const glm::mat4 w = skel_->GetWorldMatrix(static_cast<uint16_t>(i));
            inv_bind[i] = glm::inverse(w);
            bindpos[i]  = glm::vec3(w[3]);
        }

        // --- procedural boxy body: a segment per parent->child bone ---
        std::vector<gws::renderer::gpu::SkinnedVertex> verts;
        std::vector<uint32_t> idx;
        const auto& bones = skel_->GetAllBones();
        for (uint32_t i = 0; i < nb; ++i) {
            if (bones[i].parent_id < 0) continue;
            const uint32_t par = static_cast<uint32_t>(bones[i].parent_id);
            const float r = (i == spine_) ? 0.16f : 0.07f;   // torso thicker
            add_segment(verts, idx, bindpos[par], bindpos[i], r, par);
        }
        // Feet: small boxes weighted to the ankles so foot IK is visible.
        add_segment(verts, idx, bindpos[lankle_],
                    bindpos[lankle_] + glm::vec3(0, -0.02f, 0.18f), 0.06f, lankle_);
        add_segment(verts, idx, bindpos[rankle_],
                    bindpos[rankle_] + glm::vec3(0, -0.02f, 0.18f), 0.06f, rankle_);
        // Head cap.
        add_segment(verts, idx, bindpos[head_],
                    bindpos[head_] + glm::vec3(0, 0.18f, 0), 0.12f, head_);

        mesh_ = gws::renderer::gpu::SkinnedMesh::create(device, verts, idx, {}, inv_bind);
        if (!mesh_) return false;

        // --- animation clips ---
        auto bindT = [&](uint16_t bone) {
            BoneTransform t; t.position = bones[bone].local_transform.position; return t;
        };
        auto rot = [](glm::vec3 axis, float deg) {
            return glm::angleAxis(glm::radians(deg), glm::normalize(axis));
        };

        // Idle: a gentle spine bob.
        idle_ = std::make_shared<AnimationClip>("idle", 30.0f);
        { auto* tr = idle_->AddBoneAnimation(spine_);
          BoneTransform a = bindT(spine_), b = bindT(spine_);
          b.position.y += 0.03f;
          tr->AddKeyframe(0.0f, a); tr->AddKeyframe(0.9f, b); tr->AddKeyframe(1.8f, a); }
        idle_->SetLooping(true); idle_->UpdateDuration();

        // Walk: hips swing (opposite phase), knees bend on the back-swing,
        // root advances +Z (root-motion source).
        walk_ = std::make_shared<AnimationClip>("walk", 30.0f);
        auto swing = [&](uint16_t hip, uint16_t knee, float phase) {
            auto* th = walk_->AddBoneAnimation(hip);
            auto* tk = walk_->AddBoneAnimation(knee);
            for (int k = 0; k <= 4; ++k) {
                const float t = k * 0.25f;
                const float s = std::sin((t + phase) * 2.0f * 3.14159265f);
                BoneTransform h = bindT(hip);  h.rotation = rot({1, 0, 0}, 28.0f * s);
                BoneTransform kn = bindT(knee);
                kn.rotation = rot({1, 0, 0}, -40.0f * std::max(0.0f, -s)); // bend trailing leg
                th->AddKeyframe(t, h); tk->AddKeyframe(t, kn);
            }
        };
        swing(lhip_, lknee_, 0.0f);
        swing(rhip_, rknee_, 0.5f);
        { auto* trr = walk_->AddBoneAnimation(pelvis_);
          BoneTransform a = bindT(pelvis_), b = bindT(pelvis_);
          a.position.z = 0.0f; b.position.z = 1.4f;               // 1.4 m per cycle
          trr->AddKeyframe(0.0f, a); trr->AddKeyframe(1.0f, b); }
        walk_->SetLooping(true); walk_->UpdateDuration();

        // --- state machine ---
        sm_ = std::make_unique<AnimationStateMachine>(skel_);
        sm_->SetRootBone(pelvis_);
        AnimState si; si.name = "idle"; si.clip = idle_; si.loop = true;
        AnimState sw; sw.name = "walk"; sw.clip = walk_; sw.loop = true; sw.apply_root_motion = true;
        const int IDLE = sm_->AddState(si);
        const int WALK = sm_->AddState(sw);
        sm_->SetEntryState(IDLE);
        { AnimTransition t; t.from = IDLE; t.to = WALK; t.blend_duration = 0.25f;
          t.conditions.push_back({"speed", AnimCondOp::Greater, 0.1f}); sm_->AddTransition(t); }
        { AnimTransition t; t.from = WALK; t.to = IDLE; t.blend_duration = 0.25f;
          t.conditions.push_back({"speed", AnimCondOp::Less, 0.1f}); sm_->AddTransition(t); }

        // --- material (warm orange, matte) ---
        gws::renderer::gpu::MaterialUniforms mu{};
        mu.base_color_factor = glm::vec4(0.90f, 0.45f, 0.15f, 1.0f);
        mu.metallic_factor = 0.0f; mu.roughness_factor = 0.6f;
        const gws::renderer::gpu::Texture* dw = textures ? textures->white()  : nullptr;
        const gws::renderer::gpu::Texture* dn = textures ? textures->normal() : nullptr;
        const gws::renderer::gpu::Texture* db = textures ? textures->black()  : nullptr;
        mat_ = gws::renderer::gpu::Material::create(device, mat_layout, mat_pool, mu,
                                                    nullptr, nullptr, nullptr, nullptr, nullptr,
                                                    dw, dn, db);
        return mat_ != nullptr;
    }

    bool enabled_ = true;
    glm::vec3 world_pos_{0.0f};
    float facing_yaw_ = 0.0f;
    float walk_speed_ = 1.6f;

    // Navmesh patrol.
    schizo::ai::NavMesh          nav_;
    schizo::ai::PathFollower     follower_;
    std::vector<glm::vec3>       waypoints_;
    size_t                       wp_ = 0;

    std::shared_ptr<schizo::renderer::Skeleton> skel_;
    std::shared_ptr<schizo::renderer::AnimationClip> idle_, walk_;
    std::unique_ptr<schizo::renderer::AnimationStateMachine> sm_;
    std::unique_ptr<gws::renderer::gpu::SkinnedMesh> mesh_;
    std::unique_ptr<gws::renderer::gpu::Material> mat_;

    uint16_t pelvis_ = 0, spine_ = 0, head_ = 0;
    uint16_t lhip_ = 0, lknee_ = 0, lankle_ = 0, rhip_ = 0, rknee_ = 0, rankle_ = 0;
};

} // namespace schizo::editor
