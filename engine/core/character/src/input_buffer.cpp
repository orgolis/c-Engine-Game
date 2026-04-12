#include "input_buffer.h"
#include <spdlog/spdlog.h>

namespace engine::character {

void InputBuffer::BufferInput(const InputAction& input) {
    if (buffer_.size() >= BUFFER_SIZE) {
        auto logger = spdlog::get("engine");
        if (logger) {
            logger->warn("InputBuffer full, dropping oldest input");
        }
        buffer_.pop();  // Remove oldest
    }
    buffer_.push(input);
}

InputAction InputBuffer::GetNextInput() {
    if (buffer_.empty()) {
        return InputAction();  // Return default input
    }
    InputAction action = buffer_.front();
    buffer_.pop();
    return action;
}

bool InputBuffer::HasInput() const {
    return !buffer_.empty();
}

void InputBuffer::Clear() {
    while (!buffer_.empty()) {
        buffer_.pop();
    }
}

InputAction InputBuffer::PeekNextInput() const {
    if (buffer_.empty()) {
        return InputAction();
    }
    return buffer_.front();
}

} // namespace engine::character
