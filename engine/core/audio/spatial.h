#pragma once

// ====================
// Spatialisation (Stage 6 — Pillar G2)
// ====================
//
// Turns a source's world position/velocity + the listener pose into per-channel
// gains (distance attenuation + stereo pan) and a Doppler pitch multiplier. Pure
// function of its inputs, so the mixer calls it once per voice per block and the
// check tool can verify the math directly. No HRTF yet — constant-power panning
// (a good stereo approximation); HRTF can drop in here later behind the same
// interface.

#include "audio/audio_types.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace gws::audio {

struct Spatialization {
    float left_gain   = 1.0f;   // distance attenuation folded into the L pan gain
    float right_gain  = 1.0f;
    float pitch_scale = 1.0f;   // Doppler multiplier (1 = none)
};

// `radius` = distance at which the source fades to silence (linear rolloff).
inline Spatialization compute_spatial(const glm::vec3& src_pos,
                                      const glm::vec3& src_vel,
                                      float radius,
                                      const Listener& l) {
    Spatialization s;
    const glm::vec3 to_src = src_pos - l.position;
    const float     dist   = glm::length(to_src);

    // Distance attenuation: 1 at the listener → 0 at `radius`.
    const float att = (radius > 0.0f) ? std::clamp(1.0f - dist / radius, 0.0f, 1.0f) : 1.0f;

    // Pan: project the source direction onto the listener's right vector.
    float pan = 0.0f;   // -1 fully left … +1 fully right
    if (dist > 1e-4f) {
        const glm::vec3 dir   = to_src / dist;
        const glm::vec3 right = glm::normalize(glm::cross(l.forward, l.up));
        pan = std::clamp(glm::dot(dir, right), -1.0f, 1.0f);
    }
    // Constant-power pan law: L²+R² is constant across the stereo field.
    const float theta = (pan + 1.0f) * 0.5f * glm::half_pi<float>();   // 0..π/2
    s.left_gain  = std::cos(theta) * att;
    s.right_gain = std::sin(theta) * att;

    // Doppler: a source approaching the listener raises pitch. c = speed of sound.
    constexpr float c = 343.0f;
    if (dist > 1e-4f) {
        const glm::vec3 dir_s2l = -to_src / dist;                 // source → listener
        const float v_src = glm::dot(src_vel, dir_s2l);           // source speed toward listener (+)
        const float v_lis = glm::dot(l.velocity, -dir_s2l);       // listener speed toward source (+)
        const float denom = c - v_src;
        if (denom > 1.0f)
            s.pitch_scale = std::clamp((c + v_lis) / denom, 0.5f, 2.0f);
    }
    return s;
}

}  // namespace gws::audio
