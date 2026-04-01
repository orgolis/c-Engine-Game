#pragma once

#include "vec3.h"
#include "vec4.h"
#include <cstring>

namespace gws::math {

// 4x4 Matrix - affine transforms (translation + rotation + scale)
// Data layout: column-major [col0, col1, col2, col3] for GPU compatibility
// Layout: 
//   col0.xyz = right vector (local X)
//   col1.xyz = up vector (local Y)
//   col2.xyz = forward vector (local -Z)
//   col3.xyz = position
//   col0.w, col1.w, col2.w, col3.w = perspective row (usually [0, 0, 0, 1] for affine)
struct Mat4 {
    Vec4 col[4];  // Column vectors

    // Constructors
    Mat4() {
        // Identity matrix
        col[0] = Vec4(1.0f, 0.0f, 0.0f, 0.0f);
        col[1] = Vec4(0.0f, 1.0f, 0.0f, 0.0f);
        col[2] = Vec4(0.0f, 0.0f, 1.0f, 0.0f);
        col[3] = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    explicit Mat4(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3) {
        col[0] = c0;
        col[1] = c1;
        col[2] = c2;
        col[3] = c3;
    }

    // Column access
    Vec4& operator[](int index) {
        assert(index >= 0 && index < 4);
        return col[index];
    }

    const Vec4& operator[](int index) const {
        assert(index >= 0 && index < 4);
        return col[index];
    }

    // Element access: mat(row, col)
    float& operator()(int row, int col_idx) {
        assert(row >= 0 && row < 4 && col_idx >= 0 && col_idx < 4);
        return col[col_idx][row];
    }

    const float& operator()(int row, int col_idx) const {
        assert(row >= 0 && row < 4 && col_idx >= 0 && col_idx < 4);
        return col[col_idx][row];
    }

    // Get row vector
    Vec4 row(int i) const {
        return Vec4(col[0][i], col[1][i], col[2][i], col[3][i]);
    }

    // Extract position (translation component)
    Vec3 position() const {
        return Vec3(col[3].x, col[3].y, col[3].z);
    }

    // Set position
    void set_position(const Vec3& pos) {
        col[3].x = pos.x;
        col[3].y = pos.y;
        col[3].z = pos.z;
    }

    // Extract basis vectors (rotation/scale)
    Vec3 right() const { return Vec3(col[0].x, col[0].y, col[0].z); }  // X axis
    Vec3 up() const { return Vec3(col[1].x, col[1].y, col[1].z); }     // Y axis
    Vec3 forward() const { return Vec3(-col[2].x, -col[2].y, -col[2].z); }  // -Z axis (right-handed)

    // Matrix multiplication - critical for transform composition
    Mat4 operator*(const Mat4& other) const;
    Vec4 operator*(const Vec4& v) const;
    Vec3 operator*(const Vec3& v) const;  // Assumes w=1 for affine transforms

    // Scalar multiplication
    Mat4& operator*=(float scalar) {
        col[0] *= scalar;
        col[1] *= scalar;
        col[2] *= scalar;
        col[3] *= scalar;
        return *this;
    }

    friend Mat4 operator*(const Mat4& m, float scalar);
    friend Mat4 operator*(float scalar, const Mat4& m);

    // Matrix operations
    float determinant() const;
    Mat4 transpose() const;
    Mat4 inverse() const;

    // Static constructors
    static Mat4 identity() { return Mat4(); }
    static Mat4 translation(const Vec3& t);
    static Mat4 translation(float x, float y, float z);
    static Mat4 scale(const Vec3& s);
    static Mat4 scale(float sx, float sy, float sz);
    static Mat4 rotation_x(float angle);
    static Mat4 rotation_y(float angle);
    static Mat4 rotation_z(float angle);
    static Mat4 rotation_axis(const Vec3& axis, float angle);
    
    // Camera matrices
    static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up);
    
    // Projection matrices
    static Mat4 perspective(float fov_y, float aspect, float near, float far);
    static Mat4 orthographic(float left, float right, float bottom, float top, float near, float far);
    
    // Comparison
    bool operator==(const Mat4& other) const;
    bool operator!=(const Mat4& other) const;
};

}  // namespace gws::math
