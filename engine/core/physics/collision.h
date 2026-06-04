#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <cstdint>
#include <vector>

namespace schizo::physics {

// Forward declarations
class CollisionShape;
class RigidBody;

// ============================================================================
// Collision Shape Types
// ============================================================================

enum class ShapeType : uint8_t {
    Sphere,
    Box,
    Capsule,
    Cylinder,
    Cone,
    Mesh,      // Static mesh collider
    Plane,     // Infinite plane
    Custom,
};

// ============================================================================
// Collision Shape Base Class
// ============================================================================

/**
 * @class CollisionShape
 * @brief Base class for all collision shapes
 * 
 * Defines the geometry used for collision detection and response.
 * Shapes are stored in local space relative to the RigidBody.
 */
class CollisionShape {
public:
    virtual ~CollisionShape() = default;
    
    /**
     * Get shape type
     */
    virtual ShapeType GetType() const = 0;
    

    
    /**
     * Get local-space bounding box (AABB)
     */
    virtual void GetBounds(glm::vec3& min, glm::vec3& max) const = 0;
    
    /**
     * Get bounding sphere radius
     */
    virtual float GetBoundingRadius() const = 0;
    
    /**
     * Set local offset from body center
     */
    void SetLocalOffset(const glm::vec3& offset) { local_offset_ = offset; }
    glm::vec3 GetLocalOffset() const { return local_offset_; }
    
    /**
     * Set local rotation
     */
    void SetLocalRotation(const glm::quat& rotation) { local_rotation_ = rotation; }
    glm::quat GetLocalRotation() const { return local_rotation_; }
    
    /**
     * Calculate local transform matrix
     */
    glm::mat4 GetLocalTransform() const;

protected:
    CollisionShape() = default;
    
    glm::vec3 local_offset_ = glm::vec3(0);
    glm::quat local_rotation_ = glm::quat(1, 0, 0, 0);
};

// ============================================================================
// Specific Collision Shapes
// ============================================================================

/**
 * @class SphereShape
 * @brief Sphere collision shape
 */
class SphereShape : public CollisionShape {
public:
    SphereShape(float radius = 1.0f) : radius_(radius) {}
    
    ShapeType GetType() const override { return ShapeType::Sphere; }
    
    void GetBounds(glm::vec3& min, glm::vec3& max) const override {
        glm::vec3 r(radius_);
        min = local_offset_ - r;
        max = local_offset_ + r;
    }
    
    float GetBoundingRadius() const override { return radius_; }
    
    float GetRadius() const { return radius_; }
    void SetRadius(float r) { radius_ = r; }

private:
    float radius_;
};

/**
 * @class BoxShape
 * @brief Axis-aligned box collision shape
 */
class BoxShape : public CollisionShape {
public:
    BoxShape(const glm::vec3& half_extents = glm::vec3(1.0f)) 
        : half_extents_(half_extents) {}
    
    ShapeType GetType() const override { return ShapeType::Box; }
    
    void GetBounds(glm::vec3& min, glm::vec3& max) const override {
        min = local_offset_ - half_extents_;
        max = local_offset_ + half_extents_;
    }
    
    float GetBoundingRadius() const override {
        return glm::length(half_extents_);
    }
    
    glm::vec3 GetHalfExtents() const { return half_extents_; }
    void SetHalfExtents(const glm::vec3& extents) { half_extents_ = extents; }
    
    glm::vec3 GetSize() const { return half_extents_ * 2.0f; }

private:
    glm::vec3 half_extents_;
};

/**
 * @class CapsuleShape
 * @brief Capsule (cylinder with hemispherical ends)
 */
class CapsuleShape : public CollisionShape {
public:
    CapsuleShape(float radius = 0.5f, float half_height = 1.0f)
        : radius_(radius), half_height_(half_height) {}
    
    ShapeType GetType() const override { return ShapeType::Capsule; }
    
    void GetBounds(glm::vec3& min, glm::vec3& max) const override {
        glm::vec3 extents(radius_, half_height_ + radius_, radius_);
        min = local_offset_ - extents;
        max = local_offset_ + extents;
    }
    
    float GetBoundingRadius() const override {
        return std::sqrt(radius_ * radius_ + half_height_ * half_height_);
    }
    
    float GetRadius() const { return radius_; }
    float GetHalfHeight() const { return half_height_; }
    void SetRadius(float r) { radius_ = r; }
    void SetHalfHeight(float h) { half_height_ = h; }

private:
    float radius_;
    float half_height_;
};

/**
 * @class CylinderShape
 * @brief Cylinder collision shape
 */
class CylinderShape : public CollisionShape {
public:
    CylinderShape(float radius = 0.5f, float half_height = 1.0f)
        : radius_(radius), half_height_(half_height) {}
    
    ShapeType GetType() const override { return ShapeType::Cylinder; }
    
    void GetBounds(glm::vec3& min, glm::vec3& max) const override {
        glm::vec3 extents(radius_, half_height_, radius_);
        min = local_offset_ - extents;
        max = local_offset_ + extents;
    }
    
