#include "skeletal_mesh_component.h"
#include "../scene/include/entity.h"
#include "render_device.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace schizo::renderer {

SkeletalMeshComponent::SkeletalMeshComponent() = default;

void SkeletalMeshComponent::OnAttach() {
    // Initialize deformed mesh if we have base vertices and skeleton
    if (!base_vertices_.empty() && skeleton_) {
        deformed_vertices_ = base_vertices_;
        needs_update_ = true;
    }
}

void SkeletalMeshComponent::OnDetach() {
    // Cleanup if needed
}

void SkeletalMeshComponent::OnUpdate(float delta_time) {
    // Nothing typically needed in update
}

void SkeletalMeshComponent::OnLateUpdate(float delta_time) {
    // Update skin after animation system has computed bone transforms
    if (skeleton_ && animator_) {
        UpdateSkin();
    }
}

void SkeletalMeshComponent::OnEnable() {
    // Nothing needed
}

void SkeletalMeshComponent::OnDisable() {
    // Nothing needed
}

void SkeletalMeshComponent::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
    skeleton_ = skeleton;
    needs_update_ = true;
}

void SkeletalMeshComponent::SetAnimator(std::shared_ptr<Animator> animator) {
    animator_ = animator;
}

void SkeletalMeshComponent::UpdateSkin() {
    if (!skeleton_ || base_vertices_.empty()) {
        return;
    }
    
    // Ensure deformed vertices match base vertices count
    if (deformed_vertices_.size() != base_vertices_.size()) {
        deformed_vertices_ = base_vertices_;
    }
    
    // Deform each vertex based on bone weights
    for (size_t i = 0; i < base_vertices_.size(); ++i) {
        deformed_vertices_[i] = base_vertices_[i];
        deformed_vertices_[i].position = SkinVertex(base_vertices_[i]);
        deformed_vertices_[i].normal = SkinNormal(base_vertices_[i]);
    }
    
    // Update GPU mesh if we have one
    if (deformed_mesh_ && base_mesh_) {
        // Convert VertexSkinned to Vertex for rendering
        std::vector<Vertex> gpu_vertices;
        gpu_vertices.reserve(deformed_vertices_.size());
        for (const auto& v : deformed_vertices_) {
            gpu_vertices.push_back(Vertex(v.position, glm::vec4(glm::normalize(v.normal), 1.0f)));
        }
        deformed_mesh_->SetData(gpu_vertices, indices_);
    }
}

glm::vec3 SkeletalMeshComponent::SkinVertex(const VertexSkinned& vertex) const {
    glm::vec3 deformed_pos(0.0f);
    
    // Apply each bone's influence on this vertex
    for (int i = 0; i < 4; ++i) {
        if (vertex.bone_indices[i] < 0) break;
        
        int bone_id = vertex.bone_indices[i];
        float weight = vertex.bone_weights[i];
        
        // Get bone world matrix
        Bone* bone = skeleton_->GetBone(bone_id);
        if (!bone) continue;
        
        // Apply bone transformation with weight
        glm::vec4 bone_space_pos = bone->inverse_bind_matrix * glm::vec4(vertex.position, 1.0f);
        glm::vec4 world_pos = bone->world_matrix * bone_space_pos;
        
        deformed_pos += glm::vec3(world_pos) * weight;
    }
    
    return deformed_pos;
}

glm::vec3 SkeletalMeshComponent::SkinNormal(const VertexSkinned& vertex) const {
    glm::vec3 deformed_normal(0.0f);
    
    // Apply each bone's influence on normal (no translation)
    for (int i = 0; i < 4; ++i) {
        if (vertex.bone_indices[i] < 0) break;
        
        int bone_id = vertex.bone_indices[i];
        float weight = vertex.bone_weights[i];
        
        // Get bone world matrix
        Bone* bone = skeleton_->GetBone(bone_id);
        if (!bone) continue;
        
        // Apply bone transformation to normal (ignore translation)
        glm::vec3 bone_space_normal = glm::vec3(bone->inverse_bind_matrix * glm::vec4(vertex.normal, 0.0f));
        glm::vec3 world_normal = glm::vec3(bone->world_matrix * glm::vec4(bone_space_normal, 0.0f));
        
        deformed_normal += world_normal * weight;
    }
    
    return glm::normalize(deformed_normal);
}

void SkeletalMeshComponent::Render(RenderDevice* device) {
    if (!base_mesh_ || !material_) {
        return;
    }
    
    // Use deformed mesh if available, otherwise use base mesh
    auto render_mesh = deformed_mesh_ ? deformed_mesh_ : base_mesh_;
    render_mesh->Draw(device);
}

} // namespace schizo::renderer
