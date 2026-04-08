#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace schizo::physics {

// Forward declarations
class RigidBody;

// ============================================================================
// Physics Constraints & Joints
// ============================================================================

/**
 * @class Constraint
 * @brief Base class for physics constraints
 */
class Constraint {
public:
    virtual ~Constraint();
    
    /**
     * Solve the constraint
     * @param delta_time Timestep
     */
    virtual void Solve(float delta_time) = 0;
    
    /**
     * Get constraint error for stability feedback
     */
    virtual float GetError() const = 0;
    
    /**
     * Enable/disable constraint
     */
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

protected:
    Constraint();
    bool is_enabled_ = true;
    float error_ = 0.0f;
};

// ============================================================================
// Distance Constraint
// ============================================================================

/**
 * @class DistanceConstraint
 * @brief Keeps two bodies at a fixed distance (rope, cable)
 */
class DistanceConstraint : public Constraint {
public:
    /**
     * Create distance constraint
     * @param body_a First body
     * @param body_b Second body
     * @param distance Desired distance between bodies
     * @param stiffness Constraint stiffness (0-1, higher = stiffer)
     */
    DistanceConstraint(RigidBody* body_a, RigidBody* body_b,
                      float distance, float stiffness = 1.0f);
    
    void Solve(float delta_time) override;
    float GetError() const override;
    
    void SetTargetDistance(float distance);
    void SetStiffness(float stiffness);
    float GetCurrentDistance() const;

private:
    RigidBody* body_a_ = nullptr;
    RigidBody* body_b_ = nullptr;
    float distance_target_ = 0.0f;
    float stiffness_ = 1.0f;
    glm::vec3 local_offset_a_ = glm::vec3(0);
    glm::vec3 local_offset_b_ = glm::vec3(0);
    float error_ = 0.0f;
};

// ============================================================================
// Fixed Joint
// ============================================================================

/**
 * @class FixedJoint
 * @brief Rigidly connects two bodies at a relative transform
 * 
 * Bodies are locked together with fixed position and rotation.
 */
class FixedJoint : public Constraint {
public:
    /**
     * Create fixed joint
     * @param body_a First body
     * @param body_b Second body (can be nullptr for world-space)
     * @param position_offset Relative position offset
     * @param rotation_offset Relative rotation (as quaternion)
     */
    FixedJoint(RigidBody* body_a, RigidBody* body_b,
              const glm::vec3& position_offset = glm::vec3(0),
              const glm::quat& rotation_offset = glm::quat(1, 0, 0, 0));
    
    void Solve(float delta_time) override;
    float GetError() const override;
    
    void SetBreakForce(float force);
    bool IsBroken() const;
    void Reset();

private:
    RigidBody* body_a_ = nullptr;
    RigidBody* body_b_ = nullptr;
    glm::vec3 position_offset_ = glm::vec3(0);
    glm::quat rotation_offset_ = glm::quat(1, 0, 0, 0);
    float error_ = 0.0f;
    float break_force_ = 1e10f;  // Never breaks by default
    bool is_broken_ = false;
};

// ============================================================================
// Hinge Joint (1 Degree of Freedom)
// ============================================================================

/**
 * @class HingeJoint
 * @brief Connects two bodies with rotation around one axis (door hinge)
 */
class HingeJoint : public Constraint {
public:
    /**
     * Create hinge joint
     * @param body_a First body
     * @param body_b Second body
     * @param position Joint position
     * @param axis Rotation axis (world space)
     */
    HingeJoint(RigidBody* body_a, RigidBody* body_b,
              const glm::vec3& position, const glm::vec3& axis);
    
    void Solve(float delta_time) override;
    float GetError() const override;
    
    /**
     * Set angle limits
     * @param min_angle Minimum rotation (radians)
     * @param max_angle Maximum rotation (radians)
     */
    void SetAngularLimits(float min_angle, float max_angle);
    
    /**
     * Disable angle limits
     */
    void DisableLimits();
    
    /**
     * Set motorized (apply torque)
     */
    void SetMotor(float target_velocity, float max_force);
    
    /**
     * Disable motor
     */
    void DisableMotor();

private:
    RigidBody* body_a_ = nullptr;
    RigidBody* body_b_ = nullptr;
    glm::vec3 hinge_position_ = glm::vec3(0);
    glm::vec3 hinge_axis_ = glm::vec3(0, 1, 0);
    
    bool limits_enabled_ = false;
    float min_angle_ = 0.0f;
    float max_angle_ = 0.0f;
    
    bool motor_enabled_ = false;
    float motor_target_velocity_ = 0.0f;
    float motor_force_ = 0.0f;
};

// ============================================================================
// Spring Constraint
// ============================================================================

/**
 * @class SpringConstraint
 * @brief Spring-damper between two bodies
 */
class SpringConstraint : public Constraint {
public:
    /**
     * Create spring constraint with anchors
     * @param body_a First body
     * @param body_b Second body
     * @param anchor_a Anchor point on body_a
     * @param anchor_b Anchor point on body_b
     * @param rest_length Natural spring length
     * @param stiffness Spring constant (higher = stiffer)
     */
    SpringConstraint(RigidBody* body_a, RigidBody* body_b,
                    const glm::vec3& anchor_a, const glm::vec3& anchor_b,
                    float rest_length, float stiffness = 100.0f);
    
    /**
     * Create spring constraint
     * @param body_a First body
     * @param body_b Second body
     * @param rest_length Natural spring length
     * @param stiffness Spring constant (higher = stiffer)
     * @param damping Damping coefficient (higher = more damping)
     */
    SpringConstraint(RigidBody* body_a, RigidBody* body_b,
                    float rest_length, float stiffness = 100.0f,
                    float damping = 1.0f);
    
    void Solve(float delta_time) override;
    float GetError() const override;
    
    void SetRestLength(float length);
    void SetStiffness(float stiffness);
    void SetDamping(float damping);
    float GetCurrentLength() const;

private:
    RigidBody* body_a_ = nullptr;
    RigidBody* body_b_ = nullptr;
    glm::vec3 anchor_a_ = glm::vec3(0);
    glm::vec3 anchor_b_ = glm::vec3(0);
    float rest_length_ = 0.0f;
    float stiffness_ = 100.0f;
    float damping_ = 1.0f;
};

} // namespace schizo::physics
