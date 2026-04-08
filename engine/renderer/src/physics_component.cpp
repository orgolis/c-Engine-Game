#include "../include/physics_component.h"
#include "../../core/physics/rigidbody_physics.h"
#include <glm/gtc/matrix_transform.hpp>

namespace schizo::renderer {

PhysicsComponent::PhysicsComponent(const glm::vec3& position, const glm::quat& rotation)
    : physics_world_(nullptr)
    , last_position_(position)
    , last_rotation_(rotation)
    , enabled_(true)
    , rigidbody_(std::make_unique<schizo::physics::RigidBody>())
{
    // Initialize rigidbody with position/rotation
    rigidbody_->SetPosition(position);
    rigidbody_->SetRotation(rotation);
}

PhysicsComponent::~PhysicsComponent() {
    if (physics_world_) {
        OnDetach();
    }
}

void PhysicsComponent::OnAttach(schizo::physics::PhysicsWorld* world) {
    physics_world_ = world;
    if (world && rigidbody_) {
        world->AddRigidBody(rigidbody_.get());
    }
}

void PhysicsComponent::OnDetach() {
    if (physics_world_ && rigidbody_) {
        physics_world_->RemoveRigidBody(rigidbody_.get());
    }
    physics_world_ = nullptr;
}

void PhysicsComponent::OnPreUpdate(float delta_time) {
    if (!enabled_ || !rigidbody_) return;
    
    // Sync any programmatic position/rotation changes to physics body
    SyncToPhysics();
}

void PhysicsComponent::OnPostUpdate(float delta_time) {
    if (!enabled_ || !rigidbody_) return;
    
    // Sync physics results back to our cached position/rotation
    SyncFromPhysics();
}

void PhysicsComponent::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (rigidbody_) {
        rigidbody_->SetEnabled(enabled);
    }
}

bool PhysicsComponent::IsEnabled() const {
    return enabled_;
}

glm::vec3 PhysicsComponent::GetPosition() const {
    if (rigidbody_) {
        return rigidbody_->GetPosition();
    }
    return last_position_;
}

void PhysicsComponent::SetPosition(const glm::vec3& position) {
    last_position_ = position;
    if (rigidbody_) {
        rigidbody_->SetPosition(position);
    }
}

glm::quat PhysicsComponent::GetRotation() const {
    if (rigidbody_) {
        return rigidbody_->GetRotation();
    }
    return last_rotation_;
}

void PhysicsComponent::SetRotation(const glm::quat& rotation) {
    last_rotation_ = rotation;
    if (rigidbody_) {
        rigidbody_->SetRotation(rotation);
    }
}

glm::mat4 PhysicsComponent::GetTransform() const {
    glm::vec3 pos = GetPosition();
    glm::quat rot = GetRotation();
    
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), pos);
    glm::mat4 rotation = glm::mat4_cast(rot);
    
    return translation * rotation;
}

schizo::physics::RigidBody* PhysicsComponent::GetRigidBody() const {
    return rigidbody_.get();
}

void PhysicsComponent::SetMass(float mass) {
    if (rigidbody_) {
        rigidbody_->SetMass(mass);
    }
}

float PhysicsComponent::GetMass() const {
    if (rigidbody_) {
        return rigidbody_->GetMass();
    }
    return 0.0f;
}

void PhysicsComponent::SetCollisionShape(std::unique_ptr<schizo::physics::CollisionShape> shape) {
    if (rigidbody_) {
        rigidbody_->SetCollisionShape(std::move(shape));
    }
}

void PhysicsComponent::ApplyForce(const glm::vec3& force) {
    if (rigidbody_) {
        rigidbody_->ApplyForce(force);
    }
}

void PhysicsComponent::ApplyForceAtPoint(const glm::vec3& force, const glm::vec3& world_point) {
    if (rigidbody_) {
        glm::vec3 center = rigidbody_->GetPosition();
        glm::vec3 offset = world_point - center;
        rigidbody_->ApplyForce(force, offset);
    }
}

void PhysicsComponent::ApplyImpulse(const glm::vec3& impulse) {
    if (rigidbody_) {
        rigidbody_->ApplyImpulse(impulse);
    }
}

