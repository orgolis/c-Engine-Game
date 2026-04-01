#pragma once

#include "math_types.h"
#include <cmath>

namespace gws::math {

// 4D Vector - homogeneous coordinates, RGBA colors, quaternion storage
// Data layout: [x, y, z, w] - 16 bytes, naturally aligned at 4-byte boundary
struct Vec4 {
    float x, y, z, w;

    // Constructors
    Vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    explicit Vec4(float scalar) : x(scalar), y(scalar), z(scalar), w(scalar) {}
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    
    // Construct from Vec3 + scalar (common for homogeneous coordinates)
    Vec4(const struct Vec3& xyz, float w = 1.0f);

    // Element access
    float& operator[](int index) {
        assert(index >= 0 && index < 4);
        return (&x)[index];
    }

    const float& operator[](int index) const {
        assert(index >= 0 && index < 4);
        return (&x)[index];
    }

    // Unary operators
    Vec4 operator+() const { return *this; }
    Vec4 operator-() const { return Vec4(-x, -y, -z, -w); }

    // Arithmetic operators
    Vec4& operator+=(const Vec4& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
        return *this;
    }

    Vec4& operator-=(const Vec4& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        w -= other.w;
        return *this;
    }

    Vec4& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    Vec4& operator/=(float scalar) {
        assert(!approximately_zero(scalar));
        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;
        return *this;
    }

    // Binary operators (non-members defined below)
    friend Vec4 operator+(const Vec4& a, const Vec4& b);
    friend Vec4 operator-(const Vec4& a, const Vec4& b);
    friend Vec4 operator*(const Vec4& v, float scalar);
    friend Vec4 operator*(float scalar, const Vec4& v);
    friend Vec4 operator/(const Vec4& v, float scalar);

    // Dot product
    float dot(const Vec4& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;
    }

    // Length/magnitude
    float length() const {
        return std::sqrt(x * x + y * y + z * z + w * w);
    }

    float length_squared() const {
        return x * x + y * y + z * z + w * w;
    }

    // Normalization (critical for quaternions)
    Vec4 normalized() const {
        float len = length();
        if (approximately_zero(len)) {
            return Vec4(0.0f, 0.0f, 0.0f, 1.0f);  // Identity quaternion fallback
        }
        return Vec4(x / len, y / len, z / len, w / len);
    }

    Vec4& normalize() {
        float len = length();
        if (!approximately_zero(len)) {
            x /= len;
            y /= len;
            z /= len;
            w /= len;
        }
        return *this;
    }

    // Linear interpolation
    static Vec4 lerp(const Vec4& a, const Vec4& b, float t) {
        return Vec4(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        );
    }

    // Comparison
    bool operator==(const Vec4& other) const {
        return approximately_equal(x, other.x) && 
               approximately_equal(y, other.y) && 
               approximately_equal(z, other.z) &&
               approximately_equal(w, other.w);
    }

    bool operator!=(const Vec4& other) const {
        return !(*this == other);
    }
};

// Binary operators
inline Vec4 operator+(const Vec4& a, const Vec4& b) {
    return Vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

inline Vec4 operator-(const Vec4& a, const Vec4& b) {
    return Vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}

inline Vec4 operator*(const Vec4& v, float scalar) {
    return Vec4(v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar);
}

inline Vec4 operator*(float scalar, const Vec4& v) {
    return Vec4(v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar);
}

inline Vec4 operator/(const Vec4& v, float scalar) {
    assert(!approximately_zero(scalar));
    return Vec4(v.x / scalar, v.y / scalar, v.z / scalar, v.w / scalar);
}

}  // namespace gws::math
