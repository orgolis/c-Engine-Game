#include "constraints.h"
#include "rigidbody.h"
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <algorithm>

namespace schizo::physics {

// ============================================================================
// Constraint Base Class
// ============================================================================

Constraint::Constraint()
    : is_enabled_(true), error_(0.0f) {
}

Constraint::~Constraint() = default;

void Constraint::SetEnabled(bool enabled) {
    is_enabled_ = enabled;
}

bool Constraint::IsEnabled() const {
    return is_enabled_;
}

// ============================================================================
// DistanceConstraint Implementation
// ============================================================================

DistanceConstraint::DistanceConstraint(
    RigidBody* body_a,
    RigidBody* body_b,
    float distance,
    float stiffness)
    : Constraint(),
      body_a_(body_a),
      body_b_(body_b),
      distance_target_(distance),
      stiffness_(glm::clamp(stiffness, 0.0f, 1.0f)) {
    
    if (body_a && body_b) {
        glm::vec3 pos_a = body_a->GetWorldBoundsCenter();
        glm::vec3 pos_b = body_b->GetWorldBoundsCenter();
        local_offset_a_ = pos_a;
        local_offset_b_ = pos_b;
    }
}

void DistanceConstraint::Solve(float delta_time) {
    if (!body_a_ || !body_b_ || !IsEnabled()) return;
    
    glm::vec3 pos_a = body_a_->GetWorldBoundsCenter();
    glm::vec3 pos_b = body_b_->GetWorldBoundsCenter();
    
    glm::vec3 delta = pos_b - pos_a;
    float distance = glm::length(delta);
    error_ = distance - distance_target_;
    
    if (distance < 0.001f) return;
    
    glm::vec3 dir = delta / distance;
    glm::vec3 correction = dir * error_ * stiffness_;
    
    float mass_a = body_a_->GetMass();
    float mass_b = body_b_->GetMass();
    
    if (mass_a > 0) {
        body_a_->ApplyImpulse(correction * (mass_b / (mass_a + mass_b)));
    }
    if (mass_b > 0) {
        body_b_->ApplyImpulse(-correction * (mass_a / (mass_a + mass_b)));
    }
}

float DistanceConstraint::GetError() const {
    return error_;
}

void DistanceConstraint::SetTargetDistance(float distance) {
    distance_target_ = std::max(0.001f, distance);
}

void DistanceConstraint::SetStiffness(float stiffness) {
    stiffness_ = glm::clamp(stiffness, 0.0f, 1.0f);
}

float DistanceConstraint::GetCurrentDistance() const {
    if (!body_a_ || !body_b_) return 0;
    
    glm::vec3 pos_a = body_a_->GetWorldBoundsCenter();
    glm::vec3 pos_b = body_b_->GetWorldBoundsCenter();
    return glm::distance(pos_a, pos_b);
}

// ============================================================================
// FixedJoint Implementation
// ============================================================================

FixedJoint::FixedJoint(
    RigidBody* body_a,
    RigidBody* body_b,
    const glm::vec3& position_offset,
    const glm::quat& rotation_offset)
    : Constraint(),
      body_a_(body_a),
      body_b_(body_b),
      position_offset_(position_offset),
      rotation_offset_(rotation_offset),
      is_broken_(false),
      break_force_(1e8f) {
}

void FixedJoint::Solve(float delta_time) {
    if (!body_a_ || !body_b_ || !IsEnabled() || is_broken_) return;
    
    glm::vec3 pos_a = body_a_->GetWorldBoundsCenter();
    glm::vec3 pos_b = body_b_->GetWorldBoundsCenter();
    
    glm::vec3 desired_pos_b = pos_a + position_offset_;
    glm::vec3 pos_error = desired_pos_b - pos_b;
    float pos_error_mag = glm::length(pos_error);
    
    if (pos_error_mag > 0.001f) {
        float mass_a = body_a_->GetMass();
        float mass_b = body_b_->GetMass();
        
        if (mass_b > 0) {
            glm::vec3 correction = pos_error * (mass_a / (mass_a + mass_b)) * 0.5f;
            body_b_->ApplyImpulse(correction);
        }
    }
    
    if (break_force_ < 1e8f && pos_error_mag > break_force_) {
        is_broken_ = true;
    }
    
    error_ = pos_error_mag;
}

float FixedJoint::GetError() const {
    return error_;
}

void FixedJoint::SetBreakForce(float force) {
    break_force_ = std::max(0.0f, force);
}

bool FixedJoint::IsBroken() const {
    return is_broken_;
}

void FixedJoint::Reset() {
    is_broken_ = false;
    error_ = 0.0f;
}

// ============================================================================
// HingeJoint Implementation
// ============================================================================

HingeJoint::HingeJoint(
    RigidBody* body_a,
    RigidBody* body_b,
    const glm::vec3& position,
    const glm::vec3& axis)
    : Constraint(),
      body_a_(body_a),
      body_b_(body_b),
      hinge_position_(position),
      hinge_axis_(glm::normalize(axis)),
      min_angle_(0.0f),
      max_angle_(0.0f),
      limits_enabled_(false),
      motor_target_velocity_(0.0f),
      motor_force_(0.0f),
      motor_enabled_(false) {
}

void HingeJoint::Solve(float delta_time) {
    if (!body_a_ || !body_b_ || !IsEnabled()) return;
    
    // Position constraint: keep hinge position aligned
    glm::vec3 pos_a = body_a_->GetWorldBoundsCenter();
    glm::vec3 pos_b = body_b_->GetWorldBoundsCenter();
    
    glm::vec3 desired_pos_b = pos_a + (hinge_position_ - pos_a);
    glm::vec3 pos_error = desired_pos_b - pos_b;
    float pos_error_mag = glm::length(pos_error);
    
    if (pos_error_mag > 0.001f) {
        float mass_a = body_a_->GetMass();
        float mass_b = body_b_->GetMass();
        
        if (mass_b > 0) {
            glm::vec3 correction = pos_error * (mass_a / (mass_a + mass_b)) * 0.25f;
            body_b_->ApplyImpulse(correction);
        }
    }
    
    // Angular limits
    if (limits_enabled_) {
        glm::quat rot_a = body_a_->GetWorldRotation();
        glm::quat rot_b = body_b_->GetWorldRotation();
        
        // Rotate the hinge axis by each body's rotation
        glm::vec3 axis_a = rot_a * hinge_axis_;
        glm::vec3 axis_b = rot_b * hinge_axis_;
        
        float dot = glm::clamp(glm::dot(axis_a, axis_b), -1.0f, 1.0f);
        float angle = glm::degrees(glm::acos(dot));
        
        if (angle < min_angle_) {
            glm::vec3 torque = glm::cross(axis_b, axis_a) * 0.5f;
            body_a_->ApplyTorque(torque);
            body_b_->ApplyTorque(-torque);
        } else if (angle > max_angle_) {
            glm::vec3 torque = glm::cross(axis_a, axis_b) * 0.5f;
            body_a_->ApplyTorque(torque);
            body_b_->ApplyTorque(-torque);
        }
    }
    
    // Motor
    if (motor_enabled_) {
        glm::vec3 motor_torque = hinge_axis_ * (motor_target_velocity_ * motor_force_);
        body_a_->ApplyTorque(motor_torque);
        body_b_->ApplyTorque(-motor_torque);
    }
    
    error_ = glm::length(pos_error);
}

float HingeJoint::GetError() const {
    return error_;
}

void HingeJoint::SetAngularLimits(float min_angle, float max_angle) {
    min_angle_ = min_angle;
    max_angle_ = max_angle;
    limits_enabled_ = true;
}

void HingeJoint::DisableLimits() {
    limits_enabled_ = false;
}

void HingeJoint::SetMotor(float target_velocity, float max_force) {
    motor_target_velocity_ = target_velocity;
    motor_force_ = std::max(0.0f, max_force);
    motor_enabled_ = (max_force > 0);
}

void HingeJoint::DisableMotor() {
    motor_enabled_ = false;
    motor_force_ = 0;
}

// ============================================================================
// SpringConstraint Implementation
// ============================================================================

SpringConstraint::SpringConstraint(
    RigidBody* body_a,
    RigidBody* body_b,
    const glm::vec3& anchor_a,
    const glm::vec3& anchor_b,
    float rest_length,
    float stiffness)
    : Constraint(),
      body_a_(body_a),
      body_b_(body_b),
      anchor_a_(anchor_a),
      anchor_b_(anchor_b),
      rest_length_(rest_length > 0 ? rest_length : glm::distance(anchor_a, anchor_b)),
      stiffness_(stiffness),
      damping_(0.1f) {
}

SpringConstraint::SpringConstraint(
    RigidBody* body_a,
    RigidBody* body_b,
    float rest_length,
    float stiffness,
    float damping)
    : Constraint(),
      body_a_(body_a),
      body_b_(body_b),
      anchor_a_(body_a ? body_a->GetWorldBoundsCenter() : glm::vec3(0)),
      anchor_b_(body_b ? body_b->GetWorldBoundsCenter() : glm::vec3(0)),
      rest_length_(rest_length > 0 ? rest_length : 1.0f),
      stiffness_(stiffness),
      damping_(damping) {
}

void SpringConstraint::Solve(float delta_time) {
    if (!body_a_ || !body_b_ || !is_enabled_) return;
    
    glm::vec3 pos_a = body_a_->GetWorldBoundsCenter() + anchor_a_;
    glm::vec3 pos_b = body_b_->GetWorldBoundsCenter() + anchor_b_;
    
    glm::vec3 delta = pos_b - pos_a;
    float current_length = glm::length(delta);
    
    if (current_length < 1e-6f) return;
    
    glm::vec3 normal = delta / current_length;
    
    // Spring force: F = -k(x - L0)
    float displacement = current_length - rest_length_;
    float spring_force = -stiffness_ * displacement;
    
    // Damping force: F = -c * v
    glm::vec3 vel_a = body_a_->GetVelocity();
    glm::vec3 vel_b = body_b_->GetVelocity();
    glm::vec3 relative_vel = vel_b - vel_a;
    float vel_along_normal = glm::dot(relative_vel, normal);
    float damping_force = -damping_ * vel_along_normal;
    
    // Total force along constraint
    float total_force = spring_force + damping_force;
    
    // Convert to impulse
    float inv_mass_a = body_a_->GetInverseMass();
    float inv_mass_b = body_b_->GetInverseMass();
    float inv_mass_sum = inv_mass_a + inv_mass_b;
    
    if (inv_mass_sum < 1e-6f) return; // Both static
    
    float impulse = total_force / inv_mass_sum;
    
    // Apply impulse
    glm::vec3 impulse_vec = normal * impulse;
    body_a_->ApplyImpulse(impulse_vec);
    body_b_->ApplyImpulse(-impulse_vec);
}

float SpringConstraint::GetError() const {
    if (!body_a_ || !body_b_) return 0;
    
    glm::vec3 pos_a = body_a_->GetWorldBoundsCenter();
    glm::vec3 pos_b = body_b_->GetWorldBoundsCenter();
    
    float current_length = glm::distance(pos_a, pos_b);
    return current_length - rest_length_;
}

void SpringConstraint::SetRestLength(float length) {
    rest_length_ = std::max(0.001f, length);
}

void SpringConstraint::SetStiffness(float stiffness) {
    stiffness_ = std::max(0.0f, stiffness);
}

void SpringConstraint::SetDamping(float damping) {
    damping_ = std::clamp(damping, 0.0f, 1.0f);
}

float SpringConstraint::GetCurrentLength() const {
    if (!body_a_ || !body_b_) return 0;
    
    glm::vec3 pos_a = body_a_->GetWorldBoundsCenter();
    glm::vec3 pos_b = body_b_->GetWorldBoundsCenter();
    
    return glm::distance(pos_a, pos_b);
}

} // namespace schizo::physics
