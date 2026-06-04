#include "collision.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <limits>

namespace schizo::physics {

// ============================================================================
// CollisionShape Implementation
// ============================================================================

glm::mat4 CollisionShape::GetLocalTransform() const {
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), local_offset_);
    glm::mat4 rotation = glm::mat4_cast(local_rotation_);
    return translation * rotation;
}

// ============================================================================
// SphereShape Implementation
// ============================================================================
// (All implementations are inline in header)

// ============================================================================
// BoxShape Implementation
// ============================================================================
// (all implementations are inline in header)

// ============================================================================
// CapsuleShape Implementation
// ============================================================================
// (all implementations are inline in header)

// ============================================================================
// CylinderShape Implementation
// ============================================================================
// (all implementations are inline in header)

// ============================================================================
// ConeShape Implementation
// ============================================================================
// (all implementations are inline in header)

// ============================================================================
// PlaneShape Implementation
// ============================================================================
// (all implementations are inline in header)

// ============================================================================
// Collision Detection — implementations consumed by PhysicsWorld's narrow
// phase. Contract for every test:
//   contact.normal     = unit vector pointing from body A toward body B
//   contact.point      = world-space contact location
//   contact.separation = signed distance; negative means penetration depth
//   return value       = true iff bodies overlap (separation < 0)
// ============================================================================

namespace collision {

glm::vec3 ClosestPointOnLineSegment(const glm::vec3& p,
                                    const glm::vec3& a, const glm::vec3& b)
{
    glm::vec3 ab = b - a;
    float ab_len2 = glm::dot(ab, ab);
    if (ab_len2 < 1e-12f) return a;           // degenerate segment
    float t = glm::dot(p - a, ab) / ab_len2;
    t = glm::clamp(t, 0.0f, 1.0f);
    return a + ab * t;
}

glm::vec3 ClosestPointInBox(const glm::vec3& point,
                            const glm::vec3& box_center,
                            const glm::vec3& box_half_extents)
{
    glm::vec3 local = point - box_center;
    glm::vec3 clamped = glm::clamp(local, -box_half_extents, box_half_extents);
    return box_center + clamped;
}

bool SphereSphere(const glm::vec3& pos_a, float radius_a,
                  const glm::vec3& pos_b, float radius_b,
                  Contact& contact)
{
    glm::vec3 delta = pos_b - pos_a;
    float dist2 = glm::dot(delta, delta);
    float sum_r = radius_a + radius_b;
    if (dist2 >= sum_r * sum_r) return false;

    float dist = std::sqrt(std::max(dist2, 1e-12f));
    // Handle coincident centers — invent an arbitrary axis.
    contact.normal = (dist > 1e-6f) ? (delta / dist) : glm::vec3(0, 1, 0);
    contact.separation = dist - sum_r;        // < 0 because dist < sum_r
    contact.point = pos_a + contact.normal * radius_a;
    return true;
}

bool SphereBox(const glm::vec3& sphere_pos, float sphere_radius,
               const glm::vec3& box_pos, const glm::vec3& box_half_extents,
               const glm::quat& box_rot,
               Contact& contact)
{
    // Transform the sphere into box-local space (box at origin, axis-aligned).
    // The AABB math is unchanged from before; only the in/out transforms are
    // new. This handles ANY rotation of the box without needing OBB-OBB SAT.
    glm::quat inv_rot = glm::inverse(box_rot);
    glm::vec3 local_sphere = inv_rot * (sphere_pos - box_pos);

    glm::vec3 clamped = glm::clamp(local_sphere, -box_half_extents, box_half_extents);
    glm::vec3 delta_box_to_sphere = local_sphere - clamped;
    float dist2 = glm::dot(delta_box_to_sphere, delta_box_to_sphere);
    if (dist2 >= sphere_radius * sphere_radius) return false;

    float dist = std::sqrt(std::max(dist2, 1e-12f));
    glm::vec3 normal_local;
    if (dist > 1e-6f) {
        // Sphere outside the box. Normal A→B points sphere → box surface.
        normal_local = -delta_box_to_sphere / dist;
        contact.separation = dist - sphere_radius;
    } else {
        // Sphere center inside the box; eject through the nearest face.
        glm::vec3 d = box_half_extents - glm::abs(local_sphere);
        if (d.x < d.y && d.x < d.z) {
            normal_local = glm::vec3((local_sphere.x < 0) ? 1.0f : -1.0f, 0, 0);
            contact.separation = -(d.x + sphere_radius);
        } else if (d.y < d.z) {
            normal_local = glm::vec3(0, (local_sphere.y < 0) ? 1.0f : -1.0f, 0);
            contact.separation = -(d.y + sphere_radius);
        } else {
            normal_local = glm::vec3(0, 0, (local_sphere.z < 0) ? 1.0f : -1.0f);
            contact.separation = -(d.z + sphere_radius);
        }
    }
    contact.normal = box_rot * normal_local;
    contact.point = box_pos + box_rot * clamped;
    return true;
}

bool BoxBox(const glm::vec3& pos_a, const glm::vec3& extents_a, const glm::quat& rot_a,
            const glm::vec3& pos_b, const glm::vec3& extents_b, const glm::quat& rot_b,
            Contact& contact)
{
    // 15-axis SAT per Ericson §4.4.1. We work in A's local frame so A's axes
    // are (1,0,0), (0,1,0), (0,0,1). B's axes (columns of R) and the
    // translation t are expressed in A-local. For each candidate axis we
    // compute the projected radii of both boxes and the projected separation;
    // a positive separation along ANY axis means the boxes don't overlap.
    // Among the 15 axes that DO overlap, the one with the smallest overlap
    // wins — its (negated) overlap is the penetration depth and its axis is
    // the contact normal.
    const float kEps = 1e-6f;

    // Express A's basis as world-space columns. Same for B.
    glm::vec3 au[3] = { rot_a * glm::vec3(1,0,0), rot_a * glm::vec3(0,1,0), rot_a * glm::vec3(0,0,1) };
    glm::vec3 bu[3] = { rot_b * glm::vec3(1,0,0), rot_b * glm::vec3(0,1,0), rot_b * glm::vec3(0,0,1) };

    // R[i][j] = dot(au[i], bu[j]) — rotation taking B-local to A-local.
    // AbsR adds a small epsilon to handle near-parallel edges that would
    // otherwise produce a zero-length cross-product axis.
    float R[3][3], AbsR[3][3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R[i][j] = glm::dot(au[i], bu[j]);
            AbsR[i][j] = std::abs(R[i][j]) + kEps;
        }
    }

