// ============================================================================
// ik.cpp — two-bone analytic IK + foot IK. See ik.h.
// ============================================================================

#include "ik.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtx/quaternion.hpp>   // glm::rotation(from,to) — GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace schizo::renderer {

namespace {

constexpr float kEps = 1e-5f;

glm::vec3 world_pos(const Skeleton& s, uint16_t id) {
    return glm::vec3(s.GetWorldMatrix(id)[3]);
}

// World rotation with any scale stripped (quat_cast needs an orthonormal basis).
glm::quat world_rot(const Skeleton& s, uint16_t id) {
    const glm::mat4 m = s.GetWorldMatrix(id);
    glm::vec3 x = glm::vec3(m[0]), y = glm::vec3(m[1]), z = glm::vec3(m[2]);
    if (glm::length(x) < kEps || glm::length(y) < kEps || glm::length(z) < kEps)
        return glm::quat(1, 0, 0, 0);
    return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(x),
                                                   glm::normalize(y),
                                                   glm::normalize(z))));
}

float safe_acos(float x) { return std::acos(glm::clamp(x, -1.0f, 1.0f)); }

} // namespace

void SolveTwoBoneIK(Skeleton& skel, uint16_t ia, uint16_t ib, uint16_t ic,
                    const glm::vec3& target, const glm::vec3& pole, float weight) {
    if (weight <= 0.0f) return;
    Bone* bone_a = skel.GetBone(ia);
    Bone* bone_b = skel.GetBone(ib);
    if (!bone_a || !bone_b) return;

    const glm::vec3 a = world_pos(skel, ia);
    const glm::vec3 b = world_pos(skel, ib);
    const glm::vec3 c = world_pos(skel, ic);
    const glm::quat a_gr = world_rot(skel, ia);
    const glm::quat b_gr = world_rot(skel, ib);

    const float lab = glm::length(b - a);
    const float lcb = glm::length(b - c);
    if (lab < kEps || lcb < kEps) return;
    if (glm::length(c - a) < kEps) return;

    // Clamp the reach so the chain can't be asked to over-extend (which would
    // make the law-of-cosines terms leave [-1,1] → a straightened, jittery leg).
    const float lat = glm::clamp(glm::length(target - a), kEps, lab + lcb - kEps * 100.0f);

    // Current interior angles.
    const float ac_ab_0 = safe_acos(glm::dot(glm::normalize(c - a), glm::normalize(b - a)));
    const float ba_bc_0 = safe_acos(glm::dot(glm::normalize(a - b), glm::normalize(c - b)));
    const float ac_at_0 = safe_acos(glm::dot(glm::normalize(c - a), glm::normalize(target - a)));

    // Desired interior angles for the (clamped) target distance.
    const float ac_ab_1 = safe_acos((lab * lab + lat * lat - lcb * lcb) / (2.0f * lab * lat));
    const float ba_bc_1 = safe_acos((lab * lab + lcb * lcb - lat * lat) / (2.0f * lab * lcb));

    // Bend-plane axis: prefer the pole hint, fall back to the current plane.
    glm::vec3 axis = glm::cross(c - a, pole - a);
    if (glm::length(axis) < kEps) axis = glm::cross(c - a, b - a);
    if (glm::length(axis) < kEps) axis = glm::cross(c - a, glm::vec3(0, 0, 1));
    if (glm::length(axis) < kEps) axis = glm::cross(c - a, glm::vec3(0, 1, 0));
    axis = glm::normalize(axis);

    // Aim axis: rotate (c-a) onto (target-a).
    glm::vec3 aim_axis = glm::cross(c - a, target - a);
    const bool has_aim = glm::length(aim_axis) > kEps;
    if (has_aim) aim_axis = glm::normalize(aim_axis);

    // Express each world-space axis in the corresponding bone's own frame so
    // that `localRot * angleAxis(theta, invWorldRot*axis)` == a world-space
    // pre-rotation about `axis` (derivation: parentWorld*local*inv(world) == I).
    const glm::vec3 axis_a = glm::normalize(glm::inverse(a_gr) * axis);
    const glm::vec3 axis_b = glm::normalize(glm::inverse(b_gr) * axis);

    const glm::quat r_bend_a = glm::angleAxis(ac_ab_1 - ac_ab_0, axis_a);
    const glm::quat r_bend_b = glm::angleAxis(ba_bc_1 - ba_bc_0, axis_b);
    glm::quat r_aim_a(1, 0, 0, 0);
    if (has_aim)
        r_aim_a = glm::angleAxis(ac_at_0, glm::normalize(glm::inverse(a_gr) * aim_axis));

    const glm::quat local_a = bone_a->local_transform.rotation;
    const glm::quat local_b = bone_b->local_transform.rotation;
    glm::quat new_a = glm::normalize(local_a * r_aim_a * r_bend_a);
    glm::quat new_b = glm::normalize(local_b * r_bend_b);

    if (weight < 1.0f) {
        new_a = glm::normalize(glm::slerp(local_a, new_a, weight));
        new_b = glm::normalize(glm::slerp(local_b, new_b, weight));
    }

    bone_a->local_transform.rotation = new_a;
    bone_b->local_transform.rotation = new_b;
    skel.UpdateWorldTransforms();
}

