#include "character_controller.h"
#include "ground_detector.h"
#include "character_stats.h"
#include "locomotion_system.h"
#include <spdlog/spdlog.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace engine::character {

CharacterController::CharacterController(entt::entity entity)
    : entity_(entity),
      ground_detector_(std::make_unique<GroundDetector>(entity)),
      character_stats_(std::make_unique<CharacterStats>(entity)),
      locomotion_system_(std::make_unique<LocomotionSystem>(entity)) {
    
    InitializeStates();
    
    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("CharacterController created for entity");
    }
}

CharacterController::~CharacterController() = default;

void CharacterController::InitializeStates() {
    states_[static_cast<size_t>(CharacterStateType::Idle)] = std::make_unique<IdleState>();
    states_[static_cast<size_t>(CharacterStateType::Walk)] = std::make_unique<WalkState>();
    states_[static_cast<size_t>(CharacterStateType::Sprint)] = std::make_unique<SprintState>();
    states_[static_cast<size_t>(CharacterStateType::Jump)] = std::make_unique<JumpState>();
    states_[static_cast<size_t>(CharacterStateType::Fall)] = std::make_unique<FallState>();
    states_[static_cast<size_t>(CharacterStateType::WallRun)] = std::make_unique<WallRunState>();
    states_[static_cast<size_t>(CharacterStateType::Climb)] = std::make_unique<ClimbState>();
    states_[static_cast<size_t>(CharacterStateType::Slide)] = std::make_unique<SlideState>();
    
    // Start in Idle
    current_state_ = states_[static_cast<size_t>(CharacterStateType::Idle)].get();
    current_state_->OnEnter(this);
}

void CharacterController::Update(float dt) {
    // Update subsystems
    ground_detector_->Update();
    UpdateDashCooldown(dt);
    UpdateStamina(dt);
    
    // Process all buffered inputs
    while (input_buffer_.HasInput()) {
        InputAction action = input_buffer_.GetNextInput();
        ProcessInput(action);
    }
    
    // Update current state
    if (current_state_) {
        current_state_->OnUpdate(this, dt);
    }
    
    // Update movement
    UpdateMovement(dt);
    ApplyGravityAndDrag(dt);
    
    // Update animation system
    locomotion_system_->Update(GetCurrentSpeed(), desired_direction_.z, dt);
}

void CharacterController::ProcessInput(const InputAction& input) {
    // Process movement input
    desired_direction_ = glm::vec3(input.lateral, 0.0f, input.forward);

    // Normalize direction if there's any movement
    bool wants_to_move = glm::length(desired_direction_) > 0.001f;
    if (wants_to_move) {
        desired_direction_ = glm::normalize(desired_direction_);
    }

    // Movement input → Walk transition. Without this the state machine is
    // stuck in Idle (where desired_speed_ == 0), so UpdateMovement keeps
    // velocity at 0, speed never crosses the 0.5 threshold, and the
    // speed-based Idle→Walk check in UpdateMovement never fires.
    if (wants_to_move && IsInState(CharacterStateType::Idle) &&
        current_state_->CanTransitionTo(CharacterStateType::Walk)) {
        TransitionToState(CharacterStateType::Walk);
    } else if (!wants_to_move && IsInState(CharacterStateType::Walk) &&
               current_state_->CanTransitionTo(CharacterStateType::Idle)) {
        TransitionToState(CharacterStateType::Idle);
    }

    // Handle jumping
    if (input.jump && !was_jump_pressed_) {
        if (IsOnGround() && current_state_->CanTransitionTo(CharacterStateType::Jump)) {
            TransitionToState(CharacterStateType::Jump);
        }
    }
    was_jump_pressed_ = input.jump;
    
    // Handle sprinting
    if (input.sprint && !was_sprint_pressed_) {
        if (GetStaminaPercent() > 0.1f) {
            TransitionToState(CharacterStateType::Sprint);
        }
    } else if (!input.sprint && was_sprint_pressed_) {
        // Stop sprinting
        if (IsInState(CharacterStateType::Sprint)) {
            TransitionToState(CharacterStateType::Walk);
        }
    }
    was_sprint_pressed_ = input.sprint;
    
    // Handle dashing
    if (input.dash && CanDash()) {
        ActivateDash();
    }
    
    // Handle other actions
    if (input.crouch) {
        // Could transition to slide or crouch state
    }
}

bool CharacterController::TransitionToState(CharacterStateType target) {
    if (!current_state_) return false;
    
    // Check if transition is allowed
    if (!current_state_->CanTransitionTo(target)) {
        return false;
    }
    
    CharacterState* new_state = states_[static_cast<size_t>(target)].get();
    if (!new_state) return false;
    
    // Exit current state
    current_state_->OnExit(this);
    
    // Enter new state
    current_state_ = new_state;
    current_state_->OnEnter(this);
    
    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("State transition: {} -> {}", 
                     states_[static_cast<size_t>(CharacterStateType::Idle)]->GetStateName(),
                     current_state_->GetStateName());
    }
    
    return true;
}

bool CharacterController::CanTransitionTo(CharacterStateType target) const {
    if (!current_state_) return false;
    return current_state_->CanTransitionTo(target);
}

CharacterStateType CharacterController::GetCurrentStateType() const {
    if (!current_state_) return CharacterStateType::Idle;
    return current_state_->GetStateType();
}

const char* CharacterController::GetCurrentStateName() const {
    if (!current_state_) return "Unknown";
    return current_state_->GetStateName();
}

bool CharacterController::IsInState(CharacterStateType state) const {
    return GetCurrentStateType() == state;
}