    // Translation, then express it in A's local frame.
    glm::vec3 t_world = pos_b - pos_a;
    float t[3] = {
        glm::dot(t_world, au[0]),
        glm::dot(t_world, au[1]),
        glm::dot(t_world, au[2]),
    };

    const float ea[3] = { extents_a.x, extents_a.y, extents_a.z };
    const float eb[3] = { extents_b.x, extents_b.y, extents_b.z };

    float min_overlap = std::numeric_limits<float>::max();
    glm::vec3 best_axis(0.0f);   // in world space, oriented A → B

    auto consider = [&](float overlap, const glm::vec3& world_axis, float sign) -> bool {
        if (overlap < 0.0f) return false;        // separating axis found
        if (overlap < min_overlap) {
            min_overlap = overlap;
            best_axis = world_axis * sign;
        }
        return true;
    };

    // L = A's face axes (au[0..2]).
    for (int i = 0; i < 3; ++i) {
        float ra = ea[i];
        float rb = eb[0]*AbsR[i][0] + eb[1]*AbsR[i][1] + eb[2]*AbsR[i][2];
        float overlap = (ra + rb) - std::abs(t[i]);
        if (!consider(overlap, au[i], (t[i] < 0.0f) ? -1.0f : 1.0f)) return false;
    }

    // L = B's face axes (bu[0..2]). Project t onto B's frame via R^T.
    for (int i = 0; i < 3; ++i) {
        float ra = ea[0]*AbsR[0][i] + ea[1]*AbsR[1][i] + ea[2]*AbsR[2][i];
        float rb = eb[i];
        float bt = t[0]*R[0][i] + t[1]*R[1][i] + t[2]*R[2][i];
        float overlap = (ra + rb) - std::abs(bt);
        if (!consider(overlap, bu[i], (bt < 0.0f) ? -1.0f : 1.0f)) return false;
    }

