#pragma once
// ============================================================================
// pose_layers.h -- procedural layers laid over a sampled pose, on a chain of
// bones from the spine up to the head:
//
//   - look_at: the chain turns toward a point -- a glance (the head leads, the
//     spine follows a little) or an aim (the spine and chest take it all, so
//     what the chest holds, a rifle in both hands, points there: an aim offset);
//   - lean_into: the body leans into its acceleration, by the angle that
//     balances it against gravity (forward speeding up, back stopping, into a
//     turn).
//
// The angles are the model's own (it faces +Z, +Y up) and each turn is carried
// into the joint's axes (L' = P^-1 D P L), so any rig works whatever axes its
// joints have. Critically damped springs settle every angle (exact for any dt).
// Pure math on a Skeleton (no GPU): checked by tools/footik_check. Order per
// frame (docs/research/NATURAL_ANIMATION.md): clips, feet, lean, look/aim.
// ============================================================================
#include "skeleton.h"
#include "foot_placement.h"   // spring_to

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace schizo::renderer {

/// The chain from `base` up to `head` (bone ids, root-most first); empty when
/// `head` is not under `base`.
inline std::vector<int> pose_chain(const Skeleton& sk, int base, int head) {
    std::vector<int> chain;
    for (int at = head; at >= 0;) {
        chain.insert(chain.begin(), at);
        if (at == base) return chain;
        const Bone* b = sk.GetBone(static_cast<uint16_t>(at));
        at = b ? b->parent_id : -1;
    }
    return {};
}

/// Turns chain bone `b` by `d`, a rotation in the model's space about the bone's
/// own pivot (its children come along), and refreshes the world transforms.
inline void turn_bone_in_model(Skeleton& sk, int b, const glm::quat& d) {
    auto rot = [](const glm::mat4& m) {
        return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                                                       glm::normalize(glm::vec3(m[2])))));
    };
    const Bone* bone = sk.GetBone(static_cast<uint16_t>(b));
    if (!bone) return;
    const int parent = bone->parent_id;
    const glm::quat p = parent >= 0 ? rot(sk.GetWorldMatrix(static_cast<uint16_t>(parent))) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    BoneTransform tf = bone->local_transform;
    tf.rotation = glm::normalize(glm::inverse(p) * d * p * tf.rotation);
    sk.SetBoneLocalTransform(static_cast<uint16_t>(b), tf);
    sk.UpdateWorldTransforms();
}

struct LookState {
    float yaw = 0.0f, yaw_v = 0.0f, pitch = 0.0f, pitch_v = 0.0f, aim = 0.0f, aim_v = 0.0f;
};
/// The chain turns toward `world_point` by `weight` (0..1; 0 eases back), `aim`
/// 0 a glance .. 1 an aim. Up to 1.2 rad (about 70 degrees) to the side and 1
/// rad up or down; springs with a half-life of 0.08 s.
inline void look_at(Skeleton& sk, const std::vector<int>& chain, LookState& st, const glm::mat4& model, const glm::vec3& world_point,
                    float weight, float aim, float dt) {
    if (chain.empty() || dt < 0.0f) return;
    const glm::vec3 eye(sk.GetWorldMatrix(static_cast<uint16_t>(chain.back()))[3]);
    const glm::vec3 to = glm::vec3(glm::inverse(model) * glm::vec4(world_point, 1.0f)) - eye;
    float yaw = 0.0f, pitch = 0.0f;
    if (weight > 0.0f && glm::length(to) > 0.3f) {
        yaw = std::clamp(std::atan2(to.x, to.z), -1.2f, 1.2f) * weight;
        pitch = std::clamp(std::atan2(to.y, glm::length(glm::vec2(to.x, to.z))), -1.0f, 1.0f) * weight;
    }
    spring_to(st.yaw, st.yaw_v, yaw, 0.08f, dt);
    spring_to(st.pitch, st.pitch_v, pitch, 0.08f, dt);
    spring_to(st.aim, st.aim_v, std::clamp(aim, 0.0f, 1.0f), 0.08f, dt);
    if (std::abs(st.yaw) < 1e-4f && std::abs(st.pitch) < 1e-4f) return;
    // Shares down the chain: a glance grows toward the head (1, 2, 3...), an aim is
    // the lower half's alone, evenly.
    const size_t n = chain.size(), lower = (n + 1) / 2;
    float glance_total = 0.0f;
    for (size_t i = 0; i < n; ++i) glance_total += static_cast<float>(i + 1);
    for (size_t i = 0; i < n; ++i) {
        const float glance = static_cast<float>(i + 1) / glance_total, aimed = i < lower ? 1.0f / static_cast<float>(lower) : 0.0f;
        const float share = glance + (aimed - glance) * st.aim;
        if (share <= 0.0f) continue;
        turn_bone_in_model(sk, chain[i], glm::angleAxis(st.yaw * share, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                             glm::angleAxis(-st.pitch * share, glm::vec3(1.0f, 0.0f, 0.0f)));
    }
}

struct LeanState {
    glm::vec2 lean{0.0f}, lean_v{0.0f};   // (toward +X, toward +Z) in radians
};
/// The chain's lower half leans into `accel_world` (m/s^2): `strength` of the
/// angle that balances it against gravity (about half looks best), at most 12
/// degrees; springs with a half-life of 0.12 s.
inline void lean_into(Skeleton& sk, const std::vector<int>& chain, LeanState& st, const glm::mat4& model, const glm::vec3& accel_world,
                      float strength, float dt) {
    if (chain.empty() || dt < 0.0f) return;
    glm::vec3 a = glm::inverse(glm::mat3(model)) * accel_world;
    a.y = 0.0f;
    const float limit = glm::radians(12.0f);
    glm::vec2 goal(0.0f);
    const float mag = glm::length(a);
    if (mag > 0.05f) goal = glm::vec2(a.x, a.z) / mag * std::min(limit, std::atan2(mag, 9.81f) * strength);
    spring_to(st.lean.x, st.lean_v.x, goal.x, 0.12f, dt);
    spring_to(st.lean.y, st.lean_v.y, goal.y, 0.12f, dt);
    const float amount = glm::length(st.lean);
    if (amount < 1e-4f) return;
    // Tipping toward (x, z) turns +Y about cross(+Y, (x, 0, z)) = (z, 0, -x).
    const glm::vec3 axis = glm::normalize(glm::vec3(st.lean.y, 0.0f, -st.lean.x));
    const size_t lower = (chain.size() + 1) / 2;
    for (size_t i = 0; i < lower; ++i) turn_bone_in_model(sk, chain[i], glm::angleAxis(amount / static_cast<float>(lower), axis));
}

}  // namespace schizo::renderer
