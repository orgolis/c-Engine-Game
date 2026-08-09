#pragma once

#include <cstdint>
#include <queue>
#include <glm/glm.hpp>

namespace engine::character {

/**
 * @struct InputAction
 * @brief Represents a single frame's input from the player
 */
struct InputAction {
    // Movement input
    float forward = 0.0f;    // W/Up arrow: 1.0, S/Down arrow: -1.0
    float lateral = 0.0f;    // A/Left arrow: -1.0, D/Right arrow: 1.0

    // Action inputs
    bool jump = false;       // Space bar
    bool sprint = false;     // Shift
    bool crouch = false;     // Ctrl
    bool interact = false;   // E
    bool dash = false;       // Double tap or dedicated key

    // Combat/Ability
    uint8_t ability1_pressed = 0;  // Mouse click 1
    uint8_t ability2_pressed = 0;  // Mouse click 2
    uint8_t ability3_pressed = 0;  // Q
    uint8_t ability4_pressed = 0;  // E

    // Aim direction (for ranged abilities)
    glm::vec3 aim_direction = glm::vec3(0, 0, 1);

    // Timestamp for network synchronization
    uint64_t frame_number = 0;

    InputAction() = default;
};

/**
 * @class InputBuffer
 * @brief Buffers input actions to handle input lag and ensure input doesn't get lost
 *
 * This class maintains a circular buffer of recent input frames. This is critical for:
 * - Handling animation cancels
 * - Network synchronization (player's input history)
 * - Robust input handling in networked games
 *
 * Typical usage:
 * ```cpp
 * InputBuffer buffer;
 * buffer.BufferInput(input_from_keyboad);
 * InputAction next_action = buffer.GetNextInput();
 * ```
 */
class InputBuffer {
public:
    static constexpr int BUFFER_SIZE = 6;  // ~100ms @ 60Hz

    InputBuffer() = default;
    ~InputBuffer() = default;

    /**
     * Add a new input action to the buffer
     * @param input The input action to buffer
     */
    void BufferInput(const InputAction& input);

    /**
     * Get the next input action from the buffer
     * @return The oldest buffered input
     */
    InputAction GetNextInput();

    /**
     * Check if there are buffered inputs available
     * @return true if buffer has inputs
     */
    bool HasInput() const;

    /**
     * Get the size of the buffer
     * @return Number of currently buffered actions
     */
    size_t GetBufferSize() const { return buffer_.size(); }

    /**
     * Clear all buffered inputs
     */
    void Clear();

    /**
     * Get the oldest input without removing it from buffer
     */
    InputAction PeekNextInput() const;

private:
    std::queue<InputAction> buffer_;
};

} // namespace engine::character
