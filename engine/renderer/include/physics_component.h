#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

// Forward declarations
namespace schizo::physics {
    class RigidBody;
    class CollisionShape;
    class PhysicsWorld;
}

namespace schizo::renderer {

/**
 * @class PhysicsComponent
 * @brief Scene-level physics component that bridges entity transform system with physics world
 * 
 * Manages synchronization between:
 * - Entity world position/rotation
 * - RigidBody physics state
 * - PhysicsWorld simulation
 */
class PhysicsComponent {
public:
    /**
     * Create physics component with given world position and body type
     */
    PhysicsComponent(const glm::vec3& position = glm::vec3(0),
                     const glm::quat& rotation = glm::quat(1, 0, 0, 0));
    
    ~PhysicsComponent();
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    /**
     * Connect to physics world (called when added to scene)
     */
    void OnAttach(schizo::physics::PhysicsWorld* world);
    
    /**
     * Disconnect from physics world (called when removed from scene)
     */
    void OnDetach();
    
    /**
     * Update called before physics simulation
     */
    void OnPreUpdate(float delta_time);
    
    /**
     * Update called after physics simulation - sync results back to entity
     */
    void OnPostUpdate(float delta_time);
    
    /**
     * Enable/disable physics simulation for this body
     */
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    
    // ========================================================================
    // Transform Access
    // ========================================================================
    
    /**
     * Get world position (from physics body)
     */
    glm::vec3 GetPosition() const;
    
    /**
     * Set world position (updates physics body)
     */
    void SetPosition(const glm::vec3& position);
    
    /**
     * Get world rotation (from physics body)
     */
    glm::quat GetRotation() const;
    
    /**
     * Set world rotation (updates physics body)
     */
    void SetRotation(const glm::quat& rotation);
    
    /**
     * Get world transform matrix
     */
    glm::mat4 GetTransform() const;
    
    // ========================================================================
    // Physics Properties
    // ========================================================================
    
    /**
     * Get underlying physics body
     */
    schizo::physics::RigidBody* GetRigidBody() const;
    
    /**
     * Set mass (calls RigidBody::SetMass)
     */
    void SetMass(float mass);
    float GetMass() const;
    
    /**
     * Set collision shape
     */
    void SetCollisionShape(std::unique_ptr<schizo::physics::CollisionShape> shape);
    
    /**
     * Apply force to body center
     */
    void ApplyForce(const glm::vec3& force);
    
    /**
     * Apply force at world point
     */
    void ApplyForceAtPoint(const glm::vec3& force, const glm::vec3& world_point);
    
    /**
     * Apply impulse to body center
     */
    void ApplyImpulse(const glm::vec3& impulse);
    
    /**
     * Apply implicit at world point
     */
    void ApplyImpulseAtPoint(const glm::vec3& impulse, const glm::vec3& world_point);
    
    /**
     * Apply torque
     */
    void ApplyTorque(const glm::vec3& torque);
    
    /**
     * Get velocity
     */
    glm::vec3 GetVelocity() const;
    
    /**
     * Set velocity
     */
    void SetVelocity(const glm::vec3& velocity);
    
    /**
     * Get angular velocity
     */
    glm::vec3 GetAngularVelocity() const;
    
    /**
     * Set angular velocity
     */
    void SetAngularVelocity(const glm::vec3& angular_velocity);
    
    /**
     * Set physics friction (0-1)
     */
    void SetFriction(float friction);
    float GetFriction() const;
    
    /**
     * Set restitution/bounciness (0-1)
     */
    void SetRestitution(float restitution);
    float GetRestitution() const;
    
    /**
     * Set linear damping (0-1)
     */
    void SetLinearDamping(float damping);
    float GetLinearDamping() const;
    
    /**
     * Set angular damping (0-1)
     */
    void SetAngularDamping(float damping);
    float GetAngularDamping() const;
    
    /**
     * Enable/disable gravity
     */
    void SetUseGravity(bool use_gravity);
    bool GetUseGravity() const;
    
    /**
     * Set gravity scale (multiplier on world gravity)
     */
    void SetGravityScale(float scale);
    float GetGravityScale() const;
    
    /**
     * Freeze linear movement on specific axes
     */
    void SetLinearConstraint(bool x, bool y, bool z);
    
    /**
     * Freeze rotation on specific axes
     */
    void SetRotationConstraint(bool x, bool y, bool z);
    
    /**
     * Put body to sleep (optimization)
     */
    void Sleep();
    
    /**
     * Wake body from sleep
     */
    void Wake();
    
    /**
     * Check if body is sleeping
     */
    bool IsSleeping() const;

private:
    std::unique_ptr<schizo::physics::RigidBody> rigidbody_;
    schizo::physics::PhysicsWorld* physics_world_;
    
    // Cached position/rotation for change detection
    glm::vec3 last_position_;
    glm::quat last_rotation_;
    
    bool enabled_;
    
    /**
     * Sync entity transform to physics body
     */
    void SyncToPhysics();
    
    /**
     * Sync physics body results back to entity transform
     */
    void SyncFromPhysics();
};

} // namespace schizo::renderer