    // L = au[i] × bu[j]. For each pair we precompute the projected radii
    // using the cross-product property |Ra[k] · (Ra[i] × Rb[j])| = AbsR[other_i][j]
    // and the projected centre offset using two-by-two combinations of t and R.
    struct CrossAxis {
        int i, j;
        float ra_k1, ra_k2;     // which AbsR entries to multiply ea by
        float rb_k1, rb_k2;     // and for eb
        float t_a, t_b;         // |t[ta]*R[tb][?] - t[tc]*R[td][?]|
    };

    // Macro-free unrolled tests. Each block: L = au[i] × bu[j].
    auto cross_test = [&](int i, int j) -> bool {
        // Indices into A's other two axes (not i) and B's other two (not j)
        int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
        int j1 = (j + 1) % 3, j2 = (j + 2) % 3;

        float ra = ea[i1] * AbsR[i2][j] + ea[i2] * AbsR[i1][j];
        float rb = eb[j1] * AbsR[i][j2] + eb[j2] * AbsR[i][j1];

        // |t × axis| component: t[i2]*R[i1][j] - t[i1]*R[i2][j]
        float tproj = t[i2] * R[i1][j] - t[i1] * R[i2][j];
        float overlap = (ra + rb) - std::abs(tproj);
        if (overlap < 0.0f) return false;

        // World-space axis = au[i] × bu[j]; flip to point A → B.
        glm::vec3 axis = glm::cross(au[i], bu[j]);
        float axis_len2 = glm::dot(axis, axis);
        if (axis_len2 < kEps * kEps) return true;   // near-parallel, skip — face axes already cover it
        axis /= std::sqrt(axis_len2);
        return consider(overlap, axis, (tproj < 0.0f) ? -1.0f : 1.0f);
    };

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!cross_test(i, j)) return false;
        }
    }

    // Surviving — best_axis already points A → B in world space.
    contact.normal = best_axis;
    contact.separation = -min_overlap;
    // Cheap contact-point approximation: midpoint between the box centres.
    // Adequate for impulse + position correction; a precise clipping
    // implementation would refine to the deepest contact manifold.
    contact.point = (pos_a + pos_b) * 0.5f;
    return true;
}

bool SpherePlane(const glm::vec3& sphere_pos, float sphere_radius,
                 const glm::vec3& plane_pos, const glm::vec3& plane_normal,
                 Contact& contact)
{
    // plane_normal points OUT of the plane's solid side (free space).
    // Sphere "above" the plane has signed_dist > 0.
    glm::vec3 n = glm::normalize(plane_normal);
    float signed_dist = glm::dot(sphere_pos - plane_pos, n);
    if (signed_dist >= sphere_radius) return false;

    // A=sphere, B=plane. Normal A→B = sphere → plane surface = -plane_normal.
    // ResolveCollision uses `pos_a -= normal*sep` to push the sphere out;
    // with this normal that displacement lands in +plane_normal (free space).
    contact.normal = -n;
    contact.separation = signed_dist - sphere_radius;   // < 0 → penetration
    contact.point = sphere_pos - n * sphere_radius;
    return true;
}

bool BoxPlane(const glm::vec3& box_pos, const glm::vec3& box_half_extents,
              const glm::quat& box_rot,
              const glm::vec3& plane_pos, const glm::vec3& plane_normal,
              Contact& contact)
{
    glm::vec3 n = glm::normalize(plane_normal);

    // Effective "radius" of the (possibly rotated) box along the plane normal
    // = sum of |axis_i · n| * he_i over each box axis. For an identity
    // rotation this reduces to the axis-aligned formula |he·n| component-wise.
    glm::vec3 ax = box_rot * glm::vec3(1, 0, 0);
    glm::vec3 ay = box_rot * glm::vec3(0, 1, 0);
    glm::vec3 az = box_rot * glm::vec3(0, 0, 1);
    float box_radius =
          std::abs(glm::dot(ax, n)) * box_half_extents.x
        + std::abs(glm::dot(ay, n)) * box_half_extents.y
        + std::abs(glm::dot(az, n)) * box_half_extents.z;

    float signed_dist = glm::dot(box_pos - plane_pos, n);
    if (signed_dist >= box_radius) return false;

    contact.normal = -n;
    contact.separation = signed_dist - box_radius;
    contact.point = box_pos - n * signed_dist;
    return true;
}

