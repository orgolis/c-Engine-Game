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
//   4. a foot the clip has down is laid on the slope (at most 30 degrees).
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
};
struct FootRig {
    FootLeg legs[2];   // left, right
    int pelvis = -1;
    float pelvis_off = 0.0f, pelvis_v = 0.0f, weight = 0.0f;
    // A landing's compression: the legs give (an impulse into `land_v`, m/s down
    // negative) and come back on a softer spring than the ground's.
    float land_off = 0.0f, land_v = 0.0f;
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
inline void place_feet(Skeleton& sk, FootRig& rig, const glm::mat4& model, const FootGroundFn& ground, float weight, float dt) {
    if (!rig.valid() || dt < 0.0f) return;
    const float unit = glm::length(glm::vec3(model[1]));   // model units to metres, along up
    if (unit < 1e-6f) return;
    const glm::vec3 up_w = glm::vec3(model[1]) / unit;
    const glm::mat3 to_model = glm::inverse(glm::mat3(model));
    auto pos = [&](int b) { return glm::vec3(sk.GetWorldMatrix(static_cast<uint16_t>(b))[3]); };
    auto parent = [&](int b) { const Bone* bone = b >= 0 ? sk.GetBone(static_cast<uint16_t>(b)) : nullptr; return bone ? bone->parent_id : -1; };
    glm::vec3 anim_ankle[2];
    for (int k = 0; k < 2; ++k) {
        FootLeg& L = rig.legs[k];
        anim_ankle[k] = pos(L.ankle);
        // Planted while the clip has the foot down; lifted, it is mid-step.
        L.plant = std::clamp(1.0f - (anim_ankle[k].y - L.rest_height) / 0.12f, 0.0f, 1.0f);
        const glm::vec3 spot(model * glm::vec4(anim_ankle[k].x, 0.0f, anim_ankle[k].z, 1.0f));
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
    const float pelvis_down = rig.pelvis_off + rig.land_off;
    if (std::abs(pelvis_down) < 1e-4f && std::abs(rig.legs[0].offset) < 1e-4f && std::abs(rig.legs[1].offset) < 1e-4f &&
        rig.weight < 1e-3f)
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
        const glm::vec3 target = anim_ankle[k] + glm::vec3(0.0f, L.offset, 0.0f);
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
        set_local_rot(L.ankle, glm::inverse(rk) * (align * foot));
        sk.UpdateWorldTransforms();
    }
}

}  // namespace schizo::renderer