    float GetBoundingRadius() const override {
        return std::sqrt(radius_ * radius_ + half_height_ * half_height_);
    }
    
    float GetRadius() const { return radius_; }
    float GetHalfHeight() const { return half_height_; }

private:
    float radius_;
    float half_height_;
};

/**
 * @class MeshShape
 * @brief Triangle-soup collider — typically built from a glTF/OBJ asset.
 *
 * Triangles are stored as a flat 3N-vertex vector (every three vertices form
 * one triangle). Always Static-side for now: a Dynamic body backed by a
 * triangle mesh would need triangle vs triangle which we don't ship.
 */
class MeshShape : public CollisionShape {
public:
    explicit MeshShape(std::vector<glm::vec3> triangles)
        : triangles_(std::move(triangles)) {
        if (triangles_.empty()) {
            local_min_ = glm::vec3(0.0f);
            local_max_ = glm::vec3(0.0f);
            bounding_radius_ = 0.0f;
            return;
        }
        local_min_ = triangles_[0];
        local_max_ = triangles_[0];
        for (const auto& v : triangles_) {
            local_min_ = glm::min(local_min_, v);
            local_max_ = glm::max(local_max_, v);
        }
        glm::vec3 half = (local_max_ - local_min_) * 0.5f;
        bounding_radius_ = glm::length(half);
    }

    ShapeType GetType() const override { return ShapeType::Mesh; }

    void GetBounds(glm::vec3& min, glm::vec3& max) const override {
        min = local_offset_ + local_min_;
        max = local_offset_ + local_max_;
    }

    float GetBoundingRadius() const override { return bounding_radius_; }

    const std::vector<glm::vec3>& GetTriangles() const { return triangles_; }
    size_t GetTriangleCount() const { return triangles_.size() / 3; }

private:
    std::vector<glm::vec3> triangles_;   // flat 3N: tri i = triangles_[3i..3i+2]
    glm::vec3 local_min_{0.0f};
    glm::vec3 local_max_{0.0f};
    float     bounding_radius_ = 0.0f;
};

/**
 * @class PlaneShape
 * @brief Infinite plane (for ground, walls)
 */
class PlaneShape : public CollisionShape {
public:
    PlaneShape(const glm::vec3& normal = glm::vec3(0, 1, 0))
        : normal_(glm::normalize(normal)) {}
    
    ShapeType GetType() const override { return ShapeType::Plane; }
    
    void GetBounds(glm::vec3& min, glm::vec3& max) const override {
        // Infinite bounds
        min = glm::vec3(-1e10f);
        max = glm::vec3(1e10f);
    }
    
    float GetBoundingRadius() const override { return 1e10f; }
    
    glm::vec3 GetNormal() const { return normal_; }
    void SetNormal(const glm::vec3& n) { normal_ = glm::normalize(n); }

private:
    glm::vec3 normal_;
};

// ============================================================================
// Collision Information & Contact Data
// ============================================================================

/**
 * @struct Contact
 * @brief Collision contact point between two bodies
 */
struct Contact {
    glm::vec3 point;                // Contact point in world space
    glm::vec3 normal;               // Contact normal (from A to B)
    float separation = 0.0f;        // Separation distance (negative = penetration)
    float impulse = 0.0f;           // Applied impulse magnitude
    
    Contact() = default;
    Contact(const glm::vec3& p, const glm::vec3& n, float sep)
        : point(p), normal(n), separation(sep) {}
};

/**
 * @struct CollisionInfo
 * @brief Information about a collision between two bodies
 */
struct CollisionInfo {
    RigidBody* body_a = nullptr;
    RigidBody* body_b = nullptr;
    
    std::vector<Contact> contacts;
    
    glm::vec3 relative_velocity = glm::vec3(0);
    bool is_colliding = false;
    
    CollisionInfo() = default;
    CollisionInfo(RigidBody* a, RigidBody* b)
        : body_a(a), body_b(b) {}
};

// ============================================================================
// Collision Detection Functions
// ============================================================================

namespace collision {
    /**
     * Test sphere vs sphere collision
     */
    bool SphereSphere(const glm::vec3& pos_a, float radius_a,
                     const glm::vec3& pos_b, float radius_b,
                     Contact& contact);
    
    /**
     * Sphere vs box (box may be rotated; pass the box's world-space rotation).
     */
    bool SphereBox(const glm::vec3& sphere_pos, float sphere_radius,
                   const glm::vec3& box_pos, const glm::vec3& box_half_extents,
                   const glm::quat& box_rot,
                   Contact& contact);

    /**
     * Oriented-box vs oriented-box. Uses the 15-axis Separating Axis Theorem
     * (3 face normals of A, 3 of B, 9 edge-edge cross products). Identity
     * rotations reduce to plain AABB-vs-AABB.
     */
    bool BoxBox(const glm::vec3& pos_a, const glm::vec3& extents_a, const glm::quat& rot_a,
                const glm::vec3& pos_b, const glm::vec3& extents_b, const glm::quat& rot_b,
                Contact& contact);