bool CapsuleSphere(const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                   const glm::vec3& sphere_pos, float sphere_radius,
                   Contact& contact)
{
    glm::vec3 closest = ClosestPointOnLineSegment(sphere_pos, cap_a, cap_b);
    return SphereSphere(closest, cap_radius, sphere_pos, sphere_radius, contact);
}

bool CapsuleBox(const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                const glm::vec3& box_pos, const glm::vec3& box_half_extents,
                const glm::quat& box_rot,
                Contact& contact)
{
    // Approximation: find the spine point closest to the box center (in the
    // box's LOCAL frame so it works for rotated boxes), then reduce to
    // sphere-vs-box at that point. Exact for upright capsules vs any
    // orientation of box; degrades gracefully when the capsule is tilted.
    glm::quat inv_rot = glm::inverse(box_rot);
    glm::vec3 a_local = inv_rot * (cap_a - box_pos);
    glm::vec3 b_local = inv_rot * (cap_b - box_pos);
    glm::vec3 closest_local = ClosestPointOnLineSegment(glm::vec3(0.0f), a_local, b_local);
    glm::vec3 closest_world = box_pos + box_rot * closest_local;
    return SphereBox(closest_world, cap_radius, box_pos, box_half_extents, box_rot, contact);
}

bool CapsulePlane(const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                  const glm::vec3& plane_pos, const glm::vec3& plane_normal,
                  Contact& contact)
{
    glm::vec3 n = glm::normalize(plane_normal);
    float dist_a = glm::dot(cap_a - plane_pos, n);
    float dist_b = glm::dot(cap_b - plane_pos, n);
    glm::vec3 closest = (dist_a < dist_b) ? cap_a : cap_b;
    return SpherePlane(closest, cap_radius, plane_pos, n, contact);
}

