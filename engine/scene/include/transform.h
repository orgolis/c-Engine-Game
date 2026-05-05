#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <cstdint>
#include <vector>

namespace schizo::scene {

// ============================================================================
// Transform Hierarchy
// ============================================================================

/**
 * @class Transform
 * @brief 3D transform with position, rotation, and scale with parent-child hierarchy
 */
class Transform {
public:
    Transform();
    ~Transform();

    // Deleted copy (transforms are unique per entity)
    Transform(const Transform&) = delete;
    Transform& operator=(const Transform&) = delete;

    // Move is left default because nothing in the codebase actually moves a
    // Transform — entities are owned via shared_ptr. Don't move a Transform
    // with live children/parent links: the pointer fixup is not handled here.
    Transform(Transform&&) = default;
    Transform& operator=(Transform&&) = default;
    
    // ========== Local Transform ==========
    
    /**
     * @brief Set local position (relative to parent)
     */
    void SetLocalPosition(const glm::vec3& position);
    glm::vec3 GetLocalPosition() const { return local_position_; }
    
    /**
     * @brief Set local rotation (relative to parent)
     */
    void SetLocalRotation(const glm::quat& rotation);
    glm::quat GetLocalRotation() const { return local_rotation_; }
    
    /**
     * @brief Set local scale (relative to parent)
     */
    void SetLocalScale(const glm::vec3& scale);
    glm::vec3 GetLocalScale() const { return local_scale_; }
    
    // ========== World Transform ==========
    
    /**
     * @brief Get world position (absolute position in world space)
     */
    glm::vec3 GetWorldPosition() const;
    
    /**
     * @brief Get world rotation
     */
    glm::quat GetWorldRotation() const;
    
    /**
     * @brief Get world scale
     */
    glm::vec3 GetWorldScale() const;
    
    /**
     * @brief Set world position (automatically adjusts local position based on parent)
     */
    void SetWorldPosition(const glm::vec3& position);
    
    /**
     * @brief Set world rotation
     */
    void SetWorldRotation(const glm::quat& rotation);
    
    // ========== Matrices ==========
    
    /**
     * @brief Get local transformation matrix
     */
    glm::mat4 GetLocalMatrix() const;
    
    /**
     * @brief Get world transformation matrix (cached, updated on demand)
     */
    glm::mat4 GetWorldMatrix() const;
    
    /**
     * @brief Mark transform as dirty to force matrix recalculation
     */
    void MarkDirty();
    
    // ========== Direction Vectors ==========
    
    /**
     * @brief Get forward direction vector (local space, -Z axis)
     */
    glm::vec3 GetForward() const;
    
    /**
     * @brief Get right direction vector (local space, +X axis)
     */
    glm::vec3 GetRight() const;
    
    /**
     * @brief Get up direction vector (local space, +Y axis)
     */
    glm::vec3 GetUp() const;
    
    // ========== Hierarchy Management ==========
    
    /**
     * @brief Set parent transform
     */
    void SetParent(Transform* parent);
    
    /**
     * @brief Get parent transform
     */
    Transform* GetParent() const { return parent_; }
    
    /**
     * @brief Detach from parent
     */
    void RemoveParent();
    
    // ========== Utility ==========
    
    /**
     * @brief Translate in local space
     */
    void Translate(const glm::vec3& translation);
    
    /**
     * @brief Translate in world space
     */
    void TranslateWorld(const glm::vec3& translation);
    
    /**
     * @brief Rotate around local axis (radians)
     */
    void Rotate(const glm::vec3& axis, float angle);
    
    /**
     * @brief Look at target position
     */
    void LookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0, 1, 0));

protected:
    // Local transform components
    glm::vec3 local_position_ = glm::vec3(0.0f);
    glm::quat local_rotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 local_scale_ = glm::vec3(1.0f);
    
    // Cached world transform
    mutable glm::mat4 world_matrix_ = glm::mat4(1.0f);
    mutable bool world_matrix_dirty_ = true;

    // Hierarchy. children_ is maintained alongside the parent_ link so that
    // MarkDirty() can cascade to dependents — without it, a parent's move
    // wouldn't invalidate cached child world matrices and child entities
    // (e.g. cameras parented to the player) wouldn't follow.
    Transform* parent_ = nullptr;
    std::vector<Transform*> children_;

    /**
     * @brief Recalculate cached world matrix
     */
    void RecalculateWorldMatrix() const;
};

} // namespace schizo::scene
