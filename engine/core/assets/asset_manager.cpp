#include "asset_manager.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <tinygltf.hpp>
#include <glm/glm.hpp>

namespace schizo::assets {

AssetManager::AssetManager() {
    spdlog::info("AssetManager initialized");
}

std::shared_ptr<MeshAsset> AssetManager::LoadMesh(const std::string& filepath) {
    // Check if already loaded
    auto it = loaded_meshes_.find(filepath);
    if (it != loaded_meshes_.end()) {
        spdlog::debug("Returning cached mesh: {}", filepath);
        return it->second;
    }
    
    // Try to determine the actual file path
    std::string full_path;
    std::ifstream test_file;
    
    // First, try the filepath as-is (for full paths from drag-drop)
    test_file.open(filepath, std::ios::binary);
    if (test_file.is_open()) {
        full_path = filepath;
        test_file.close();
        spdlog::debug("Found mesh file at dropped path: {}", filepath);
    } else {
        // Try relative to asset directory
        full_path = asset_directory_ + "/" + filepath;
        test_file.open(full_path, std::ios::binary);
        if (test_file.is_open()) {
            test_file.close();
            spdlog::debug("Found mesh file in asset directory: {}", full_path);
        } else {
            spdlog::error("Mesh file not found. Tried: '{}' and '{}'", filepath, full_path);
            return nullptr;
        }
    }
    
    // Determine format from extension
    std::shared_ptr<MeshAsset> mesh;
    if (filepath.find(".gltf") != std::string::npos || filepath.find(".glb") != std::string::npos) {
        mesh = LoadGltfMesh(full_path);
    } else if (filepath.find(".obj") != std::string::npos) {
        mesh = LoadObjMesh(full_path);
    } else {
        spdlog::warn("Unknown mesh format: {}", filepath);
        return nullptr;
    }
    
    if (mesh) {
        mesh->SetName(filepath);
        loaded_meshes_[filepath] = mesh;
        spdlog::info("Loaded mesh: {} ({} vertices, {} indices)", 
                    filepath, mesh->GetVertexCount(), mesh->GetIndexCount());
        return mesh;
    }
    
    spdlog::error("Failed to load mesh: {}", filepath);
    return nullptr;
}

std::shared_ptr<MeshAsset> AssetManager::GetMesh(const std::string& filepath) const {
    auto it = loaded_meshes_.find(filepath);
    if (it != loaded_meshes_.end()) {
        return it->second;
    }
    return nullptr;
}

void AssetManager::UnloadMesh(const std::string& filepath) {
    auto it = loaded_meshes_.find(filepath);
    if (it != loaded_meshes_.end()) {
        spdlog::info("Unloading mesh: {}", filepath);
        loaded_meshes_.erase(it);
    }
}

void AssetManager::Clear() {
    spdlog::info("Clearing all loaded meshes");
    loaded_meshes_.clear();
}

std::vector<std::string> AssetManager::GetLoadedMeshes() const {
    std::vector<std::string> paths;
    for (const auto& pair : loaded_meshes_) {
        paths.push_back(pair.first);
    }
    return paths;
}

std::shared_ptr<MeshAsset> AssetManager::LoadGltfMesh(const std::string& filepath) {
    std::string err, warn;
    tinygltf::Model gltf_model;
    tinygltf::TinyGLTF loader;

    // Determine if binary or ASCII format
    bool is_binary = filepath.find(".glb") != std::string::npos;
    bool success = false;
    
    if (is_binary) {
        success = loader.LoadBinaryFromFile(&gltf_model, &err, &warn, filepath);
    } else {
        success = loader.LoadASCIIFromFile(&gltf_model, &err, &warn, filepath);
    }

    if (!success) {
        spdlog::error("Failed to load glTF file: {} - Error: {}", filepath, err);
        if (!warn.empty()) spdlog::warn("glTF loading warning: {}", warn);
        return nullptr;
    }

    if (!warn.empty()) {
        spdlog::warn("glTF loading warning: {}", warn);
    }

    auto mesh_asset = std::make_shared<MeshAsset>(filepath);

    // Process all meshes in the scene
    for (const auto& gltf_mesh : gltf_model.meshes) {
        // Process each primitive in the mesh
        for (const auto& primitive : gltf_mesh.primitives) {
            MeshPrimitive mesh_primitive;

            // Get position accessor
            if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
                int pos_accessor_idx = primitive.attributes.at("POSITION");
                const auto& accessor = gltf_model.accessors[pos_accessor_idx];
                const auto& buffer_view = gltf_model.bufferViews[accessor.bufferView];
                const auto& buffer = gltf_model.buffers[buffer_view.buffer];

                // Extract vertex positions, normals, and texcoords
                std::vector<float> positions;
                std::vector<float> normals;
                std::vector<float> texcoords;
                std::vector<uint32_t> indices;

                // Get position data
                int stride = buffer_view.byteStride > 0 ? buffer_view.byteStride : sizeof(float) * 3;
                int start_offset = buffer_view.byteOffset + accessor.byteOffset;

                for (int i = 0; i < accessor.count; ++i) {
                    int offset = start_offset + i * stride;
                    float x = *reinterpret_cast<float*>(const_cast<uint8_t*>(buffer.data.data() + offset));
                    float y = *reinterpret_cast<float*>(const_cast<uint8_t*>(buffer.data.data() + offset + 4));
                    float z = *reinterpret_cast<float*>(const_cast<uint8_t*>(buffer.data.data() + offset + 8));
                    positions.push_back(x);
                    positions.push_back(y);
                    positions.push_back(z);
                }

                // Get normal data if available
                if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                    int norm_accessor_idx = primitive.attributes.at("NORMAL");
                    const auto& norm_accessor = gltf_model.accessors[norm_accessor_idx];
                    const auto& norm_buffer_view = gltf_model.bufferViews[norm_accessor.bufferView];
                    const auto& norm_buffer = gltf_model.buffers[norm_buffer_view.buffer];

                    int norm_stride = norm_buffer_view.byteStride > 0 ? norm_buffer_view.byteStride : sizeof(float) * 3;
                    int norm_start_offset = norm_buffer_view.byteOffset + norm_accessor.byteOffset;

                    for (int i = 0; i < norm_accessor.count; ++i) {
                        int offset = norm_start_offset + i * norm_stride;
                        float x = *reinterpret_cast<float*>(const_cast<uint8_t*>(norm_buffer.data.data() + offset));
                        float y = *reinterpret_cast<float*>(const_cast<uint8_t*>(norm_buffer.data.data() + offset + 4));
                        float z = *reinterpret_cast<float*>(const_cast<uint8_t*>(norm_buffer.data.data() + offset + 8));
                        normals.push_back(x);
                        normals.push_back(y);
                        normals.push_back(z);
                    }
                } else {
                    // Generate flat normals if not provided
                    normals.resize(positions.size(), 0.0f);
                }

                // Get texcoord data if available
                if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                    int tex_accessor_idx = primitive.attributes.at("TEXCOORD_0");
                    const auto& tex_accessor = gltf_model.accessors[tex_accessor_idx];
                    const auto& tex_buffer_view = gltf_model.bufferViews[tex_accessor.bufferView];
                    const auto& tex_buffer = gltf_model.buffers[tex_buffer_view.buffer];

                    int tex_stride = tex_buffer_view.byteStride > 0 ? tex_buffer_view.byteStride : sizeof(float) * 2;
                    int tex_start_offset = tex_buffer_view.byteOffset + tex_accessor.byteOffset;

                    for (int i = 0; i < tex_accessor.count; ++i) {
                        int offset = tex_start_offset + i * tex_stride;
                        float u = *reinterpret_cast<float*>(const_cast<uint8_t*>(tex_buffer.data.data() + offset));
                        float v = *reinterpret_cast<float*>(const_cast<uint8_t*>(tex_buffer.data.data() + offset + 4));
                        texcoords.push_back(u);
                        texcoords.push_back(v);
                    }
                }

                // Get index data if available
                if (primitive.indices >= 0) {
                    const auto& idx_accessor = gltf_model.accessors[primitive.indices];
                    const auto& idx_buffer_view = gltf_model.bufferViews[idx_accessor.bufferView];
                    const auto& idx_buffer = gltf_model.buffers[idx_buffer_view.buffer];

                    int idx_start_offset = idx_buffer_view.byteOffset + idx_accessor.byteOffset;
                    
                    if (idx_accessor.componentType == 5123) { // UNSIGNED_SHORT
                        for (int i = 0; i < idx_accessor.count; ++i) {
                            uint16_t idx = *reinterpret_cast<uint16_t*>(
                                const_cast<uint8_t*>(idx_buffer.data.data() + idx_start_offset + i * 2));
                            indices.push_back((uint32_t)idx);
                        }
                    } else if (idx_accessor.componentType == 5125) { // UNSIGNED_INT
                        for (int i = 0; i < idx_accessor.count; ++i) {
                            uint32_t idx = *reinterpret_cast<uint32_t*>(
                                const_cast<uint8_t*>(idx_buffer.data.data() + idx_start_offset + i * 4));
                            indices.push_back(idx);
                        }
                    }
                }

                // Create vertices from extracted data
                int vertex_count = positions.size() / 3;
                for (int i = 0; i < vertex_count; ++i) {
                    Vertex vertex;
                    vertex.position = glm::vec3(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
                    vertex.normal = glm::vec3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
                    
                    if (texcoords.size() > i * 2) {
                        vertex.texcoord = glm::vec2(texcoords[i * 2], texcoords[i * 2 + 1]);
                    } else {
                        vertex.texcoord = glm::vec2(0.0f, 0.0f);
                    }
                    
                    vertex.color = glm::vec3(1.0f, 1.0f, 1.0f); // Default white
                    mesh_primitive.vertices.push_back(vertex);
                }

                // Set indices
                mesh_primitive.indices = indices;

                mesh_asset->AddPrimitive(mesh_primitive);
            }
        }
    }

    // Calculate mesh bounds
    mesh_asset->CalculateBounds();

    spdlog::info("Successfully loaded glTF mesh: {} ({} vertices, {} indices)", 
                filepath, mesh_asset->GetVertexCount(), mesh_asset->GetIndexCount());

    return mesh_asset;
}

