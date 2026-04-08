#pragma once

#include "entity.h"
#include "transform.h"
#include <glm/glm.hpp>

namespace schizo::scene {

// ============================================================================
// Transform Component
// ============================================================================

/**
 * @class TransformComponent
 * @brief Component that provides transform functionality to entities
 * 
 * This component wraps the entity's built-in transform, providing 
 * component-based access to position, rotation, and scale.
 */
class TransformComponent : public Component {
public:
    TransformComponent() = default;
    virtual ~TransformComponent() = default;
    
    // Deleted copy, allow move
    TransformComponent(const TransformComponent&) = delete;
    TransformComponent& operator=(const TransformComponent&) = delete;
    
    TransformComponent(TransformComponent&&) = default;
    TransformComponent& operator=(TransformComponent&&) = default;
    
    /**
     * @brief Get the transform from this component's entity
     */
    Transform* GetTransform() const {
        if (entity_) {
            return entity_->GetTransform();
        }
        return nullptr;
    }
    
    // ========== Convenience accessors ==========
    
    /**
     * @brief Get local position
     */
    glm::vec3 GetLocalPosition() const {
        auto t = GetTransform();
        return t ? t->GetLocalPosition() : glm::vec3(0.0f);
    }
    
    /**
     * @brief Set local position
     */
    void SetLocalPosition(const glm::vec3& position) {
        if (auto t = GetTransform()) {
            t->SetLocalPosition(position);
        }
    }
    
    /**
     * @brief Get local rotation (quaternion)
     */
    glm::quat GetLocalRotation() const {
        auto t = GetTransform();
        return t ? t->GetLocalRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    
    /**
     * @brief Set local rotation (quaternion)
     */
    void SetLocalRotation(const glm::quat& rotation) {
        if (auto t = GetTransform()) {
            t->SetLocalRotation(rotation);
        }
    }
    
    /**
     * @brief Get local scale
     */
    glm::vec3 GetLocalScale() const {
        auto t = GetTransform();
        return t ? t->GetLocalScale() : glm::vec3(1.0f);
    }
    
    /**
     * @brief Set local scale
     */
    void SetLocalScale(const glm::vec3& scale) {
        if (auto t = GetTransform()) {
            t->SetLocalScale(scale);
        }
    }
    
    /**
     * @brief Get world position
     */
    glm::vec3 GetWorldPosition() const {
        auto t = GetTransform();
        return t ? t->GetWorldPosition() : glm::vec3(0.0f);
    }
    
    /**
     * @brief Set world position
     */
    void SetWorldPosition(const glm::vec3& position) {
        if (auto t = GetTransform()) {
            t->SetWorldPosition(position);
        }
    }
    
    /**
     * @brief Get world rotation
     */
    glm::quat GetWorldRotation() const {
        auto t = GetTransform();
        return t ? t->GetWorldRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    
    /**
     * @brief Set world rotation
     */
    void SetWorldRotation(const glm::quat& rotation) {
        if (auto t = GetTransform()) {
            t->SetWorldRotation(rotation);
        }
    }
    
    /**
     * @brief Get world scale
     */
    glm::vec3 GetWorldScale() const {
        auto t = GetTransform();
        return t ? t->GetWorldScale() : glm::vec3(1.0f);
    }
    
    /**
     * @brief Get local transformation matrix
     */
    glm::mat4 GetLocalMatrix() const {
        if (auto t = GetTransform()) {
            return t->GetLocalMatrix();
        }
        return glm::mat4(1.0f);
    }
    
    /**
     * @brief Get world transformation matrix
     */
    glm::mat4 GetWorldMatrix() const {
        if (auto t = GetTransform()) {
            return t->GetWorldMatrix();
        }
        return glm::mat4(1.0f);
    }
    
    /**
     * @brief Get forward direction vector (local space, -Z axis)
     */
    glm::vec3 GetForward() const {
        if (auto t = GetTransform()) {
            return t->GetForward();
        }
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    
    /**
     * @brief Get right direction vector (local space, +X axis)
     */
    glm::vec3 GetRight() const {
        if (auto t = GetTransform()) {
            return t->GetRight();
        }
        return glm::vec3(1.0f, 0.0f, 0.0f);
    }
    
    /**
     * @brief Get up direction vector (local space, +Y axis)
     */
    glm::vec3 GetUp() const {
        if (auto t = GetTransform()) {
            return t->GetUp();
        }
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    
protected:
    // TransformComponent doesn't need additional state since it proxies to Entity's transform
};

} // namespace schizo::scene
