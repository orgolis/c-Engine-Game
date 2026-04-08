#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace schizo::editor {

// ============================================================================
// Primitive Mesh Generator
// ============================================================================

/**
 * @struct Vertex
 * @brief Simple vertex for rendering (position + color)
 */
struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    
    Vertex() = default;
    Vertex(const glm::vec3& pos, const glm::vec3& col = glm::vec3(1.0f))
        : position(pos), color(col) {}
};

/**
 * @class MeshGenerator
 * @brief Generates simple primitive meshes for editor visualization
 */
class MeshGenerator {
public:
    /**
     * @brief Generate a cube mesh
     * @return Pair of (vertices, indices)
     */
    static std::pair<std::vector<Vertex>, std::vector<uint32_t>> GenerateCube(float size = 1.0f);
    
    /**
     * @brief Generate a sphere mesh
     * @return Pair of (vertices, indices)
     */
    static std::pair<std::vector<Vertex>, std::vector<uint32_t>> GenerateSphere(float radius = 0.5f, uint32_t segments = 16, uint32_t rings = 16);
    
    /**
     * @brief Generate a cylinder mesh
     * @return Pair of (vertices, indices)
     */
    static std::pair<std::vector<Vertex>, std::vector<uint32_t>> GenerateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 16);
    
    /**
     * @brief Generate a simple pyramid mesh
     * @return Pair of (vertices, indices)
     */
    static std::pair<std::vector<Vertex>, std::vector<uint32_t>> GeneratePyramid(float size = 1.0f);
    
    /**
     * @brief Generate a plane mesh
     * @return Pair of (vertices, indices)
     */
    static std::pair<std::vector<Vertex>, std::vector<uint32_t>> GeneratePlane(float size = 1.0f);
    
    /**
     * @brief Generate a capsule mesh
     * @return Pair of (vertices, indices)
     */
    static std::pair<std::vector<Vertex>, std::vector<uint32_t>> GenerateCapsule(float radius = 0.5f, float height = 2.0f, uint32_t segments = 16);
};

} // namespace schizo::editor
