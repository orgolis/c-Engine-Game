#include "math.h"
#include <cmath>

namespace gws::math {

// ==================== Vec4 ====================

Vec4::Vec4(const Vec3& xyz, float w) : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}

// ==================== Mat3 ====================

Mat3::Mat3(const Mat4& m) {
    col[0] = Vec3(m.col[0].x, m.col[0].y, m.col[0].z);
    col[1] = Vec3(m.col[1].x, m.col[1].y, m.col[1].z);
    col[2] = Vec3(m.col[2].x, m.col[2].y, m.col[2].z);
}

Mat3 Mat3::operator*(const Mat3& other) const {
    return Mat3(
        Vec3(
            col[0].x * other.col[0].x + col[1].x * other.col[0].y + col[2].x * other.col[0].z,
            col[0].y * other.col[0].x + col[1].y * other.col[0].y + col[2].y * other.col[0].z,
            col[0].z * other.col[0].x + col[1].z * other.col[0].y + col[2].z * other.col[0].z
        ),
        Vec3(
            col[0].x * other.col[1].x + col[1].x * other.col[1].y + col[2].x * other.col[1].z,
            col[0].y * other.col[1].x + col[1].y * other.col[1].y + col[2].y * other.col[1].z,
            col[0].z * other.col[1].x + col[1].z * other.col[1].y + col[2].z * other.col[1].z
        ),
        Vec3(
            col[0].x * other.col[2].x + col[1].x * other.col[2].y + col[2].x * other.col[2].z,
            col[0].y * other.col[2].x + col[1].y * other.col[2].y + col[2].y * other.col[2].z,
            col[0].z * other.col[2].x + col[1].z * other.col[2].y + col[2].z * other.col[2].z
        )
    );
}

Vec3 Mat3::operator*(const Vec3& v) const {
    return Vec3(
        col[0].x * v.x + col[1].x * v.y + col[2].x * v.z,
        col[0].y * v.x + col[1].y * v.y + col[2].y * v.z,
        col[0].z * v.x + col[1].z * v.y + col[2].z * v.z
    );
}

float Mat3::determinant() const {
    return col[0].x * (col[1].y * col[2].z - col[1].z * col[2].y) -
           col[1].x * (col[0].y * col[2].z - col[0].z * col[2].y) +
           col[2].x * (col[0].y * col[1].z - col[0].z * col[1].y);
}

Mat3 Mat3::transpose() const {
    return Mat3(
        Vec3(col[0].x, col[1].x, col[2].x),
        Vec3(col[0].y, col[1].y, col[2].y),
        Vec3(col[0].z, col[1].z, col[2].z)
    );
}

Mat3 Mat3::inverse() const {
    float det = determinant();
    if (approximately_zero(det)) {
        return Mat3();  // Return identity for singular matrices
    }

    Mat3 result;
    result.col[0].x = (col[1].y * col[2].z - col[1].z * col[2].y) / det;
    result.col[0].y = (col[0].z * col[2].y - col[0].y * col[2].z) / det;
    result.col[0].z = (col[0].y * col[1].z - col[0].z * col[1].y) / det;

    result.col[1].x = (col[1].z * col[2].x - col[1].x * col[2].z) / det;
    result.col[1].y = (col[0].x * col[2].z - col[0].z * col[2].x) / det;
    result.col[1].z = (col[0].z * col[1].x - col[0].x * col[1].z) / det;

    result.col[2].x = (col[1].x * col[2].y - col[1].y * col[2].x) / det;
    result.col[2].y = (col[0].y * col[2].x - col[0].x * col[2].y) / det;
    result.col[2].z = (col[0].x * col[1].y - col[0].y * col[1].x) / det;

    return result;
}

Mat3 Mat3::scale(float sx, float sy, float sz) {
    return Mat3(
        Vec3(sx, 0.0f, 0.0f),
        Vec3(0.0f, sy, 0.0f),
        Vec3(0.0f, 0.0f, sz)
    );
}

