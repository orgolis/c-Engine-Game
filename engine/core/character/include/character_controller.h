#pragma once

#include <memory>
#include <array>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include "input_buffer.h"
#include "character_state.h"

namespace engine::character {

class GroundDetector;
class CharacterStats;
class LocomotionSystem;

/**
 * @class CharacterController
 * @brief Main controller for player character movement and state management
 *
 * This class manages:
 * - State machine (8 states: Idle, Walk, Sprint, Jump, Fall, WallRun, Climb, Slide)
 * - Input buffering for responsive control
 * - Physics integration (velocity, acceleration)
 * - Ground detection via raycasting
 * - Animation parameter calculation
 * - Networked movement
 */
class CharacterController {
public:
    // Movement parameters
    static constexpr float WALK_SPEED = 5.0f;      // m/s
    static constexpr float SPRINT_SPEED = 10.0f;   // m/s
    static constexpr float JUMP_FORCE = 15.0f;     // m/s
    static constexpr float GRAVITY = -9.81f;       // m/s²
    static constexpr float GROUND_DRAG = 0.1f;     // friction coefficient
    static constexpr float AIR_DRAG = 0.01f;       // air friction

    /**
     * Create a character controller for an entity
     * @param entity The entity to control (must have Transform and RigidBody)
     */
    explicit CharacterController(entt::entity entity);
    ~CharacterController();

    /**
     * Update the character controller
     * @param dt Delta time in seconds
     */
    void Update(float dt);

    /**
     * Process input from the player
     * @param input The input action
     */
    void ProcessInput(const InputAction& input);

    /**
     * Transition to a new state
     * @param target The target state type
     * @return true if transition was successful
     */
    bool TransitionToState(CharacterStateType target);

    /**
     * Transition to a state and return if it succeeded
     * Checks if current state allows transition to target state
     */
    bool CanTransitionTo(CharacterStateType target) const;

    // ============ STATE QUERIES ============

    /**
     * Get the current state type
     */
    CharacterStateType GetCurrentStateType() const;

    /**
     * Get the current state name
     */
    const char* GetCurrentStateName() const;

    /**
     * Check if in a specific state
     */
    bool IsInState(CharacterStateType state) const;

    // ============ MOVEMENT QUERIES ============

    /**
     * Get the character's current velocity
     */
    glm::vec3 GetVelocity() const;

    /**
     * Set the character's velocity directly
     */
    void SetVelocity(const glm::vec3& velocity);

    /**
     * Get the character's current speed (magnitude of horizontal velocity)
     */
    float GetCurrentSpeed() const;

    /**
     * Get the character's position
     */
    glm::vec3 GetPosition() const;

    /**
     * Get the character's forward direction
     */
    glm::vec3 GetForwardDirection() const;

    // ============ GROUND DETECTION ============

    /**
     * Check if the character is on the ground
     */
    bool IsOnGround() const;

    /**
     * Get the ground normal at the character's position
     */
    glm::vec3 GetGroundNormal() const;

    /**
     * Get the ground contact point
     */
    glm::vec3 GetGroundContactPoint() const;

    // ============ ANIMATION PARAMETERS ============

    /**
     * Get the locomotion speed parameter (0 = idle, 0.5 = walk, 1.0 = sprint)
     * Used for animation blending
     */
    float GetLocomotionParameter() const;

    /**
     * Get the direction parameter for animation blending
     */
    float GetDirectionParameter() const;

    /**
     * Get the vertical velocity for animation (jump/fall)
     */
    float GetVerticalVelocity() const;

    // ============ STATS ACCESS ============

    /**
     * Get pointer to character stats (health, mana, stamina, etc)
     */
    CharacterStats* GetCharacterStats();
    const CharacterStats* GetCharacterStats() const;

    /**
     * Get remaining stamina (0.0 - 1.0)
     */
    float GetStaminaPercent() const;

    /**
     * Consume stamina for an action
     * @param amount Amount to consume (0.0 - 1.0)
     * @return true if stamina was consumed, false if not enough stamina
     */
    bool ConsumeStamina(float amount);

    // ============ DASH/ABILITY INTEGRATION ============

    /**
     * Apply a dash/ability impulse
     * @param direction Direction to dash
     * @param force Speed of dash
     * @param duration How long the dash state should last
     */
    void ApplyDashImpulse(const glm::vec3& direction, float force, float duration);

    /**
     * Check if dash is available (on cooldown)
     */
    bool CanDash() const { return dash_cooldown_ <= 0.0f; }

    /**
     * Activate dash ability
     */
    void ActivateDash();

private:
    entt::entity entity_;

    // State machine
    std::array<std::unique_ptr<CharacterState>, static_cast<size_t>(CharacterStateType::Count)> states_;
    CharacterState* current_state_ = nullptr;

    // Input
    InputBuffer input_buffer_;

    // Subsystems
    std::unique_ptr<GroundDetector> ground_detector_;
    std::unique_ptr<CharacterStats> character_stats_;
    std::unique_ptr<LocomotionSystem> locomotion_system_;

    // Physics
    glm::vec3 velocity_ = glm::vec3(0.0f);
    glm::vec3 desired_direction_ = glm::vec3(0.0f);

    // Movement parameters
    float current_speed_ = 0.0f;
    float move_speed_ = WALK_SPEED;
    float desired_speed_ = 0.0f;

    // Stamina system
    float stamina_ = 100.0f;
    float max_stamina_ = 100.0f;
    float stamina_regen_rate_ = 15.0f;  // per second

    // Dash
    float dash_cooldown_ = 0.0f;
    static constexpr float DASH_COOLDOWN_DURATION = 1.5f;

    // Input state tracking
    bool was_jump_pressed_ = false;
    bool was_sprint_pressed_ = false;

    // Initialize all states
    void InitializeStates();

    // Internal update methods
    void UpdateMovement(float dt);
    void UpdateStamina(float dt);
    void UpdateDashCooldown(float dt);
    void ApplyGravityAndDrag(float dt);
};

} // namespace engine::character
