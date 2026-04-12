#pragma once

#include <memory>
#include <glm/glm.hpp>

namespace engine::character {

// Character state types
enum class CharacterStateType {
    Idle,
    Walk,
    Sprint,
    Jump,
    Fall,
    WallRun,
    Climb,
    Slide,
    Count
};

// Forward declarations
class CharacterController;

/**
 * @class CharacterState
 * @brief Base class for character states in the state machine
 */
class CharacterState {
public:
    virtual ~CharacterState() = default;

    /**
     * Called when entering this state
     */
    virtual void OnEnter(CharacterController* controller) = 0;

    /**
     * Called every frame while in this state
     * @param controller The parent character controller
     * @param dt Delta time in seconds
     */
    virtual void OnUpdate(CharacterController* controller, float dt) = 0;

    /**
     * Called when leaving this state
     */
    virtual void OnExit(CharacterController* controller) = 0;

    /**
     * Check if this state can transition to the target state
     * @param target The target state type
     * @return true if transition is allowed
     */
    virtual bool CanTransitionTo(CharacterStateType target) const = 0;

    /**
     * Get the current state type
     */
    virtual CharacterStateType GetStateType() const = 0;

    /**
     * Get the current state name for debugging
     */
    virtual const char* GetStateName() const = 0;
};

/**
 * @class IdleState
 * @brief State when character is standing still
 */
class IdleState : public CharacterState {
public:
    void OnEnter(CharacterController* controller) override;
    void OnUpdate(CharacterController* controller, float dt) override;
    void OnExit(CharacterController* controller) override;
    bool CanTransitionTo(CharacterStateType target) const override;
    CharacterStateType GetStateType() const override { return CharacterStateType::Idle; }
    const char* GetStateName() const override { return "Idle"; }

private:
    float idle_time_ = 0.0f;
};

/**
 * @class WalkState
 * @brief State when character is walking (0 < speed <= walk_speed)
 */
class WalkState : public CharacterState {
public:
    void OnEnter(CharacterController* controller) override;
    void OnUpdate(CharacterController* controller, float dt) override;
    void OnExit(CharacterController* controller) override;
    bool CanTransitionTo(CharacterStateType target) const override;
    CharacterStateType GetStateType() const override { return CharacterStateType::Walk; }
    const char* GetStateName() const override { return "Walk"; }

private:
    glm::vec3 walk_direction_ = glm::vec3(0.0f);
};

/**
 * @class SprintState
 * @brief State when character is sprinting (speed > walk_speed)
 */
class SprintState : public CharacterState {
public:
    void OnEnter(CharacterController* controller) override;
    void OnUpdate(CharacterController* controller, float dt) override;
    void OnExit(CharacterController* controller) override;
    bool CanTransitionTo(CharacterStateType target) const override;
    CharacterStateType GetStateType() const override { return CharacterStateType::Sprint; }
    const char* GetStateName() const override { return "Sprint"; }

private:
    float stamina_drain_rate_ = 20.0f;  // per second
};

/**
 * @class JumpState
 * @brief State when character is jumping
 */
class JumpState : public CharacterState {
public:
    void OnEnter(CharacterController* controller) override;
    void OnUpdate(CharacterController* controller, float dt) override;
    void OnExit(CharacterController* controller) override;
    bool CanTransitionTo(CharacterStateType target) const override;
    CharacterStateType GetStateType() const override { return CharacterStateType::Jump; }
    const char* GetStateName() const override { return "Jump"; }

private:
    float jump_time_ = 0.0f;
    float total_jump_duration_ = 0.5f;  // Total time in jump state
};

/**
 * @class FallState
 * @brief State when character is falling (velocity.y < 0)
 */
class FallState : public CharacterState {
public:
    void OnEnter(CharacterController* controller) override;
    void OnUpdate(CharacterController* controller, float dt) override;
    void OnExit(CharacterController* controller) override;
    bool CanTransitionTo(CharacterStateType target) const override;
    CharacterStateType GetStateType() const override { return CharacterStateType::Fall; }
    const char* GetStateName() const override { return "Fall"; }

private:
    float fall_time_ = 0.0f;
    bool falling_damage_applied_ = false;
};

/**
 * @class WallRunState
 * @brief State when character is running on a wall
 */
class WallRunState : public CharacterState {
public:
    void OnEnter(CharacterController* controller) override;
    void OnUpdate(CharacterController* controller, float dt) override;
    void OnExit(CharacterController* controller) override;
    bool CanTransitionTo(CharacterStateType target) const override;
    CharacterStateType GetStateType() const override { return CharacterStateType::WallRun; }
    const char* GetStateName() const override { return "WallRun"; }

private:
    glm::vec3 wall_normal_ = glm::vec3(0.0f);
};

/**
 * @class ClimbState
 * @brief State when character is climbing
 */
class ClimbState : public CharacterState {
public:
    void OnEnter(CharacterController* controller) override;
    void OnUpdate(CharacterController* controller, float dt) override;
    void OnExit(CharacterController* controller) override;
    bool CanTransitionTo(CharacterStateType target) const override;
    CharacterStateType GetStateType() const override { return CharacterStateType::Climb; }
    const char* GetStateName() const override { return "Climb"; }
};

/**
 * @class SlideState
 * @brief State when character is sliding
 */
class SlideState : public CharacterState {
public:
    void OnEnter(CharacterController* controller) override;
    void OnUpdate(CharacterController* controller, float dt) override;
    void OnExit(CharacterController* controller) override;
    bool CanTransitionTo(CharacterStateType target) const override;
    CharacterStateType GetStateType() const override { return CharacterStateType::Slide; }
    const char* GetStateName() const override { return "Slide"; }

private:
    float slide_time_ = 0.0f;
    float total_slide_duration_ = 0.8f;
};

} // namespace engine::character