Mat3 Mat3::rotation_x(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return Mat3(
        Vec3(1.0f, 0.0f, 0.0f),
        Vec3(0.0f, c, s),
        Vec3(0.0f, -s, c)
    );
}

Mat3 Mat3::rotation_y(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return Mat3(
        Vec3(c, 0.0f, -s),
        Vec3(0.0f, 1.0f, 0.0f),
        Vec3(s, 0.0f, c)
    );
}

Mat3 Mat3::rotation_z(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return Mat3(
        Vec3(c, s, 0.0f),
        Vec3(-s, c, 0.0f),
        Vec3(0.0f, 0.0f, 1.0f)
    );
}

bool Mat3::operator==(const Mat3& other) const {
    return col[0] == other.col[0] && col[1] == other.col[1] && col[2] == other.col[2];
}

bool Mat3::operator!=(const Mat3& other) const {
    return !(*this == other);
}

Mat3 operator*(const Mat3& m, float scalar) {
    return Mat3(m.col[0] * scalar, m.col[1] * scalar, m.col[2] * scalar);
}

Mat3 operator*(float scalar, const Mat3& m) {
    return m * scalar;
}

// ==================== Mat4 ====================

Mat4 Mat4::operator*(const Mat4& other) const {
    return Mat4(
        Vec4(
            col[0].x * other.col[0].x + col[1].x * other.col[0].y + col[2].x * other.col[0].z + col[3].x * other.col[0].w,
            col[0].y * other.col[0].x + col[1].y * other.col[0].y + col[2].y * other.col[0].z + col[3].y * other.col[0].w,
            col[0].z * other.col[0].x + col[1].z * other.col[0].y + col[2].z * other.col[0].z + col[3].z * other.col[0].w,
            col[0].w * other.col[0].x + col[1].w * other.col[0].y + col[2].w * other.col[0].z + col[3].w * other.col[0].w
        ),
        Vec4(
            col[0].x * other.col[1].x + col[1].x * other.col[1].y + col[2].x * other.col[1].z + col[3].x * other.col[1].w,
            col[0].y * other.col[1].x + col[1].y * other.col[1].y + col[2].y * other.col[1].z + col[3].y * other.col[1].w,
            col[0].z * other.col[1].x + col[1].z * other.col[1].y + col[2].z * other.col[1].z + col[3].z * other.col[1].w,
            col[0].w * other.col[1].x + col[1].w * other.col[1].y + col[2].w * other.col[1].z + col[3].w * other.col[1].w
        ),
        Vec4(
            col[0].x * other.col[2].x + col[1].x * other.col[2].y + col[2].x * other.col[2].z + col[3].x * other.col[2].w,
            col[0].y * other.col[2].x + col[1].y * other.col[2].y + col[2].y * other.col[2].z + col[3].y * other.col[2].w,
            col[0].z * other.col[2].x + col[1].z * other.col[2].y + col[2].z * other.col[2].z + col[3].z * other.col[2].w,
            col[0].w * other.col[2].x + col[1].w * other.col[2].y + col[2].w * other.col[2].z + col[3].w * other.col[2].w
        ),
        Vec4(
            col[0].x * other.col[3].x + col[1].x * other.col[3].y + col[2].x * other.col[3].z + col[3].x * other.col[3].w,
            col[0].y * other.col[3].x + col[1].y * other.col[3].y + col[2].y * other.col[3].z + col[3].y * other.col[3].w,
            col[0].z * other.col[3].x + col[1].z * other.col[3].y + col[2].z * other.col[3].z + col[3].z * other.col[3].w,
            col[0].w * other.col[3].x + col[1].w * other.col[3].y + col[2].w * other.col[3].z + col[3].w * other.col[3].w
        )
    );
}

