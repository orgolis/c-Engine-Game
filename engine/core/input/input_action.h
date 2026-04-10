#pragma once

#include <cstdint>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <string>

namespace schizo::input {

/**
 * @brief Input action types - typed actions rather than raw keys
 * 
 * This enum represents high-level game actions. The input system converts
 * raw keyboard/gamepad input into these typed actions, providing flexibility
 * for remapping and multi-input handling.
 * 
 * Key design: Game code reads InputAction events, not raw keys.
 * This enables ability system to route activations correctly.
 */
enum class InputAction : uint8_t {
    // Movement
    MoveForward = 0,
    MoveBack,
    StrafeLeft,
    StrafeRight,
    
    // Locomotion
    Jump,
    Dash,
    Crouch,
    
    // Abilities (4 slots + ultimate)
    AbilitySlot1,
    AbilitySlot2,
    AbilitySlot3,
    AbilitySlot4,
    UltimateAbility,
    
    // Interaction
    Interact,
    OpenInventory,
    OpenMap,
    OpenCharacterSheet,
    
    // Combat
    LockOn,
    ParryBlock,
    
    // UI/System
    Pause,
    ShowDebug,
    
    // Total count
    COUNT
};

/**
 * @brief Input event state
 */
enum class InputState : uint8_t {
    Pressed,    // Key pressed this frame
    Held,       // Key held (pressed last frame, still down)
    Released,   // Key released this frame
    Idle        // Not pressed
};

/**
 * @brief Typed input event
 * 
 * Instead of reporting "Key W was pressed", the input system
 * reports "MoveForward action was pressed".
 */
struct InputEvent {
    InputAction action;
    InputState state;
    float value;            // For analog inputs (0.0 - 1.0)
    double timestamp;       // When this event occurred
    
    InputEvent() = default;
    InputEvent(InputAction a, InputState s, float v = 1.0f, double ts = 0.0)
        : action(a), state(s), value(v), timestamp(ts) {}
};

/**
 * @brief Directional input (combines movement axes)
 * 
 * Merges movement input into a single direction vector.
 * Range: [-1, 1] on both x and z axes
 */
struct MovementInput {
    float forward;   // Z axis (W/S or gamepad Y)
    float strafe;    // X axis (A/D or gamepad X)
    
    MovementInput() : forward(0.0f), strafe(0.0f) {}
    MovementInput(float f, float s) : forward(f), strafe(s) {}
    
    // Computed properties
    bool IsMoving() const { return forward != 0.0f || strafe != 0.0f; }
    
    float GetMagnitude() const {
        return std::sqrt(forward * forward + strafe * strafe);
    }
};

/**
 * @brief Game input state snapshot
 * 
 * Contains current input state for deterministic replay.
 * All values are fixed-point for bit-perfect replay.
 */
struct InputSnapshot {
    MovementInput movement;
    bool jump;
    bool dash;
    bool ability_slot1;
    bool ability_slot2;
    bool ability_slot3;
    bool ability_slot4;
    bool ultimate;
    bool crouch;
    bool lock_on;
    bool parry;
    
    double timestamp;
    uint32_t frame_number;
    
    InputSnapshot()
        : jump(false), dash(false), ability_slot1(false), ability_slot2(false),
          ability_slot3(false), ability_slot4(false), ultimate(false), crouch(false),
          lock_on(false), parry(false), timestamp(0.0), frame_number(0) {
    }
};

} // namespace schizo::input
