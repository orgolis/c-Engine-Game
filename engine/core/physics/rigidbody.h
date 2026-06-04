#pragma once

#include "collision.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <cstdint>
#include <set>

// Forward declare scene types to avoid circular dependency
namespace schizo::scene {
    class Entity;
}

namespace schizo::physics {

// Forward declarations
class PhysicsWorld;
class CollisionShape;

// ============================================================================
// Rigid Body Types
// ============================================================================

enum class BodyType : uint8_t {
    Static,        // Never moves, infinite mass
    Dynamic,       // Affected by forces and collisions
    Kinematic,     // Moves but not affected by physics (script-controlled)
};

// ============================================================================
// Rigid Body Physics Component
// ============================================================================

/**
 * @class RigidBody
 * @brief Physics component for simulating physical objects
 * 
 * Represents a physical body in the world with mass, velocity, and forces.
 * Responds to gravity and collisions based on body type.
 * 
 * Note: This class is independent of scene::Component. Integration with
 * the scene system is done through a wrapper or adapter class.
 * 
 * Usage:
 * auto rb = std::make_unique<RigidBody>();
 * rb->SetMass(2.0f);
 * rb->SetCollisionShape(std::make_shared<BoxShape>(glm::vec3(1, 1, 1)));
 */
class RigidBody {
public:
    RigidBody();
    ~RigidBody();
    
    // ========== Lifecycle Callbacks ==========
    
    void OnAttach();
    void OnDetach();
    void OnEnable();
    void OnDisable();
    void OnFixedUpdate(float delta_time);
    
    // ========== Entity Association ==========
    
    /**
     * Set the owning entity (called when attached to scene entity)
     */
    void SetEntity(schizo::scene::Entity* entity) { entity_ = entity; }
    
    /**
     * Get the owning entity
     */
    schizo::scene::Entity* GetEntity() const { return entity_; }
    
    // ========== Body Configuration ==========
    
    /**
     * Set body type (static, dynamic, kinematic)
     */
    void SetBodyType(BodyType type);
    BodyType GetBodyType() const { return body_type_; }
    
    /**
     * Set mass in kilograms
     * @param mass Mass (>0 for dynamic, 0 = infinite/static)
     */
    void SetMass(float mass);
    float GetMass() const;
    float GetInverseMass() const;
    
    /**
     * Set collision shape
     */
    void SetCollisionShape(std::unique_ptr<CollisionShape> shape);
    
    CollisionShape* GetCollisionShape() const { return collision_shape_.get(); }
    
    // ========== Position & Rotation (World Space) ==========
    
    /**
     * Set world position
     */
    void SetWorldPosition(const glm::vec3& position) { world_position_ = position; }
    glm::vec3 GetWorldPosition() const { return world_position_; }
    
    /**
     * Set world rotation
     */
    void SetWorldRotation(const glm::quat& rotation) { world_rotation_ = rotation; }
    glm::quat GetWorldRotation() const { return world_rotation_; }
    
    // ========== Physics State ==========
    
    /**
     * Set linear velocity
     */
    void SetVelocity(const glm::vec3& velocity);
    glm::vec3 GetVelocity() const;
    
    /**
     * Set angular velocity (rotation speed)
     */
    void SetAngularVelocity(const glm::vec3& angular_vel);
    glm::vec3 GetAngularVelocity() const;
    
    /**
     * Apply force at center of mass
     * @param force Force vector (Newtons)
     */
    void ApplyForce(const glm::vec3& force) {
        force_ += force;
    }
    
    /**
     * Apply force at point on body
     * @param force Force vector
     * @param point Position in world space
     */
    void ApplyForceAtPoint(const glm::vec3& force, const glm::vec3& point);
    
    /**
     * Apply impulse (instantaneous force)
     * @param impulse Impulse vector
     */
    void ApplyImpulse(const glm::vec3& impulse) {
        if (body_type_ == BodyType::Dynamic) {
            velocity_ += impulse * inverse_mass_;
        }
    }
    
    /**
     * Apply torque (rotational force)
     * @param torque Torque vector
     */
    void ApplyTorque(const glm::vec3& torque) {
        torque_ += torque;
    }
    
    /**
     * Clear all forces
     */
    void ClearForces();
    
    /**
     * Get current force
     */
    glm::vec3 GetForce() const;
    
    /**
     * Get current torque
     */
    glm::vec3 GetTorque() const;
    
    // ========== Physics Parameters ==========
    
    /**
     * Set linear damping (friction-like effect, 0-1)
     */
    void SetLinearDamping(float damping) { linear_damping_ = glm::clamp(damping, 0.0f, 1.0f); }
    float GetLinearDamping() const { return linear_damping_; }
    
    /**
     * Set angular damping (rotational friction, 0-1)
     */
    void SetAngularDamping(float damping) { angular_damping_ = glm::clamp(damping, 0.0f, 1.0f); }
    float GetAngularDamping() const { return angular_damping_; }
    
    /**
     * Set restitution (bounciness, 0-1)
     */
    void SetRestitution(float r) { restitution_ = glm::clamp(r, 0.0f, 1.0f); }
    float GetRestitution() const { return restitution_; }
    
    /**
     * Set friction
     */
    void SetFriction(float f) { friction_ = glm::max(f, 0.0f); }
    float GetFriction() const { return friction_; }
    