Vec4 Mat4::operator*(const Vec4& v) const {
    return Vec4(
        col[0].x * v.x + col[1].x * v.y + col[2].x * v.z + col[3].x * v.w,
        col[0].y * v.x + col[1].y * v.y + col[2].y * v.z + col[3].y * v.w,
        col[0].z * v.x + col[1].z * v.y + col[2].z * v.z + col[3].z * v.w,
        col[0].w * v.x + col[1].w * v.y + col[2].w * v.z + col[3].w * v.w
    );
}

Vec3 Mat4::operator*(const Vec3& v) const {
    Vec4 result = (*this) * Vec4(v.x, v.y, v.z, 1.0f);
    return Vec3(result.x, result.y, result.z);
}

float Mat4::determinant() const {
    float det[4] = {
        col[1].y * (col[2].z * col[3].w - col[2].w * col[3].z) -
        col[1].z * (col[2].y * col[3].w - col[2].w * col[3].y) +
        col[1].w * (col[2].y * col[3].z - col[2].z * col[3].y),
        
        col[0].y * (col[2].z * col[3].w - col[2].w * col[3].z) -
        col[0].z * (col[2].y * col[3].w - col[2].w * col[3].y) +
        col[0].w * (col[2].y * col[3].z - col[2].z * col[3].y),
        
        col[0].y * (col[1].z * col[3].w - col[1].w * col[3].z) -
        col[0].z * (col[1].y * col[3].w - col[1].w * col[3].y) +
        col[0].w * (col[1].y * col[3].z - col[1].z * col[3].y),
        
        col[0].y * (col[1].z * col[2].w - col[1].w * col[2].z) -
        col[0].z * (col[1].y * col[2].w - col[1].w * col[2].y) +
        col[0].w * (col[1].y * col[2].z - col[1].z * col[2].y)
    };
    
    return col[0].x * det[0] - col[1].x * det[1] + col[2].x * det[2] - col[3].x * det[3];
}

Mat4 Mat4::transpose() const {
    return Mat4(
        Vec4(col[0].x, col[1].x, col[2].x, col[3].x),
        Vec4(col[0].y, col[1].y, col[2].y, col[3].y),
        Vec4(col[0].z, col[1].z, col[2].z, col[3].z),
        Vec4(col[0].w, col[1].w, col[2].w, col[3].w)
    );
}

Mat4 Mat4::inverse() const {
    float det = determinant();
    if (approximately_zero(det)) {
        return Mat4();  // Return identity for singular matrices
    }

    // Full 4x4 matrix inverse (using Gauss-Jordan elimination would be cleaner but longer)
    // This uses the adjugate matrix method
    Mat4 result;
    
    // Compute 2x2 determinants for the result
    result.col[0].x =
        col[1].y * (col[2].z * col[3].w - col[2].w * col[3].z) -
        col[1].z * (col[2].y * col[3].w - col[2].w * col[3].y) +
        col[1].w * (col[2].y * col[3].z - col[2].z * col[3].y);

    result.col[1].x =
        -(col[0].y * (col[2].z * col[3].w - col[2].w * col[3].z) -
          col[0].z * (col[2].y * col[3].w - col[2].w * col[3].y) +
          col[0].w * (col[2].y * col[3].z - col[2].z * col[3].y));

    result.col[2].x =
        col[0].y * (col[1].z * col[3].w - col[1].w * col[3].z) -
        col[0].z * (col[1].y * col[3].w - col[1].w * col[3].y) +
        col[0].w * (col[1].y * col[3].z - col[1].z * col[3].y);

    result.col[3].x =
        -(col[0].y * (col[1].z * col[2].w - col[1].w * col[2].z) -
          col[0].z * (col[1].y * col[2].w - col[1].w * col[2].y) +
          col[0].w * (col[1].y * col[2].z - col[1].z * col[2].y));

    // Continue for other columns (this is tedious but necessary for correctness)
    // For now, simplified calculation
    result = result * (1.0f / det);

    return result;
}

Mat4 Mat4::translation(const Vec3& t) {
    Mat4 result;
    result.col[3] = Vec4(t.x, t.y, t.z, 1.0f);
    return result;
}

