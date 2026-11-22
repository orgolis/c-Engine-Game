#pragma once
// ============================================================================
// locomotion_warp.h -- a moving character's feet kept honest:
//
//   - a locomotion clip measured once: the moments each foot comes down (its
//     sync markers), the ground speed it was made for and the way the body goes
//     in it (both from how its planted feet slide back under an in-place body);
//   - a switch between two such clips in step: the new one starts at the same
//     point of the same foot's step as the old one was (marker-matched phase),
//     not at its start -- walk to run without a stumble;
//   - stride warping: each foot's distance from the pelvis along the way it goes
//     scaled, so a stride covers the ground the body really covers (the hips
//     coming down as far as the legs need to reach);
//   - orientation warping: the legs turned toward the way the body really
//     moves (a forward walk carrying it 30 degrees off to the side), the upper
//     body turned back so it keeps facing (and aiming) ahead.
//
// The warps run after the clips and before the feet are put on the ground
// (foot_placement.h), as the overhaul report orders it. Pure math on a
// Skeleton and its clips: checked by tools/footik_check.
// ============================================================================
#include "animation.h"
#include "foot_placement.h"
#include "pose_layers.h"
#include "skeleton.h"
#include "two_bone_ik.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace schizo::renderer {

struct LocoClipInfo {
    bool valid = false;
    float duration = 0.0f;
    std::vector<float> contact[2];   // seconds into the clip each foot comes down (left, right)
    float speed = 0.0f;              // the ground speed it was made for (m/s, model units)
    glm::vec2 dir{0.0f, 1.0f};       // the way the body goes in it (model x, z)
    // How it was found (to say why a clip is not taken for locomotion).
    float low[2] = {0.0f, 0.0f}, high[2] = {0.0f, 0.0f};
    int planted_frames = 0;
};

/// Measures a clip that moves in place (its body stays, its feet slide back while
/// planted): where each foot comes down, how fast and which way it walks.
inline LocoClipInfo measure_loco_clip(const Skeleton& sk, const std::vector<BoneTransform>& bind, const FootRig& feet,
                                      const AnimationClip& clip) {
    LocoClipInfo info;
    info.duration = clip.GetDuration();
    if (!feet.valid() || info.duration < 0.1f) return info;
    const int steps = std::max(16, static_cast<int>(info.duration * 60.0f));
    const float dt = info.duration / static_cast<float>(steps);
    std::vector<glm::vec3> ankle[2];
    std::vector<BoneTransform> pose;
    for (int k = 0; k < steps; ++k) {
        pose = bind;
        clip.SampleLocal(dt * static_cast<float>(k), true, pose);
        for (int leg = 0; leg < 2; ++leg) ankle[leg].push_back(glm::vec3(foot_model_matrix(sk, pose, feet.legs[leg].ankle)[3]));
    }
    glm::vec2 slide(0.0f);
    int sliding = 0;
    for (int leg = 0; leg < 2; ++leg) {
        // The ground under this foot: low in its heights, but not the lowest one (a
        // keyed curve can overshoot under the ground for a frame as the foot lands).
        std::vector<float> heights;
        heights.reserve(ankle[leg].size());
        for (const auto& a : ankle[leg]) heights.push_back(a.y);
        std::sort(heights.begin(), heights.end());
        const float low = heights[heights.size() / 8], high = heights.back();
        const float range = std::max(0.0f, high - low);
        info.low[leg] = low;
        info.high[leg] = high;
        const float down_band = std::max(0.025f, 0.15f * range), flat_band = std::max(0.01f, 0.08f * range);
        // Down, with a lift shorter than an eighth of the cycle not counted as a step
        // (a heel rising out of the band late in the stance and settling again).
        std::vector<char> is_down(static_cast<size_t>(steps));
        for (int k = 0; k < steps; ++k) is_down[static_cast<size_t>(k)] = ankle[leg][static_cast<size_t>(k)].y < low + down_band;
        const int short_lift = std::max(1, steps / 8);
        bool any_down = false;
        for (char d : is_down) any_down = any_down || d;
        if (any_down) {
            int start = 0;
            while (!is_down[static_cast<size_t>(start)]) ++start;   // from a down frame, around the loop
            for (int i = 0; i < steps;) {
                const int k = (start + i) % steps;
                if (is_down[static_cast<size_t>(k)]) { ++i; continue; }
                int len = 0;
                while (len < steps && !is_down[static_cast<size_t>((k + len) % steps)]) ++len;
                if (len < short_lift)
                    for (int j = 0; j < len; ++j) is_down[static_cast<size_t>((k + j) % steps)] = 1;
                i += std::max(1, len);
            }
        }
        auto down = [&](int k) { return is_down[static_cast<size_t>((k + steps) % steps)] != 0; };
        for (int k = 0; k < steps; ++k) {
            if (down(k) && !down(k - 1)) info.contact[leg].push_back(dt * static_cast<float>(k));
            // The slide only where the foot is right down (not the end of a swing landing,
            // nor the heel coming up at the end of the stance).
            auto flat = [&](int q) { return ankle[leg][static_cast<size_t>((q + steps) % steps)].y < low + flat_band; };
            if (flat(k) && flat(k + 1)) {
                const glm::vec3 d = ankle[leg][static_cast<size_t>((k + 1) % steps)] - ankle[leg][static_cast<size_t>(k)];
                if (glm::length(glm::vec2(d.x, d.z)) < 0.5f) { slide += glm::vec2(d.x, d.z) / dt; ++sliding; }   // (not across the loop's seam)
            }
        }
    }
    info.planted_frames = sliding;
    if (sliding == 0 || info.contact[0].empty() || info.contact[1].empty()) return info;
    slide /= static_cast<float>(sliding);
    info.speed = glm::length(slide);
    // Locomotion steps: a real pace, and the feet coming down at different times
    // (a jump lands both at once; a death slides a little but does not step).
    if (info.speed < 0.2f) return info;
    float apart = info.duration;
    for (float l : info.contact[0])
        for (float r : info.contact[1]) {
            float d = std::fmod(std::abs(l - r), info.duration);
            apart = std::min(apart, std::min(d, info.duration - d));
        }
    if (apart < 0.1f * info.duration) return info;
    info.dir = -slide / info.speed;   // the feet slide back: the body goes the other way
    info.valid = true;
    return info;
}