    /**
     * Enable/disable gravity
     */
    void SetGravityScale(float scale) { gravity_scale_ = scale; }
    float GetGravityScale() const { return gravity_scale_; }

    // Trigger: still collected by the narrow phase (contact is recorded for
    // game code to read), but skipped by ResolveCollision so bodies overlap
    // without being pushed apart.
    bool IsTrigger() const { return is_trigger_; }
    void SetTrigger(bool t) { is_trigger_ = t; }

    // Layer this body sits on (0..31). Default 0.
    uint8_t GetLayer() const { return layer_; }
    void    SetLayer(uint8_t layer) { layer_ = static_cast<uint8_t>(layer & 31u); }

    // Bitmask of layers this body is willing to collide with. Default all-on.
    uint32_t GetCollisionMask() const { return collision_mask_; }
    void     SetCollisionMask(uint32_t mask) { collision_mask_ = mask; }
    
    // ========== Constraints ==========
    
    /**
     * Constrain velocity on specific axes (for 2D, etc.)
     */
    void SetVelocityConstraints(bool x, bool y, bool z) {
        constraint_velocity_ = glm::bvec3(x, y, z);
    }
    
    glm::bvec3 GetVelocityConstraints() const { return constraint_velocity_; }
    
    /**
     * Freeze rotation on specific axes
     */
    void SetRotationConstraints(bool x, bool y, bool z) {
        constraint_rotation_ = glm::bvec3(x, y, z);
    }
    
    glm::bvec3 GetRotationConstraints() const { return constraint_rotation_; }
    
    // ========== Sleeping ==========
    
    /**
     * Put body to sleep (no simulation until woken)
     */
    void Sleep();
    
    /**
     * Wake body up
     */
    void Wake();
    
    bool IsSleeping() const;
    
    /**
     * Set sleep threshold (velocity below this causes sleep)
     */
    void SetSleepThreshold(float threshold);
    
    // ========== Collision Queries ==========
    
    /**
     * Check if this body is colliding
     */
    bool IsCollidingWith(RigidBody* other) const;
    
    /**
     * Get all active collisions
     */
    const std::vector<CollisionInfo>& GetCollisions() const { return collisions_; }
    
    /**
     * Get world bounding sphere center
     */
    glm::vec3 GetWorldBoundsCenter() const;
    
    /**
     * Get bounding sphere radius
     */
    float GetBoundsRadius() const;
    
    /**
     * Get world bounds AABB
     */
    void GetWorldBounds(glm::vec3& min, glm::vec3& max) const;
    
    // ========== Internal ==========
    
    /**
     * Sync with entity transform (called after physics update)
     */
    void SyncTransform();
    
    /**
     * Update collisions list
     */
    void UpdateCollisions(const std::vector<CollisionInfo>& collisions);
    
    /**
     * Get physics world this body belongs to
     */
    PhysicsWorld* GetPhysicsWorld() const { return physics_world_; }
    
    /**
     * Set physics world (internal use)
     */
    void SetPhysicsWorld(PhysicsWorld* world) { physics_world_ = world; }

private:
    // Body properties
    BodyType body_type_ = BodyType::Dynamic;
    float mass_ = 1.0f;
    float inverse_mass_ = 1.0f;
    std::unique_ptr<CollisionShape> collision_shape_;
    
    // Transform state
    glm::vec3 world_position_ = glm::vec3(0);
    glm::quat world_rotation_ = glm::quat(1, 0, 0, 0);
    
    // Physics state
    glm::vec3 velocity_ = glm::vec3(0);
    glm::vec3 angular_velocity_ = glm::vec3(0);
    
    // Forces and torques
    glm::vec3 force_ = glm::vec3(0);
    glm::vec3 torque_ = glm::vec3(0);
    
    // Physics parameters
    float linear_damping_ = 0.05f;      // Linear friction
    float angular_damping_ = 0.05f;     // Rotational friction
    float restitution_ = 0.3f;          // Bounciness
    float friction_ = 0.5f;             // Surface friction
    float gravity_scale_ = 1.0f;        // Gravity multiplier
    
    // Constraints
    glm::bvec3 constraint_velocity_ = glm::bvec3(false);  // Don't constrain by default
    glm::bvec3 constraint_rotation_ = glm::bvec3(false);
    
    // Sleeping
    bool is_sleeping_ = false;
    float sleep_timer_ = 0.0f;
    float sleep_threshold_ = 0.1f;

    // Trigger + layer filtering. Defaults: not a trigger, on layer 0,
    // collides with every other layer.
    bool     is_trigger_     = false;
    uint8_t  layer_          = 0;
    uint32_t collision_mask_ = 0xFFFFFFFFu;
    
    // Collision tracking
    std::vector<CollisionInfo> collisions_;
    
    // Entity association (optional, for scene integration)
    schizo::scene::Entity* entity_ = nullptr;
    
    // Reference to physics world
    PhysicsWorld* physics_world_ = nullptr;
    
    /**
     * Integrate position and velocity
     */
    void IntegrateVelocity(float delta_time);
    
    /**
     * Integrate rotation from angular velocity
     */
    void IntegrateRotation(float delta_time);
    
    /**
     * Apply damping to velocity and angular velocity
     */
    void ApplyDamping(float delta_time);
    
    /**
     * Check for sleep conditions
     */
    void UpdateSleepState(float delta_time);
};

} // namespace schizo::physics
