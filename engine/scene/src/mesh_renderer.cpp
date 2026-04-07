#include "mesh_renderer.h"
#include "entity.h"
#include "transform.h"
#include "mesh.h"
#include "material.h"
#include "render_device.h"
#include <glm/gtc/matrix_transform.hpp>

namespace schizo::scene {

// ============================================================================
// MeshRenderer Implementation
// ============================================================================

void MeshRenderer::OnAttach() {
    // Component attached to entity - cache entity reference if needed
}

void MeshRenderer::OnDetach() {
    mesh_ = nullptr;
    material_ = nullptr;
}

void MeshRenderer::OnEnable() {
    // Component enabled
}

void MeshRenderer::OnDisable() {
    // Component disabled
}

void MeshRenderer::OnUpdate(float delta_time) {
    // Update bounds if dirty (e.g., after scale changes)
    if (bounds_dirty_) {
        UpdateBounds();
        bounds_dirty_ = false;
    }
}

glm::vec3 MeshRenderer::GetBoundsCenter() const {
    auto entity = GetEntity();
    if (!entity) return local_bounds_center_;
    
    auto transform = entity->GetTransform();
    return glm::vec3(transform->GetWorldMatrix() * glm::vec4(local_bounds_center_, 1.0f));
}

glm::vec3 MeshRenderer::GetBoundsExtents() const {
    auto entity = GetEntity();
    if (!entity) return local_bounds_extents_;
    
    auto transform = entity->GetTransform();
    auto scale = transform->GetWorldScale();
    return local_bounds_extents_ * scale;
}

float MeshRenderer::GetBoundsRadius() const {
    auto entity = GetEntity();
    if (!entity) return local_bounds_radius_;
    
    auto transform = entity->GetTransform();
    auto scale = transform->GetWorldScale();
    float max_scale = std::max({scale.x, scale.y, scale.z});
    return local_bounds_radius_ * max_scale;
}

void MeshRenderer::UpdateBounds() {
    if (!mesh_) {
        local_bounds_center_ = glm::vec3(0);
        local_bounds_extents_ = glm::vec3(0);
        local_bounds_radius_ = 0.0f;
        return;
    }
    
    local_bounds_center_ = mesh_->GetBoundsCenter();
    local_bounds_extents_ = mesh_->GetBoundsExtents();
    local_bounds_radius_ = mesh_->GetBoundsRadius();
}

void MeshRenderer::Render(schizo::renderer::RenderDevice* device,
                         const glm::mat4& view_matrix,
                         const glm::mat4& projection_matrix) {
    if (!visible_ || !mesh_ || !device) return;
    
    auto entity = GetEntity();
    if (!entity || !entity->IsActiveInHierarchy()) return;
    
    auto transform = entity->GetTransform();
    if (!transform) return;
    
    // Get world matrix from transform
    glm::mat4 model_matrix = transform->GetWorldMatrix();
    
    // Bind material
    if (material_) {
        material_->Bind(device);
        material_->SetUniformMat4(device, "u_model", model_matrix);
        material_->SetUniformMat4(device, "u_view", view_matrix);
        material_->SetUniformMat4(device, "u_projection", projection_matrix);
    }
    
    // Draw mesh (with optional LOD)
    if (force_lod_) {
        mesh_->DrawLOD(device, forced_lod_level_);
    } else {
        // For auto-LOD, would calculate distance from camera
        mesh_->Draw(device);
    }
}

void MeshRenderer::RenderShadow(schizo::renderer::RenderDevice* device,
                               const glm::mat4& light_space_matrix) {
    if (!visible_ || !mesh_ || !device || !cast_shadows_) return;
    
    auto entity = GetEntity();
    if (!entity || !entity->IsActiveInHierarchy()) return;
    
    auto transform = entity->GetTransform();
    if (!transform) return;
    
    glm::mat4 model_matrix = transform->GetWorldMatrix();
    
    // Simple shadow rendering - would use depth shader
    // For now, just skip if no material
    if (!material_) return;
    
    material_->Bind(device);
    material_->SetUniformMat4(device, "u_light_space_matrix", light_space_matrix * model_matrix);
    mesh_->Draw(device);
}

// ============================================================================
// SkinnedMeshRenderer Implementation
// ============================================================================

void SkinnedMeshRenderer::OnAttach() {
    // Initialize skinned mesh rendering
}

void SkinnedMeshRenderer::OnDetach() {
    mesh_ = nullptr;
    material_ = nullptr;
}

void SkinnedMeshRenderer::OnUpdate(float delta_time) {
    // Update skeletal animations, bone matrices, etc.
    // This would be implemented in the animation system
}

void SkinnedMeshRenderer::Render(schizo::renderer::RenderDevice* device,
                                const glm::mat4& view_matrix,
                                const glm::mat4& projection_matrix) {
    if (!visible_ || !mesh_ || !device) return;
    
    auto entity = GetEntity();
    if (!entity || !entity->IsActiveInHierarchy()) return;
    
    auto transform = entity->GetTransform();
    if (!transform) return;
    
    glm::mat4 model_matrix = transform->GetWorldMatrix();
    
    if (material_) {
        material_->Bind(device);
        material_->SetUniformMat4(device, "u_model", model_matrix);
        material_->SetUniformMat4(device, "u_view", view_matrix);
        material_->SetUniformMat4(device, "u_projection", projection_matrix);
        
        // Would also set bone matrices here
        // material_->SetUniformMatrixArray(device, "u_bones", bone_matrices);
    }
    
    mesh_->Draw(device);
}

// ============================================================================
// LineRenderer Implementation
// ============================================================================

void LineRenderer::OnAttach() {
    // Create line mesh if needed
}

void LineRenderer::OnDetach() {
    lines_.clear();
    line_mesh_ = nullptr;
}

void LineRenderer::OnUpdate(float delta_time) {
    // Update line durations and remove expired ones
    for (auto it = lines_.begin(); it != lines_.end(); ) {
        if (it->duration > 0.0f) {
            it->elapsed += delta_time;
            if (it->elapsed >= it->duration) {
                it = lines_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void LineRenderer::AddLine(const glm::vec3& start, const glm::vec3& end,
                          const glm::vec4& color, float duration) {
    lines_.push_back({start, end, color, duration, 0.0f});
}

void LineRenderer::AddBox(const glm::vec3& min, const glm::vec3& max,
                         const glm::vec4& color) {
    // 12 edges of a box
    // Bottom face
    AddLine(min, glm::vec3(max.x, min.y, min.z), color);
    AddLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, min.y, max.z), color);
    AddLine(glm::vec3(max.x, min.y, max.z), glm::vec3(min.x, min.y, max.z), color);
    AddLine(glm::vec3(min.x, min.y, max.z), min, color);
    
    // Top face
    AddLine(glm::vec3(min.x, max.y, min.z), glm::vec3(max.x, max.y, min.z), color);
    AddLine(glm::vec3(max.x, max.y, min.z), max, color);
    AddLine(max, glm::vec3(min.x, max.y, max.z), color);
    AddLine(glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, max.y, min.z), color);
    
    // Vertical edges
    AddLine(min, glm::vec3(min.x, max.y, min.z), color);
    AddLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, max.y, min.z), color);
    AddLine(glm::vec3(max.x, min.y, max.z), max, color);
    AddLine(glm::vec3(min.x, min.y, max.z), glm::vec3(min.x, max.y, max.z), color);
}

