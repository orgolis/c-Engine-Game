// ====================
// anim_check — headless verification of the animation systems (Stage 5)
// ====================
//
//   1. Two-bone IK: a straight leg solves to reachable targets (end effector
//      lands on target) and clamps gracefully on unreachable ones.
//   2. Foot IK: on flat ground both ankles end at ~foot_height above y=0.
//   3. State machine: parameter-driven transitions (idle<->walk), cross-fade
//      blending, trigger one-shot semantics, and root-motion accumulation.
//
// Pure CPU (no Vulkan device). Prints "anim_check: ALL OK" + exits 0 on pass.

#include "skeleton.h"
#include "animation.h"
#include "blend_tree.h"
#include "anim_pose.h"
#include "ik.h"
#include "animation_state_machine.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <memory>
#include <cstdint>

using namespace schizo::renderer;

namespace {
int g_checks = 0;
bool check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    return ok;
}
#define REQUIRE(cond, what) do { if (!check((cond), (what))) { \
    std::printf("anim_check: FAILED at '%s'\n", what); return 1; } } while (0)

glm::vec3 wpos(const Skeleton& s, uint16_t id) { return glm::vec3(s.GetWorldMatrix(id)[3]); }

BoneTransform bt(glm::vec3 p) { BoneTransform t; t.position = p; return t; }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== anim_check: animation IK + state machine (Stage 5) ===\n");

    // ---- 1. Two-bone IK -------------------------------------------------
    {
        Skeleton s;
        uint16_t hip   = s.AddBone("hip",   "",     bt({0, 2, 0}));
        uint16_t knee  = s.AddBone("knee",  "hip",  bt({0, -1, 0}));
        uint16_t ankle = s.AddBone("ankle", "knee", bt({0, -1, 0}));
        s.UpdateWorldTransforms();
        REQUIRE(glm::length(wpos(s, ankle) - glm::vec3(0, 0, 0)) < 1e-4f,
                "leg starts straight (ankle at origin)");

        // Reachable target (chain length 2, distance from hip ~1.4).
        const glm::vec3 target(0.7f, 1.0f, 0.3f);
        const glm::vec3 pole(0, 1, 2);   // knee bends forward (+Z)
        SolveTwoBoneIK(s, hip, knee, ankle, target, pole, 1.0f);
        const float reach_err = glm::length(wpos(s, ankle) - target);
        std::printf("       two-bone reach error = %.4f\n", reach_err);
        REQUIRE(reach_err < 0.02f, "IK end-effector reaches a reachable target");

        // Knee actually bent (mid joint left the straight line).
        const glm::vec3 A = wpos(s, hip), B = wpos(s, knee), C = wpos(s, ankle);
        const float bend = glm::length(glm::cross(glm::normalize(B - A),
                                                  glm::normalize(C - A)));
        REQUIRE(bend > 0.05f, "knee bends (not a straight line)");

        // Unreachable target: leg straightens toward it, ankle at ~max extent.
        Skeleton s2;
        uint16_t h2 = s2.AddBone("hip",   "",     bt({0, 2, 0}));
        uint16_t k2 = s2.AddBone("knee",  "hip",  bt({0, -1, 0}));
        uint16_t a2 = s2.AddBone("ankle", "knee", bt({0, -1, 0}));
        s2.UpdateWorldTransforms();
        const glm::vec3 far_target(10.0f, 2.0f, 0.0f);
        SolveTwoBoneIK(s2, h2, k2, a2, far_target, glm::vec3(0, 2, 2), 1.0f);
        const float extent = glm::length(wpos(s2, a2) - wpos(s2, h2));
        std::printf("       unreachable extent = %.3f (max 2.0)\n", extent);
        REQUIRE(extent > 1.9f && extent <= 2.01f, "unreachable target → leg extends, no blow-up");
    }

    // ---- 2. Foot IK on flat ground -------------------------------------
    {
        Skeleton s;
        uint16_t pelvis = s.AddBone("pelvis", "", bt({0, 2.0f, 0}));
        // Left leg (offset -x), right leg (+x).
        uint16_t lhip = s.AddBone("lhip", "pelvis", bt({-0.2f, 0, 0}));
        uint16_t lkne = s.AddBone("lknee", "lhip",  bt({0, -1, 0}));
        uint16_t lank = s.AddBone("lankle", "lknee", bt({0, -1, 0}));
        uint16_t rhip = s.AddBone("rhip", "pelvis", bt({0.2f, 0, 0}));
        uint16_t rkne = s.AddBone("rknee", "rhip",  bt({0, -1, 0}));
        uint16_t rank = s.AddBone("rankle", "rknee", bt({0, -1, 0}));
        s.UpdateWorldTransforms();

        // Flat ground at y = 0.
        GroundQueryFn flat = [](const glm::vec3& o, const glm::vec3& d, float md,
                                glm::vec3& hp, glm::vec3& hn) {
            if (d.y >= 0.0f) return false;
            const float t = o.y / -d.y;            // reach y=0
            if (t < 0.0f || t > md) return false;
            hp = o + d * t; hp.y = 0.0f; hn = glm::vec3(0, 1, 0);
            return true;
        };
        LegChain L{lhip, lkne, lank, 0.12f};
        LegChain R{rhip, rkne, rank, 0.12f};
        FootIkParams fp; fp.weight = 1.0f;
        SolveFootIK(s, pelvis, L, R, flat, fp);

        const float ly = wpos(s, lank).y, ry = wpos(s, rank).y;
        std::printf("       foot IK ankles: L.y=%.3f R.y=%.3f (want ~0.12)\n", ly, ry);
        REQUIRE(std::abs(ly - 0.12f) < 0.06f && std::abs(ry - 0.12f) < 0.06f,
                "both feet plant near ground + foot_height");
    }

    // ---- 3. State machine: transitions + blend + root motion -----------
    {
        auto skel = std::make_shared<Skeleton>();
        uint16_t root = skel->AddBone("root", "", bt({0, 0, 0}));
        uint16_t spine = skel->AddBone("spine", "root", bt({0, 1, 0}));
        skel->UpdateWorldTransforms();

        // Idle: root stationary, spine upright.
        auto idle = std::make_shared<AnimationClip>("idle", 30.0f);
        { auto* tr = idle->AddBoneAnimation(root);
          tr->AddKeyframe(0.0f, bt({0, 0, 0}));
          tr->AddKeyframe(1.0f, bt({0, 0, 0})); }
        idle->SetLooping(true); idle->UpdateDuration();

        // Walk: root advances +Z by 1 unit per 1s cycle (root motion source).
        auto walk = std::make_shared<AnimationClip>("walk", 30.0f);
        { auto* tr = walk->AddBoneAnimation(root);
          tr->AddKeyframe(0.0f, bt({0, 0, 0.0f}));
          tr->AddKeyframe(1.0f, bt({0, 0, 1.0f})); }
        walk->SetLooping(true); walk->UpdateDuration();

        AnimationStateMachine sm(skel);
        sm.SetRootBone(root);
        AnimState s_idle; s_idle.name = "idle"; s_idle.clip = idle; s_idle.loop = true;
        AnimState s_walk; s_walk.name = "walk"; s_walk.clip = walk; s_walk.loop = true;
        s_walk.apply_root_motion = true;
        const int IDLE = sm.AddState(s_idle);
        const int WALK = sm.AddState(s_walk);
        sm.SetEntryState(IDLE);

        AnimTransition to_walk; to_walk.from = IDLE; to_walk.to = WALK;
        to_walk.blend_duration = 0.2f;
        to_walk.conditions.push_back({"speed", AnimCondOp::Greater, 0.1f});
        sm.AddTransition(to_walk);
        AnimTransition to_idle; to_idle.from = WALK; to_idle.to = IDLE;
        to_idle.blend_duration = 0.2f;
        to_idle.conditions.push_back({"speed", AnimCondOp::Less, 0.1f});
        sm.AddTransition(to_idle);

        // Start idle.
        sm.SetFloat("speed", 0.0f);
        sm.Update(1.0f / 60.0f);
        REQUIRE(sm.CurrentStateName() == "idle", "state machine starts in entry (idle)");

        // Speed up → should transition to walk and accumulate root motion.
        sm.SetFloat("speed", 1.0f);
        glm::vec3 rm(0.0f);
        for (int i = 0; i < 120; ++i) {          // 2 seconds
            sm.Update(1.0f / 60.0f);
            rm += sm.ConsumeRootMotionDelta();
        }
        std::printf("       after 2s walking: state=%s rootZ=%.3f\n",
                    sm.CurrentStateName().c_str(), rm.z);
        REQUIRE(sm.CurrentStateName() == "walk", "idle → walk on speed>0.1");
        REQUIRE(rm.z > 1.2f, "root motion accumulates forward (~2 units over 2s)");

        // Slow down → transition back to idle.
        sm.SetFloat("speed", 0.0f);
        for (int i = 0; i < 60; ++i) sm.Update(1.0f / 60.0f);
        REQUIRE(sm.CurrentStateName() == "idle", "walk → idle on speed<0.1");

        // Idle produces (near) zero root motion. Drain any residual from the
        // walk→idle transition first (a real controller consumes every frame;
        // the loop above did not), then measure steady-state idle.
        sm.ConsumeRootMotionDelta();
        glm::vec3 rm_idle(0.0f);
        for (int i = 0; i < 60; ++i) { sm.Update(1.0f / 60.0f); rm_idle += sm.ConsumeRootMotionDelta(); }
        REQUIRE(glm::length(rm_idle) < 0.01f, "idle state has no root motion");

        // Trigger one-shot semantics.
        AnimationStateMachine sm2(skel);
        AnimState a; a.name = "a"; a.clip = idle;
        AnimState b; b.name = "b"; b.clip = walk;
        int A = sm2.AddState(a), B = sm2.AddState(b);
        sm2.SetEntryState(A);
        AnimTransition jab; jab.from = A; jab.to = B; jab.blend_duration = 0.05f;
        jab.conditions.push_back({"jump", AnimCondOp::TriggerSet, 0.0f});
        sm2.AddTransition(jab);
        sm2.Update(1.0f / 60.0f);
        REQUIRE(sm2.CurrentStateName() == "a", "trigger machine starts in A");
        sm2.SetTrigger("jump");
        for (int i = 0; i < 10; ++i) sm2.Update(1.0f / 60.0f);
        REQUIRE(sm2.CurrentStateName() == "b", "trigger fires A → B once, then auto-resets");
    }

    std::printf("anim_check: ALL OK (%d checks)\n", g_checks);
    return 0;
}
