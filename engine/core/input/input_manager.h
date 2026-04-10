#pragma once

#include "input_action.h"
#include <vector>
#include <unordered_map>
#include <queue>
#include <memory>

struct GLFWwindow;

namespace schizo::input {

/**
 * @brief Input manager - converts raw input to typed actions
 * 
 * Responsibilities:
 * 1. Poll raw keyboard/gamepad input from OS
 * 2. Map raw input to InputAction via configurable bindings
 * 3. Generate InputEvent for each action change
 * 4. Provide snapshot of current input for deterministic replay
 * 
 * Architecture:
 * - GLFW window events are polled per frame
 * - Key state tracked to distinguish pressed/held/released
 * - Events queued for game code to consume
 * - Snapshots generated for networking/replay
 */
class InputManager {
public:
    virtual ~InputManager() = default;
    
    /**
     * @brief Poll input from OS and generate events
     * 
     * Called once per frame. Converts raw input to InputEvents.
     * Must be called before GetNextEvent().
     */
    virtual void PollInput(double delta_time) = 0;
    
    /**
     * @brief Get next queued input event
     * @return nullptr if no more events this frame
     */
    virtual std::unique_ptr<InputEvent> GetNextEvent() = 0;
    
    /**
     * @brief Get current input state snapshot for deterministic replay
     * 
     * Combines all active input into single frame-deterministic snapshot.
     * Frame number automatically incremented.
     */
    virtual InputSnapshot GetSnapshot() = 0;
    
    /**
     * @brief Check if action is currently held
     * @return true if pressed or held
     */
    virtual bool IsActionActive(InputAction action) const = 0;
    
    /**
     * @brief Get movement input vector
     */
    virtual MovementInput GetMovementInput() const = 0;
    
    /**
     * @brief Remap a key to an action
     * @param key GLFW key code (e.g., GLFW_KEY_W)
     * @param action Target InputAction
     */
    virtual void RemapKey(int glfw_key, InputAction action) = 0;
    
    /**
     * @brief Clear all keybindings and reset to defaults
     */
    virtual void ResetToDefaultBindings() = 0;
    
    /**
     * @brief Set gamepad deadzone for analog sticks
     */
    virtual void SetGamepadDeadzone(float deadzone) = 0;
    
    /**
     * @brief Check if gamepad is connected
     */
    virtual bool IsGamepadConnected(int gamepad_id = 0) const = 0;
    
    /**
     * @brief Factory method
     * @param window GLFW window for input polling
     */
    static std::shared_ptr<InputManager> Create(GLFWwindow* window);
};

} // namespace schizo::input
