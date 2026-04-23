/**
 * @file mesh.cpp
 * @brief Vulkan Mesh implementation
 */

#include "mesh.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace vks {

// ============================================================================
// VulkanMesh Implementation
// ============================================================================

VulkanMesh VulkanMesh::create(VkDevice device, VkPhysicalDevice pdev,
                             const MeshData& data) {
    VulkanMesh mesh;
    
    if (data.vertices.empty() || data.indices.empty()) {
        throw std::runtime_error("VulkanMesh::create: empty vertex or index data");
    }
    
    mesh.vertex_count = static_cast<uint32_t>(data.vertices.size());
    mesh.index_count = static_cast<uint32_t>(data.indices.size());
    
    // Create vertex buffer
    size_t vertex_size = data.vertices.size() * sizeof(Vertex);
    mesh.vertex_buffer = VulkanBuffer::create(device, pdev, BufferUsage::VERTEX, 
                                              vertex_size, data.vertices.data());
    
    // Create index buffer
    size_t index_size = data.indices.size() * sizeof(uint32_t);
    mesh.index_buffer = VulkanBuffer::create(device, pdev, BufferUsage::INDEX,
                                            index_size, data.indices.data());
    
    // Create single submesh
    Submesh submesh{};
    submesh.index_offset = 0;
    submesh.index_count = mesh.index_count;
    submesh.material_id = 0;
    mesh.submeshes.push_back(submesh);
    
    spdlog::debug("✓ VulkanMesh created: {} vertices, {} indices",
                 mesh.vertex_count, mesh.index_count);
    
    return mesh;
}

VulkanMesh::VulkanMesh(VulkanMesh&& other) noexcept
    : vertex_buffer(std::move(other.vertex_buffer)),
      index_buffer(std::move(other.index_buffer)),
      submeshes(std::move(other.submeshes)),
      vertex_count(other.vertex_count),
      index_count(other.index_count) {
    other.vertex_count = 0;
    other.index_count = 0;
}

VulkanMesh& VulkanMesh::operator=(VulkanMesh&& other) noexcept {
    vertex_buffer = std::move(other.vertex_buffer);
    index_buffer = std::move(other.index_buffer);
    submeshes = std::move(other.submeshes);
    vertex_count = other.vertex_count;
    index_count = other.index_count;
    
    other.vertex_count = 0;
    other.index_count = 0;
    
    return *this;
}

}  // namespace vks
