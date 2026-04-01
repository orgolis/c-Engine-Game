#pragma once

#include <cmath>

namespace gws::math {

// ====================
// Easing Functions - Smooth interpolation for animations
// t ∈ [0, 1], output typically ∈ [0, 1]
// Critical for camera movement, character animation, ability timing
// ====================

// Linear - no easing
inline float ease_linear(float t) {
    return t;
}

// Quadratic easing - (2nd order polynomial)
inline float ease_in_quad(float t) {
    return t * t;
}

inline float ease_out_quad(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}

inline float ease_in_out_quad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
}

// Cubic easing - (3rd order polynomial) - smoother than quadratic
inline float ease_in_cubic(float t) {
    return t * t * t;
}

inline float ease_out_cubic(float t) {
    float t_inv = 1.0f - t;
    return 1.0f - t_inv * t_inv * t_inv;
}

inline float ease_in_out_cubic(float t) {
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    } else {
        float t_inv = 1.0f - t;
        return 1.0f - 4.0f * t_inv * t_inv * t_inv;
    }
}

// Quartic easing - (4th order)
inline float ease_in_quart(float t) {
    return t * t * t * t;
}

inline float ease_out_quart(float t) {
    float t_inv = 1.0f - t;
    return 1.0f - t_inv * t_inv * t_inv * t_inv;
}

// Quintic easing - (5th order) - very smooth
inline float ease_in_quint(float t) {
    return t * t * t * t * t;
}

inline float ease_out_quint(float t) {
    float t_inv = 1.0f - t;
    return 1.0f - t_inv * t_inv * t_inv * t_inv * t_inv;
}

// Sine easing - smooth, natural acceleration
inline float ease_in_sine(float t) {
    return 1.0f - std::cos((t * PI) / 2.0f);
}

inline float ease_out_sine(float t) {
    return std::sin((t * PI) / 2.0f);
}

inline float ease_in_out_sine(float t) {
    return -(std::cos(PI * t) - 1.0f) / 2.0f;
}

// Exponential easing - very fast at the end
inline float ease_in_expo(float t) {
    return approximately_zero(t) ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
}

inline float ease_out_expo(float t) {
    return approximately_equal(t, 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}

// Elastic easing - spring-like oscillation
// Great for impact feedback, dash abilities
inline float ease_in_elastic(float t) {
    if (approximately_zero(t)) return 0.0f;
    if (approximately_equal(t, 1.0f)) return 1.0f;
    
    float c4 = (2.0f * PI) / 3.0f;
    return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
}

inline float ease_out_elastic(float t) {
    if (approximately_zero(t)) return 0.0f;
    if (approximately_equal(t, 1.0f)) return 1.0f;
    
    float c4 = (2.0f * PI) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

// Back easing - overshoot and settle
// Perfect for dash startup/dash impact feel (Wuthering Waves style)
inline float ease_in_back(float t) {
    float c1 = 1.70158f;
    float c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}

inline float ease_out_back(float t) {
    float c1 = 1.70158f;
    float c3 = c1 + 1.0f;
    float t_inv = t - 1.0f;
    return 1.0f + c3 * t_inv * t_inv * t_inv + c1 * t_inv * t_inv;
}

inline float ease_in_out_back(float t) {
    float c1 = 1.70158f;
    float c2 = c1 * 1.525f;
    
    if (t < 0.5f) {
        return (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) / 2.0f;
    } else {
        return (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) / 2.0f;
    }
}

// Bounce easing - falling and bouncing back
inline float ease_out_bounce(float t) {
    float n1 = 7.5625f;
    float d1 = 2.75f;
    
    if (t < 1.0f / d1) {
        return n1 * t * t;
    } else if (t < 2.0f / d1) {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    } else if (t < 2.5f / d1) {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    } else {
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
}

inline float ease_in_bounce(float t) {
    return 1.0f - ease_out_bounce(1.0f - t);
}

// Circular easing - motion based on circle
inline float ease_in_circ(float t) {
    return 1.0f - std::sqrt(1.0f - std::pow(t, 2.0f));
}

inline float ease_out_circ(float t) {
    return std::sqrt(1.0f - std::pow(t - 1.0f, 2.0f));
}

inline float ease_in_out_circ(float t) {
    if (t < 0.5f) {
        return (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) / 2.0f;
    } else {
        return (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
    }
}

}  // namespace gws::math
