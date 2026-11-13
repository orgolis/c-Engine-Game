#pragma once

// ============================================================================
// Camera Rig -- a third-person camera for the player, as data and one step.
// ============================================================================
//
// Without a Camera Rig the player sees through the camera as authored (first
// person, usually). With one, the camera sits behind and beside the player,
// looking the way the player looks; aiming brings it in over the shoulder;
// walls between the player and the camera pull it in (and it eases back out).
//
// `place_camera_rig` is pure: it sees the world through a ray callback.
// Checked by tools/movement_check.

#include "world.h"
#include "authorable_components.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace schizo::ecs {

struct CameraRig {
    bool  third_person     = true;   // off: the authored camera is used as it is
    float distance         = 3.2f;   // behind the pivot, m
    float height           = 0.55f;  // pivot above the capsule's centre, m
    float shoulder         = 0.45f;  // to the right (negative: left)
    float aim_distance     = 1.4f;   // while aiming
    float aim_shoulder     = 0.75f;
    float aim_time         = 0.12f;  // s to move in for aiming
    float collision_radius = 0.25f;  // kept clear of walls
    float min_distance     = 0.35f;
    float return_speed     = 5.0f;   // m/s back out once a wall is gone

    // Runtime (not saved)
    float cur_distance = -1.0f;
    float cur_shoulder = -1e9f;
    float aim = 0.0f;
};

// Distance to the first hit along `dir` (unit) within `max_dist`, or < 0.
using CameraRayFn = std::function<float(const glm::vec3& origin, const glm::vec3& dir, float max_dist)>;

/// Where the camera goes this frame. `view_fwd` is the way the player looks
/// (unit, with pitch), `right` the player's right (unit, horizontal).
inline glm::vec3 place_camera_rig(CameraRig& r, const glm::vec3& pivot, const glm::vec3& view_fwd,
                                  const glm::vec3& right, bool aiming, const CameraRayFn& ray, float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    const float step = r.aim_time > 1e-4f ? dt / r.aim_time : 1.0f;
    r.aim = std::clamp(r.aim + (aiming ? step : -step), 0.0f, 1.0f);
    const float a = r.aim * r.aim * (3.0f - 2.0f * r.aim);
    const float want_dist = r.distance + (r.aim_distance - r.distance) * a;
    const float want_sh = r.shoulder + (r.aim_shoulder - r.shoulder) * a;
    const float pad = std::max(0.0f, r.collision_radius);

    // The shoulder: as far to the side as the walls allow.
    float sh = want_sh;
    if (std::fabs(want_sh) > 1e-4f) {
        const glm::vec3 sd = right * (want_sh > 0.0f ? 1.0f : -1.0f);
        const float hit = ray(pivot, sd, std::fabs(want_sh) + pad);
        if (hit >= 0.0f) sh = (want_sh > 0.0f ? 1.0f : -1.0f) * std::max(0.0f, hit - pad);
    }
    if (r.cur_shoulder < -1e8f) r.cur_shoulder = sh;
    r.cur_shoulder = (std::fabs(sh) < std::fabs(r.cur_shoulder)) ? sh
                   : r.cur_shoulder + std::clamp(sh - r.cur_shoulder, -r.return_speed * dt, r.return_speed * dt);
    const glm::vec3 sp = pivot + right * r.cur_shoulder;

    // The boom: back along the view, in front of the first wall.
    const glm::vec3 back = -view_fwd;
    const float hit = ray(sp, back, want_dist + pad);
    const float allowed = hit >= 0.0f ? std::clamp(hit - pad, std::min(r.min_distance, want_dist), want_dist) : want_dist;
    if (r.cur_distance < 0.0f || allowed < r.cur_distance) r.cur_distance = allowed;   // a wall pulls in at once
    else r.cur_distance = std::min(allowed, r.cur_distance + r.return_speed * dt);     // and lets go gently
    return sp + back * r.cur_distance;
}

inline void serialize_camera_rig(const void* obj, std::vector<uint8_t>& out) {
    const auto& r = *static_cast<const CameraRig*>(obj);
    detail::put_i32(out, 1);
    const float f[] = {r.third_person ? 1.0f : 0.0f, r.distance, r.height, r.shoulder, r.aim_distance,
                       r.aim_shoulder, r.aim_time, r.collision_radius, r.min_distance, r.return_speed};
    detail::put_i32(out, static_cast<int32_t>(sizeof f / sizeof f[0]));
    for (float v : f) detail::put_f32(out, v);
}
inline void deserialize_camera_rig(void* obj, const uint8_t* data, size_t n) {
    auto& r = *static_cast<CameraRig*>(obj);
    r = CameraRig{};
    const uint8_t* p = data; const uint8_t* e = data + n;
    int32_t ver = 0, count = 0;
    if (!detail::get_i32(p, e, ver) || ver < 1 || !detail::get_i32(p, e, count)) return;
    float* fields[] = {nullptr, &r.distance, &r.height, &r.shoulder, &r.aim_distance,
                       &r.aim_shoulder, &r.aim_time, &r.collision_radius, &r.min_distance, &r.return_speed};
    for (int i = 0; i < count; ++i) {
        float v = 0.0f;
        if (!detail::get_f32(p, e, v)) return;
        if (i >= static_cast<int>(sizeof fields / sizeof fields[0])) continue;
        if (i == 0) r.third_person = v > 0.5f;
        else *fields[i] = v;
    }
}
inline void register_camera_components() {
    register_authorable(make_authorable_custom<CameraRig>("Camera Rig", &serialize_camera_rig, &deserialize_camera_rig));
}

}  // namespace schizo::ecs
