#include "input_manager.h"
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <cstring>
#include <algorithm>

namespace schizo::input {

/**
 * @brief GLFW-based input manager implementation
 */
class GLFWInputManager : public InputManager {
private:
    GLFWwindow* window_;
    
    // Key state tracking (for pressed/held/released detection)
    bool key_state_[GLFW_KEY_LAST + 1] = {};
    bool key_state_prev_[GLFW_KEY_LAST + 1] = {};
    
    // Gamepad state
    float gamepad_deadzone_ = 0.15f;
    float gamepad_state_[6] = {};  // Current analog values
    
    // Key to action mapping
    std::unordered_map<int, InputAction> key_bindings_;
    
    // Input events queue
    std::queue<InputEvent> event_queue_;
    
    // Current input snapshot
    InputSnapshot current_snapshot_;
    
    // Logger
    std::shared_ptr<spdlog::logger> logger_;
    
public:
    explicit GLFWInputManager(GLFWwindow* window)
        : window_(window) {
        logger_ = spdlog::get("engine");
        
        // Initialize default bindings
        ResetToDefaultBindings();
        
        if (logger_) {
            logger_->info("Input system initialized (GLFW)");
        }
    }
    
    void PollInput(double delta_time) override {
        // Clear previous state
        std::memcpy(key_state_prev_, key_state_, sizeof(key_state_));
        
        // Clear event queue
        while (!event_queue_.empty()) {
            event_queue_.pop();
        }
        
        // Poll keyboard
        for (int key = 0; key <= GLFW_KEY_LAST; ++key) {
            bool pressed = glfwGetKey(window_, key) == GLFW_PRESS;
            
            if (key_bindings_.find(key) != key_bindings_.end()) {
                InputAction action = key_bindings_[key];
                
                // Detect state transitions
                if (pressed && !key_state_[key]) {
                    // Pressed this frame
                    event_queue_.push(InputEvent(action, InputState::Pressed, 1.0f, glfwGetTime()));
                } else if (pressed && key_state_[key]) {
                    // Still held
                    event_queue_.push(InputEvent(action, InputState::Held, 1.0f, glfwGetTime()));
                } else if (!pressed && key_state_[key]) {
                    // Released this frame
                    event_queue_.push(InputEvent(action, InputState::Released, 0.0f, glfwGetTime()));
                }
            }
            
            key_state_[key] = pressed;
        }
        
        // Poll gamepad if connected
        if (IsGamepadConnected(0)) {
            PollGamepad(delta_time);
        }
        
        // Build snapshot from current state
        BuildSnapshot(delta_time);
    }
    
    std::unique_ptr<InputEvent> GetNextEvent() override {
        if (event_queue_.empty()) {
            return nullptr;
        }
        
        auto event = std::make_unique<InputEvent>(event_queue_.front());
        event_queue_.pop();
        return event;
    }
    
    InputSnapshot GetSnapshot() override {
        current_snapshot_.frame_number++;
        current_snapshot_.timestamp = glfwGetTime();
        return current_snapshot_;
    }
    
    bool IsActionActive(InputAction action) const override {
        // Check if any key mapped to this action is pressed
        for (const auto& [key, mapped_action] : key_bindings_) {
            if (mapped_action == action && key_state_[key]) {
                return true;
            }
        }
        return false;
    }
    
    MovementInput GetMovementInput() const override {
        MovementInput input;
        
        // Keyboard movement
        if (IsActionActive(InputAction::MoveForward))
            input.forward += 1.0f;
        if (IsActionActive(InputAction::MoveBack))
            input.forward -= 1.0f;
        if (IsActionActive(InputAction::StrafeRight))
            input.strafe += 1.0f;
        if (IsActionActive(InputAction::StrafeLeft))
            input.strafe -= 1.0f;
        
        // Gamepad movement (overwrites if stronger)
        if (gamepad_state_[0] != 0.0f || gamepad_state_[1] != 0.0f) {
            input.forward = gamepad_state_[1];
            input.strafe = gamepad_state_[0];
        }
        
        return input;
    }
    
    void RemapKey(int glfw_key, InputAction action) override {
        key_bindings_[glfw_key] = action;
        logger_->debug("Remapped key {} to action {}", glfw_key, static_cast<int>(action));
    }
    
    void ResetToDefaultBindings() override {
        key_bindings_.clear();
        
        // Movement
        key_bindings_[GLFW_KEY_W] = InputAction::MoveForward;
        key_bindings_[GLFW_KEY_S] = InputAction::MoveBack;
        key_bindings_[GLFW_KEY_A] = InputAction::StrafeLeft;
        key_bindings_[GLFW_KEY_D] = InputAction::StrafeRight;
        
        // Locomotion
        key_bindings_[GLFW_KEY_SPACE] = InputAction::Jump;
        key_bindings_[GLFW_KEY_LEFT_SHIFT] = InputAction::Dash;
        key_bindings_[GLFW_KEY_LEFT_CONTROL] = InputAction::Crouch;
        
        // Abilities (Q, E, F, R = slots 1-4)
        key_bindings_[GLFW_KEY_Q] = InputAction::AbilitySlot1;
        key_bindings_[GLFW_KEY_E] = InputAction::AbilitySlot2;
        key_bindings_[GLFW_KEY_F] = InputAction::AbilitySlot3;
        key_bindings_[GLFW_KEY_R] = InputAction::AbilitySlot4;
        
        // Ultimate
        key_bindings_[GLFW_KEY_T] = InputAction::UltimateAbility;
        
        // Interaction
        key_bindings_[GLFW_KEY_X] = InputAction::Interact;
        key_bindings_[GLFW_KEY_I] = InputAction::OpenInventory;
        key_bindings_[GLFW_KEY_M] = InputAction::OpenMap;
        key_bindings_[GLFW_KEY_P] = InputAction::OpenCharacterSheet;
        
        // Combat
        key_bindings_[GLFW_KEY_L] = InputAction::LockOn;
        key_bindings_[GLFW_KEY_RIGHT_SHIFT] = InputAction::ParryBlock;
        
        // UI/System
        key_bindings_[GLFW_KEY_ESCAPE] = InputAction::Pause;
        key_bindings_[GLFW_KEY_GRAVE_ACCENT] = InputAction::ShowDebug;
        
        logger_->info("Input bindings reset to defaults");
    }
    
