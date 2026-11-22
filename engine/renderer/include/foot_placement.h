#pragma once
// ============================================================================
// foot_placement.h -- feet on the ground they stand on (foot IK), for any biped
// rig, as a post-pass after its clips are sampled:
//
//   1. under each foot (where the clip has it, on the model's own ground plane)
//      the ground is found by a ray, and the foot goes up or down by as much as
//      the ground there differs from the model's ground plane;
//   2. the pelvis drops as far as the lower foot needs (never rises), on a
//      spring; a landing compresses the legs on a softer one (an impulse);
//   3. each leg is bent onto its foot by analytic two-bone IK, the knee keeping
//      its way;
//   4. a foot the clip has down is laid on the slope (at most 30 degrees);
//   5. foot locking (`lock`): a foot the clip puts down is held where it came
//      down in the world -- and keeps the turn it had -- while the clip keeps it
//      down; lifted, or left too far behind (a third of the leg) or turned too
//      far from (35 degrees) by the body, it lets go and goes back to the clip's
//      spot on a quick spring, a little lifted if the clip has it down (a
//      shuffle step). One foot steps at a time; a jump (`weight` under a half)
//      lets go of both, a teleport snaps. Standing still a moment with a foot
//      held off its spot, that foot steps back into the clip's stance.
//
// Every offset moves on a critically damped spring (exact for any time step),
// so a kerb or an edge does not snap. Pure math on a Skeleton (no GPU): checked
// by tools/footik_check. Used by the editor's skinned characters
// (editor/include/imported_skinned_actor.h).
// ============================================================================
#include "skeleton.h"
#include "two_bone_ik.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

