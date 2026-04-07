#pragma once

#include "entity.h"
#include <memory>
#include <glm/glm.hpp>

namespace schizo::renderer {
    // Forward declarations
    class Mesh;
    class Material;
    class RenderDevice;
}

namespace schizo::scene {

/**
 * @class MeshRenderer
 * @brief Component for rendering geometry on an entity
 * 
 * Attaches a mesh and material to an entity, making it visible in the renderer.
 * Uses the entity's transform for positioning.
 * 
 * Usage:
 * auto entity = scene->CreateEntity("Cube");
 * auto renderer = entity->AddComponent<MeshRenderer>();
 * renderer->SetMesh(my_mesh);
 * renderer->SetMaterial(my_material);
 */
class MeshRenderer : public Component {
public:
    MeshRenderer() = default;
    ~MeshRenderer() override = default;
    
    // ========== Lifecycle ==========
    
    void OnAttach() override;
    void OnDetach() override;
    void OnEnable() override;
    void OnDisable() override;
    void OnUpdate(float delta_time) override;
    
    // ========== Mesh & Material ==========
    
    /**
     * Set the mesh to render
     */
    void SetMesh(std::shared_ptr<schizo::renderer::Mesh> mesh) {
        mesh_ = mesh;
        UpdateBounds();
    }
    
    /**
     * Get the mesh
     */
    std::shared_ptr<schizo::renderer::Mesh> GetMesh() const { return mesh_; }
    
    /**
     * Set the material
     */
    void SetMaterial(std::shared_ptr<schizo::renderer::Material> material) {
        material_ = material;
    }
    
    /**
     * Get the material
     */
    std::shared_ptr<schizo::renderer::Material> GetMaterial() const { return material_; }
    
    /**
     * Set material by index (for multi-material meshes)
     */
    void SetMaterialIndex(uint32_t index) {
        material_index_ = index;
    }
    
    uint32_t GetMaterialIndex() const { return material_index_; }
    
    // ========== Rendering Control ==========
    
    /**
     * Enable/disable rendering
     */
    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }
    
    /**
     * Set cast shadows
     */
    void SetCastShadows(bool cast) { cast_shadows_ = cast; }
    bool GetCastShadows() const { return cast_shadows_; }
    
    /**
     * Set receive shadows
     */
    void SetReceiveShadows(bool receive) { receive_shadows_ = receive; }
    bool GetReceiveShadows() const { return receive_shadows_; }
    
    // ========== LOD Control ==========
    
    /**
     * Set LOD bias (0 = normal, > 0 = closer LOD, < 0 = farther LOD)
     */
    void SetLODBias(float bias) { lod_bias_ = bias; }
    float GetLODBias() const { return lod_bias_; }
    
    /**
     * Set LOD manually (skip automatic selection)
     */
    void SetForceLOD(uint32_t lod_level) {
        force_lod_ = true;
        forced_lod_level_ = lod_level;
    }
    
    /**
     * Disable forced LOD (use automatic)
     */
    void UnsetForceLOD() { force_lod_ = false; }
    
    // ========== Bounds ==========
    
    /**
     * Get world-space bounds center
     */
    glm::vec3 GetBoundsCenter() const;
    
    /**
     * Get world-space bounds extents
     */
    glm::vec3 GetBoundsExtents() const;
    
    /**
     * Get world-space bounding sphere radius
     */
    float GetBoundsRadius() const;
    
    /**
     * Recalculate bounds (call after mesh changes)
     */
    void UpdateBounds();
    
    // ========== Rendering Interface ==========
    
    /**
     * Render this mesh (called by renderer)
     * @param device Graphics device
     * @param view_matrix Camera view matrix
     * @param projection_matrix Camera projection matrix
     */
    void Render(schizo::renderer::RenderDevice* device,
               const glm::mat4& view_matrix,
               const glm::mat4& projection_matrix);
    
