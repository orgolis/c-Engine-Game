#include "batch.h"
#include "render_device.h"
#include "mesh.h"
#include "material.h"

#include <cstdint>

namespace schizo::renderer {

// ============================================================================
// StaticBatch Implementation
// ============================================================================

std::unique_ptr<StaticBatch> StaticBatch::Create(RenderDevice* device) {
    return std::make_unique<StaticBatch>(device);
}

StaticBatch::StaticBatch(RenderDevice* device)
    : device_(device), next_instance_id_(1) {
}

StaticBatch::~StaticBatch() {
    Clear();
}

bool StaticBatch::Initialize() {
    return device_ != nullptr;
}

uint32_t StaticBatch::AddInstance(std::shared_ptr<Mesh> mesh,
                                   std::shared_ptr<Material> material,
                                   const glm::mat4& transform) {
    if (!mesh) return 0;
    
    BatchedMeshInstance instance;
    instance.id = next_instance_id_++;
    instance.mesh = mesh;
    instance.material = material;
    instance.transform = transform;
    instance.visible = true;
    
    instances_.push_back(instance);
    return instance.id;
}

bool StaticBatch::RemoveInstance(uint32_t instance_id) {
    auto it = std::find_if(instances_.begin(), instances_.end(),
        [instance_id](const BatchedMeshInstance& inst) { return inst.id == instance_id; });
    
    if (it != instances_.end()) {
        instances_.erase(it);
        return true;
    }
    return false;
}

void StaticBatch::UpdateInstanceTransform(uint32_t instance_id, const glm::mat4& transform) {
    for (auto& instance : instances_) {
        if (instance.id == instance_id) {
            instance.transform = transform;
            return;
        }
    }
}

void StaticBatch::SetInstanceVisible(uint32_t instance_id, bool visible) {
    for (auto& instance : instances_) {
        if (instance.id == instance_id) {
            instance.visible = visible;
            return;
        }
    }
}

uint32_t StaticBatch::GetVisibleInstanceCount() const {
    uint32_t count = 0;
    for (const auto& instance : instances_) {
        if (instance.visible) count++;
    }
    return count;
}

void StaticBatch::Draw(RenderDevice* device) {
    if (!device || instances_.empty()) return;
    
    // Group by mesh and material for maximum batching
    std::unordered_map<std::uintptr_t, std::vector<BatchedMeshInstance*>> mesh_groups;
    
    for (auto& instance : instances_) {
        if (!instance.visible) continue;
        mesh_groups[reinterpret_cast<std::uintptr_t>(instance.mesh.get())].push_back(&instance);
    }
    
    // Draw each mesh group
    for (auto& mesh_group : mesh_groups) {
        if (mesh_group.second.empty()) continue;
        
        auto* mesh = mesh_group.second[0]->mesh.get();
        
        // Group by material within mesh group
        std::unordered_map<std::uintptr_t, std::vector<BatchedMeshInstance*>> material_groups;
        for (auto* inst : mesh_group.second) {
            material_groups[reinterpret_cast<std::uintptr_t>(inst->material.get())].push_back(inst);
        }
        
        // Draw each material group
        for (auto& mat_group : material_groups) {
            if (mat_group.second.empty()) continue;
            
            auto* material = mat_group.second[0]->material.get();
            if (material) {
                material->Bind(device);
            }
            
            // Draw each instance with this mesh/material combination
            for (auto* inst : mat_group.second) {
                if (material) {
                    material->SetUniformMat4(device, "u_model", inst->transform);
                }
                mesh->Draw(device);
            }
        }
    }
}

void StaticBatch::DrawInstance(RenderDevice* device, uint32_t instance_id) {
    for (const auto& instance : instances_) {
        if (instance.id == instance_id) {
            if (instance.material) {
                instance.material->Bind(device);
                instance.material->SetUniformMat4(device, "u_model", instance.transform);
            }
            if (instance.mesh) {
                instance.mesh->Draw(device);
            }
            return;
        }
    }
}

void StaticBatch::Clear() {
    instances_.clear();
    next_instance_id_ = 1;
}

void StaticBatch::Rebuild() {
    GroupInstances();
}

StaticBatch::Stats StaticBatch::GetStats() const {
    Stats stats;
    stats.total_instances = instances_.size();
    stats.visible_instances = GetVisibleInstanceCount();
    
    // Count draw calls (one per unique mesh/material combination)
    std::set<std::pair<std::uintptr_t, std::uintptr_t>> combinations;
    for (const auto& inst : instances_) {
        if (inst.visible) {
            combinations.insert({
                reinterpret_cast<std::uintptr_t>(inst.mesh.get()),
                reinterpret_cast<std::uintptr_t>(inst.material.get())
            });
        }
    }
    stats.draw_calls = combinations.size();
    
    // Count triangles
    for (const auto& inst : instances_) {
        if (inst.visible && inst.mesh) {
            stats.triangles_rendered += inst.mesh->GetIndexCount() / 3;
        }
    }
    
    return stats;
}

void StaticBatch::GroupInstances() {
    // Simple sorting by mesh pointer for cache efficiency
    std::sort(instances_.begin(), instances_.end(),
        [](const BatchedMeshInstance& a, const BatchedMeshInstance& b) {
            return a.mesh.get() < b.mesh.get();
        });
}

// ============================================================================
// DynamicBatch Implementation
// ============================================================================

std::unique_ptr<DynamicBatch> DynamicBatch::Create(RenderDevice* device, uint32_t max_instances) {
    return std::make_unique<DynamicBatch>(device, max_instances);
}

DynamicBatch::DynamicBatch(RenderDevice* device, uint32_t max_instances)
    : device_(device), max_instances_(max_instances), next_instance_id_(1) {
}

DynamicBatch::~DynamicBatch() {
    Clear();
    if (device_ && indirect_buffer_) {
        device_->DeleteBuffer(indirect_buffer_);
    }
    if (device_ && instance_data_buffer_) {
        device_->DeleteBuffer(instance_data_buffer_);
    }
}

bool DynamicBatch::Initialize() {
    if (!device_) return false;
    
    // Create indirect buffer for draw commands
    const size_t indirect_size = max_instances_ * sizeof(uint32_t) * 5;  // 5 uints per command
    indirect_buffer_ = device_->CreateBuffer(0x8F3C, indirect_size, nullptr, true);  // GL_DRAW_INDIRECT_BUFFER
    
    // Create instance data buffer
    const size_t instance_size = max_instances_ * sizeof(glm::mat4);
    instance_data_buffer_ = device_->CreateBuffer(0x88EC, instance_size, nullptr, true);  // GL_SHADER_STORAGE_BUFFER
    
    return indirect_buffer_ != 0 && instance_data_buffer_ != 0;
}

uint32_t DynamicBatch::AddInstance(std::shared_ptr<Mesh> mesh,
                                    std::shared_ptr<Material> material,
                                    const glm::mat4& transform) {
    if (!mesh || instances_.size() >= max_instances_) return 0;
    
    BatchedMeshInstance instance;
    instance.id = next_instance_id_++;
    instance.mesh = mesh;
    instance.material = material;
    instance.transform = transform;
    instance.visible = true;
    
    instances_.push_back(instance);
    UpdateGPUBuffers();
    
    return instance.id;
}

bool DynamicBatch::RemoveInstance(uint32_t instance_id) {
    auto it = std::find_if(instances_.begin(), instances_.end(),
        [instance_id](const BatchedMeshInstance& inst) { return inst.id == instance_id; });
    
    if (it != instances_.end()) {
        instances_.erase(it);
        UpdateGPUBuffers();
        return true;
    }
    return false;
}

void DynamicBatch::UpdateInstanceTransform(uint32_t instance_id, const glm::mat4& transform) {
    for (auto& instance : instances_) {
        if (instance.id == instance_id) {
            instance.transform = transform;
            UpdateGPUBuffers();
            return;
        }
    }
}

void DynamicBatch::SetInstanceVisible(uint32_t instance_id, bool visible) {
    for (auto& instance : instances_) {
        if (instance.id == instance_id) {
            instance.visible = visible;
            return;
        }
    }
}

uint32_t DynamicBatch::GetVisibleInstanceCount() const {
    uint32_t count = 0;
    for (const auto& instance : instances_) {
        if (instance.visible) count++;
    }
    return count;
}

void DynamicBatch::DrawIndirect(RenderDevice* device) {
    if (!device || instances_.empty()) return;
    
    // Update GPU buffers
    UpdateGPUBuffers();
    
    // For each mesh, issue indirect draw
    std::unordered_map<std::uintptr_t, std::vector<BatchedMeshInstance*>> mesh_groups;
    for (auto& instance : instances_) {
        if (instance.visible) {
            mesh_groups[reinterpret_cast<std::uintptr_t>(instance.mesh.get())].push_back(&instance);
        }
    }
    
    for (auto& group : mesh_groups) {
        if (group.second.empty()) continue;
        
        auto* mesh = group.second[0]->mesh.get();
        
        // Simple indirect rendering (single draw for all instances with this mesh)
        // In production, would use proper indirect command buffers
        for (auto* inst : group.second) {
            if (inst->material) {
                inst->material->Bind(device);
                inst->material->SetUniformMat4(device, "u_model", inst->transform);
            }
            mesh->Draw(device);
        }
    }
}

void DynamicBatch::Clear() {
    instances_.clear();
    next_instance_id_ = 1;
}

void DynamicBatch::UpdateGPUBuffers() {
    if (!device_ || !instance_data_buffer_) return;
    
    // Update instance data buffer with transforms
    std::vector<glm::mat4> transforms;
    for (const auto& instance : instances_) {
        transforms.push_back(instance.transform);
    }
    
    if (!transforms.empty()) {
        device_->UpdateBuffer(instance_data_buffer_, 0, 
                            transforms.size() * sizeof(glm::mat4),
                            transforms.data());
    }
}

} // namespace schizo::renderer
