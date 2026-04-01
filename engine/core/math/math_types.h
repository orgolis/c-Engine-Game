#pragma once

#include <cmath>
#include <limits>
#include <cassert>

namespace gws::math {

// Constants
constexpr float PI = 3.14159265359f;
constexpr float TAU = 6.28318530718f;
constexpr float EPSILON = 1e-6f;
constexpr float EPSILON_LOOSE = 1e-4f;  // For less strict comparisons

// Utility functions
inline float radians(float degrees) {
    return degrees * (PI / 180.0f);
}

inline float degrees(float radians) {
    return radians * (180.0f / PI);
}

// Safe float comparison accounting for floating-point precision
inline bool approximately_equal(float a, float b, float epsilon = EPSILON) {
    return std::abs(a - b) < epsilon;
}

// Safe zero check
inline bool approximately_zero(float value, float epsilon = EPSILON) {
    return std::abs(value) < epsilon;
}

// Clamping
inline float clamp(float value, float min, float max) {
    return (value < min) ? min : (value > max) ? max : value;
}

// Smoothstep (useful for animations)
inline float smoothstep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Linear interpolation
inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

}  // namespace gws::math
