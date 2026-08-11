#include "../include/physics_system.h"
#include "../../core/physics/rigidbody_physics.h"
#include "physics_component.h"

#include <cstdint>

namespace schizo::scene {

PhysicsSystem::PhysicsSystem()
    : accumulator_(0.0f)
    , time_scale_(1.0f)
    , enabled_(true)
    , debug_visualization_(false)
{
}

PhysicsSystem::~PhysicsSystem() {
    Shutdown();
}

void PhysicsSystem::Initialize(const glm::vec3& gravity) {
    // Create physics world with default configuration
    physics_world_ = std::make_unique<schizo::physics::DefaultPhysicsWorld>();
    
    schizo::physics::PhysicsConfig config;
    config.gravity = gravity;
    config.time_scale = 1.0f;
    config.solver_iterations = 4;
    config.max_bodies = 10000;
    
    physics_world_->SetGravity(gravity);
}

void PhysicsSystem::Shutdown() {
    if (physics_world_) {
        physics_world_->Clear();
        physics_world_.reset();
    }
}

void PhysicsSystem::Update(float delta_time, float fixed_timestep) {
    if (!enabled_ || !physics_world_) {
        return;
    }
    
    // Apply time scale
    float scaled_delta = delta_time * time_scale_;
    
    // Accumulate time for fixed timestep simulation
    accumulator_ += scaled_delta;
    
    // Run fixed timestep updates
    while (accumulator_ >= fixed_timestep) {
        FixedUpdate(fixed_timestep);
        accumulator_ -= fixed_timestep;
    }
}

void PhysicsSystem::FixedUpdate(float fixed_timestep) {
    if (!physics_world_) return;
    
    // Step the physics world
    physics_world_->Step(fixed_timestep);
}

void PhysicsSystem::SetGravity(const glm::vec3& gravity) {
    if (physics_world_) {
        physics_world_->SetGravity(gravity);
    }
}

glm::vec3 PhysicsSystem::GetGravity() const {
    if (physics_world_) {
        return physics_world_->GetGravity();
    }
    return glm::vec3(0, -9.81f, 0);
}

void PhysicsSystem::SetTimeScale(float scale) {
    time_scale_ = glm::max(0.0f, scale);
    if (physics_world_) {
        physics_world_->SetTimeScale(time_scale_);
    }
}

float PhysicsSystem::GetTimeScale() const {
    return time_scale_;
}

void PhysicsSystem::SetSolverIterations(uint32_t iterations) {
    // Note: Would require storing in PhysicsWorld
    // This is a placeholder for the API
}

uint32_t PhysicsSystem::GetSolverIterations() const {
    // Placeholder
    return 4;
}

void PhysicsSystem::SetMaxBodies(uint32_t max_bodies) {
    // Placeholder - would require PhysicsWorld resizing
}

uint32_t PhysicsSystem::GetMaxBodies() const {
    // Placeholder
    return 10000;
}

void PhysicsSystem::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (physics_world_) {
        physics_world_->SetEnabled(enabled);
    }
}

bool PhysicsSystem::IsEnabled() const {
    return enabled_;
}

schizo::physics::PhysicsWorld* PhysicsSystem::GetPhysicsWorld() const {
    return physics_world_.get();
}

uint32_t PhysicsSystem::GetActiveBodyCount() const {
    if (physics_world_) {
        return physics_world_->GetActiveBodyCount();
    }
    return 0;
}

uint32_t PhysicsSystem::GetSleepingBodyCount() const {
    if (physics_world_) {
        return physics_world_->GetSleepingBodyCount();
    }
    return 0;
}

uint32_t PhysicsSystem::GetTotalBodyCount() const {
    if (physics_world_) {
        return physics_world_->GetBodyCount();
    }
    return 0;
}

uint32_t PhysicsSystem::GetCollisionCount() const {
    if (physics_world_) {
        return physics_world_->GetCollisionCount();
    }
    return 0;
}

uint32_t PhysicsSystem::GetCollisionTestCount() const {
    if (physics_world_) {
        return physics_world_->GetCollisionTestCount();
    }
    return 0;
}

void PhysicsSystem::SetDebugVisualization(bool enabled) {
    debug_visualization_ = enabled;
}

bool PhysicsSystem::GetDebugVisualization() const {
    return debug_visualization_;
}

} // namespace schizo::scene