    /**
     * Sphere vs plane.
     */
    bool SpherePlane(const glm::vec3& sphere_pos, float sphere_radius,
                     const glm::vec3& plane_pos, const glm::vec3& plane_normal,
                     Contact& contact);

    /**
     * Box vs plane (box may be rotated).
     */
    bool BoxPlane(const glm::vec3& box_pos, const glm::vec3& box_half_extents,
                  const glm::quat& box_rot,
                  const glm::vec3& plane_pos, const glm::vec3& plane_normal,
                  Contact& contact);

    /**
     * Capsule vs sphere. Capsule is described by its two spine endpoints in
     * world space (so the caller bakes in the body's rotation).
     */
    bool CapsuleSphere(const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                       const glm::vec3& sphere_pos, float sphere_radius,
                       Contact& contact);

    /**
     * Capsule vs box (box may be rotated).
     */
    bool CapsuleBox(const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                    const glm::vec3& box_pos, const glm::vec3& box_half_extents,
                    const glm::quat& box_rot,
                    Contact& contact);

    /**
     * Capsule vs plane.
     */
    bool CapsulePlane(const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                      const glm::vec3& plane_pos, const glm::vec3& plane_normal,
                      Contact& contact);

    /**
     * Capsule vs capsule.
     */
    bool CapsuleCapsule(const glm::vec3& a0, const glm::vec3& a1, float radius_a,
                        const glm::vec3& b0, const glm::vec3& b1, float radius_b,
                        Contact& contact);

    // --- Cylinder pairs (Y-axis cylinder, rotated by cyl_rot) ---
    // Cylinder has flat caps (no hemispheres). Conventions follow the rest
    // of the file: contact.normal A→B, contact.separation < 0 when overlapping.
    bool CylinderSphere(const glm::vec3& cyl_pos, float cyl_radius, float cyl_half_height,
                        const glm::quat& cyl_rot,
                        const glm::vec3& sphere_pos, float sphere_radius,
                        Contact& contact);

    bool CylinderPlane(const glm::vec3& cyl_pos, float cyl_radius, float cyl_half_height,
                       const glm::quat& cyl_rot,
                       const glm::vec3& plane_pos, const glm::vec3& plane_normal,
                       Contact& contact);

    // Box / Capsule / Cylinder vs Cylinder fall back to a capsule reduction
    // (using a capsule that bounds the cylinder), so cap-vs-face contacts are
    // slightly less precise than the Sphere/Plane native paths.
    bool CylinderBox(const glm::vec3& cyl_pos, float cyl_radius, float cyl_half_height,
                     const glm::quat& cyl_rot,
                     const glm::vec3& box_pos, const glm::vec3& box_he, const glm::quat& box_rot,
                     Contact& contact);

    bool CylinderCapsule(const glm::vec3& cyl_pos, float cyl_radius, float cyl_half_height,
                         const glm::quat& cyl_rot,
                         const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                         Contact& contact);

    bool CylinderCylinder(const glm::vec3& a_pos, float a_radius, float a_half_height, const glm::quat& a_rot,
                          const glm::vec3& b_pos, float b_radius, float b_half_height, const glm::quat& b_rot,
                          Contact& contact);

    // --- Triangle primitives ---------------------------------------------
    // Used by mesh queries below. ClosestPointOnTriangle is the canonical
    // RTCD §5.1.5 routine — handles interior, edges, and vertices.

    glm::vec3 ClosestPointOnTriangle(const glm::vec3& p,
                                     const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

    bool SphereTriangle(const glm::vec3& sphere_pos, float sphere_radius,
                        const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                        Contact& contact);

    bool CapsuleTriangle(const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                         const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                         Contact& contact);

    // --- Mesh pairs ------------------------------------------------------
    // The mesh's triangles are in mesh-local space; mesh_pos/mesh_rot
    // transform them into world. Each function iterates all triangles and
    // keeps the deepest-penetrating contact (no BVH yet — fine for
    // hundreds of triangles).

    bool SphereMesh(const glm::vec3& sphere_pos, float sphere_radius,
                    const glm::vec3& mesh_pos, const glm::quat& mesh_rot,
                    const std::vector<glm::vec3>& triangles,
                    Contact& contact);

    bool CapsuleMesh(const glm::vec3& cap_a, const glm::vec3& cap_b, float cap_radius,
                     const glm::vec3& mesh_pos, const glm::quat& mesh_rot,
                     const std::vector<glm::vec3>& triangles,
                     Contact& contact);
    
    /**
     * Closest point on line segment to point
     */
    glm::vec3 ClosestPointOnLineSegment(const glm::vec3& p,
                                       const glm::vec3& a, const glm::vec3& b);
    
    /**
     * Closest point in box to point
     */
    glm::vec3 ClosestPointInBox(const glm::vec3& point,
                               const glm::vec3& box_center, const glm::vec3& box_half_extents);
}

} // namespace schizo::physics
