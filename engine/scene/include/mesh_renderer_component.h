#pragma once

#include "entity.h"
#include <glm/glm.hpp>
#include <memory>

namespace schizo::scene {

// ============================================================================
// Mesh Renderer Component
// ============================================================================

/**
 * @enum MeshType
 * @brief Predefined mesh types for simple visualization
 */
enum class MeshType {
    Cube,
    Sphere,
    Cylinder,
    Plane,
    Pyramid,
    Capsule
};

/**
 * @class MeshRendererComponent
 * @brief Component that renders a mesh with color
 */
class MeshRendererComponent : public Component {
public:
    MeshRendererComponent(MeshType type = MeshType::Cube, const glm::vec4& color = glm::vec4(1.0f))
        : mesh_type_(type), color_(color) {}
    
    virtual ~MeshRendererComponent() = default;
    
    // Deleted copy, allow move
    MeshRendererComponent(const MeshRendererComponent&) = delete;
    MeshRendererComponent& operator=(const MeshRendererComponent&) = delete;
    
    MeshRendererComponent(MeshRendererComponent&&) = default;
    MeshRendererComponent& operator=(MeshRendererComponent&&) = default;
    
    /**
     * @brief Get mesh type
     */
    MeshType GetMeshType() const { return mesh_type_; }
    
    /**
     * @brief Set mesh type
     */
    void SetMeshType(MeshType type) { mesh_type_ = type; }
    
    /**
     * @brief Get mesh color
     */
    const glm::vec4& GetColor() const { return color_; }
    
    /**
     * @brief Set mesh color
     */
    void SetColor(const glm::vec4& color) { color_ = color; }
    
    /**
     * @brief Set mesh color with individual components
     */
    void SetColor(float r, float g, float b, float a = 1.0f) {
        color_ = glm::vec4(r, g, b, a);
    }
    
protected:
    MeshType mesh_type_ = MeshType::Cube;
    glm::vec4 color_ = glm::vec4(1.0f);  // White by default
};

} // namespace schizo::scene
