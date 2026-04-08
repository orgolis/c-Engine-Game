#include "constraints.h"
#include "rigidbody.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace schizo::physics {

// ============================================================================
// DistanceConstraint Implementation
// ============================================================================

DistanceConstraint::DistanceConstraint(
    RigidBody* body_a,
    RigidBody* body_b,
    float distance,
    float stiffness)
    : body_a_(body_a),
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
    float total_mass = mass_a + mass_b;
    
    if (total_mass > 0) {
        if (mass_a > 0) {
            body_a_->ApplyImpulse(correction * (mass_b / total_mass));
        }
        if (mass_b > 0) {
            body_b_->ApplyImpulse(-correction * (mass_a / total_mass));
        }
    }
}

float DistanceConstraint::GetError() const {
    return error_;
}

// ============================================================================
// FixedJoint Implementation
// ============================================================================

FixedJoint::FixedJoint(
    RigidBody* body_a,
    RigidBody* body_b,
    const glm::vec3& position_offset,
    const glm::quat& rotation_offset)
    : body_a_(body_a),
      body_b_(body_b),
      position_offset_(position_offset),
      rotation_offset_(rotation_offset) {
}

void FixedJoint::Solve(float delta_time) {
    if (!body_a_ || !body_b_ || !IsEnabled() || is_broken_) return;
    
    glm::vec3 pos_a = body_a_->GetWorldBoundsCenter();
    glm::vec3 pos_b = body_b_->GetWorldBoundsCenter();
    
    glm::vec3 desired_pos_b = pos_a + position_offset_;
    glm::vec3 pos_error = desired_pos_b - pos_b;
    error_ = glm::length(pos_error);
    
    if (error_ > 0.001f) {
        float mass_a = body_a_->GetMass();
        float mass_b = body_b_->GetMass();
        
        if (mass_b > 0) {
            glm::vec3 correction = pos_error * (mass_a / (mass_a + mass_b)) * 0.5f;
            body_b_->ApplyImpulse(correction);
        }
    }
    
    // Check for break force
    if (break_force_ < 1e8f && error_ > break_force_) {
        is_broken_ = true;
    }
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

// ============================================================================
// HingeJoint Implementation
// ============================================================================

HingeJoint::HingeJoint(
    RigidBody* body_a,
    RigidBody* body_b,
    const glm::vec3& position,
    const glm::vec3& axis)
    : body_a_(body_a),
      body_b_(body_b) {
    // Hinge joint implementation placeholder
}

void HingeJoint::Solve(float delta_time) {
    if (!body_a_ || !body_b_ || !IsEnabled()) return;
    // Simplified hinge constraint
}

float HingeJoint::GetError() const {
    return 0.0f;
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
    : body_a_(body_a),
      body_b_(body_b) {
    // Spring constraint implementation placeholder
}

void SpringConstraint::Solve(float delta_time) {
    if (!body_a_ || !body_b_ || !IsEnabled()) return;
    // Spring constraint solver
}

float SpringConstraint::GetError() const {
    return 0.0f;
}

} // namespace schizo::physics
