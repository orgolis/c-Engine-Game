#include "character_state.h"
#include "character_controller.h"
#include "character_stats.h"
#include "ground_detector.h"
#include <spdlog/spdlog.h>

namespace engine::character {

// ============ IdleState ============

void IdleState::OnEnter(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Entering Idle state");
    idle_time_ = 0.0f;
}

void IdleState::OnUpdate(CharacterController* controller, float dt) {
    idle_time_ += dt;
    
    // Regenerate stamina while idle
    auto* stats = controller->GetCharacterStats();
    if (stats) {
        stats->Update(dt);
    }
}

void IdleState::OnExit(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Exiting Idle state");
}

bool IdleState::CanTransitionTo(CharacterStateType target) const {
    // From Idle, can go to Walk, Jump (if we add jump on idle)
    return target == CharacterStateType::Walk ||
           target == CharacterStateType::Jump ||
           target == CharacterStateType::Fall ||
           target == CharacterStateType::Slide;
}

// ============ WalkState ============

void WalkState::OnEnter(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Entering Walk state");
    walk_direction_ = glm::vec3(0.0f);
}

void WalkState::OnUpdate(CharacterController* controller, float dt) {
    // Apply steady movement at walk speed
    auto current_pos = controller->GetPosition();
    
    // Blend towards desired speed smoothly
    float desired_speed = controller->GetCurrentSpeed();
    desired_speed = glm::clamp(desired_speed, 0.0f, CharacterController::WALK_SPEED);
}

void WalkState::OnExit(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Exiting Walk state");
}

bool WalkState::CanTransitionTo(CharacterStateType target) const {
    // From Walk, can go to Idle, Sprint, Jump, Fall
    return target == CharacterStateType::Idle ||
           target == CharacterStateType::Sprint ||
           target == CharacterStateType::Jump ||
           target == CharacterStateType::Fall ||
           target == CharacterStateType::Climb ||
           target == CharacterStateType::Slide;
}

// ============ SprintState ============

void SprintState::OnEnter(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Entering Sprint state");
}

void SprintState::OnUpdate(CharacterController* controller, float dt) {
    // Drain stamina while sprinting
    if (!controller->ConsumeStamina(stamina_drain_rate_ * dt)) {
        // Out of stamina, try to transition to walk
        controller->TransitionToState(CharacterStateType::Walk);
    }
}

void SprintState::OnExit(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Exiting Sprint state");
}

bool SprintState::CanTransitionTo(CharacterStateType target) const {
    // From Sprint, can go to Walk, Jump, Fall
    return target == CharacterStateType::Walk ||
           target == CharacterStateType::Jump ||
           target == CharacterStateType::Fall ||
           target == CharacterStateType::Idle;
}

// ============ JumpState ============

void JumpState::OnEnter(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Entering Jump state");
    
    jump_time_ = 0.0f;
    
    // Apply jump impulse
    glm::vec3 current_velocity = controller->GetVelocity();
    current_velocity.y = CharacterController::JUMP_FORCE;
    controller->SetVelocity(current_velocity);
}

void JumpState::OnUpdate(CharacterController* controller, float dt) {
    jump_time_ += dt;
    
    // Transition to fall after jump duration
    if (jump_time_ >= total_jump_duration_) {
        auto velocity = controller->GetVelocity();
        if (velocity.y < 0) {
            controller->TransitionToState(CharacterStateType::Fall);
        }
    }
}

void JumpState::OnExit(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Exiting Jump state");
}

bool JumpState::CanTransitionTo(CharacterStateType target) const {
    // From Jump, can go to Fall
    return target == CharacterStateType::Fall ||
           target == CharacterStateType::WallRun;
}

// ============ FallState ============

void FallState::OnEnter(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Entering Fall state");
    
    fall_time_ = 0.0f;
    falling_damage_applied_ = false;
}

void FallState::OnUpdate(CharacterController* controller, float dt) {
    fall_time_ += dt;
    
    // Check if we landed
    if (controller->IsOnGround()) {
        // Calculate fall damage
        float fall_distance = fall_time_ * 10.0f;  // Approximate
        
        if (fall_distance > 5.0f && !falling_damage_applied_) {
            // Apply landing damage
            float damage = (fall_distance - 5.0f) * 2.0f;
            auto* stats = controller->GetCharacterStats();
            if (stats) {
                stats->TakeDamage(damage);
                auto logger = spdlog::get("engine");
                if (logger) logger->debug("Fall damage applied: {}", damage);
            }
            falling_damage_applied_ = true;
        }
        
        // Transition to appropriate ground state
        controller->TransitionToState(CharacterStateType::Idle);
    }
}

void FallState::OnExit(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Exiting Fall state");
}

bool FallState::CanTransitionTo(CharacterStateType target) const {
    // From Fall, can go to Idle, Walk, or other ground states
    return target == CharacterStateType::Idle ||
           target == CharacterStateType::Walk ||
           target == CharacterStateType::WallRun ||
           target == CharacterStateType::Climb ||
           target == CharacterStateType::Slide;
}

// ============ WallRunState ============

void WallRunState::OnEnter(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Entering WallRun state");
}

void WallRunState::OnUpdate(CharacterController* controller, float dt) {
    // Reduce fall speed while wallrunning
    auto velocity = controller->GetVelocity();
    velocity.y = glm::max(velocity.y, -5.0f);  // Max fall speed
    controller->SetVelocity(velocity);
}

void WallRunState::OnExit(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Exiting WallRun state");
}

bool WallRunState::CanTransitionTo(CharacterStateType target) const {
    return target == CharacterStateType::Jump ||
           target == CharacterStateType::Fall ||
           target == CharacterStateType::Climb;
}

// ============ ClimbState ============

void ClimbState::OnEnter(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Entering Climb state");
}

void ClimbState::OnUpdate(CharacterController* controller, float dt) {
    // Climbing implementation
    // When climbing, character can move up/down at controlled speed
}

void ClimbState::OnExit(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Exiting Climb state");
}

bool ClimbState::CanTransitionTo(CharacterStateType target) const {
    return target == CharacterStateType::Fall ||
           target == CharacterStateType::Jump ||
           target == CharacterStateType::Idle;
}

// ============ SlideState ============

void SlideState::OnEnter(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Entering Slide state");
    slide_time_ = 0.0f;
}

void SlideState::OnUpdate(CharacterController* controller, float dt) {
    slide_time_ += dt;
    
    // Slide gradually slows down
    auto velocity = controller->GetVelocity();
    float speed_decay = glm::mix(1.0f, 0.2f, slide_time_ / total_slide_duration_);
    velocity.x *= speed_decay;
    velocity.z *= speed_decay;
    controller->SetVelocity(velocity);
    
    // Exit slide after duration
    if (slide_time_ >= total_slide_duration_) {
        controller->TransitionToState(CharacterStateType::Walk);
    }
}

void SlideState::OnExit(CharacterController* controller) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Exiting Slide state");
}

bool SlideState::CanTransitionTo(CharacterStateType target) const {
    return target == CharacterStateType::Idle ||
           target == CharacterStateType::Walk ||
           target == CharacterStateType::Fall;
}

} // namespace engine::character
