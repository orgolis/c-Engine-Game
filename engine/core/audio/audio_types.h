#pragma once

// ====================
// Audio core types (Stage 6 — Pillar G)
// ====================
//
// POD types shared across the mixer / engine / ECS bridge. Kept free of any
// backend (miniaudio) type so the mixer is testable headless and the public
// API never leaks the device library.

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace gws::audio {

using ClipId  = uint32_t;   // handle to a resident decoded clip (0 = invalid)
using VoiceId = uint32_t;   // handle to a playing instance     (0 = invalid)

inline constexpr ClipId  kInvalidClip  = 0;
inline constexpr VoiceId kInvalidVoice = 0;

// The mixer always produces this format (the device matches it).
struct AudioFormat {
    uint32_t sample_rate = 48000;
    uint32_t channels    = 2;     // output channels (stereo)
};

// Decoded, resident PCM — interleaved float in [-1,1], `channels` at `sample_rate`.
struct Clip {
    std::vector<float> samples;       // interleaved
    uint32_t           channels    = 1;
    uint32_t           sample_rate = 48000;
    uint64_t frames() const { return channels ? static_cast<uint64_t>(samples.size()) / channels : 0; }
};

// Per-voice playback parameters (game thread → mixer via the command queue).
struct VoiceParams {
    float     gain    = 1.0f;
    float     pitch   = 1.0f;         // playback-rate multiplier (1 = native)
    bool      looping = false;
    bool      spatial = false;        // distance/pan applied when true (Step 3)
    glm::vec3 position{0.0f};         // world position (spatial voices)
    glm::vec3 velocity{0.0f};         // world velocity (Doppler, Step 3)
    float     radius  = 10.0f;        // distance at which the voice fades out
    float     occlusion = 0.0f;       // 0 clear … 1 fully blocked (lowpass + duck, Step 6)
};

// Listener pose (game thread → mixer). Position/orientation come from the
// listener entity's transform; master_gain is the global volume.
struct Listener {
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    float     master_gain = 1.0f;
};

}  // namespace gws::audio
