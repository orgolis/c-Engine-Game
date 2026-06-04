#pragma once

#include "entity.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace schizo::scene {

/**
 * @enum ColliderShape
 * @brief Authoring-time shape for the collider. Phase 2 maps each to a
 * physics::CollisionShape when the scene starts playing.
 */
enum class ColliderShape : uint8_t {
    Box = 0,
    Sphere = 1,
    Plane = 2,
    Capsule = 3,
    Cylinder = 4,
    Mesh = 5,        // triangles loaded from the entity's MeshComponent.mesh_path
    MAX_SHAPES
};

/**
 * @class ColliderComponent
 * @brief Authoring data for collision. Holds shape, dimensions, local offset,
 * and the static/dynamic + mass hints used to build a physics::RigidBody at
 * play time.
 *
 * Phase 1 scope: data layer only — inspector edits it, serializer round-trips
 * it. No physics simulation reads it yet. Phase 2 will walk entities with this
 * component on StartPlayback and create matching RigidBody + CollisionShape
 * instances inside the PhysicsWorld.
 */
class ColliderComponent : public Component {
public:
    explicit ColliderComponent(ColliderShape shape = ColliderShape::Box)
        : shape_(shape) {}
    virtual ~ColliderComponent() = default;

    ColliderComponent(const ColliderComponent&) = delete;
    ColliderComponent& operator=(const ColliderComponent&) = delete;
    ColliderComponent(ColliderComponent&&) = default;
    ColliderComponent& operator=(ColliderComponent&&) = default;

    // ---- Shape selection -------------------------------------------------

    ColliderShape GetShape() const { return shape_; }
    void SetShape(ColliderShape shape) { shape_ = shape; }

    // ---- Box dimensions --------------------------------------------------
    // half_extents on each axis. Final world size is 2*half_extents scaled by
    // the entity's transform when the rigid body is built.

    const glm::vec3& GetHalfExtents() const { return half_extents_; }
    void SetHalfExtents(const glm::vec3& he) {
        half_extents_ = glm::max(he, glm::vec3(0.01f));
    }

    // ---- Sphere / Capsule dimensions ------------------------------------

    float GetRadius() const { return radius_; }
    void SetRadius(float r) { radius_ = std::max(0.01f, r); }

    float GetHeight() const { return height_; }   // Capsule cylindrical span
    void SetHeight(float h) { height_ = std::max(0.0f, h); }

    // ---- Plane normal ----------------------------------------------------

    const glm::vec3& GetPlaneNormal() const { return plane_normal_; }
    void SetPlaneNormal(const glm::vec3& n) {
        // Avoid normalising a zero vector — fall back to world up.
        float len2 = glm::dot(n, n);
        plane_normal_ = (len2 > 1e-8f) ? (n / std::sqrt(len2)) : glm::vec3(0, 1, 0);
    }

    // ---- Local offset from the entity's origin --------------------------

    const glm::vec3& GetOffset() const { return offset_; }
    void SetOffset(const glm::vec3& o) { offset_ = o; }

    // ---- Physics body type (consumed by Phase 2 only) -------------------
    // is_dynamic_ == false  → physics::BodyType::Static (default for level
    // geometry). True → Dynamic. The player is the only case where Phase 3
    // overrides this to Kinematic.

    bool IsDynamic() const { return is_dynamic_; }
    void SetDynamic(bool dynamic) { is_dynamic_ = dynamic; }

    float GetMass() const { return mass_; }
    void SetMass(float mass) { mass_ = std::max(0.001f, mass); }

    // ---- Trigger (overlap-without-resolve) ------------------------------
    // When true, the body still participates in collision detection (so the
    // engine records contacts that gameplay code can read), but the world's
    // ResolveCollision step is skipped — bodies pass through.
    bool IsTrigger() const { return is_trigger_; }
    void SetTrigger(bool t) { is_trigger_ = t; }

    // ---- Collision layers / masks --------------------------------------
    // Each body sits on a single layer (0..31). The mask is a bitfield of
    // OTHER layers this body is willing to collide with. A pair is tested
    // only if (1 << A.layer) & B.mask AND (1 << B.layer) & A.mask. Default
    // is layer 0 with a mask of "all on" so unconfigured bodies collide
    // with everything.
    uint8_t  GetLayer() const { return layer_; }
    void     SetLayer(uint8_t layer) { layer_ = (layer & 31u); }

    uint32_t GetMask() const { return mask_; }
    void     SetMask(uint32_t mask) { mask_ = mask; }

private:
    ColliderShape shape_ = ColliderShape::Box;

    // Geometry. Only the fields relevant to the current shape are read by
    // Phase 2 — keeping them all in one struct lets the inspector toggle
    // shape without losing previously-entered values.
    glm::vec3 half_extents_  = glm::vec3(0.5f);
    float     radius_        = 0.5f;
    float     height_        = 1.0f;
    glm::vec3 plane_normal_  = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 offset_        = glm::vec3(0.0f);

    // Physics hints. Default Static so level meshes act as colliders without
    // anyone having to remember to flip a checkbox.
    bool  is_dynamic_ = false;
    float mass_       = 1.0f;

    // Trigger + filtering. Defaults preserve "collides with everything,
    // resolves collisions" — i.e., behaviour before #7 and #8 were added.
    bool     is_trigger_ = false;
    uint8_t  layer_      = 0;
    uint32_t mask_       = 0xFFFFFFFFu;
};

} // namespace schizo::scene
