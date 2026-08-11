#pragma once

#include "../scene/include/entity.h"
#include "skeleton.h"
#include "animator.h"
#include "mesh.h"
#include "material.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <cstdint>

namespace schizo::renderer {

/**
 * @class SkeletalMeshComponent
 * @brief Renders a mesh deformed by skeleton bones
 * 
 * Features:
 * - CPU-based mesh skinning (bone weights applied per vertex)
 * - Integrates with Skeleton and Animator
 * - Support for multiple materials
 * - Dynamic mesh deformation based on bone transforms
 */
class SkeletalMeshComponent : public schizo::scene::Component {
public:
    SkeletalMeshComponent();
    virtual ~SkeletalMeshComponent() = default;
    
    // Component lifecycle
    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float delta_time) override;
    void OnLateUpdate(float delta_time) override;
    void OnEnable() override;
    void OnDisable() override;
    
    // Mesh setup
    void SetMesh(std::shared_ptr<Mesh> mesh) { base_mesh_ = mesh; }
    std::shared_ptr<Mesh> GetMesh() const { return base_mesh_; }
    
    void SetBaseVertices(const std::vector<VertexSkinned>& vertices) { base_vertices_ = vertices; }
    const std::vector<VertexSkinned>& GetBaseVertices() const { return base_vertices_; }
    
    void SetIndices(const std::vector<uint32_t>& indices) { indices_ = indices; }
    const std::vector<uint32_t>& GetIndices() const { return indices_; }
    
    // Skeleton/Animator binding
    void SetSkeleton(std::shared_ptr<Skeleton> skeleton);
    std::shared_ptr<Skeleton> GetSkeleton() const { return skeleton_; }
    
    void SetAnimator(std::shared_ptr<Animator> animator);
    std::shared_ptr<Animator> GetAnimator() const { return animator_; }
    
    // Material
    void SetMaterial(std::shared_ptr<Material> material) { material_ = material; }
    std::shared_ptr<Material> GetMaterial() const { return material_; }
    
    // Skinning computation
    /**
     * Compute deformed vertex positions and normals based on bone transforms
     * This should be called after skeleton bones are updated
     */
    void UpdateSkin();
    
    /**
     * Get deformed vertices (result of skinning)
     */
    const std::vector<VertexSkinned>& GetDeformedVertices() const { return deformed_vertices_; }
    
    // Rendering
    /**
     * Render the deformed mesh
     */
    void Render(class RenderDevice* device);
    
private:
    std::shared_ptr<Mesh> base_mesh_;
    std::shared_ptr<Skeleton> skeleton_;
    std::shared_ptr<Animator> animator_;
    std::shared_ptr<Material> material_;
    
    // Vertex and index data
    std::vector<VertexSkinned> base_vertices_;      // Original vertices with bone weights
    std::vector<VertexSkinned> deformed_vertices_;  // Result after skinning
    std::vector<uint32_t> indices_;
    
    // Cached deformed mesh for GPU rendering
    std::shared_ptr<Mesh> deformed_mesh_;
    bool needs_update_ = false;
    
    /**
     * Compute deformed position for a vertex using bone weights
     */
    glm::vec3 SkinVertex(const VertexSkinned& vertex) const;
    
    /**
     * Compute deformed normal for a vertex using bone weights (no translation)
     */
    glm::vec3 SkinNormal(const VertexSkinned& vertex) const;
};

} // namespace schizo::renderer
