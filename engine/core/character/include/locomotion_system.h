#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace engine::character {

enum class LocomotionState {
    Idle,
    Walk,
    Sprint
};

/**
 * @class LocomotionSystem
 * @brief Manages animation blending for locomotion
 *
 * This system handles the animation blend space for character movement:
 * - Idle (parameter = 0.0)
 * - Walk (parameter = 0.0 - 0.5)
 * - Sprint (parameter = 0.5 - 1.0)
 *
 * The animation system should blend between these states using the
 * locomotion_parameter.
 */
class LocomotionSystem {
public:
    /**
     * Create locomotion system for entity
     */
    explicit LocomotionSystem(entt::entity entity);

    /**
     * Update locomotion animation parameters
     * @param speed Current movement speed
     * @param direction Movement direction (-1, 0, 1 for back/idle/forward)
     * @param dt Delta time
     */
    void Update(float speed, float direction, float dt);

    /**
     * Get animation blend parameter (0.0 = idle, 0.5 = walk, 1.0 = sprint)
     */
    float GetLocomotionParameter() const { return locomotion_parameter_; }

    /**
     * Get direction blend parameter (-1 = backward, 0 = neutral, 1 = forward)
     */
    float GetDirectionParameter() const { return direction_parameter_; }

    /**
     * Get current locomotion state
     */
    LocomotionState GetLocomotionState() const { return current_state_; }

    /**
     * Set animation parameters directly
     */
    void SetAnimationParameters(float locomotion, float direction);

private:
    entt::entity entity_;
    float locomotion_parameter_ = 0.0f;
    float direction_parameter_ = 0.0f;
    LocomotionState current_state_ = LocomotionState::Idle;

    static constexpr float BLEND_SPEED = 5.0f;  // Blend transition speed
    static constexpr float WALK_THRESHOLD = 0.1f;
    static constexpr float SPRINT_THRESHOLD = 5.0f;

    LocomotionState GetStateForSpeed(float speed) const;
};

} // namespace engine::character