void PhysicsComponent::ApplyImpulseAtPoint(const glm::vec3& impulse, const glm::vec3& world_point) {
    if (rigidbody_) {
        glm::vec3 center = rigidbody_->GetPosition();
        glm::vec3 offset = world_point - center;
        rigidbody_->ApplyImpulse(impulse, offset);
    }
}

void PhysicsComponent::ApplyTorque(const glm::vec3& torque) {
    if (rigidbody_) {
        rigidbody_->ApplyTorque(torque);
    }
}

glm::vec3 PhysicsComponent::GetVelocity() const {
    if (rigidbody_) {
        return rigidbody_->GetVelocity();
    }
    return glm::vec3(0);
}

void PhysicsComponent::SetVelocity(const glm::vec3& velocity) {
    if (rigidbody_) {
        rigidbody_->SetVelocity(velocity);
    }
}

glm::vec3 PhysicsComponent::GetAngularVelocity() const {
    if (rigidbody_) {
        return rigidbody_->GetAngularVelocity();
    }
    return glm::vec3(0);
}

void PhysicsComponent::SetAngularVelocity(const glm::vec3& angular_velocity) {
    if (rigidbody_) {
        rigidbody_->SetAngularVelocity(angular_velocity);
    }
}

void PhysicsComponent::SetFriction(float friction) {
    if (rigidbody_) {
        rigidbody_->SetFriction(friction);
    }
}

float PhysicsComponent::GetFriction() const {
    if (rigidbody_) {
        return rigidbody_->GetFriction();
    }
    return 0.0f;
}

void PhysicsComponent::SetRestitution(float restitution) {
    if (rigidbody_) {
        rigidbody_->SetRestitution(restitution);
    }
}

float PhysicsComponent::GetRestitution() const {
    if (rigidbody_) {
        return rigidbody_->GetRestitution();
    }
    return 0.0f;
}

void PhysicsComponent::SetLinearDamping(float damping) {
    if (rigidbody_) {
        rigidbody_->SetLinearDamping(damping);
    }
}

float PhysicsComponent::GetLinearDamping() const {
    if (rigidbody_) {
        return rigidbody_->GetLinearDamping();
    }
    return 0.0f;
}

void PhysicsComponent::SetAngularDamping(float damping) {
    if (rigidbody_) {
        rigidbody_->SetAngularDamping(damping);
    }
}

float PhysicsComponent::GetAngularDamping() const {
    if (rigidbody_) {
        return rigidbody_->GetAngularDamping();
    }
    return 0.0f;
}

void PhysicsComponent::SetUseGravity(bool use_gravity) {
    if (rigidbody_) {
        rigidbody_->SetGravityScale(use_gravity ? 1.0f : 0.0f);
    }
}

bool PhysicsComponent::GetUseGravity() const {
    if (rigidbody_) {
        return rigidbody_->GetGravityScale() > 0.0f;
    }
    return true;
}

void PhysicsComponent::SetGravityScale(float scale) {
    if (rigidbody_) {
        rigidbody_->SetGravityScale(scale);
    }
}

float PhysicsComponent::GetGravityScale() const {
    if (rigidbody_) {
        return rigidbody_->GetGravityScale();
    }
    return 1.0f;
}

void PhysicsComponent::SetLinearConstraint(bool x, bool y, bool z) {
    if (rigidbody_) {
        rigidbody_->SetLinearVelocityConstraint(x, y, z);
    }
}

void PhysicsComponent::SetRotationConstraint(bool x, bool y, bool z) {
    if (rigidbody_) {
        rigidbody_->SetAngularVelocityConstraint(x, y, z);
    }
}

void PhysicsComponent::Sleep() {
    if (rigidbody_) {
        rigidbody_->Sleep();
    }
}

void PhysicsComponent::Wake() {
    if (rigidbody_) {
        rigidbody_->Wake();
    }
}

bool PhysicsComponent::IsSleeping() const {
    if (rigidbody_) {
        return rigidbody_->IsSleeping();
    }
    return true;
}

void PhysicsComponent::SyncToPhysics() {
    // In a full scene system, this would check if entity position changed
    // and sync changes to the physics body. For now it's a stub.
}

void PhysicsComponent::SyncFromPhysics() {
    // Cache current physics state
    if (rigidbody_) {
        last_position_ = rigidbody_->GetPosition();
        last_rotation_ = rigidbody_->GetRotation();
    }
}

} // namespace schizo::renderer
