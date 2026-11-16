#pragma once
// ============================================================================
// two_bone_ik.h -- an arm (or a leg) reaching a point: the shoulder stays,
// the elbow bends toward a pole direction, the wrist lands on the target (or
// as close as the bones reach). Used for the first-person arms: the player's
// own arms holding the gun in view (editor/include/imported_skinned_actor.h).
// Pure math, checked by tools/skinning_check.
// ============================================================================

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace schizo::renderer {

/// The shortest rotation taking direction `u` onto direction `v` (unit or not).
inline glm::quat rotation_between(glm::vec3 u, glm::vec3 v) {
    const float lu = glm::length(u), lv = glm::length(v);
    if (lu < 1e-8f || lv < 1e-8f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    u /= lu;
    v /= lv;
    const float d = glm::dot(u, v);
    if (d < -0.99999f) {   // opposite: half a turn about any perpendicular
        glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), u);
        if (glm::length(axis) < 1e-4f) axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), u);
        return glm::angleAxis(3.14159265f, glm::normalize(axis));
    }
    const glm::vec3 c = glm::cross(u, v);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

struct TwoBoneIk {
    glm::quat upper{1.0f, 0.0f, 0.0f, 0.0f};   // rotation to apply to the upper bone (about the shoulder)
    glm::quat fore{1.0f, 0.0f, 0.0f, 0.0f};    // then to the lower bone (about the new elbow)
    glm::vec3 elbow{0.0f}, wrist{0.0f};         // where they end up
    bool      reached = true;                   // false: the target is out of reach (stretched toward it)
};

/// Shoulder `s`, elbow `e` and wrist `w` as they are; the wrist to go to `t`,
/// the elbow bending toward `pole` (a direction). Bone lengths are kept.
inline TwoBoneIk solve_two_bone_ik(const glm::vec3& s, const glm::vec3& e, const glm::vec3& w,
                                   const glm::vec3& t, const glm::vec3& pole) {
    TwoBoneIk r;
    const float a = glm::length(e - s), b = glm::length(w - e);
    if (a < 1e-6f || b < 1e-6f) { r.elbow = e; r.wrist = w; return r; }
    glm::vec3 to = t - s;
    float d = glm::length(to);
    const glm::vec3 dir = d > 1e-6f ? to / d : glm::normalize(w - s);
    const float dmin = std::fabs(a - b) + 1e-4f, dmax = a + b - 1e-4f;
    r.reached = d >= dmin && d <= dmax;
    d = std::clamp(d, dmin, dmax);
    const float cos_a = std::clamp((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
    const float sin_a = std::sqrt(std::max(0.0f, 1.0f - cos_a * cos_a));
    // The pole's part across the shoulder-target line: the way the elbow goes.
    glm::vec3 across = pole - dir * glm::dot(pole, dir);
    if (glm::length(across) < 1e-5f) {   // pole along the line: keep the elbow's own side
        across = (e - s) - dir * glm::dot(e - s, dir);
        if (glm::length(across) < 1e-5f) across = glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::length(across) < 1e-5f) across = glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    across = glm::normalize(across);
    r.elbow = s + dir * (a * cos_a) + across * (a * sin_a);
    r.wrist = s + dir * d;
    r.upper = rotation_between(e - s, r.elbow - s);
    r.fore  = rotation_between(r.upper * (w - e), r.wrist - r.elbow);
    return r;
}

}  // namespace schizo::renderer
