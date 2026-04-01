#pragma once

#include "math_types.h"
#include <cmath>

namespace gws::math {

// 3D Vector - core type for positions, directions, velocities, normals
// Data layout: [x, y, z, ?] - 12 bytes, naturally aligned
// Note: We don't pad to 16 bytes by default for memory efficiency.
// If SIMD operations prove critical, we can use aligned_alloc for arrays.
struct Vec3 {
    float x, y, z;

    // Constructors
    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    explicit Vec3(float scalar) : x(scalar), y(scalar), z(scalar) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    // Element access
    float& operator[](int index) {
        assert(index >= 0 && index < 3);
        return (&x)[index];
    }

    const float& operator[](int index) const {
        assert(index >= 0 && index < 3);
        return (&x)[index];
    }

    // Unary operators
    Vec3 operator+() const { return *this; }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }

    // Arithmetic operators
    Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vec3& operator-=(const Vec3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    Vec3& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    Vec3& operator/=(float scalar) {
        assert(!approximately_zero(scalar));
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }

    // Binary operators (non-members defined below)
    friend Vec3 operator+(const Vec3& a, const Vec3& b);
    friend Vec3 operator-(const Vec3& a, const Vec3& b);
    friend Vec3 operator*(const Vec3& v, float scalar);
    friend Vec3 operator*(float scalar, const Vec3& v);
    friend Vec3 operator/(const Vec3& v, float scalar);

    // Dot product - fundamental for physics and lighting
    float dot(const Vec3& other) const {
        return x * other.x + y * other.y + z * other.z;
    }

    // Cross product - critical for normals, torque, angular velocity
    // Physics accuracy: order matters, this is a × b
    Vec3 cross(const Vec3& other) const {
        return Vec3(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }

    // Length/magnitude
    float length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    float length_squared() const {
        return x * x + y * y + z * z;
    }

    // Normalization - critical for directions and normals in physics
    Vec3 normalized() const {
        float len = length();
        if (approximately_zero(len)) {
            return Vec3(0.0f, 1.0f, 0.0f);  // Fallback up direction
        }
        return Vec3(x / len, y / len, z / len);
    }

    Vec3& normalize() {
        float len = length();
        if (!approximately_zero(len)) {
            x /= len;
            y /= len;
            z /= len;
        }
        return *this;
    }

    // Fast approximate normalization (for when precision is less critical)
    // Using Newton-Raphson for 1/sqrt(x)
    Vec3 normalized_fast() const {
        float len_sq = length_squared();
        if (approximately_zero(len_sq)) {
            return Vec3(0.0f, 1.0f, 0.0f);
        }
        float inv_len = 1.0f / std::sqrt(len_sq);
        return Vec3(x * inv_len, y * inv_len, z * inv_len);
    }

    // Linear interpolation
    static Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
        return Vec3(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t
        );
    }

    // Spherical linear interpolation (for smooth direction blending)
    // More expensive than lerp, use only when necessary
    static Vec3 slerp(const Vec3& a, const Vec3& b, float t);

    // Comparison
    bool operator==(const Vec3& other) const {
        return approximately_equal(x, other.x) && 
               approximately_equal(y, other.y) && 
               approximately_equal(z, other.z);
    }

    bool operator!=(const Vec3& other) const {
        return !(*this == other);
    }
};

// Binary operators
inline Vec3 operator+(const Vec3& a, const Vec3& b) {
    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

inline Vec3 operator*(const Vec3& v, float scalar) {
    return Vec3(v.x * scalar, v.y * scalar, v.z * scalar);
}

inline Vec3 operator*(float scalar, const Vec3& v) {
    return Vec3(v.x * scalar, v.y * scalar, v.z * scalar);
}

inline Vec3 operator/(const Vec3& v, float scalar) {
    assert(!approximately_zero(scalar));
    return Vec3(v.x / scalar, v.y / scalar, v.z / scalar);
}

// Implementations for more complex operations
inline Vec3 Vec3::slerp(const Vec3& a, const Vec3& b, float t) {
    float dot_product = a.dot(b);
    // Clamp to avoid numerical errors in acos
    dot_product = clamp(dot_product, -1.0f, 1.0f);

    if (dot_product < 0.0f) {
        // Take shorter path
        return Vec3::slerp(a, Vec3(-b.x, -b.y, -b.z), t);
    }

    float theta = std::acos(dot_product);
    float sin_theta = std::sin(theta);

    if (approximately_zero(sin_theta)) {
        // Vectors are nearly parallel
        return Vec3::lerp(a, b, t);
    }

    float weight_a = std::sin((1.0f - t) * theta) / sin_theta;
    float weight_b = std::sin(t * theta) / sin_theta;

    return Vec3(
        weight_a * a.x + weight_b * b.x,
        weight_a * a.y + weight_b * b.y,
        weight_a * a.z + weight_b * b.z
    );
}

}  // namespace gws::math
