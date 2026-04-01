#pragma once

#include "vec3.h"
#include "vec4.h"
#include <cmath>

namespace gws::math {

// Quaternion - unit quaternion for 3D rotations
// Avoids gimbal lock, enables smooth interpolation (slerp)
// Data layout: [x, y, z, w] where w is the scalar part
// Stored as Vec4 internally for convenience with SIMD and matrix operations
// Note: Always keep normalized (length = 1) for valid rotations
struct Quaternion {
    // Store as Vec4 (x, y, z, w)
    float x, y, z, w;

    // Constructors
    // Identity rotation (no rotation)
    Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    
    // Explicit constructor with components
    Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    
    // Construct from axis-angle (axis must be normalized)
    // angle in radians
    Quaternion(const Vec3& axis, float angle) {
        float half_angle = angle * 0.5f;
        float sin_half = std::sin(half_angle);
        x = axis.x * sin_half;
        y = axis.y * sin_half;
        z = axis.z * sin_half;
        w = std::cos(half_angle);
    }

    // Construct from Euler angles (pitch, yaw, roll in radians)
    // Order: yaw around Y, pitch around X, roll around Z (Z-X-Y extrinsic = Y-X-Z intrinsic)
    static Quaternion from_euler(float pitch, float yaw, float roll);

    // Element access
    float& operator[](int index) {
        assert(index >= 0 && index < 4);
        return (&x)[index];
    }

    const float& operator[](int index) const {
        assert(index >= 0 && index < 4);
        return (&x)[index];
    }

    // Quaternion multiplication (composition of rotations)
    // q1 * q2 = rotation q1 followed by rotation q2
    Quaternion operator*(const Quaternion& other) const;
    
    // Rotate a vector
    Vec3 operator*(const Vec3& v) const;

    // Scalar multiplication (for scaling before normalization)
    Quaternion& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    friend Quaternion operator*(const Quaternion& q, float scalar);
    friend Quaternion operator*(float scalar, const Quaternion& q);

    // Length/magnitude
    float length() const {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    float length_squared() const {
        return x * x + y * y + z * z + w * w;
    }

    // Normalization - CRITICAL for quaternions to represent valid rotations
    Quaternion normalized() const {
        float len = length();
        if (approximately_zero(len)) {
            return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);  // Identity fallback
        }
        return Quaternion(x / len, y / len, z / len, w / len);
    }

    Quaternion& normalize() {
        float len = length();
        if (!approximately_zero(len)) {
            x /= len;
            y /= len;
            z /= len;
            w /= len;
        }
        return *this;
    }

    // Conjugate - inverse for unit quaternions (which we always maintain)
    Quaternion conjugate() const {
        return Quaternion(-x, -y, -z, w);
    }

    // Inverse
    Quaternion inverse() const {
        float len_sq = length_squared();
        if (approximately_zero(len_sq)) {
            return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);  // Identity fallback
        }
        return Quaternion(-x / len_sq, -y / len_sq, -z / len_sq, w / len_sq);
    }

    // Dot product (measures angle between two rotations)
    float dot(const Quaternion& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    // Linear interpolation (fast, but not constant velocity)
    static Quaternion lerp(const Quaternion& a, const Quaternion& b, float t) {
        Quaternion result = Quaternion(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        );
        return result.normalized();
    }

    // Normalized linear interpolation (faster than slerp, reasonable for animation)
    static Quaternion nlerp(const Quaternion& a, const Quaternion& b, float t) {
        Quaternion result = Quaternion(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        );
        return result.normalized();
    }

    // Spherical linear interpolation (constant velocity, smooth, most accurate)
    // Use when smooth animation blending is critical
    static Quaternion slerp(const Quaternion& a, const Quaternion& b, float t);

    // Get rotation axis
    Vec3 axis() const;

    // Get rotation angle in radians
    float angle() const {
        return 2.0f * std::acos(clamp(w, -1.0f, 1.0f));
    }

    // Comparison (with floating-point tolerance)
    bool operator==(const Quaternion& other) const {
        return approximately_equal(x, other.x) &&
               approximately_equal(y, other.y) &&
               approximately_equal(z, other.z) &&
               approximately_equal(w, other.w);
    }

    bool operator!=(const Quaternion& other) const {
        return !(*this == other);
    }

    // Static constructors
    static Quaternion identity() { return Quaternion(0.0f, 0.0f, 0.0f, 1.0f); }
    static Quaternion rotation_x(float angle);
    static Quaternion rotation_y(float angle);
    static Quaternion rotation_z(float angle);
};

}  // namespace gws::math