std::shared_ptr<MeshAsset> AssetManager::LoadObjMesh(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("Cannot open OBJ file: {}", filepath);
        return nullptr;
    }

    auto mesh_asset = std::make_shared<MeshAsset>(filepath);
    MeshPrimitive mesh_primitive;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;

    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        
        std::string type = line.substr(start, line.find(' ', start) - start);

        // Parse vertex position: v x y z
        if (type == "v") {
            float x, y, z;
            if (std::sscanf(line.c_str(), "v %f %f %f", &x, &y, &z) == 3) {
                positions.push_back(glm::vec3(x, y, z));
            }
        }
        // Parse texture coordinate: vt u v
        else if (type == "vt") {
            float u, v;
            if (std::sscanf(line.c_str(), "vt %f %f", &u, &v) >= 2) {
                texcoords.push_back(glm::vec2(u, v));
            }
        }
        // Parse normal: vn x y z
        else if (type == "vn") {
            float x, y, z;
            if (std::sscanf(line.c_str(), "vn %f %f %f", &x, &y, &z) == 3) {
                normals.push_back(glm::vec3(x, y, z));
            }
        }
        // Parse face: f v/vt/vn v/vt/vn v/vt/vn
        else if (type == "f") {
            // Simple face parsing - handles triangles and quads (converts quads to triangles)
            std::vector<std::tuple<int, int, int>> face_vertices; // v_idx, vt_idx, vn_idx

            std::istringstream iss(line.substr(start + 1)); // Skip 'f '
            std::string vertex_str;

            while (iss >> vertex_str) {
                int v_idx = 0, vt_idx = 0, vn_idx = 0;
                
                size_t pos = 0;
                // Parse v
                size_t slash = vertex_str.find('/');
                if (slash != std::string::npos) {
                    v_idx = std::stoi(vertex_str.substr(0, slash)) - 1; // OBJ indices are 1-based
                    
                    // Parse vt
                    pos = slash + 1;
                    slash = vertex_str.find('/', pos);
                    if (slash != std::string::npos) {
                        if (pos != slash) { // Check if vt exists
                            vt_idx = std::stoi(vertex_str.substr(pos, slash - pos)) - 1;
                        }
                        // Parse vn
                        pos = slash + 1;
                        if (pos < vertex_str.length()) {
                            vn_idx = std::stoi(vertex_str.substr(pos)) - 1;
                        }
                    } else if (pos < vertex_str.length()) {
                        vt_idx = std::stoi(vertex_str.substr(pos)) - 1;
                    }
                } else {
                    v_idx = std::stoi(vertex_str) - 1;
                }

                face_vertices.push_back(std::make_tuple(v_idx, vt_idx, vn_idx));
            }

            // Convert face to triangles (fan triangulation)
            for (size_t i = 1; i < face_vertices.size() - 1; ++i) {
                // Triangle: vertex 0, vertex i, vertex i+1
                for (int j = 0; j < 3; ++j) {
                    size_t vert_idx_in_face = (j == 0) ? 0 : (i + j - 1);
                    auto [v_idx, vt_idx, vn_idx] = face_vertices[vert_idx_in_face];
                    
                    Vertex vert;
                    if (v_idx >= 0 && v_idx < (int)positions.size()) {
                        vert.position = positions[v_idx];
                    }
                    if (vt_idx >= 0 && vt_idx < (int)texcoords.size()) {
                        vert.texcoord = texcoords[vt_idx];
                    } else {
                        vert.texcoord = glm::vec2(0.0f, 0.0f);
                    }
                    if (vn_idx >= 0 && vn_idx < (int)normals.size()) {
                        vert.normal = normals[vn_idx];
                    } else {
                        vert.normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default up normal
                    }
                    vert.color = glm::vec3(1.0f, 1.0f, 1.0f);
                    
                    // Add vertex and its index
                    mesh_primitive.vertices.push_back(vert);
                    mesh_primitive.indices.push_back(mesh_primitive.vertices.size() - 1);
                }
            }
        }
    }

    file.close();

    if (mesh_primitive.vertices.empty()) {
        spdlog::error("OBJ file is empty or has no faces: {}", filepath);
        return nullptr;
    }

    mesh_asset->AddPrimitive(mesh_primitive);
    mesh_asset->CalculateBounds();

    spdlog::info("Successfully loaded OBJ mesh: {} ({} vertices, {} indices)", 
                filepath, mesh_asset->GetVertexCount(), mesh_asset->GetIndexCount());

    return mesh_asset;
}

} // namespace schizo::assets