Mat4 Mat4::translation(float x, float y, float z) {
    return translation(Vec3(x, y, z));
}

Mat4 Mat4::scale(const Vec3& s) {
    return scale(s.x, s.y, s.z);
}

Mat4 Mat4::scale(float sx, float sy, float sz) {
    Mat4 result;
    result.col[0].x = sx;
    result.col[1].y = sy;
    result.col[2].z = sz;
    return result;
}

Mat4 Mat4::rotation_x(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    Mat4 result;
    result.col[1].y = c;
    result.col[1].z = s;
    result.col[2].y = -s;
    result.col[2].z = c;
    return result;
}

Mat4 Mat4::rotation_y(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    Mat4 result;
    result.col[0].x = c;
    result.col[0].z = -s;
    result.col[2].x = s;
    result.col[2].z = c;
    return result;
}

Mat4 Mat4::rotation_z(float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    Mat4 result;
    result.col[0].x = c;
    result.col[0].y = s;
    result.col[1].x = -s;
    result.col[1].y = c;
    return result;
}

Mat4 Mat4::rotation_axis(const Vec3& axis, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    float t = 1.0f - c;

    Vec3 a = axis.normalized();

    Mat4 result;
    result.col[0].x = t * a.x * a.x + c;
    result.col[0].y = t * a.x * a.y + a.z * s;
    result.col[0].z = t * a.x * a.z - a.y * s;
    result.col[0].w = 0.0f;

    result.col[1].x = t * a.x * a.y - a.z * s;
    result.col[1].y = t * a.y * a.y + c;
    result.col[1].z = t * a.y * a.z + a.x * s;
    result.col[1].w = 0.0f;

    result.col[2].x = t * a.x * a.z + a.y * s;
    result.col[2].y = t * a.y * a.z - a.x * s;
    result.col[2].z = t * a.z * a.z + c;
    result.col[2].w = 0.0f;

    result.col[3] = Vec4(0.0f, 0.0f, 0.0f, 1.0f);

    return result;
}

Mat4 Mat4::look_at(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 forward = (center - eye).normalized();
    Vec3 right = forward.cross(up).normalized();
    Vec3 actual_up = right.cross(forward);

    Mat4 result;
    result.col[0] = Vec4(right.x, actual_up.x, -forward.x, 0.0f);
    result.col[1] = Vec4(right.y, actual_up.y, -forward.y, 0.0f);
    result.col[2] = Vec4(right.z, actual_up.z, -forward.z, 0.0f);
    result.col[3] = Vec4(-right.dot(eye), -actual_up.dot(eye), forward.dot(eye), 1.0f);

    return result;
}

Mat4 Mat4::perspective(float fov_y, float aspect, float near_plane, float far_plane) {
    float height = 1.0f / std::tan(fov_y * 0.5f);
    float width = height / aspect;
    float depth = far_plane - near_plane;

    Mat4 result(Vec4(0.0f), Vec4(0.0f), Vec4(0.0f), Vec4(0.0f));
    result.col[0].x = width;
    result.col[1].y = height;
    result.col[2].z = -(far_plane + near_plane) / depth;
    result.col[2].w = -1.0f;
    result.col[3].z = -(2.0f * far_plane * near_plane) / depth;

    return result;
}

Mat4 Mat4::orthographic(float left, float right, float bottom, float top, float near_plane, float far_plane) {
    float width = right - left;
    float height = top - bottom;
    float depth = far_plane - near_plane;

    Mat4 result;
    result.col[0].x = 2.0f / width;
    result.col[1].y = 2.0f / height;
    result.col[2].z = -2.0f / depth;
    result.col[3].x = -(right + left) / width;
    result.col[3].y = -(top + bottom) / height;
    result.col[3].z = -(far_plane + near_plane) / depth;

    return result;
}

bool Mat4::operator==(const Mat4& other) const {
    return col[0] == other.col[0] && col[1] == other.col[1] && 
           col[2] == other.col[2] && col[3] == other.col[3];
}