glm::vec3 CharacterController::GetVelocity() const {
    return velocity_;
}

void CharacterController::SetVelocity(const glm::vec3& velocity) {
    velocity_ = velocity;
}

float CharacterController::GetCurrentSpeed() const {
    glm::vec3 horizontal_velocity = glm::vec3(velocity_.x, 0.0f, velocity_.z);
    return glm::length(horizontal_velocity);
}

glm::vec3 CharacterController::GetPosition() const {
    return external_position_;
}

void CharacterController::SetPosition(const glm::vec3& position) {
    external_position_ = position;
}

glm::vec3 CharacterController::GetForwardDirection() const {
    return glm::normalize(glm::vec3(desired_direction_.x, 0.0f, desired_direction_.z));
}

bool CharacterController::IsOnGround() const {
    if (grounded_override_.has_value()) return *grounded_override_;
    return ground_detector_->IsOnGround();
}

void CharacterController::SetGrounded(bool grounded) {
    grounded_override_ = grounded;
}

void CharacterController::ClearGroundedOverride() {
    grounded_override_.reset();
}

glm::vec3 CharacterController::GetGroundNormal() const {
    return ground_detector_->GetGroundNormal();
}

glm::vec3 CharacterController::GetGroundContactPoint() const {
    return ground_detector_->GetGroundContactPoint();
}

float CharacterController::GetLocomotionParameter() const {
    return locomotion_system_->GetLocomotionParameter();
}

float CharacterController::GetDirectionParameter() const {
    return locomotion_system_->GetDirectionParameter();
}

float CharacterController::GetVerticalVelocity() const {
    return velocity_.y;
}

CharacterStats* CharacterController::GetCharacterStats() {
    return character_stats_.get();
}

const CharacterStats* CharacterController::GetCharacterStats() const {
    return character_stats_.get();
}

float CharacterController::GetStaminaPercent() const {
    if (character_stats_) {
        return character_stats_->GetStaminaPercent();
    }
    return 0.0f;
}

bool CharacterController::ConsumeStamina(float amount) {
    if (character_stats_) {
        return character_stats_->ConsumeStamina(amount);
    }
    return false;
}

void CharacterController::ApplyDashImpulse(const glm::vec3& direction, float force, float duration) {
    velocity_ = glm::normalize(direction) * force;
}

void CharacterController::ActivateDash() {
    auto dash_direction = desired_direction_;
    if (glm::length(dash_direction) < 0.001f) {
        dash_direction = GetForwardDirection();
    }
    
    ApplyDashImpulse(dash_direction, 20.0f, 0.3f);
    dash_cooldown_ = DASH_COOLDOWN_DURATION;
    
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Dash activated");
}

void CharacterController::UpdateMovement(float dt) {
    // Determine target horizontal speed. Jump and Fall keep `WALK_SPEED` so
    // the player has full air control — pressing W mid-jump moves you
    // forward instead of letting the speed lerp decay to zero, which is the
    // arcade default. Set this to a fraction of WALK_SPEED if you want
    // reduced air control.
    if (IsInState(CharacterStateType::Sprint)) {
        desired_speed_ = SPRINT_SPEED;
    } else if (IsInState(CharacterStateType::Walk) ||
               IsInState(CharacterStateType::Jump) ||
               IsInState(CharacterStateType::Fall)) {
        desired_speed_ = WALK_SPEED;
    } else {
        desired_speed_ = 0.0f;
    }
    
    // Smoothly blend to target speed
    current_speed_ = glm::mix(current_speed_, desired_speed_, 5.0f * dt);
    
    // Apply movement in desired direction
    if (glm::length(desired_direction_) > 0.001f) {
        velocity_.x = desired_direction_.x * current_speed_;
        velocity_.z = desired_direction_.z * current_speed_;
    } else {
        // Decelerate smoothly. Clamp the decay factor to [0, 1] so a large
        // dt (post-stutter frame) can't make velocity overshoot into the
        // opposite direction.
        float decay = std::max(0.0f, 1.0f - GROUND_DRAG * dt);
        velocity_.x *= decay;
        velocity_.z *= decay;
    }
    
    // Transition between states based on speed
    float speed = GetCurrentSpeed();
    
    if (speed < 0.5f && IsInState(CharacterStateType::Walk)) {
        TransitionToState(CharacterStateType::Idle);
    } else if (speed >= 0.5f && IsInState(CharacterStateType::Idle)) {
        TransitionToState(CharacterStateType::Walk);
    } else if (speed >= SPRINT_SPEED * 0.9f && IsInState(CharacterStateType::Walk)) {
        TransitionToState(CharacterStateType::Sprint);
    }
}

void CharacterController::UpdateStamina(float dt) {
    if (character_stats_) {
        character_stats_->Update(dt);
    }
}

void CharacterController::UpdateDashCooldown(float dt) {
    if (dash_cooldown_ > 0.0f) {
        dash_cooldown_ -= dt;
    }
}

void CharacterController::ApplyGravityAndDrag(float dt) {
    if (!IsOnGround()) {
        // Apply gravity
        velocity_.y += GRAVITY * dt;
        
        // Apply air drag
        glm::vec3 horizontal = glm::vec3(velocity_.x, 0.0f, velocity_.z);
        horizontal *= (1.0f - AIR_DRAG * dt);
        velocity_.x = horizontal.x;
        velocity_.z = horizontal.z;
    } else {
        // On ground
        if (velocity_.y < 0.0f) {
            velocity_.y = 0.0f;  // Clamp negative velocity to 0
        }
    }
}

} // namespace engine::character