float SolveFootIK(Skeleton& skel, uint16_t pelvis,
                  const LegChain& left, const LegChain& right,
                  const GroundQueryFn& ground, const FootIkParams& params) {
    if (!ground || params.weight <= 0.0f) return 0.0f;
    skel.UpdateWorldTransforms();

    // 1) Ground-probe each ankle. desired_y = ground hit raised by foot_height.
    auto probe = [&](const LegChain& leg, bool& hit, glm::vec3& target,
                     glm::vec3& normal) {
        const glm::vec3 ankle = world_pos(skel, leg.ankle);
        const glm::vec3 origin = ankle + glm::vec3(0, params.trace_up, 0);
        glm::vec3 hp, hn;
        hit = ground(origin, glm::vec3(0, -1, 0),
                     params.trace_up + params.trace_down, hp, hn);
        if (hit) {
            normal = hn;
            target = hp + glm::vec3(0, leg.foot_height, 0);
        }
    };

    bool lhit = false, rhit = false;
    glm::vec3 ltar(0), rtar(0), lnrm(0, 1, 0), rnrm(0, 1, 0);
    probe(left, lhit, ltar, lnrm);
    probe(right, rhit, rtar, rnrm);
    if (!lhit && !rhit) return 0.0f;

    // 2) Pelvis drop: the foot that must go LOWEST (largest downward delta from
    //    its animated position) pulls the pelvis down so the other leg can
    //    still reach. Only drop, never lift (lifting would break the planted
    //    foot of a normal stride).
    const float la = world_pos(skel, left.ankle).y;
    const float ra = world_pos(skel, right.ankle).y;
    float drop = 0.0f;
    if (lhit) drop = std::min(drop, ltar.y - la);
    if (rhit) drop = std::min(drop, rtar.y - ra);
    drop = std::max(drop, -params.max_pelvis_drop) * params.weight;

    if (drop < 0.0f) {
        Bone* pb = skel.GetBone(pelvis);
        if (pb) {
            // Pelvis local translation is in its parent's space; for a root-ish
            // pelvis that's world-aligned this is a good approximation.
            pb->local_transform.position.y += drop;
            skel.UpdateWorldTransforms();
        }
    }

    // 3) Two-bone IK each leg onto its ground target. Pole hint = knee pushed
    //    forward (+Z) from its current position so knees bend naturally.
    auto solve_leg = [&](const LegChain& leg, bool hit, const glm::vec3& target,
                         const glm::vec3& normal) {
        if (!hit) return;
        const glm::vec3 knee = world_pos(skel, leg.knee);
        const glm::vec3 pole = knee + glm::vec3(0, 0, 1);
        SolveTwoBoneIK(skel, leg.hip, leg.knee, leg.ankle, target, pole, params.weight);

        if (params.align_to_normal) {
            // Rotate the ankle so its sole matches the ground normal.
            Bone* ab = skel.GetBone(leg.ankle);
            if (ab) {
                const glm::quat cur = world_rot(skel, leg.ankle);
                const glm::vec3 up_cur = cur * glm::vec3(0, 1, 0);
                glm::quat align = glm::rotation(glm::normalize(up_cur),
                                                glm::normalize(normal));
                align = glm::slerp(glm::quat(1, 0, 0, 0), align, params.weight);
                const glm::quat parent = (ab->parent_id >= 0)
                    ? world_rot(skel, static_cast<uint16_t>(ab->parent_id))
                    : glm::quat(1, 0, 0, 0);
                // new world = align * cur  ->  new local = inv(parent) * align * cur
                ab->local_transform.rotation =
                    glm::normalize(glm::inverse(parent) * align * cur);
                skel.UpdateWorldTransforms();
            }
        }
    };
    solve_leg(left, lhit, ltar, lnrm);
    solve_leg(right, rhit, rtar, rnrm);

    return drop;
}

} // namespace schizo::renderer
