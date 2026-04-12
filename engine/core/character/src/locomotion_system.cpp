#include "locomotion_system.h"
#include <spdlog/spdlog.h>
#include <glm/common.hpp>

namespace engine::character {

LocomotionSystem::LocomotionSystem(entt::entity entity)
    : entity_(entity) {
}

void LocomotionSystem::Update(float speed, float direction, float dt) {
    // Determine target state based on speed
    LocomotionState target_state = GetStateForSpeed(speed);
    
    // Blend locomotion parameter smoothly
    if (target_state == LocomotionState::Idle) {
        locomotion_parameter_ = glm::mix(locomotion_parameter_, 0.0f, BLEND_SPEED * dt);
    } else if (target_state == LocomotionState::Walk) {
        locomotion_parameter_ = glm::mix(locomotion_parameter_, 0.5f, BLEND_SPEED * dt);
    } else if (target_state == LocomotionState::Sprint) {
        locomotion_parameter_ = glm::mix(locomotion_parameter_, 1.0f, BLEND_SPEED * dt);
    }
    
    // Blend direction parameter smoothly
    direction_parameter_ = glm::mix(direction_parameter_, direction, BLEND_SPEED * dt);

    current_state_ = target_state;
}

void LocomotionSystem::SetAnimationParameters(float locomotion, float direction) {
    locomotion_parameter_ = glm::clamp(locomotion, 0.0f, 1.0f);
    direction_parameter_ = glm::clamp(direction, -1.0f, 1.0f);
}

LocomotionState LocomotionSystem::GetStateForSpeed(float speed) const {
    if (speed < WALK_THRESHOLD) {
        return LocomotionState::Idle;
    } else if (speed < SPRINT_THRESHOLD) {
        return LocomotionState::Walk;
    } else {
        return LocomotionState::Sprint;
    }
}

} // namespace engine::character
