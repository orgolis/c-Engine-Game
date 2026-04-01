#pragma once

#include "vec3.h"
#include <cstring>

namespace gws::math {

// 3x3 Matrix - for rotations and 3D linear transforms
// Data layout: column-major [col0, col1, col2] for GPU compatibility
// Each column is a Vec3, so layout is [x0, y0, z0, x1, y1, z1, x2, y2, z2]
struct Mat3 {
    Vec3 col[3];  // Column vectors

    // Constructors
    Mat3() {
        // Identity matrix
        col[0] = Vec3(1.0f, 0.0f, 0.0f);
        col[1] = Vec3(0.0f, 1.0f, 0.0f);
        col[2] = Vec3(0.0f, 0.0f, 1.0f);
    }

    explicit Mat3(const Vec3& c0, const Vec3& c1, const Vec3& c2) {
        col[0] = c0;
        col[1] = c1;
        col[2] = c2;
    }

    // Construct from a 4x4 matrix (take upper-left 3x3)
    explicit Mat3(const struct Mat4& m);

    // Column access
    Vec3& operator[](int index) {
        assert(index >= 0 && index < 3);
        return col[index];
    }

    const Vec3& operator[](int index) const {
        assert(index >= 0 && index < 3);
        return col[index];
    }

    // Element access: mat(row, col)
    float& operator()(int row, int col_idx) {
        assert(row >= 0 && row < 3 && col_idx >= 0 && col_idx < 3);
        return col[col_idx][row];
    }

    const float& operator()(int row, int col_idx) const {
        assert(row >= 0 && row < 3 && col_idx >= 0 && col_idx < 3);
        return col[col_idx][row];
    }

    // Get row vector
    Vec3 row(int i) const {
        return Vec3(col[0][i], col[1][i], col[2][i]);
    }

    // Matrix multiplication
    Mat3 operator*(const Mat3& other) const;
    Vec3 operator*(const Vec3& v) const;

    // Scalar multiplication
    Mat3& operator*=(float scalar) {
        col[0] *= scalar;
        col[1] *= scalar;
        col[2] *= scalar;
        return *this;
    }

    friend Mat3 operator*(const Mat3& m, float scalar);
    friend Mat3 operator*(float scalar, const Mat3& m);

    // Matrix operations
    float determinant() const;
    Mat3 transpose() const;
    Mat3 inverse() const;

    // Static constructors
    static Mat3 identity() { return Mat3(); }
    static Mat3 scale(float sx, float sy, float sz);
    static Mat3 rotation_x(float angle);
    static Mat3 rotation_y(float angle);
    static Mat3 rotation_z(float angle);
    
    // Comparison
    bool operator==(const Mat3& other) const;
    bool operator!=(const Mat3& other) const;
};

}  // namespace gws::math