namespace schizo::renderer {

/// A critically damped spring taking `x` (moving at `v`) to `goal`; half-life in
/// seconds (Holden's: from rest 60% is left after one). Exact for any dt.
inline void spring_to(float& x, float& v, float goal, float halflife, float dt) {
    const float y = (4.0f * 0.69314718f) / (halflife + 1e-5f) * 0.5f;
    const float j0 = x - goal, j1 = v + j0 * y, e = std::exp(-y * dt);
    x = e * (j0 + j1 * dt) + goal;
    v = e * (v - j1 * y * dt);
}

/// The ground under a point: a ray from `from` along `down` (world, unit) up to
/// `reach`; the hit and its normal (world). false: no ground there.
using FootGroundFn = std::function<bool(const glm::vec3& from, const glm::vec3& down, float reach, glm::vec3& hit, glm::vec3& normal)>;

struct FootLeg {
    int hip = -1, knee = -1, ankle = -1;
    float rest_height = 0.0f;              // the ankle's height standing (bind pose), model units
    float offset = 0.0f, offset_v = 0.0f;  // the foot's ground offset, and its spring's rate
    float plant = 0.0f;                    // 1 while the clip has the foot down
    float ground = 0.0f;                   // the ground under it, measured (before the weight)
    glm::vec3 normal{0.0f, 1.0f, 0.0f};    // the ground's normal under it (model space)
    // Foot locking: held (where in the world, the body's forward then); off the
    // clip's spot by `slip` (world, horizontal) and turned by `twist` (radians
    // about up), each with its spring's rate while it goes back.
    bool locked = false;
    glm::vec3 lock_at{0.0f}, lock_fwd{0.0f, 0.0f, 1.0f};
    glm::vec3 slip{0.0f}, slip_v{0.0f};
    float twist = 0.0f, twist_v = 0.0f;
};
struct FootRig {
    FootLeg legs[2];   // left, right
    int pelvis = -1;
    float pelvis_off = 0.0f, pelvis_v = 0.0f, weight = 0.0f;
    // A landing's compression: the legs give (an impulse into `land_v`, m/s down
    // negative) and come back on a softer spring than the ground's.
    float land_off = 0.0f, land_v = 0.0f;
    // Foot locking: how long the body has stood still (where and which way it was).
    float still = 0.0f;
    glm::vec3 last_pos{0.0f}, last_fwd{0.0f, 0.0f, 1.0f};
    bool valid() const { return legs[0].ankle >= 0 && legs[1].ankle >= 0; }
};

/// A bone's matrix in the model from a pose of local transforms (bone id order).
inline glm::mat4 foot_model_matrix(const Skeleton& sk, const std::vector<BoneTransform>& locals, int b) {
    glm::mat4 m(1.0f);
    for (int at = b; at >= 0 && static_cast<size_t>(at) < locals.size();) {
        m = locals[static_cast<size_t>(at)].ToMatrix() * m;
        const Bone* bone = sk.GetBone(static_cast<uint16_t>(at));
        at = bone ? bone->parent_id : -1;
    }
    return m;
}

/// Finds the legs: a foot bone on each side by name (`foot.L`, `LeftFoot`,
/// `l_foot`, `Foot_R`...; not a toe or an IK target), its parent the knee, that
/// one's the hip, and the pelvis above. `bind` is the bind pose's locals (the
/// standing ankle height comes from it). An invalid rig when either is missing.
inline FootRig find_foot_rig(const Skeleton& sk, const std::vector<BoneTransform>& bind) {
    FootRig rig;
    auto lower = [](std::string s) {
        if (const size_t at = s.find("__"); at != std::string::npos) s.resize(at);   // the loader's "<name>__<node>"
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    auto side_of = [](const std::string& n) {
        auto ends = [&](const char* x) { const std::string e(x); return n.size() >= e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0; };
        if (n.find("left") != std::string::npos || ends(".l") || ends("_l") || ends("-l") || n.rfind("l_", 0) == 0) return 0;
        if (n.find("right") != std::string::npos || ends(".r") || ends("_r") || ends("-r") || n.rfind("r_", 0) == 0) return 1;
        return -1;
    };
    auto parent = [&](int b) { const Bone* bone = b >= 0 ? sk.GetBone(static_cast<uint16_t>(b)) : nullptr; return bone ? bone->parent_id : -1; };
    const int n = static_cast<int>(sk.GetBoneCount());
    for (int b = 0; b < n; ++b) {
        const std::string name = lower(sk.GetBone(static_cast<uint16_t>(b))->name);
        if (name.find("foot") == std::string::npos && name.find("ankle") == std::string::npos) continue;
        if (name.find("toe") != std::string::npos || name.find("ik") != std::string::npos || name.find("target") != std::string::npos) continue;
        const int side = side_of(name);
        if (side < 0 || rig.legs[side].ankle >= 0) continue;
        const int knee = parent(b), hip = knee >= 0 ? parent(knee) : -1;
        if (hip < 0) continue;
        rig.legs[side].ankle = b;
        rig.legs[side].knee = knee;
        rig.legs[side].hip = hip;
        rig.legs[side].rest_height = glm::vec3(foot_model_matrix(sk, bind, b)[3]).y;
    }
    if (!rig.valid()) return FootRig{};
    rig.pelvis = parent(rig.legs[0].hip);
    return rig;
}

/// The post-pass (see the top of this file). The skeleton holds the sampled pose
/// with its world transforms current; `model` places it in the world (the model
/// faces +Z, +Y up, its ground plane at y 0). `weight` 0..1 (0 in the air or
/// dead), eased. Safe on any thread if `ground` is.
inline void place_feet(Skeleton& sk, FootRig& rig, const glm::mat4& model, const FootGroundFn& ground, float weight, float dt,
                       bool lock = false) {
    if (!rig.valid() || dt < 0.0f) return;
    const float unit = glm::length(glm::vec3(model[1]));   // model units to metres, along up
    if (unit < 1e-6f) return;
    const glm::vec3 up_w = glm::vec3(model[1]) / unit;
    const glm::mat3 to_model = glm::inverse(glm::mat3(model));
    auto pos = [&](int b) { return glm::vec3(sk.GetWorldMatrix(static_cast<uint16_t>(b))[3]); };
    auto parent = [&](int b) { const Bone* bone = b >= 0 ? sk.GetBone(static_cast<uint16_t>(b)) : nullptr; return bone ? bone->parent_id : -1; };
    auto flat = [&](const glm::vec3& v) { return v - up_w * glm::dot(v, up_w); };
    glm::vec3 anim_ankle[2], anim_w[2];
    for (int k = 0; k < 2; ++k) {
        anim_ankle[k] = pos(rig.legs[k].ankle);
        anim_w[k] = glm::vec3(model * glm::vec4(anim_ankle[k], 1.0f));
        // Planted while the clip has the foot down; lifted, it is mid-step.
        rig.legs[k].plant = std::clamp(1.0f - (anim_ankle[k].y - rig.legs[k].rest_height) / 0.12f, 0.0f, 1.0f);
    }
    // Foot locking: how far the body may leave a held foot (a third of the leg,
    // in metres) and turn from it.
    const float leg_m = (glm::length(pos(rig.legs[0].knee) - pos(rig.legs[0].hip)) + glm::length(anim_ankle[0] - pos(rig.legs[0].knee))) * unit;
    const float max_slip = 0.33f * leg_m, max_turn = glm::radians(35.0f);
    glm::vec3 fwd_w = flat(glm::vec3(model[2]));
    fwd_w = glm::length(fwd_w) > 1e-6f ? glm::normalize(fwd_w) : glm::vec3(0.0f, 0.0f, 1.0f);
    auto turned = [&](const FootLeg& L) {   // the body's turn about up since L locked (radians)
        return std::atan2(glm::dot(glm::cross(L.lock_fwd, fwd_w), up_w), glm::dot(L.lock_fwd, fwd_w));
    };
    const bool can_lock = lock && weight > 0.5f;
    float over[2] = {0.0f, 0.0f};   // how far past its limits each held foot is (0: within)
    for (int k = 0; k < 2; ++k) {
        FootLeg& L = rig.legs[k];
        if (!L.locked) continue;
        const glm::vec3 off = flat(L.lock_at - anim_w[k]);
        over[k] = std::max(glm::length(off) / max_slip, std::abs(turned(L)) / max_turn);
    }
    // Standing still (under 5 cm/s and 10 degrees/s) a third of a second with both
    // feet held: the one furthest off its spot (past 15% of a limit) steps back.
    const glm::vec3 model_pos(model[3]);
    const bool still_now = dt > 0.0f && glm::length(flat(model_pos - rig.last_pos)) < 0.05f * dt &&
                           glm::dot(fwd_w, rig.last_fwd) > std::cos(glm::radians(10.0f) * dt);
    rig.still = still_now ? rig.still + dt : 0.0f;
    rig.last_pos = model_pos;
    rig.last_fwd = fwd_w;
    int settle = -1;
    if (rig.still > 0.33f && rig.legs[0].locked && rig.legs[1].locked) {
        const int far = over[0] >= over[1] ? 0 : 1;
        if (over[far] > 0.15f) settle = far;
    }
    for (int k = 0; k < 2; ++k) {
        FootLeg& L = rig.legs[k];
        const FootLeg& other = rig.legs[1 - k];
        if (L.locked) {
            const glm::vec3 off = flat(L.lock_at - anim_w[k]);
            // Let go: lifted, in the air, a teleport; past a limit while the other foot
            // holds (within its own, or further past it: one foot steps at a time);
            // twice past a limit regardless.
            const bool stretched = over[k] >= 1.0f && other.locked && (over[1 - k] < 1.0f || over[k] >= over[1 - k]);
            const bool teleport = glm::length(off) > 3.0f * leg_m;
            if (!can_lock || L.plant < 0.7f || teleport || over[k] >= 2.0f || stretched || k == settle) {
                if (k == settle) rig.still = 0.0f;
                L.locked = false;
                L.slip = teleport ? glm::vec3(0.0f) : off;
                L.twist = teleport ? 0.0f : -turned(L);
                L.slip_v = glm::vec3(0.0f);
                L.twist_v = 0.0f;
            } else {
                L.slip = off;
                L.twist = -turned(L);
            }
        }
        if (!L.locked) {
            // Back to the clip's spot (quick: a step), then held again once it is down there.
            for (int c = 0; c < 3; ++c) spring_to(L.slip[c], L.slip_v[c], 0.0f, 0.05f, dt);
            spring_to(L.twist, L.twist_v, 0.0f, 0.05f, dt);
            if (can_lock && L.plant > 0.9f && glm::length(L.slip) < 0.015f * leg_m && std::abs(L.twist) < glm::radians(2.0f)) {
                L.locked = true;
                L.lock_at = anim_w[k] + L.slip;
                L.lock_fwd = glm::vec3(glm::angleAxis(L.twist, up_w) * fwd_w);   // (so the turn since is -twist)
            }
        }
    }
    for (int k = 0; k < 2; ++k) {
        FootLeg& L = rig.legs[k];
        const glm::vec3 spot = glm::vec3(model * glm::vec4(anim_ankle[k].x, 0.0f, anim_ankle[k].z, 1.0f)) + flat(L.slip);
        float goal = 0.0f;
        glm::vec3 normal(0.0f, 1.0f, 0.0f), hit, nrm;
        if (ground(spot + up_w * (0.6f * unit), -up_w, 1.4f * unit, hit, nrm)) {
            goal = std::clamp(glm::dot(hit - spot, up_w) / unit, -0.55f, 0.45f);
            const glm::vec3 nm = to_model * nrm;
            if (glm::length(nm) > 1e-4f && glm::normalize(nm).y > 0.5f) normal = glm::normalize(nm);   // ground, not a wall
        }
        L.ground = goal;
        spring_to(L.offset, L.offset_v, goal * weight, 0.05f, dt);
        L.normal = glm::normalize(glm::mix(L.normal, normal, std::clamp(dt * 12.0f, 0.0f, 1.0f)));
    }
    spring_to(rig.pelvis_off, rig.pelvis_v, std::min(0.0f, std::min(rig.legs[0].offset, rig.legs[1].offset)), 0.07f, dt);
    spring_to(rig.land_off, rig.land_v, 0.0f, 0.12f, dt);
    rig.land_off = std::max(rig.land_off, -0.35f);
    rig.weight += (weight - rig.weight) * std::clamp(dt * 8.0f, 0.0f, 1.0f);
    // Each foot's target (model): the clip's spot, off it by a hold's slip, a shuffle
    // step's lift (a foot the clip has down, stepping back from a hold), the ground.
    glm::vec3 goal_at[2];
    float reach_drop = 0.0f;
    for (int k = 0; k < 2; ++k) {
        const FootLeg& L = rig.legs[k];
        const glm::vec3 slip_m = to_model * flat(L.slip);
        const float step_up = L.locked ? 0.0f : std::min(0.08f * leg_m / unit, 0.5f * glm::length(slip_m)) * L.plant;
        goal_at[k] = anim_ankle[k] + slip_m + glm::vec3(0.0f, L.offset + step_up, 0.0f);
        // A held foot left behind (or ahead) may be past the leg's reach: the hips
        // come down as far as it needs (with a little bend left in the knee).
        if (glm::length(slip_m) > 1e-4f) {
            const glm::vec3 H = pos(L.hip) + glm::vec3(0.0f, rig.pelvis_off + rig.land_off, 0.0f), K = pos(L.knee);
            const float reach = 0.985f * (glm::length(K - H) + glm::length(anim_ankle[k] - K));
            const glm::vec3 to = goal_at[k] - H;
            const float across = glm::length(glm::vec2(to.x, to.z));
            if (across < reach && glm::length(to) > reach) reach_drop = std::max(reach_drop, (H.y - goal_at[k].y) - std::sqrt(reach * reach - across * across));
        }
    }
    reach_drop = std::min(reach_drop, 0.15f * leg_m / unit);
    const float pelvis_down = rig.pelvis_off + rig.land_off - reach_drop;
    auto held = [&](const FootLeg& L) { return glm::length(L.slip) > 1e-4f || std::abs(L.twist) > 1e-4f; };
    if (std::abs(pelvis_down) < 1e-4f && std::abs(rig.legs[0].offset) < 1e-4f && std::abs(rig.legs[1].offset) < 1e-4f &&
        rig.weight < 1e-3f && !held(rig.legs[0]) && !held(rig.legs[1]))
        return;
    auto rot = [](const glm::mat4& m) {
        return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                                                       glm::normalize(glm::vec3(m[2])))));
    };
    auto world_rot = [&](int b) { return b >= 0 ? rot(sk.GetWorldMatrix(static_cast<uint16_t>(b))) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f); };
    auto set_local_rot = [&](int b, const glm::quat& q) {
        BoneTransform tf = sk.GetBone(static_cast<uint16_t>(b))->local_transform;
        tf.rotation = glm::normalize(q);
        sk.SetBoneLocalTransform(static_cast<uint16_t>(b), tf);
    };
    // The pelvis down (in the model, carried into its parent's axes).
    if (rig.pelvis >= 0 && std::abs(pelvis_down) > 1e-5f) {
        BoneTransform tf = sk.GetBone(static_cast<uint16_t>(rig.pelvis))->local_transform;
        tf.position += glm::inverse(world_rot(parent(rig.pelvis))) * glm::vec3(0.0f, pelvis_down, 0.0f);
        sk.SetBoneLocalTransform(static_cast<uint16_t>(rig.pelvis), tf);
        sk.UpdateWorldTransforms();
    }
    for (int k = 0; k < 2; ++k) {
        const FootLeg& L = rig.legs[k];
        const glm::vec3 target = goal_at[k];
        const glm::vec3 H = pos(L.hip), K = pos(L.knee), A = pos(L.ankle);
        const float lay = L.plant * rig.weight;
        if (glm::length(target - A) < 1e-4f && lay < 1e-3f) continue;
        glm::vec3 pole = K - (H + A) * 0.5f;
        if (glm::length(pole) < 1e-4f) pole = glm::vec3(0.0f, 0.0f, 1.0f);   // the model faces +Z: knees forward
        const auto ik = solve_two_bone_ik(H, K, A, target, pole);
        const glm::quat foot = world_rot(L.ankle);
        const glm::quat ru = ik.upper * world_rot(L.hip);
        const glm::quat rk = ik.fore * ik.upper * world_rot(L.knee);
        set_local_rot(L.hip, glm::inverse(world_rot(parent(L.hip))) * ru);
        set_local_rot(L.knee, glm::inverse(ru) * rk);
        // The foot keeps its turn in the model, laid on the slope while planted (at most 30 degrees).
        glm::quat align = rotation_between(glm::vec3(0.0f, 1.0f, 0.0f), L.normal);
        const float angle = glm::angle(align);
        if (angle > glm::radians(30.0f)) align = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), align, glm::radians(30.0f) / angle);
        align = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), align, lay);
        const glm::quat twist = glm::angleAxis(L.twist, glm::vec3(0.0f, 1.0f, 0.0f));   // held: the turn it had
        set_local_rot(L.ankle, glm::inverse(rk) * (align * twist * foot));
        sk.UpdateWorldTransforms();
    }
}

}  // namespace schizo::renderer
