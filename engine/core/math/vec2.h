#pragma once

#include "math_types.h"
#include <cmath>

namespace gws::math {

// 2D Vector - used for screen space, UV coordinates, 2D physics
// Data layout: [x, y] - 8 bytes, naturally aligned
struct Vec2 {
    float x, y;

    // Constructors
    Vec2() : x(0.0f), y(0.0f) {}
    explicit Vec2(float scalar) : x(scalar), y(scalar) {}
    Vec2(float x, float y) : x(x), y(y) {}

    // Element access
    float& operator[](int index) {
        assert(index >= 0 && index < 2);
        return (&x)[index];
    }

    const float& operator[](int index) const {
        assert(index >= 0 && index < 2);
        return (&x)[index];
    }

    // Unary operators
    Vec2 operator+() const { return *this; }
    Vec2 operator-() const { return Vec2(-x, -y); }

    // Arithmetic operators
    Vec2& operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vec2& operator-=(const Vec2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vec2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    Vec2& operator/=(float scalar) {
        assert(!approximately_zero(scalar));
        x /= scalar;
        y /= scalar;
        return *this;
    }

    // Binary operators (non-members defined below)
    friend Vec2 operator+(const Vec2& a, const Vec2& b);
    friend Vec2 operator-(const Vec2& a, const Vec2& b);
    friend Vec2 operator*(const Vec2& v, float scalar);
    friend Vec2 operator*(float scalar, const Vec2& v);
    friend Vec2 operator/(const Vec2& v, float scalar);

    // Dot product
    float dot(const Vec2& other) const {
        return x * other.x + y * other.y;
    }

    // Perp dot (cross product in 2D, returns scalar)
    float perp_dot(const Vec2& other) const {
        return x * other.y - y * other.x;
    }

    // Length/magnitude
    float length() const {
        return std::sqrt(x * x + y * y);
    }

    float length_squared() const {
        return x * x + y * y;
    }

    // Normalization
    Vec2 normalized() const {
        float len = length();
        if (approximately_zero(len)) {
            return Vec2(1.0f, 0.0f);  // Fallback direction
        }
        return Vec2(x / len, y / len);
    }

    Vec2& normalize() {
        float len = length();
        if (!approximately_zero(len)) {
            x /= len;
            y /= len;
        }
        return *this;
    }

    // Linear interpolation
    static Vec2 lerp(const Vec2& a, const Vec2& b, float t) {
        return Vec2(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t
        );
    }

    // Comparison
    bool operator==(const Vec2& other) const {
        return approximately_equal(x, other.x) && approximately_equal(y, other.y);
    }

    bool operator!=(const Vec2& other) const {
        return !(*this == other);
    }
};

// Binary operators
inline Vec2 operator+(const Vec2& a, const Vec2& b) {
    return Vec2(a.x + b.x, a.y + b.y);
}

inline Vec2 operator-(const Vec2& a, const Vec2& b) {
    return Vec2(a.x - b.x, a.y - b.y);
}

inline Vec2 operator*(const Vec2& v, float scalar) {
    return Vec2(v.x * scalar, v.y * scalar);
}

inline Vec2 operator*(float scalar, const Vec2& v) {
    return Vec2(v.x * scalar, v.y * scalar);
}

inline Vec2 operator/(const Vec2& v, float scalar) {
    assert(!approximately_zero(scalar));
    return Vec2(v.x / scalar, v.y / scalar);
}

}  // namespace gws::math