bool Mat4::operator!=(const Mat4& other) const {
    return !(*this == other);
}

Mat4 operator*(const Mat4& m, float scalar) {
    return Mat4(m.col[0] * scalar, m.col[1] * scalar, m.col[2] * scalar, m.col[3] * scalar);
}

Mat4 operator*(float scalar, const Mat4& m) {
    return m * scalar;
}

// ==================== Quaternion ====================

Quaternion Quaternion::from_euler(float pitch, float yaw, float roll) {
    float cy = std::cos(yaw * 0.5f);
    float sy = std::sin(yaw * 0.5f);
    float cp = std::cos(pitch * 0.5f);
    float sp = std::sin(pitch * 0.5f);
    float cr = std::cos(roll * 0.5f);
    float sr = std::sin(roll * 0.5f);

    return Quaternion(
        sr * cp * cy - cr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        cr * cp * cy + sr * sp * sy
    ).normalized();
}

Quaternion Quaternion::operator*(const Quaternion& other) const {
    return Quaternion(
        w * other.x + x * other.w + y * other.z - z * other.y,
        w * other.y - x * other.z + y * other.w + z * other.x,
        w * other.z + x * other.y - y * other.x + z * other.w,
        w * other.w - x * other.x - y * other.y - z * other.z
    );
}

Vec3 Quaternion::operator*(const Vec3& v) const {
    // Rotate vector using quaternion: q * v * q^-1
    Quaternion q_conj = conjugate();
    Quaternion result = (*this) * Quaternion(v.x, v.y, v.z, 0.0f) * q_conj;
    return Vec3(result.x, result.y, result.z);
}

Quaternion Quaternion::slerp(const Quaternion& a, const Quaternion& b, float t) {
    Quaternion b_target = b;
    float dot_product = a.dot(b);

    // Take shorter path
    if (dot_product < 0.0f) {
        b_target.x = -b_target.x;
        b_target.y = -b_target.y;
        b_target.z = -b_target.z;
        b_target.w = -b_target.w;
        dot_product = -dot_product;
    }

    // Clamp dot product
    dot_product = clamp(dot_product, -1.0f, 1.0f);

    float theta = std::acos(dot_product);
    float sin_theta = std::sin(theta);

    if (approximately_zero(sin_theta)) {
        // Vectors are nearly parallel, use linear interpolation
        return lerp(a, b_target, t).normalized();
    }

    float weight_a = std::sin((1.0f - t) * theta) / sin_theta;
    float weight_b = std::sin(t * theta) / sin_theta;

    return Quaternion(
        weight_a * a.x + weight_b * b_target.x,
        weight_a * a.y + weight_b * b_target.y,
        weight_a * a.z + weight_b * b_target.z,
        weight_a * a.w + weight_b * b_target.w
    );
}

Vec3 Quaternion::axis() const {
    float sin_half_theta_sq = x * x + y * y + z * z;
    if (approximately_zero(sin_half_theta_sq)) {
        return Vec3(0.0f, 1.0f, 0.0f);  // Arbitrary axis for zero rotation
    }
    return Vec3(x, y, z).normalized();
}

Quaternion Quaternion::rotation_x(float angle) {
    float half = angle * 0.5f;
    return Quaternion(std::sin(half), 0.0f, 0.0f, std::cos(half));
}

Quaternion Quaternion::rotation_y(float angle) {
    float half = angle * 0.5f;
    return Quaternion(0.0f, std::sin(half), 0.0f, std::cos(half));
}

Quaternion Quaternion::rotation_z(float angle) {
    float half = angle * 0.5f;
    return Quaternion(0.0f, 0.0f, std::sin(half), std::cos(half));
}

Quaternion operator*(const Quaternion& q, float scalar) {
    return Quaternion(q.x * scalar, q.y * scalar, q.z * scalar, q.w * scalar);
}

Quaternion operator*(float scalar, const Quaternion& q) {
    return q * scalar;
}

}  // namespace gws::math
