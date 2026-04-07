#include "scene/include/transform.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>

namespace schizo::scene {

// ============================================================================
// Transform Implementation
// ============================================================================

Transform::Transform() = default;

void Transform::SetLocalPosition(const glm::vec3& position) {
    if (local_position_ != position) {
        local_position_ = position;
        MarkDirty();
    }
}

void Transform::SetLocalRotation(const glm::quat& rotation) {
    if (local_rotation_ != rotation) {
        local_rotation_ = glm::normalize(rotation);
        MarkDirty();
    }
}

void Transform::SetLocalScale(const glm::vec3& scale) {
    if (local_scale_ != scale) {
        local_scale_ = scale;
        MarkDirty();
    }
}

glm::vec3 Transform::GetWorldPosition() const {
    return glm::vec3(GetWorldMatrix()[3]);
}

glm::quat Transform::GetWorldRotation() const {
    glm::vec3 scale = GetWorldScale();
    glm::mat4 rotmat = GetWorldMatrix();
    rotmat[0] /= scale.x;
    rotmat[1] /= scale.y;
    rotmat[2] /= scale.z;
    return glm::quat_cast(rotmat);
}

glm::vec3 Transform::GetWorldScale() const {
    glm::vec3 scale;
    glm::mat4 mat = GetWorldMatrix();
    scale.x = glm::length(glm::vec3(mat[0]));
    scale.y = glm::length(glm::vec3(mat[1]));
    scale.z = glm::length(glm::vec3(mat[2]));
    return scale;
}

void Transform::SetWorldPosition(const glm::vec3& position) {
    if (parent_) {
        glm::vec3 parent_world_pos = parent_->GetWorldPosition();
        glm::quat parent_world_rot = parent_->GetWorldRotation();
        
        // Transform position to parent local space
        glm::vec3 relative_pos = position - parent_world_pos;
        relative_pos = glm::rotate(glm::inverse(parent_world_rot), relative_pos);
        
        SetLocalPosition(relative_pos);
    } else {
        SetLocalPosition(position);
    }
}

void Transform::SetWorldRotation(const glm::quat& rotation) {
    if (parent_) {
        glm::quat parent_world_rot = parent_->GetWorldRotation();
        SetLocalRotation(glm::inverse(parent_world_rot) * rotation);
    } else {
        SetLocalRotation(rotation);
    }
}

glm::mat4 Transform::GetLocalMatrix() const {
    glm::mat4 pos_mat = glm::translate(glm::mat4(1.0f), local_position_);
    glm::mat4 rot_mat = glm::mat4_cast(local_rotation_);
    glm::mat4 scale_mat = glm::scale(glm::mat4(1.0f), local_scale_);
    return pos_mat * rot_mat * scale_mat;
}

glm::mat4 Transform::GetWorldMatrix() const {
    if (world_matrix_dirty_) {
        RecalculateWorldMatrix();
    }
    return world_matrix_;
}

void Transform::MarkDirty() {
    world_matrix_dirty_ = true;
    // Mark children as dirty too
}

glm::vec3 Transform::GetForward() const {
    // Local forward is -Z
    return glm::normalize(glm::rotate(local_rotation_, glm::vec3(0.0f, 0.0f, -1.0f)));
}

glm::vec3 Transform::GetRight() const {
    // Local right is +X
    return glm::normalize(glm::rotate(local_rotation_, glm::vec3(1.0f, 0.0f, 0.0f)));
}

glm::vec3 Transform::GetUp() const {
    // Local up is +Y
    return glm::normalize(glm::rotate(local_rotation_, glm::vec3(0.0f, 1.0f, 0.0f)));
}

void Transform::SetParent(Transform* parent) {
    if (parent_ != parent) {
        parent_ = parent;
        MarkDirty();
    }
}

void Transform::RemoveParent() {
    SetParent(nullptr);
}

void Transform::Translate(const glm::vec3& translation) {
    SetLocalPosition(local_position_ + translation);
}

void Transform::TranslateWorld(const glm::vec3& translation) {
    SetWorldPosition(GetWorldPosition() + translation);
}

void Transform::Rotate(const glm::vec3& axis, float angle) {
    glm::quat rotation = glm::angleAxis(angle, glm::normalize(axis));
    SetLocalRotation(rotation * local_rotation_);
}

void Transform::LookAt(const glm::vec3& target, const glm::vec3& up) {
    glm::vec3 forward = glm::normalize(target - GetWorldPosition());
    glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::vec3 new_up = glm::normalize(glm::cross(right, forward));
    
    glm::mat3 rot_mat;
    rot_mat[0] = right;
    rot_mat[1] = new_up;
    rot_mat[2] = -forward;  // Camera convention
    
    SetWorldRotation(glm::quat_cast(glm::mat4(rot_mat)));
}

void Transform::RecalculateWorldMatrix() const {
    glm::mat4 local_mat = GetLocalMatrix();
    
    if (parent_) {
        world_matrix_ = parent_->GetWorldMatrix() * local_mat;
    } else {
        world_matrix_ = local_mat;
    }
    
    world_matrix_dirty_ = false;
}

} // namespace schizo::scene
