/**
 * @file gltf_loader.h
 * @brief glTF 2.0 model loader - converts glTF files to Vulkan resources
 * 
 * Loads glTF/GLB format models and creates:
 * - VulkanMesh objects (geometry)
 * - VulkanImage objects (textures)
 * - Material definitions with textures
 */

#pragma once

#include "mesh.h"
#include "image.h"
#include "material.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace vks {

/**
 * @struct LoadedModel
 * @brief Complete loaded glTF model with all resources
 */
struct LoadedModel {
    struct MeshNode {
        uint32_t mesh_id;
        uint32_t material_id;
        glm::mat4 transform;
        std::string name;
    };
    
    std::vector<VulkanMesh> meshes;           ///< GPU meshes
    std::vector<VulkanImage> textures;        ///< GPU textures
    std::vector<Material> materials;          ///< GPU materials
    std::vector<MeshNode> nodes;              ///< Scene hierarchy
    glm::vec3 scene_center;                   ///< Centroid for auto-centering
    float scene_radius;                       ///< Bounding sphere radius
};

/**
 * @class glTFLoader
 * @brief Static utility for loading glTF 2.0 files
 * 
 * Usage:
 * ```cpp
 * auto model = glTFLoader::load(device, pdev, "model.glb");
 * for (auto& mesh : model.meshes) {
 *   // Render mesh with materials
 * }
 * ```
 */
class glTFLoader {
public:
    /**
     * @brief Load glTF/GLB file
     * @param device Vulkan device
     * @param pdev Physical device
     * @param path File path (supports .glb and .gltf)
     * @return Loaded model with all resources created on GPU
     * @throws std::runtime_error if file not found or invalid format
     */
    static LoadedModel load(VkDevice device, VkPhysicalDevice pdev,
                           const std::string& path);
    
    /**
     * @brief Load from memory buffer
     * @param device Vulkan device
     * @param pdev Physical device
     * @param buffer File data
     * @param buffer_size Size in bytes
     * @param is_glb True if GLB format, false if JSON+separate bin
     * @return Loaded model
     */
    static LoadedModel load_from_memory(VkDevice device, VkPhysicalDevice pdev,
                                       const uint8_t* buffer, size_t buffer_size,
                                       bool is_glb);

private:
    struct LoadContext {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice pdev = VK_NULL_HANDLE;
        std::vector<uint8_t> file_data;
        std::string file_directory;  // for relative texture paths
    };
    
    static LoadedModel load_internal(const LoadContext& ctx);
};

}  // namespace vks
