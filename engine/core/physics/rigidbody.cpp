#include "rigidbody.h"
#include "collision.h"
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace schizo::physics {

// ============================================================================
// RigidBody Implementation
// ============================================================================

RigidBody::RigidBody()
    : body_type_(BodyType::Dynamic),
      mass_(1.0f),
      inverse_mass_(1.0f),
      velocity_(0),
      angular_velocity_(0),
      force_(0),
      torque_(0),
      linear_damping_(0.05f),
      angular_damping_(0.05f),
      restitution_(0.3f),
      friction_(0.5f),
      gravity_scale_(1.0f),
      is_sleeping_(false),
      sleep_timer_(0),
      sleep_threshold_(0.1f),
      physics_world_(nullptr) {
}

RigidBody::~RigidBody() {
}

// Initialization and lifecycle
void RigidBody::OnAttach() {
    // entity_ is automatically set by Entity class
    // collision_shape is initialized separately
}

void RigidBody::OnDetach() {
    collision_shape_ = nullptr;
}

void RigidBody::OnEnable() {
    is_sleeping_ = false;
}

void RigidBody::OnDisable() {
    ClearForces();
}

void RigidBody::OnFixedUpdate(float delta_time) {
    // Static bodies don't simulate
    if (body_type_ == BodyType::Static) return;
    
    // Update velocity and position
    IntegrateVelocity(delta_time);
    
    // Update rotation
    IntegrateRotation(delta_time);
    
    // Apply damping
    ApplyDamping(delta_time);
    
    // Check sleep conditions
    UpdateSleepState(delta_time);
}

// Physics configuration
void RigidBody::SetBodyType(BodyType type) {
    body_type_ = type;
    
    if (type == BodyType::Static) {
        velocity_ = glm::vec3(0);
        angular_velocity_ = glm::vec3(0);
        mass_ = 1e10f; // Effectively infinite
    } else if (type == BodyType::Kinematic) {
        // Kinematic bodies have mass but don't respond to forces
    }
}

void RigidBody::SetMass(float mass) {
    if (body_type_ == BodyType::Static) return; // Static bodies have infinite mass
    
    mass_ = std::max(0.01f, mass); // Minimum mass to avoid division issues
    inverse_mass_ = 1.0f / mass_;
}

void RigidBody::SetCollisionShape(std::unique_ptr<CollisionShape> shape) {
    collision_shape_ = std::move(shape);
}

float RigidBody::GetMass() const {
    return (body_type_ == BodyType::Static) ? 1e10f : mass_;
}

float RigidBody::GetInverseMass() const {
    if (body_type_ == BodyType::Static) return 0;
    return 1.0f / mass_;
}

// Velocity and rotation control
void RigidBody::SetVelocity(const glm::vec3& vel) {
    if (body_type_ == BodyType::Static) return;
    velocity_ = vel;
    is_sleeping_ = false;
}

glm::vec3 RigidBody::GetVelocity() const {
    return velocity_;
}

void RigidBody::SetAngularVelocity(const glm::vec3& ang_vel) {
    if (body_type_ == BodyType::Static) return;
    angular_velocity_ = ang_vel;
    is_sleeping_ = false;
}

glm::vec3 RigidBody::GetAngularVelocity() const {
    return angular_velocity_;
}

// Force application
void RigidBody::ApplyForceAtPoint(const glm::vec3& force, const glm::vec3& world_point) {
    if (body_type_ == BodyType::Static) return;
    
    ApplyForce(force);
    
    // Calculate torque from off-center force
    glm::vec3 r = world_point - world_position_;
    ApplyTorque(glm::cross(r, force));
}

void RigidBody::ClearForces() {
    force_ = glm::vec3(0);
    torque_ = glm::vec3(0);
}

glm::vec3 RigidBody::GetForce() const {
    return force_;
}

glm::vec3 RigidBody::GetTorque() const {
    return torque_;
}

// Constraint control
// These are handled inline in the header

// Sleeping system
void RigidBody::Sleep() {
    if (is_sleeping_) return;
    
    is_sleeping_ = true;
    velocity_ = glm::vec3(0);
    angular_velocity_ = glm::vec3(0);
    sleep_timer_ = 0;
}

void RigidBody::Wake() {
    is_sleeping_ = false;
    sleep_timer_ = 0;
}

bool RigidBody::IsSleeping() const {
    return is_sleeping_;
}

void RigidBody::SetSleepThreshold(float threshold) {
    sleep_threshold_ = threshold;
}

// Collision queries
bool RigidBody::IsCollidingWith(RigidBody* other) const {
    if (!other) return false;
    for (const auto& collision : collisions_) {
        if (collision.body_a == other || collision.body_b == other) {
            return true;
        }
    }
    return false;
}

// Bounds queries
glm::vec3 RigidBody::GetWorldBoundsCenter() const {
    if (!collision_shape_) return world_position_;
    
    return world_position_ + (world_rotation_ * collision_shape_->GetLocalOffset());
}

float RigidBody::GetBoundsRadius() const {
    if (!collision_shape_) return 0;
    return collision_shape_->GetBoundingRadius();
}

void RigidBody::GetWorldBounds(glm::vec3& min, glm::vec3& max) const {
    if (!collision_shape_) {
        min = glm::vec3(0);
        max = glm::vec3(0);
        return;
    }
    
    glm::vec3 center = GetWorldBoundsCenter();
    collision_shape_->GetBounds(min, max);
    
    // Add center offset
    min += center;
    max += center;
}

// Internal physics methods
void RigidBody::IntegrateVelocity(float delta_time) {
    if (body_type_ == BodyType::Static || body_type_ == BodyType::Kinematic) return;
    
    // Apply forces (F = ma)
    glm::vec3 acceleration = force_ * GetInverseMass();
    
    // Apply gravity
    // Note: You would get gravity from PhysicsWorld
    glm::vec3 gravity = glm::vec3(0, -9.81f, 0) * gravity_scale_;
    acceleration += gravity;
    
    // Update velocity
    velocity_ += acceleration * delta_time;
    
    // Apply linear constraints
    if (constraint_velocity_.x) velocity_.x = 0;
    if (constraint_velocity_.y) velocity_.y = 0;
    if (constraint_velocity_.z) velocity_.z = 0;
    
    // Update position
    world_position_ += velocity_ * delta_time;
}

// Note: Entity transform sync is done by scene integration layer

void RigidBody::IntegrateRotation(float delta_time) {
    if (body_type_ == BodyType::Static) return;
    
    // Simple angular integration (without inertia tensor for now)
    if (glm::length(torque_) > 1e-6f) {
        // Angular acceleration from torque
        glm::vec3 angular_acceleration = torque_ * GetInverseMass();
        angular_velocity_ += angular_acceleration * delta_time;
    }
    
    // Apply angular constraints
    if (constraint_rotation_.x) angular_velocity_.x = 0;
    if (constraint_rotation_.y) angular_velocity_.y = 0;
    if (constraint_rotation_.z) angular_velocity_.z = 0;
    
    // Update rotation
    if (glm::length(angular_velocity_) > 1e-6f) {
        float angle = glm::length(angular_velocity_) * delta_time;
        glm::vec3 axis = glm::normalize(angular_velocity_);
        glm::quat delta_rot = glm::angleAxis(angle, axis);
        
        world_rotation_ = delta_rot * world_rotation_;
    }
}

// Note: Entity transform sync is done by scene integration layer

void RigidBody::ApplyDamping(float delta_time) {
    // Linear damping (air resistance)
    velocity_ *= std::max(0.0f, 1.0f - linear_damping_ * delta_time);
    
    // Angular damping
    angular_velocity_ *= std::max(0.0f, 1.0f - angular_damping_ * delta_time);
    
    // Stop very small velocities to prevent jitter
    const float min_velocity = 1e-6f;
    if (glm::length(velocity_) < min_velocity) velocity_ = glm::vec3(0);
    if (glm::length(angular_velocity_) < min_velocity) angular_velocity_ = glm::vec3(0);
}

void RigidBody::UpdateSleepState(float delta_time) {
    if (body_type_ == BodyType::Static) {
        is_sleeping_ = true;
        return;
    }
    
    float velocity_magnitude = glm::length(velocity_);
    float angular_magnitude = glm::length(angular_velocity_);
    
    if (velocity_magnitude < sleep_threshold_ && angular_magnitude < sleep_threshold_) {
        sleep_timer_ += delta_time;
        if (sleep_timer_ > 0.5f) { // Sleep after 0.5 seconds of stillness
            Sleep();
        }
    } else {
        sleep_timer_ = 0;
        is_sleeping_ = false;
    }
}

void RigidBody::SyncTransform() {
    // Called by scene integration layer to sync physics state with entity transform
    // The actual implementation is in the scene-physics adapter
}

void RigidBody::UpdateCollisions(const std::vector<CollisionInfo>& collisions) {
    collisions_ = collisions;
}

} // namespace schizo::physics

