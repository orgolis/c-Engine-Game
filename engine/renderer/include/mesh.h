#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>
#include <string>

namespace schizo::renderer {

// Forward declaration
class RenderDevice;

// ============================================================================
// Vertex Structures
// ============================================================================

/**
 * @struct Vertex
 * @brief Basic vertex with position and color
 */
struct Vertex {
    glm::vec3 position;
    glm::vec4 color;
    
    Vertex() = default;
    Vertex(const glm::vec3& pos, const glm::vec4& col = glm::vec4(1.0f))
        : position(pos), color(col) {}
};

/**
 * @struct VertexPN
 * @brief Vertex with position and normal
 */
struct VertexPN {
    glm::vec3 position;
    glm::vec3 normal;
    
    VertexPN() = default;
    VertexPN(const glm::vec3& pos, const glm::vec3& norm = glm::vec3(0, 1, 0))
        : position(pos), normal(norm) {}
};

/**
 * @struct VertexPNC
 * @brief Vertex with position, normal, and color
 */
struct VertexPNC {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 color;
    
    VertexPNC() = default;
    VertexPNC(const glm::vec3& pos, const glm::vec3& norm, const glm::vec4& col)
        : position(pos), normal(norm), color(col) {}
};

/**
 * @struct VertexPNUV
 * @brief Vertex with position, normal, and UV coordinates (12 floats)
 */
struct VertexPNUV {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    
    VertexPNUV() = default;
    VertexPNUV(const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& tex)
        : position(pos), normal(norm), uv(tex) {}
};

/**
 * @struct VertexPNTUV
 * @brief Full vertex: position, normal, tangent, UV (16 floats) - for normal mapping
 */
struct VertexPNTUV {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 tangent;      // w = handedness (-1 or 1)
    glm::vec2 uv;
    
    VertexPNTUV() = default;
    VertexPNTUV(const glm::vec3& pos, const glm::vec3& norm, const glm::vec4& tan, const glm::vec2& tex)
        : position(pos), normal(norm), tangent(tan), uv(tex) {}
};

/**
 * @class Mesh
 * @brief Manages vertex/index buffers and rendering
 */
class Mesh {
public:
    Mesh();
    ~Mesh();
    
    // Deleted copy operations
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    
    // Allow move operations
    Mesh(Mesh&&) = default;
    Mesh& operator=(Mesh&&) = default;
    
