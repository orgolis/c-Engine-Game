#pragma once
// ============================================================================
// snapping — grid, angle and scale snapping for the transform gizmo (4.7).
//
// Without it, "line these up" means dragging until two numbers look close and
// then typing them by hand in the inspector. Every greybox is a few
// thousandths off, walls do not meet, and tiling geometry shows seams that
// nobody can find because the offset is 0.003.
//
// Pure functions, apart from the gizmo, because the rounding rules are the part
// that can be subtly wrong: snapping negatives the wrong way, or drifting when
// a drag is applied repeatedly, is invisible on any single drag and obvious
// after twenty.
// ============================================================================

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace schizo::editor {

/// Round `v` to the nearest multiple of `step`. A step of 0 or less means "no
/// snapping" and returns `v` untouched, so callers can pass a user setting
/// straight through without guarding it.
///
/// std::round, NOT a truncating cast: casting rounds toward zero, so -0.4 with
/// a step of 1 would snap to 0 while +0.4 snaps to 0 as well -- symmetric by
/// luck -- but -1.6 would snap to -1 while +1.6 snaps to +2. Geometry laid out
/// across the origin then comes out subtly asymmetric, which is exactly the
/// kind of error nobody thinks to look for.
inline float snap_to(float v, float step) {
    if (step <= 0.0f) return v;
    return std::round(v / step) * step;
}

/// Snap each axis independently to `step` units.
inline glm::vec3 snap_position(const glm::vec3& p, float step) {
    if (step <= 0.0f) return p;
    return glm::vec3(snap_to(p.x, step), snap_to(p.y, step), snap_to(p.z, step));
}

/// Snap a position relative to an ORIGIN rather than to world zero.
///
/// This is what makes snapping useful on a level that was not authored on the
/// grid: an artist aligning to an existing wall wants the spacing to be exact,
/// not the absolute coordinate. Snapping to world zero would drag their whole
/// layout onto a grid it was never on.
inline glm::vec3 snap_position_relative(const glm::vec3& p, const glm::vec3& origin,
                                        float step) {
    if (step <= 0.0f) return p;
    return origin + snap_position(p - origin, step);
}

/// Snap an angle in DEGREES to `step` degrees.
inline float snap_angle_deg(float deg, float step) { return snap_to(deg, step); }

/// Snap a rotation to `step`-degree increments about each axis.
///
/// Converted through Euler angles and back, which is lossy in general -- but
/// the snapped result is a rotation that IS expressible as whole degree steps
/// per axis, which is what the artist asked for. Preserving the exact original
/// orientation is the opposite of the request.
inline glm::quat snap_rotation(const glm::quat& q, float step_deg) {
    if (step_deg <= 0.0f) return q;
    glm::vec3 e = glm::degrees(glm::eulerAngles(q));
    e.x = snap_to(e.x, step_deg);
    e.y = snap_to(e.y, step_deg);
    e.z = snap_to(e.z, step_deg);
    return glm::quat(glm::radians(e));
}

/// Snap a scale, with a floor so an object can never be snapped to zero size.
///
/// A zero scale collapses a mesh to a point, produces a degenerate collider,
/// and cannot be dragged back out because every subsequent multiply is zero.
/// The floor is one step, not epsilon: recovering from "very slightly there"
/// is nearly as bad as recovering from nothing.
inline glm::vec3 snap_scale(const glm::vec3& s, float step) {
    if (step <= 0.0f) return s;
    glm::vec3 out = snap_position(s, step);
    out.x = out.x == 0.0f ? step : out.x;
    out.y = out.y == 0.0f ? step : out.y;
    out.z = out.z == 0.0f ? step : out.z;
    return out;
}

/// The editor's snap settings, one per transform mode because the useful step
/// sizes are nothing alike: 0.25 units, 15 degrees, 0.1 scale.
struct SnapSettings {
    bool  enabled       = false;   // off by default; Ctrl toggles it per-drag
    float translate     = 0.25f;
    float rotate_deg    = 15.0f;
    float scale         = 0.1f;
    bool  relative      = false;   // snap relative to where the drag began

    float step_for_translate() const { return enabled ? translate  : 0.0f; }
    float step_for_rotate()    const { return enabled ? rotate_deg : 0.0f; }
    float step_for_scale()     const { return enabled ? scale      : 0.0f; }
};

}  // namespace schizo::editor
