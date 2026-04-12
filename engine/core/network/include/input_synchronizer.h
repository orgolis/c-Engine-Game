#pragma once

#include <cstdint>
#include <queue>
#include <vector>

// Forward declaration
namespace engine::character {
    struct InputAction;
}

namespace engine::network {

/**
 * @class InputSynchronizer
 * @brief Buffers and synchronizes input across network
 *
 * Responsibilities:
 * - Buffer local player input with tick numbers
 * - Provide input for a specific tick during replay/rollback
 * - Confirm which input ticks have been acknowledged by server
 * - Detect and handle input loss/duplication
 *
 * Input flow:
 * 1. Local input captured each frame → BufferLocalInput()
 * 2. Input associated with a tick number
 * 3. Sent to server periodically (e.g., every tick)
 * 4. Server responds with confirmation → ConfirmInputTick()
 * 5. During rollback, retrieve historical input → GetInputForTick()
 */
class InputSynchronizer {
public:
    static constexpr int MAX_BUFFERED_INPUTS = 180;  // 3 seconds @ 60Hz

    InputSynchronizer();
    ~InputSynchronizer() = default;

    /**
     * Record local input for current frame
     * Automatically associates with a tick number
     *
     * @param input The input to buffer
     * @param tick_number The network tick this input belongs to
     */
    void BufferLocalInput(const engine::character::InputAction& input, uint64_t tick_number);

    /**
     * Get input for a specific tick (for replay/rollback)
     *
     * @param tick Tick number to retrieve input for
     * @return Input for that tick, or empty input if not found
     */
    engine::character::InputAction GetInputForTick(uint64_t tick) const;

    /**
     * Get the most recent buffered input
     */
    engine::character::InputAction GetLatestInput() const;

    /**
     * Check if input exists for a tick
     */
    bool HasInputForTick(uint64_t tick) const;

    /**
     * Confirm that the server has received and processed input up to this tick
     * Allows cleanup of old input data
     *
     * @param confirmed_tick The last tick the server confirmed
     */
    void ConfirmInputTick(uint64_t confirmed_tick);

    /**
     * Get the last confirmed tick by the server
     */
    uint64_t GetLastConfirmedTick() const { return last_confirmed_tick_; }

    /**
     * Get pending ticks (sent but not yet confirmed)
     */
    std::vector<uint64_t> GetPendingInputTicks() const;

    /**
     * Check if input needs to be resent (sent but not confirmed)
     */
    bool IsInputPending(uint64_t tick) const;

    /**
     * Get number of buffered inputs
     */
    size_t GetBufferedInputCount() const { return input_history_.size(); }

    /**
     * Clear old input data
     */
    void PruneOldInputs(uint64_t keep_from_tick);

    /**
     * Reset synchronizer
     */
    void Reset();

private:
    struct BufferedInput {
        uint64_t tick;
        engine::character::InputAction action;
    };

    std::vector<BufferedInput> input_history_;
    uint64_t last_confirmed_tick_ = 0;

    /**
     * Find input by tick using binary search
     */
    auto FindInputForTick(uint64_t tick);
    auto FindInputForTick(uint64_t tick) const;
};

} // namespace engine::network