// Closest points between two 3D line segments (p1→q1 and p2→q2).
// Classic Ericson Real-Time Collision Detection §5.1.9; handles all degenerate
// cases (both points, one point, parallel lines).
static void ClosestPointsOnSegments(const glm::vec3& p1, const glm::vec3& q1,
                                    const glm::vec3& p2, const glm::vec3& q2,
                                    float& s, float& t,
                                    glm::vec3& c1, glm::vec3& c2)
{
    constexpr float kEps = 1e-8f;
    glm::vec3 d1 = q1 - p1;
    glm::vec3 d2 = q2 - p2;
    glm::vec3 r  = p1 - p2;
    float a = glm::dot(d1, d1);
    float e = glm::dot(d2, d2);
    float f = glm::dot(d2, r);

    if (a <= kEps && e <= kEps) {
        s = 0.0f; t = 0.0f; c1 = p1; c2 = p2;
        return;
    }
    if (a <= kEps) {
        s = 0.0f;
        t = glm::clamp(f / e, 0.0f, 1.0f);
    } else {
        float c = glm::dot(d1, r);
        if (e <= kEps) {
            t = 0.0f;
            s = glm::clamp(-c / a, 0.0f, 1.0f);
        } else {
            float b = glm::dot(d1, d2);
            float denom = a * e - b * b;
            s = (denom != 0.0f) ? glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = glm::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = glm::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

bool CapsuleCapsule(const glm::vec3& a0, const glm::vec3& a1, float radius_a,
                    const glm::vec3& b0, const glm::vec3& b1, float radius_b,
                    Contact& contact)
{
    glm::vec3 ca, cb;
    float s = 0.0f, t = 0.0f;
    ClosestPointsOnSegments(a0, a1, b0, b1, s, t, ca, cb);
    return SphereSphere(ca, radius_a, cb, radius_b, contact);
}

// ----------------------------------------------------------------------------
// Cylinder pair tests.
// ----------------------------------------------------------------------------

bool CylinderSphere(const glm::vec3& cyl_pos, float cyl_radius, float cyl_hh,
                    const glm::quat& cyl_rot,
                    const glm::vec3& sphere_pos, float sphere_radius,
                    Contact& contact)
{
    // Work in cylinder-local space: cylinder is centred on the origin, Y-axis,
    // radius r, span [-hh, +hh]. The closest point on the cylinder to the
    // sphere center has a clean closed form (clamp Y, clamp radial XZ).
    glm::quat inv_rot = glm::inverse(cyl_rot);
    glm::vec3 ls = inv_rot * (sphere_pos - cyl_pos);

    float xz_dist = std::sqrt(ls.x * ls.x + ls.z * ls.z);
    bool inside_radial = xz_dist <= cyl_radius;
    bool inside_height = std::abs(ls.y) <= cyl_hh;

    glm::vec3 closest_local;
    glm::vec3 normal_local;
    float separation;

    if (inside_radial && inside_height) {
        // Sphere centre is inside the cylinder. Push out via the closest
        // face — top cap, bottom cap, or side, whichever is nearest.
        float d_top  = cyl_hh - ls.y;
        float d_bot  = ls.y + cyl_hh;
        float d_side = cyl_radius - xz_dist;

        if (d_side <= d_top && d_side <= d_bot) {
            float k = (xz_dist > 1e-6f) ? (cyl_radius / xz_dist) : 1.0f;
            closest_local = glm::vec3(ls.x * k, ls.y, ls.z * k);
            normal_local  = (xz_dist > 1e-6f)
                ? glm::vec3(ls.x / xz_dist, 0.0f, ls.z / xz_dist)
                : glm::vec3(1, 0, 0);
            separation = -(d_side + sphere_radius);
        } else if (d_top <= d_bot) {
            closest_local = glm::vec3(ls.x, cyl_hh, ls.z);
            normal_local  = glm::vec3(0, 1, 0);
            separation = -(d_top + sphere_radius);
        } else {
            closest_local = glm::vec3(ls.x, -cyl_hh, ls.z);
            normal_local  = glm::vec3(0, -1, 0);
            separation = -(d_bot + sphere_radius);
        }
    } else {
        // Sphere centre is outside the cylinder. Clamp to the surface.
        float clamped_y = glm::clamp(ls.y, -cyl_hh, cyl_hh);
        glm::vec3 clamped_xz_ls = ls;
        clamped_xz_ls.y = 0.0f;
        if (xz_dist > cyl_radius) {
            clamped_xz_ls *= cyl_radius / xz_dist;
        }
        closest_local = glm::vec3(clamped_xz_ls.x, clamped_y, clamped_xz_ls.z);

        glm::vec3 delta = ls - closest_local;             // surface → sphere
        float dist2 = glm::dot(delta, delta);
        if (dist2 >= sphere_radius * sphere_radius) return false;

        float dist = std::sqrt(std::max(dist2, 1e-12f));
        if (dist > 1e-6f) {
            // A=cylinder, B=sphere. Normal A→B points from cyl outward.
            normal_local = delta / dist;
        } else {
            normal_local = glm::vec3(0, 1, 0);
        }
        separation = dist - sphere_radius;
    }

    contact.normal = cyl_rot * normal_local;
    contact.separation = separation;
    contact.point = cyl_pos + cyl_rot * closest_local;
    return true;
}

bool CylinderPlane(const glm::vec3& cyl_pos, float cyl_radius, float cyl_hh,
                   const glm::quat& cyl_rot,
                   const glm::vec3& plane_pos, const glm::vec3& plane_normal,
                   Contact& contact)
{
    // Project the cylinder onto the plane normal. A Y-axis cylinder rotated
    // by R has effective "half-extent" along n equal to
    //     |axis·n| * hh + sqrt(1 - (axis·n)²) * radius
    // where axis = R·(0,1,0). The first term is the cylinder's reach along
    // its own axis projected onto n; the second is the radial spread in the
    // plane perpendicular to axis.
    glm::vec3 n = glm::normalize(plane_normal);
    glm::vec3 axis = cyl_rot * glm::vec3(0, 1, 0);
    float axis_dot_n = glm::dot(axis, n);
    float perp = std::sqrt(std::max(0.0f, 1.0f - axis_dot_n * axis_dot_n));
    float reach = std::abs(axis_dot_n) * cyl_hh + perp * cyl_radius;

    float signed_dist = glm::dot(cyl_pos - plane_pos, n);
    if (signed_dist >= reach) return false;

    contact.normal = -n;
    contact.separation = signed_dist - reach;
    contact.point = cyl_pos - n * signed_dist;
    return true;
}

// Helper: derive a bounding capsule from a cylinder. We keep the radius and
// reduce the cylindrical half-height by `radius` so the capsule's
// hemispheres overlap the cylinder's flat caps instead of extending past
// them. When radius >= hh the cylinder degenerates to a sphere of `radius`.
static void CylinderToBoundingCapsule(const glm::vec3& cyl_pos, float cyl_radius, float cyl_hh,
                                      const glm::quat& cyl_rot,
                                      glm::vec3& out_a, glm::vec3& out_b, float& out_radius)
{
    glm::vec3 axis = cyl_rot * glm::vec3(0, 1, 0);
    float hh = std::max(0.0f, cyl_hh - cyl_radius);
    out_a = cyl_pos - axis * hh;
    out_b = cyl_pos + axis * hh;
    out_radius = cyl_radius;
}

bool CylinderBox(const glm::vec3& cyl_pos, float cyl_radius, float cyl_hh,
                 const glm::quat& cyl_rot,
                 const glm::vec3& box_pos, const glm::vec3& box_he, const glm::quat& box_rot,
                 Contact& contact)
{
    // Capsule reduction — see CylinderToBoundingCapsule. Loses precision at
    // cap-vs-face contact (where a true cylinder rests on a flat face by
    // its whole disc, not a single hemisphere point). Adequate for the
    // common "cylinder standing on box floor" case.
    glm::vec3 a, b; float r;
    CylinderToBoundingCapsule(cyl_pos, cyl_radius, cyl_hh, cyl_rot, a, b, r);
    return CapsuleBox(a, b, r, box_pos, box_he, box_rot, contact);
}

bool CylinderCapsule(const glm::vec3& cyl_pos, float cyl_radius, float cyl_hh,
                     const glm::quat& cyl_rot,
                     const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                     Contact& contact)
{
    glm::vec3 a, b; float r;
    CylinderToBoundingCapsule(cyl_pos, cyl_radius, cyl_hh, cyl_rot, a, b, r);
    return CapsuleCapsule(a, b, r, cap_a, cap_b, cap_radius, contact);
}

bool CylinderCylinder(const glm::vec3& a_pos, float a_radius, float a_hh, const glm::quat& a_rot,
                      const glm::vec3& b_pos, float b_radius, float b_hh, const glm::quat& b_rot,
                      Contact& contact)
{
    glm::vec3 a0, a1; float ar;
    glm::vec3 b0, b1; float br;
    CylinderToBoundingCapsule(a_pos, a_radius, a_hh, a_rot, a0, a1, ar);
    CylinderToBoundingCapsule(b_pos, b_radius, b_hh, b_rot, b0, b1, br);
    return CapsuleCapsule(a0, a1, ar, b0, b1, br, contact);
}

// ----------------------------------------------------------------------------
// Triangle primitives.
// ----------------------------------------------------------------------------

glm::vec3 ClosestPointOnTriangle(const glm::vec3& p,
                                 const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
{
    // Ericson §5.1.5 — handles all seven Voronoi regions (3 vertices, 3
    // edges, 1 interior) using barycentric coordinates.
    glm::vec3 ab = b - a;
    glm::vec3 ac = c - a;
    glm::vec3 ap = p - a;
    float d1 = glm::dot(ab, ap);
    float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;          // vertex A region

    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp);
    float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;            // vertex B region

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return a + ab * v;                            // edge AB
    }

    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp);
    float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;             // vertex C region

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return a + ac * w;                            // edge AC
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w;                       // edge BC
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return a + ab * v + ac * w;                       // inside triangle
}

bool SphereTriangle(const glm::vec3& sphere_pos, float sphere_radius,
                    const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                    Contact& contact)
{
    glm::vec3 closest = ClosestPointOnTriangle(sphere_pos, a, b, c);
    glm::vec3 delta = sphere_pos - closest;
    float dist2 = glm::dot(delta, delta);
    if (dist2 >= sphere_radius * sphere_radius) return false;

    float dist = std::sqrt(std::max(dist2, 1e-12f));
    if (dist > 1e-6f) {
        // A=sphere, B=triangle. Normal A→B = from sphere TO triangle.
        contact.normal = -delta / dist;
    } else {
        // Center on the triangle. Use triangle face normal pointing
        // into the sphere as a sane fallback.
        glm::vec3 tn = glm::cross(b - a, c - a);
        float tn_len = glm::length(tn);
        contact.normal = (tn_len > 1e-6f) ? -tn / tn_len : glm::vec3(0, 1, 0);
    }
    contact.separation = dist - sphere_radius;
    contact.point = closest;
    return true;
}

bool CapsuleTriangle(const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                     const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                     Contact& contact)
{
    // Cheap approximation: sample the capsule spine at a few points (the
    // endpoints + 2 mids), test each as a sphere against the triangle, and
    // keep the deepest contact. Misses contacts that fall strictly between
    // samples but is fine for the common standing/walking case. A precise
    // segment-vs-triangle closest-point routine is future polish.
    constexpr int kSamples = 5;
    bool found = false;
    float deepest = 0.0f;
    Contact best{};
    for (int i = 0; i < kSamples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kSamples - 1);
        glm::vec3 p = glm::mix(cap_a, cap_b, t);
        Contact local;
        if (!SphereTriangle(p, cap_radius, a, b, c, local)) continue;
        float pen = -local.separation;
        if (pen > deepest) {
            deepest = pen;
            best = local;
            found = true;
        }
    }
    if (!found) return false;
    contact = best;
    return true;
}