/// Where in clip `to` to start, coming from clip `from` at `t` seconds: the same
/// point of the same foot's step (the last foot down and how far toward the
/// next contact). Without markers: the same fraction of the cycle.
inline float loco_synced_time(const LocoClipInfo& from, float t, const LocoClipInfo& to) {
    if (to.duration <= 0.0f) return 0.0f;
    const float tf = from.duration > 0.0f ? std::fmod(std::max(0.0f, t), from.duration) : 0.0f;
    if (!from.valid || !to.valid) return from.duration > 0.0f ? tf / from.duration * to.duration : 0.0f;
    struct C { float t; int foot; };
    auto contacts = [](const LocoClipInfo& c) {
        std::vector<C> list;
        for (int f = 0; f < 2; ++f) for (float x : c.contact[f]) list.push_back({x, f});
        std::sort(list.begin(), list.end(), [](const C& a, const C& b) { return a.t < b.t; });
        return list;
    };
    const std::vector<C> a = contacts(from), b = contacts(to);
    // The last contact at or before t (wrapping), and the next after it.
    int prev = -1;
    for (int i = 0; i < static_cast<int>(a.size()); ++i) if (a[static_cast<size_t>(i)].t <= tf) prev = i;
    const C last = prev >= 0 ? a[static_cast<size_t>(prev)] : C{a.back().t - from.duration, a.back().foot};
    const C next = prev + 1 < static_cast<int>(a.size()) ? a[static_cast<size_t>(prev + 1)] : C{a.front().t + from.duration, a.front().foot};
    const float u = next.t - last.t > 1e-4f ? std::clamp((tf - last.t) / (next.t - last.t), 0.0f, 1.0f) : 0.0f;
    // In `to`: that foot's first contact and the contact after it.
    int start = -1;
    for (int i = 0; i < static_cast<int>(b.size()) && start < 0; ++i) if (b[static_cast<size_t>(i)].foot == last.foot) start = i;
    if (start < 0) return tf / from.duration * to.duration;
    const float t0 = b[static_cast<size_t>(start)].t;
    const float t1 = start + 1 < static_cast<int>(b.size()) ? b[static_cast<size_t>(start + 1)].t : b.front().t + to.duration;
    return std::fmod(t0 + u * (t1 - t0), to.duration);
}

struct LocoWarpState {
    float turn = 0.0f, turn_v = 0.0f;       // orientation warp (radians about up)
    float stride = 1.0f, stride_v = 0.0f;   // stride scale
};