    void SetGamepadDeadzone(float deadzone) override {
        gamepad_deadzone_ = std::max(0.0f, std::min(deadzone, 1.0f));
    }
    
    bool IsGamepadConnected(int gamepad_id = 0) const override {
        return glfwJoystickPresent(gamepad_id) == GLFW_TRUE;
    }
    
private:
    /**
     * @brief Poll gamepad input (first connected gamepad)
     */
    void PollGamepad(double delta_time) {
        int button_count;
        const unsigned char* buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1, &button_count);
        
        int axis_count;
        const float* axes = glfwGetJoystickAxes(GLFW_JOYSTICK_1, &axis_count);
        
        if (!buttons || !axes) return;
        
        // Left stick movement (axes 0-1)
        if (axis_count >= 2) {
            gamepad_state_[0] = std::abs(axes[0]) > gamepad_deadzone_ ? axes[0] : 0.0f;
            gamepad_state_[1] = std::abs(axes[1]) > gamepad_deadzone_ ? -axes[1] : 0.0f;  // Invert Y
        }
        
        // Right stick (axes 2-3)
        if (axis_count >= 4) {
            gamepad_state_[2] = std::abs(axes[2]) > gamepad_deadzone_ ? axes[2] : 0.0f;
            gamepad_state_[3] = std::abs(axes[3]) > gamepad_deadzone_ ? -axes[3] : 0.0f;
        }
        
        // Triggers (axes 4-5)
        if (axis_count >= 6) {
            gamepad_state_[4] = (axes[4] + 1.0f) * 0.5f;  // LT: [-1, 1] -> [0, 1]
            gamepad_state_[5] = (axes[5] + 1.0f) * 0.5f;  // RT: [-1, 1] -> [0, 1]
        }
        
        // Gamepad buttons (0-9 are action buttons)
        if (button_count >= 1 && buttons[0] == GLFW_PRESS) {
            if (!current_snapshot_.ability_slot1) {
                event_queue_.push(InputEvent(InputAction::AbilitySlot1, InputState::Pressed));
                current_snapshot_.ability_slot1 = true;
            }
        }
        
        if (button_count >= 2 && buttons[1] == GLFW_PRESS) {
            if (!current_snapshot_.ability_slot2) {
                event_queue_.push(InputEvent(InputAction::AbilitySlot2, InputState::Pressed));
                current_snapshot_.ability_slot2 = true;
            }
        }
        
        if (button_count >= 3 && buttons[2] == GLFW_PRESS) {
            if (!current_snapshot_.ability_slot3) {
                event_queue_.push(InputEvent(InputAction::AbilitySlot3, InputState::Pressed));
                current_snapshot_.ability_slot3 = true;
            }
        }
        
        if (button_count >= 4 && buttons[3] == GLFW_PRESS) {
            if (!current_snapshot_.ultimate) {
                event_queue_.push(InputEvent(InputAction::UltimateAbility, InputState::Pressed));
                current_snapshot_.ultimate = true;
            }
        }
    }
    
    /**
     * @brief Build input snapshot from current state
     */
    void BuildSnapshot(double delta_time) {
        current_snapshot_.timestamp = glfwGetTime();
        current_snapshot_.movement = GetMovementInput();
        
        current_snapshot_.jump = IsActionActive(InputAction::Jump);
        current_snapshot_.dash = IsActionActive(InputAction::Dash);
        current_snapshot_.crouch = IsActionActive(InputAction::Crouch);
        current_snapshot_.ability_slot1 = IsActionActive(InputAction::AbilitySlot1);
        current_snapshot_.ability_slot2 = IsActionActive(InputAction::AbilitySlot2);
        current_snapshot_.ability_slot3 = IsActionActive(InputAction::AbilitySlot3);
        current_snapshot_.ability_slot4 = IsActionActive(InputAction::AbilitySlot4);
        current_snapshot_.ultimate = IsActionActive(InputAction::UltimateAbility);
        current_snapshot_.lock_on = IsActionActive(InputAction::LockOn);
        current_snapshot_.parry = IsActionActive(InputAction::ParryBlock);
    }
};

// Factory implementation
std::shared_ptr<InputManager> InputManager::Create(GLFWwindow* window) {
    return std::make_shared<GLFWInputManager>(window);
}

} // namespace schizo::input
