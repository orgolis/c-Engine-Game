/**
 * @file gltf_loader.cpp
 * @brief glTF 2.0 loader implementation
 */

#include "gltf_loader.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <fstream>
#include <filesystem>
#include <algorithm>

// tinygltf would be included here in production
// #include <tiny_gltf.h>

namespace vks {

// ============================================================================
// Placeholder implementation (full tinygltf integration in production)
// ============================================================================

LoadedModel glTFLoader::load(VkDevice device, VkPhysicalDevice pdev,
                            const std::string& path) {
    // Read file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("glTFLoader: file not found: " + path);
    }
    
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    file.close();
    
    bool is_glb = path.ends_with(".glb");
    
    LoadContext ctx;
    ctx.device = device;
    ctx.pdev = pdev;
    ctx.file_data = std::move(buffer);
    ctx.file_directory = std::filesystem::path(path).parent_path().string();
    
    return load_from_memory(device, pdev, ctx.file_data.data(),
                           ctx.file_data.size(), is_glb);
}

LoadedModel glTFLoader::load_from_memory(VkDevice device, VkPhysicalDevice pdev,
                                        const uint8_t* buffer, size_t buffer_size,
                                        bool is_glb) {
    LoadContext ctx;
    ctx.device = device;
    ctx.pdev = pdev;
    ctx.file_data.assign(buffer, buffer + buffer_size);
    
    return load_internal(ctx);
}

LoadedModel glTFLoader::load_internal(const LoadContext& ctx) {
    LoadedModel result;
    
    // ========================================================================
    // PLACEHOLDER: Real implementation uses tinygltf
    // 
    // tinygltf::Model model;
    // tinygltf::TinyGLTF loader;
    // std::string error_msg, warning_msg;
    // 
    // bool loaded = loader.LoadBinaryFromMemory(&model, &error_msg, &warning_msg,
    //     ctx.file_data.data(), ctx.file_data.size());
    // 
    // if (!loaded) {
    //     throw std::runtime_error("glTFLoader: " + error_msg);
    // }
    // 
    // // Parse meshes from model.meshes[]
    // for (const auto& gltf_mesh : model.meshes) {
    //     for (const auto& primitive : gltf_mesh.primitives) {
    //         // Get vertex data from accessor
    //         // Get index data from accessor
    //         // Create VulkanMesh
    //     }
    // }
    // 
    // // Parse textures from model.images[]
    // for (const auto& image : model.images) {
    //     // Load image data
    //     // Create VulkanImage
    // }
    // 
    // // Parse materials from model.materials[]
    // for (const auto& mat : model.materials) {
    //     // Create Material with textures
    // }
    // ========================================================================
    
    // For Phase 3 Week 2, create a simple test cube
    spdlog::info("glTFLoader: placeholder implementation (tinygltf integration pending)");
    
    // Create simple cube geometry for testing
    std::vector<Vertex> cube_vertices = {
        // Front face
        {{-0.5f, -0.5f,  0.5f}, {0, 0, 1}, {0, 0}, {1, 0, 0}},
        {{ 0.5f, -0.5f,  0.5f}, {0, 0, 1}, {1, 0}, {1, 0, 0}},
        {{ 0.5f,  0.5f,  0.5f}, {0, 0, 1}, {1, 1}, {1, 0, 0}},
        {{-0.5f,  0.5f,  0.5f}, {0, 0, 1}, {0, 1}, {1, 0, 0}},
        
        // Back face
        {{-0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 0}, {1, 0, 0}},
        {{-0.5f,  0.5f, -0.5f}, {0, 0, -1}, {1, 1}, {1, 0, 0}},
        {{ 0.5f,  0.5f, -0.5f}, {0, 0, -1}, {0, 1}, {1, 0, 0}},
        {{ 0.5f, -0.5f, -0.5f}, {0, 0, -1}, {0, 0}, {1, 0, 0}},
        
        // Top face
        {{-0.5f,  0.5f, -0.5f}, {0, 1, 0}, {0, 1}, {1, 0, 0}},
        {{-0.5f,  0.5f,  0.5f}, {0, 1, 0}, {0, 0}, {1, 0, 0}},
        {{ 0.5f,  0.5f,  0.5f}, {0, 1, 0}, {1, 0}, {1, 0, 0}},
        {{ 0.5f,  0.5f, -0.5f}, {0, 1, 0}, {1, 1}, {1, 0, 0}},
        
        // Bottom face
        {{-0.5f, -0.5f, -0.5f}, {0, -1, 0}, {1, 1}, {1, 0, 0}},
        {{ 0.5f, -0.5f, -0.5f}, {0, -1, 0}, {0, 1}, {1, 0, 0}},
        {{ 0.5f, -0.5f,  0.5f}, {0, -1, 0}, {0, 0}, {1, 0, 0}},
        {{-0.5f, -0.5f,  0.5f}, {0, -1, 0}, {1, 0}, {1, 0, 0}},
        
        // Right face
        {{ 0.5f, -0.5f, -0.5f}, {1, 0, 0}, {1, 0}, {1, 0, 0}},
        {{ 0.5f,  0.5f, -0.5f}, {1, 0, 0}, {1, 1}, {1, 0, 0}},
        {{ 0.5f,  0.5f,  0.5f}, {1, 0, 0}, {0, 1}, {1, 0, 0}},
        {{ 0.5f, -0.5f,  0.5f}, {1, 0, 0}, {0, 0}, {1, 0, 0}},
        
        // Left face
        {{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {0, 0}, {1, 0, 0}},
        {{-0.5f, -0.5f,  0.5f}, {-1, 0, 0}, {1, 0}, {1, 0, 0}},
        {{-0.5f,  0.5f,  0.5f}, {-1, 0, 0}, {1, 1}, {1, 0, 0}},
        {{-0.5f,  0.5f, -0.5f}, {-1, 0, 0}, {0, 1}, {1, 0, 0}},
    };
    
    std::vector<uint32_t> cube_indices = {
        // Front
        0, 1, 2, 0, 2, 3,
        // Back
        4, 6, 5, 4, 7, 6,
        // Top
        8, 9, 10, 8, 10, 11,
        // Bottom
        12, 14, 13, 12, 15, 14,
        // Right
        16, 17, 18, 16, 18, 19,
        // Left
        20, 21, 22, 20, 22, 23,
    };
    
    MeshData cube_data{};
    cube_data.vertices = cube_vertices;
    cube_data.indices = cube_indices;
    
    // Create mesh
    auto cube_mesh = VulkanMesh::create(ctx.device, ctx.pdev, cube_data);
    result.meshes.push_back(std::move(cube_mesh));
    
    // Create a simple white texture
    auto white_tex = VulkanImage::create(ctx.device, ctx.pdev, 2, 2,
                                        ImageFormat::RGBA8_SRGB, ImageUsage::TEXTURE);
    result.textures.push_back(std::move(white_tex));
    
    // Set scene bounds
    result.scene_center = {0, 0, 0};
    result.scene_radius = 1.0f;
    
    // Create a test node
    LoadedModel::MeshNode node{};
    node.mesh_id = 0;
    node.material_id = 0;
    node.transform = glm::mat4(1.0f);
    node.name = "Cube";
    result.nodes.push_back(node);
    
    spdlog::info("✓ glTFLoader: created test cube (real glTF loading with tinygltf in Phase 4)");
    return result;
}

}  // namespace vks
