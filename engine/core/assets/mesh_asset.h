#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>

namespace schizo::assets {

/**
 * @struct Vertex
 * @brief Vertex data for mesh rendering
 */
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texcoord;
    glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);  // Default white
};

/**
 * @struct MeshPrimitive
 * @brief A single mesh primitive (subset of vertices/indices)
 */
struct MeshPrimitive {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::string material_name;
};

/**
 * @class MeshAsset
 * @brief Represents a loaded 3D mesh from a file (glTF, obj, etc)
 */
class MeshAsset {
public:
    MeshAsset() = default;
    explicit MeshAsset(const std::string& filepath) : filepath_(filepath) {}
    
    ~MeshAsset() = default;
    
    // Deleted copy, allow move
    MeshAsset(const MeshAsset&) = delete;
    MeshAsset& operator=(const MeshAsset&) = delete;
    MeshAsset(MeshAsset&&) = default;
    MeshAsset& operator=(MeshAsset&&) = default;
    
    /**
     * @brief Get the file path this mesh was loaded from
     */
    const std::string& GetFilePath() const { return filepath_; }
    
    /**
     * @brief Get the mesh name
     */
    const std::string& GetName() const { return name_; }
    void SetName(const std::string& name) { name_ = name; }
    
    /**
     * @brief Get primitives (mesh parts)
     */
    const std::vector<MeshPrimitive>& GetPrimitives() const { return primitives_; }
    std::vector<MeshPrimitive>& GetPrimitives() { return primitives_; }
    
    /**
     * @brief Add a primitive to this mesh
     */
    void AddPrimitive(const MeshPrimitive& primitive) {
        primitives_.push_back(primitive);
    }
    
    /**
     * @brief Get bounds
     */
    glm::vec3 GetBoundsMin() const { return bounds_min_; }
    glm::vec3 GetBoundsMax() const { return bounds_max_; }
    
    void SetBounds(const glm::vec3& min, const glm::vec3& max) {
        bounds_min_ = min;
        bounds_max_ = max;
    }
    
    /**
     * @brief Calculate bounds from all vertices
     */
    void CalculateBounds() {
        if (primitives_.empty()) {
            bounds_min_ = glm::vec3(0.0f);
            bounds_max_ = glm::vec3(0.0f);
            return;
        }
        
        bounds_min_ = glm::vec3(std::numeric_limits<float>::max());
        bounds_max_ = glm::vec3(std::numeric_limits<float>::lowest());
        
        for (const auto& prim : primitives_) {
            for (const auto& vertex : prim.vertices) {
                bounds_min_ = glm::min(bounds_min_, vertex.position);
                bounds_max_ = glm::max(bounds_max_, vertex.position);
            }
        }
    }
    
    /**
     * @brief Get total vertex count across all primitives
     */
    size_t GetVertexCount() const {
        size_t count = 0;
        for (const auto& prim : primitives_) {
            count += prim.vertices.size();
        }
        return count;
    }
    
    /**
     * @brief Get total index count across all primitives
     */
    size_t GetIndexCount() const {
        size_t count = 0;
        for (const auto& prim : primitives_) {
            count += prim.indices.size();
        }
        return count;
    }

private:
    std::string filepath_;
    std::string name_;
    std::vector<MeshPrimitive> primitives_;
    glm::vec3 bounds_min_ = glm::vec3(0.0f);
    glm::vec3 bounds_max_ = glm::vec3(0.0f);
};

} // namespace schizo::assets
