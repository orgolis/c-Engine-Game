#include "input_synchronizer.h"
#include "input_buffer.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <cstdint>

namespace engine::network {

InputSynchronizer::InputSynchronizer() {
    input_history_.reserve(MAX_BUFFERED_INPUTS);
}

// ============ Helper Methods (defined before use) ============

auto InputSynchronizer::FindInputForTick(uint64_t tick) {
    return std::find_if(input_history_.begin(), input_history_.end(),
        [tick](const BufferedInput& bi) { return bi.tick == tick; });
}

auto InputSynchronizer::FindInputForTick(uint64_t tick) const {
    return std::find_if(input_history_.begin(), input_history_.end(),
        [tick](const BufferedInput& bi) { return bi.tick == tick; });
}

// ============ Public Interface ============

void InputSynchronizer::BufferLocalInput(const engine::character::InputAction& input, uint64_t tick_number) {
    // Check if we're inserting in order (should be monotonically increasing)
    if (!input_history_.empty() && tick_number <= input_history_.back().tick) {
        auto logger = spdlog::get("engine");
        if (logger) {
            logger->warn("InputSynchronizer: Out of order input tick {} (last: {})",
                        tick_number, input_history_.back().tick);
        }
        return;
    }

    input_history_.push_back({tick_number, input});

    // Remove oldest inputs if buffer is full
    if (input_history_.size() > MAX_BUFFERED_INPUTS) {
        input_history_.erase(input_history_.begin());
    }
}

engine::character::InputAction InputSynchronizer::GetInputForTick(uint64_t tick) const {
    auto it = FindInputForTick(tick);
    if (it != input_history_.end()) {
        return it->action;
    }
    return engine::character::InputAction();  // Return empty input
}

engine::character::InputAction InputSynchronizer::GetLatestInput() const {
    if (input_history_.empty()) {
        return engine::character::InputAction();
    }
    return input_history_.back().action;
}

bool InputSynchronizer::HasInputForTick(uint64_t tick) const {
    return FindInputForTick(tick) != input_history_.end();
}

void InputSynchronizer::ConfirmInputTick(uint64_t confirmed_tick) {
    if (confirmed_tick > last_confirmed_tick_) {
        last_confirmed_tick_ = confirmed_tick;
    }
}

std::vector<uint64_t> InputSynchronizer::GetPendingInputTicks() const {
    std::vector<uint64_t> pending;
    for (const auto& buffered : input_history_) {
        if (buffered.tick > last_confirmed_tick_) {
            pending.push_back(buffered.tick);
        }
    }
    return pending;
}

bool InputSynchronizer::IsInputPending(uint64_t tick) const {
    return tick > last_confirmed_tick_ && HasInputForTick(tick);
}

void InputSynchronizer::PruneOldInputs(uint64_t keep_from_tick) {
    auto it = std::remove_if(input_history_.begin(), input_history_.end(),
        [keep_from_tick](const BufferedInput& bi) { return bi.tick < keep_from_tick; });
    input_history_.erase(it, input_history_.end());
}

void InputSynchronizer::Reset() {
    input_history_.clear();
    last_confirmed_tick_ = 0;
}

} // namespace engine::network
