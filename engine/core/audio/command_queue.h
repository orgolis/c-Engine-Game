#pragma once

// ====================
// Audio command queue (Stage 6 — Pillar G1)
// ====================
//
// Single-producer (game thread) / single-consumer (audio thread) ring buffer.
// Lock-free AND allocation-free, so the real-time audio thread drains it at the
// top of every block without ever blocking the game thread or being blocked by
// it. The game thread only ever push()es; the audio thread only ever pop()s.

#include "audio/audio_types.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace gws::audio {

struct Command {
    enum class Type : uint8_t { Play, Stop, SetVoice, SetListener, SetOcclusion, SetBus };
    Type        type     = Type::Play;
    VoiceId     voice    = kInvalidVoice;
    ClipId      clip     = kInvalidClip;
    float       scalar   = 0.0f;        // single-value payload (e.g. occlusion)
    VoiceParams params;
    Listener    listener;
    // SetBus payload. Deliberately the bus FIELDS and not a Bus struct: Bus
    // carries a std::string name, and a ring buffer the audio thread drains
    // must stay trivially copyable and allocation-free. Names are set at setup
    // time (like add_clip), never from a command.
    BusId       bus       = kInvalidBus;
    float       bus_gain  = 1.0f;
    bool        bus_mute  = false;
    bool        bus_solo  = false;
};

class CommandQueue {
public:
    // Producer (game thread). Returns false if the ring is full (command dropped
    // rather than blocking the caller — never stalls gameplay).
    bool push(const Command& c) {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) return false;  // full
        buffer_[head] = c;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer (audio thread). Returns false when empty.
    bool pop(Command& out) {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;  // empty
        out = buffer_[tail];
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kCapacity = 1024;            // power of two
    static constexpr uint32_t kMask     = kCapacity - 1;
    std::array<Command, kCapacity> buffer_{};
    std::atomic<uint32_t>          head_{0};   // producer cursor
    std::atomic<uint32_t>          tail_{0};   // consumer cursor
};

}  // namespace gws::audio
