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
     * Test sphere vs box collision
     */
    bool SphereBox(const glm::vec3& sphere_pos, float sphere_radius,
                  const glm::vec3& box_pos, const glm::vec3& box_half_extents,
                  Contact& contact);
    
    /**
     * Test box vs box collision (AABB)
     */
    bool BoxBox(const glm::vec3& pos_a, const glm::vec3& extents_a,
               const glm::vec3& pos_b, const glm::vec3& extents_b,
               Contact& contact);
    
    /**
     * Test sphere vs plane collision
     */
    bool SpherePlane(const glm::vec3& sphere_pos, float sphere_radius,
                    const glm::vec3& plane_pos, const glm::vec3& plane_normal,
                    Contact& contact);
    
    /**
     * Test capsule vs sphere collision
     */
    bool CapsuleSphere(const glm::vec3& capsule_pos, float capsule_radius, float capsule_height,
                      const glm::vec3& sphere_pos, float sphere_radius,
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
