/**
 * @file mesh.h
 * @brief Vulkan Mesh - vertex and index buffer aggregation for rendering
 * 
 * High-level mesh abstraction combining VulkanBuffer objects with draw parameters.
 */

#pragma once

#include "buffer.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace vks {

/**
 * @struct Vertex
 * @brief Standard vertex format with position, normal, UV, tangent
 */
struct Vertex {
    glm::vec3 position;   ///< Vertex position
    glm::vec3 normal;     ///< Surface normal
    glm::vec2 uv;         ///< Texture coordinates
    glm::vec3 tangent;    ///< Tangent for normal mapping
};

/**
 * @struct MeshData
 * @brief Raw mesh vertex and index data
 */
struct MeshData {
    std::vector<Vertex> vertices;     ///< Vertex list
    std::vector<uint32_t> indices;    ///< Index list
};

/**
 * @struct Submesh
 * @brief Individual mesh segment with material reference
 */
struct Submesh {
    uint32_t index_offset;    ///< Start index in index buffer
    uint32_t index_count;     ///< Number of indices to draw
    uint32_t material_id;     ///< Material reference (Phase 3)
};

/**
 * @class VulkanMesh
 * @brief GPU mesh with vertex and index buffers
 * 
 * Manages vertex and index buffers for rendering.
 * Supports multiple submeshes for complex models.
 */
class VulkanMesh {
public:
    VulkanMesh() = default;
    ~VulkanMesh() = default;
    
    /**
     * @brief Create a single-submesh from vertex and index data
     * @param device Vulkan device
     * @param pdev Physical device
     * @param data Mesh vertex/index data
     * @return Created mesh
     */
    static VulkanMesh create(VkDevice device, VkPhysicalDevice pdev,
                            const MeshData& data);
    
    /**
     * @brief Get vertex buffer
     */
    const VulkanBuffer& get_vertex_buffer() const { return vertex_buffer; }
    VulkanBuffer& get_vertex_buffer() { return vertex_buffer; }
    
    /**
     * @brief Get index buffer
     */
    const VulkanBuffer& get_index_buffer() const { return index_buffer; }
    VulkanBuffer& get_index_buffer() { return index_buffer; }
    
    /**
     * @brief Get submesh array
     */
    const std::vector<Submesh>& get_submeshes() const { return submeshes; }
    
    /**
     * @brief Get total vertex count
     */
    uint32_t get_vertex_count() const { return vertex_count; }
    
    /**
     * @brief Get total index count
     */
    uint32_t get_index_count() const { return index_count; }
    
    /**
     * @brief Get number of submeshes
     */
    uint32_t get_submesh_count() const { return static_cast<uint32_t>(submeshes.size()); }
    
    /**
     * @brief Check if mesh is valid
     */
    bool is_valid() const { return vertex_count > 0 && index_count > 0; }
    
    // Move semantics
    VulkanMesh(VulkanMesh&& other) noexcept;
    VulkanMesh& operator=(VulkanMesh&& other) noexcept;
    
    // Delete copies (buffers are non-copyable)
    VulkanMesh(const VulkanMesh&) = delete;
    VulkanMesh& operator=(const VulkanMesh&) = delete;

private:
    VulkanBuffer vertex_buffer;
    VulkanBuffer index_buffer;
    std::vector<Submesh> submeshes;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
};

}  // namespace vks