    /**
     * Render shadow map (for shadow rendering)
     * @param device Graphics device
     * @param light_space_matrix Transformed view-projection from light
     */
    void RenderShadow(schizo::renderer::RenderDevice* device,
                     const glm::mat4& light_space_matrix);

private:
    // Rendering data
    std::shared_ptr<schizo::renderer::Mesh> mesh_;
    std::shared_ptr<schizo::renderer::Material> material_;
    uint32_t material_index_ = 0;
    
    // Rendering flags
    bool visible_ = true;
    bool cast_shadows_ = true;
    bool receive_shadows_ = true;
    
    // LOD control
    float lod_bias_ = 0.0f;
    bool force_lod_ = false;
    uint32_t forced_lod_level_ = 0;
    
    // Cached bounds (in local space)
    glm::vec3 local_bounds_center_ = glm::vec3(0);
    glm::vec3 local_bounds_extents_ = glm::vec3(0);
    float local_bounds_radius_ = 0.0f;
    
    // Dirty flag for bounds update
    bool bounds_dirty_ = true;
};

/**
 * @class SkinnedMeshRenderer
 * @brief Component for rendering skinned/skeletal animated meshes
 * 
 * Similar to MeshRenderer but with skeleton/bone support for animations.
 * (Implementation stub for now - full implementation in animation system)
 */
class SkinnedMeshRenderer : public Component {
public:
    SkinnedMeshRenderer() = default;
    ~SkinnedMeshRenderer() override = default;
    
    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float delta_time) override;
    
    void SetMesh(std::shared_ptr<schizo::renderer::Mesh> mesh) {
        mesh_ = mesh;
    }
    
    std::shared_ptr<schizo::renderer::Mesh> GetMesh() const { return mesh_; }
    
    void SetMaterial(std::shared_ptr<schizo::renderer::Material> material) {
        material_ = material;
    }
    
    std::shared_ptr<schizo::renderer::Material> GetMaterial() const { return material_; }
    
    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }
    
    void Render(schizo::renderer::RenderDevice* device,
               const glm::mat4& view_matrix,
               const glm::mat4& projection_matrix);

private:
    std::shared_ptr<schizo::renderer::Mesh> mesh_;
    std::shared_ptr<schizo::renderer::Material> material_;
    bool visible_ = true;
    // Skeleton and bone matrices would go here
};

/**
 * @class LineRenderer
 * @brief Debug line rendering component
 * 
 * Renders debug lines for visualization (gizmos, paths, bounds, etc.)
 */
class LineRenderer : public Component {
public:
    LineRenderer() = default;
    ~LineRenderer() override = default;
    
    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float delta_time) override;
    
    /**
     * Add a line to render
     * @param start World position start
     * @param end World position end
     * @param color RGBA color
     * @param duration How long to display (0 = indefinite)
     */
    void AddLine(const glm::vec3& start, const glm::vec3& end,
                const glm::vec4& color = glm::vec4(1, 1, 1, 1),
                float duration = 0.0f);
    
    /**
     * Add a wireframe box
     */
    void AddBox(const glm::vec3& min, const glm::vec3& max,
               const glm::vec4& color = glm::vec4(1, 1, 1, 1));
    
    /**
     * Add a wireframe sphere
     */
    void AddSphere(const glm::vec3& center, float radius,
                  const glm::vec4& color = glm::vec4(1, 1, 1, 1));
    
    /**
     * Clear all lines
     */
    void Clear();
    
    void SetVisible(bool visible) { visible_ = visible; }
    bool IsVisible() const { return visible_; }
    
    void Render(schizo::renderer::RenderDevice* device,
               const glm::mat4& view_matrix,
               const glm::mat4& projection_matrix);

private:
    struct Line {
        glm::vec3 start;
        glm::vec3 end;
        glm::vec4 color;
        float duration;  // 0 = indefinite
        float elapsed = 0.0f;
    };
    
    std::vector<Line> lines_;
    bool visible_ = true;
    std::shared_ptr<schizo::renderer::Mesh> line_mesh_;
};

} // namespace schizo::scene