// ----------------------------------------------------------------------------
// Mesh pairs. Triangles are stored in the mesh's local frame; transform the
// query shape into that frame, scan the triangle list, then transform the
// contact back to world.
// ----------------------------------------------------------------------------

bool SphereMesh(const glm::vec3& sphere_pos, float sphere_radius,
                const glm::vec3& mesh_pos, const glm::quat& mesh_rot,
                const std::vector<glm::vec3>& triangles,
                Contact& contact)
{
    if (triangles.size() < 3) return false;

    glm::quat inv_rot = glm::inverse(mesh_rot);
    glm::vec3 local_sphere = inv_rot * (sphere_pos - mesh_pos);

    bool found = false;
    float deepest = 0.0f;
    Contact best{};

    for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
        Contact local;
        if (!SphereTriangle(local_sphere, sphere_radius,
                            triangles[i], triangles[i+1], triangles[i+2],
                            local)) continue;
        float pen = -local.separation;
        if (pen > deepest) {
            deepest = pen;
            best = local;
            found = true;
        }
    }
    if (!found) return false;

    contact.normal = mesh_rot * best.normal;
    contact.separation = best.separation;
    contact.point = mesh_pos + mesh_rot * best.point;
    return true;
}

bool CapsuleMesh(const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                 const glm::vec3& mesh_pos, const glm::quat& mesh_rot,
                 const std::vector<glm::vec3>& triangles,
                 Contact& contact)
{
    if (triangles.size() < 3) return false;

    glm::quat inv_rot = glm::inverse(mesh_rot);
    glm::vec3 local_a = inv_rot * (cap_a - mesh_pos);
    glm::vec3 local_b = inv_rot * (cap_b - mesh_pos);

    bool found = false;
    float deepest = 0.0f;
    Contact best{};

    for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
        Contact local;
        if (!CapsuleTriangle(local_a, local_b, cap_radius,
                             triangles[i], triangles[i+1], triangles[i+2],
                             local)) continue;
        float pen = -local.separation;
        if (pen > deepest) {
            deepest = pen;
            best = local;
            found = true;
        }
    }
    if (!found) return false;

    contact.normal = mesh_rot * best.normal;
    contact.separation = best.separation;
    contact.point = mesh_pos + mesh_rot * best.point;
    return true;
}

} // namespace collision

} // namespace schizo::physics

