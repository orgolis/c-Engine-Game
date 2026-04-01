#pragma once

#include "vec3.h"
#include "quaternion.h"
#include "mat4.h"

namespace gws::math {

// Transform - combined position, rotation, and scale
// More efficient than storing a full Mat4 for many objects
// For runtime: compose to Mat4 only when needed
struct Transform {
    Vec3 position;
    Quaternion rotation;
    Vec3 scale;

    // Constructor with defaults
    Transform() 
        : position(0.0f, 0.0f, 0.0f),
          rotation(0.0f, 0.0f, 0.0f, 1.0f),
          scale(1.0f, 1.0f, 1.0f) {}

    explicit Transform(const Vec3& pos, const Quaternion& rot = Quaternion(), const Vec3& scl = Vec3(1.0f, 1.0f, 1.0f))
        : position(pos), rotation(rot), scale(scl) {}

    // Compose to a 4x4 matrix
    Mat4 to_mat4() const {
        // First apply rotation (as 3x3 in the upper-left of a 4x4)
        // Then scale
        // Then translation
        
        // For now, return identity - implementation will come later
        Mat4 result;
        // TODO: Implement proper composition
        return result;
    }

    // Transform a point (apply translation, rotation, scale)
    Vec3 transform_point(const Vec3& point) const {
        Vec3 scaled = Vec3(point.x * scale.x, point.y * scale.y, point.z * scale.z);
        Vec3 rotated = rotation * scaled;
        return rotated + position;
    }

    // Transform a direction (apply rotation and scale, no translation)
    Vec3 transform_direction(const Vec3& direction) const {
        Vec3 scaled = Vec3(direction.x * scale.x, direction.y * scale.y, direction.z * scale.z);
        return rotation * scaled;
    }

    // Apply another transform on top of this one
    Transform operator*(const Transform& other) const {
        Transform result;
        result.position = transform_point(other.position);
        result.rotation = rotation * other.rotation;
        result.scale = Vec3(scale.x * other.scale.x, scale.y * other.scale.y, scale.z * other.scale.z);
        return result;
    }

    // Comparison
    bool operator==(const Transform& other) const {
        return position == other.position &&
               rotation == other.rotation &&
               scale == other.scale;
    }

    bool operator!=(const Transform& other) const {
        return !(*this == other);
    }

    // Get forward direction (local -Z)
    Vec3 forward() const {
        return rotation * Vec3(0.0f, 0.0f, -1.0f);
    }

    // Get right direction (local +X)
    Vec3 right() const {
        return rotation * Vec3(1.0f, 0.0f, 0.0f);
    }

    // Get up direction (local +Y)
    Vec3 up() const {
        return rotation * Vec3(0.0f, 1.0f, 0.0f);
    }
};

}  // namespace gws::math