void LineRenderer::AddSphere(const glm::vec3& center, float radius,
                            const glm::vec4& color) {
    // Draw 3 circles (XY, XZ, YZ planes)
    const int segments = 16;
    for (int i = 0; i < segments; ++i) {
        float angle1 = (i / static_cast<float>(segments)) * 6.28318f;
        float angle2 = ((i + 1) / static_cast<float>(segments)) * 6.28318f;
        
        // XY circle
        glm::vec3 p1 = center + glm::vec3(radius * std::cos(angle1), radius * std::sin(angle1), 0);
        glm::vec3 p2 = center + glm::vec3(radius * std::cos(angle2), radius * std::sin(angle2), 0);
        AddLine(p1, p2, color);
        
        // XZ circle
        p1 = center + glm::vec3(radius * std::cos(angle1), 0, radius * std::sin(angle1));
        p2 = center + glm::vec3(radius * std::cos(angle2), 0, radius * std::sin(angle2));
        AddLine(p1, p2, color);
        
        // YZ circle
        p1 = center + glm::vec3(0, radius * std::cos(angle1), radius * std::sin(angle1));
        p2 = center + glm::vec3(0, radius * std::cos(angle2), radius * std::sin(angle2));
        AddLine(p1, p2, color);
    }
}

void LineRenderer::Clear() {
    lines_.clear();
}

void LineRenderer::Render(schizo::renderer::RenderDevice* device,
                         const glm::mat4& view_matrix,
                         const glm::mat4& projection_matrix) {
    if (!visible_ || lines_.empty() || !device) return;
    
    // Would draw all lines here
    // This would require creating/maintaining a dynamic line mesh
    // For now, this is a stub
}

} // namespace schizo::scene