    /**
     * Initialize mesh with vertex and index data
     */
    void SetData(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    
    /**
     * Draw the mesh using indexed rendering
     */
    void Draw(RenderDevice* device, uint32_t mode = 0x0004);  // GL_TRIANGLES = 0x0004
    
    /**
     * Get vertex/index counts
     */
    uint32_t GetVertexCount() const { return vertex_count_; }
    uint32_t GetIndexCount() const { return index_count_; }
    
    // Primitive generators
    /**
     * Create a simple triangle mesh
     */
    static std::unique_ptr<Mesh> CreateTriangle(RenderDevice* device);
    
    /**
     * Create a cube mesh
     */
    static std::unique_ptr<Mesh> CreateCube(RenderDevice* device);
    
    /**
     * Create a grid/plane mesh
     */
    static std::unique_ptr<Mesh> CreateGrid(RenderDevice* device, uint32_t width, uint32_t height);
    

    /**
     * Create a UV sphere mesh
     */
    static std::unique_ptr<Mesh> CreateSphere(RenderDevice* device, float radius, uint32_t segments, uint32_t rings);
    
    /**
     * Create a torus mesh
     */
    static std::unique_ptr<Mesh> CreateTorus(RenderDevice* device, float major_radius, float minor_radius, 
                                            uint32_t segments, uint32_t rings);
    
    /**
     * Create a cylinder mesh
     */
    static std::unique_ptr<Mesh> CreateCylinder(RenderDevice* device, float radius, float height,
                                               uint32_t segments, bool include_caps = true);
    
    /**
     * Create a capsule mesh (cylinder with hemispherical caps)
     */
    static std::unique_ptr<Mesh> CreateCapsule(RenderDevice* device, float radius, float height,
                                              uint32_t segments, uint32_t rings);
    
    /**
     * Create a cone mesh
     */
    static std::unique_ptr<Mesh> CreateCone(RenderDevice* device, float radius, float height,
                                           uint32_t segments, bool include_cap = true);
    
    /**
     * Create a pyramid mesh
     */
    static std::unique_ptr<Mesh> CreatePyramid(RenderDevice* device, float base_size, float height);
    
    /**
     * Create an icosphere mesh (more uniform than UV sphere)
     */
    static std::unique_ptr<Mesh> CreateIcosphere(RenderDevice* device, float radius, uint32_t subdivisions);
    
    /**
     * Create a full-screen quad mesh (for post-processing and deferred rendering)
     * Covers NDC space from -1 to 1 in both dimensions
     */
    static std::unique_ptr<Mesh> CreateQuad(RenderDevice* device);

    // ========== Bounding Box ==========
    
    /**
     * Calculate and cache bounding box
     */
    void CalculateBounds();
    
    /**
     * Get mesh bounding box (center)
     */
    glm::vec3 GetBoundsCenter() const { return bounds_center_; }
    
    /**
     * Get mesh bounding box (extents)
     */
    glm::vec3 GetBoundsExtents() const { return bounds_extents_; }
    
    /**
     * Get mesh bounding box (radius)
     */
    float GetBoundsRadius() const { return bounds_radius_; }
    
    // ========== Level of Detail (LOD) System ==========
    
    /**
     * Add a LOD level to this mesh
     * @param lod_level LOD index (0 = highest detail)
     * @param vertices LOD vertex data
     * @param indices LOD index data
     * @param distance_threshold Distance at which to switch to this LOD
     * @return True if successfully added
     */
    bool AddLODLevel(uint32_t lod_level, 
                     const std::vector<Vertex>& vertices,
                     const std::vector<uint32_t>& indices,
                     float distance_threshold = 0.0f);
    
    /**
     * Get number of LOD levels
     */
    uint32_t GetLODCount() const { return lod_levels_.size(); }
    
    /**
     * Select LOD based on camera distance
     * @param camera_distance Distance from camera
     * @return Selected LOD level (0-based)
     */
    uint32_t SelectLOD(float camera_distance) const;
    
    /**
     * Draw specific LOD level
     * @param device Graphics device
     * @param lod_level LOD index to draw
     * @param mode Draw mode (default GL_TRIANGLES)
     */
    void DrawLOD(RenderDevice* device, uint32_t lod_level, uint32_t mode = 0x0004);
    
    /**
     * Draw with automatic LOD selection based on distance
     * @param device Graphics device
     * @param camera_distance Distance from camera
     * @param mode Draw mode
     */
    void DrawAuto(RenderDevice* device, float camera_distance, uint32_t mode = 0x0004);
    
    /**
     * Get vertex count for specific LOD
     */
    uint32_t GetVertexCountLOD(uint32_t lod_level) const;
    
    /**
     * Get index count for specific LOD
     */
    uint32_t GetIndexCountLOD(uint32_t lod_level) const;

private:
    // GPU resources
    uint32_t vao_ = 0;           // Vertex Array Object
    uint32_t vbo_ = 0;           // Vertex Buffer Object
    uint32_t ibo_ = 0;           // Index Buffer Object
    
    // CPU data
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    
    uint32_t vertex_count_ = 0;
    uint32_t index_count_ = 0;

    // Bounding box
    glm::vec3 bounds_center_ = glm::vec3(0);
    glm::vec3 bounds_extents_ = glm::vec3(0);
    float bounds_radius_ = 0.0f;
    
    // LOD System
    struct LODLevel {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t vao = 0;
        uint32_t vbo = 0;
        uint32_t ibo = 0;
        float distance_threshold = 0.0f;
    };
    
    std::vector<LODLevel> lod_levels_;
    
    /**
     * Upload vertex/index data to GPU
     */
    void UploadToGPU(RenderDevice* device);
    
    /**
     * Cleanup GPU resources
     */
    void Cleanup(RenderDevice* device);
};

} // namespace schizo::renderer