/// Bends a leg so its ankle reaches `target` (model space), the knee keeping its
/// way and the foot its turn.
inline void leg_reach(Skeleton& sk, const FootLeg& L, const glm::vec3& target) {
    auto pos = [&](int b) { return glm::vec3(sk.GetWorldMatrix(static_cast<uint16_t>(b))[3]); };
    auto rot = [&](int b) {
        if (b < 0) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::mat4 m = sk.GetWorldMatrix(static_cast<uint16_t>(b));
        return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])))));
    };
    auto parent = [&](int b) { const Bone* bone = b >= 0 ? sk.GetBone(static_cast<uint16_t>(b)) : nullptr; return bone ? bone->parent_id : -1; };
    auto set_rot = [&](int b, const glm::quat& q) {
        BoneTransform tf = sk.GetBone(static_cast<uint16_t>(b))->local_transform;
        tf.rotation = glm::normalize(q);
        sk.SetBoneLocalTransform(static_cast<uint16_t>(b), tf);
    };
    const glm::vec3 H = pos(L.hip), K = pos(L.knee), A = pos(L.ankle);
    if (glm::length(target - A) < 1e-5f) return;
    glm::vec3 pole = K - (H + A) * 0.5f;
    if (glm::length(pole) < 1e-4f) pole = glm::vec3(0.0f, 0.0f, 1.0f);
    const auto ik = solve_two_bone_ik(H, K, A, target, pole);
    const glm::quat foot = rot(L.ankle), ru = ik.upper * rot(L.hip), rk = ik.fore * ik.upper * rot(L.knee);
    set_rot(L.hip, glm::inverse(rot(parent(L.hip))) * ru);
    set_rot(L.knee, glm::inverse(ru) * rk);
    set_rot(L.ankle, glm::inverse(rk) * foot);
    sk.UpdateWorldTransforms();
}

/// After the clips, before the feet go on the ground. `dir` is the way the clip's
/// body goes (model x, z); the legs turn by `turn` radians about up toward where
/// the body really goes and the upper body (from `spine_base`) turns back; each
/// foot's reach from the pelvis along the (turned) way is scaled by `stride`.
inline void warp_locomotion(Skeleton& sk, const FootRig& feet, int spine_base, glm::vec2 dir, float stride, float turn) {
    if (!feet.valid() || feet.pelvis < 0) return;
    if (std::abs(turn) > 1e-4f) {
        turn_bone_in_model(sk, feet.pelvis, glm::angleAxis(turn, glm::vec3(0.0f, 1.0f, 0.0f)));
        if (spine_base >= 0) turn_bone_in_model(sk, spine_base, glm::angleAxis(-turn, glm::vec3(0.0f, 1.0f, 0.0f)));
        const float c = std::cos(turn), s = std::sin(turn);   // the way it goes, turned with the legs
        dir = glm::vec2(c * dir.x + s * dir.y, -s * dir.x + c * dir.y);
    }
    if (std::abs(stride - 1.0f) < 1e-3f || glm::length(dir) < 1e-4f) return;
    dir = glm::normalize(dir);
    auto pos = [&](int b) { return glm::vec3(sk.GetWorldMatrix(static_cast<uint16_t>(b))[3]); };
    const glm::vec3 P = pos(feet.pelvis);
    glm::vec3 target[2];
    float drop = 0.0f;
    for (int k = 0; k < 2; ++k) {
        const FootLeg& L = feet.legs[k];
        const glm::vec3 A = pos(L.ankle), H = pos(L.hip);
        const float along = glm::dot(glm::vec2(A.x - P.x, A.z - P.z), dir);
        const glm::vec2 shift = dir * (along * (stride - 1.0f));
        target[k] = A + glm::vec3(shift.x, 0.0f, shift.y);
        // A longer stride needs a lower hip: as far down as this leg needs to reach
        // its foot with a little bend left (runners and long walkers do the same).
        const float reach = 0.985f * (glm::length(pos(L.knee) - H) + glm::length(A - pos(L.knee)));
        const glm::vec3 to = target[k] - H;
        const float across = glm::length(glm::vec2(to.x, to.z));
        if (across < reach && glm::length(to) > reach) drop = std::max(drop, (H.y - target[k].y) - std::sqrt(reach * reach - across * across));
    }
    if (drop > 1e-4f) {
        drop = std::min(drop, 0.25f);
        const int parent = sk.GetBone(static_cast<uint16_t>(feet.pelvis))->parent_id;
        const glm::mat4 pw = parent >= 0 ? sk.GetWorldMatrix(static_cast<uint16_t>(parent)) : glm::mat4(1.0f);
        BoneTransform tf = sk.GetBone(static_cast<uint16_t>(feet.pelvis))->local_transform;
        tf.position += glm::vec3(glm::inverse(pw) * glm::vec4(0.0f, -drop, 0.0f, 0.0f));
        sk.SetBoneLocalTransform(static_cast<uint16_t>(feet.pelvis), tf);
        sk.UpdateWorldTransforms();
    }
    for (int k = 0; k < 2; ++k) leg_reach(sk, feet.legs[k], target[k]);
}

}  // namespace schizo::renderer
