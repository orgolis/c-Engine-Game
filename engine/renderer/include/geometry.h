#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>
#include <string>

namespace schizo::geometry {

// Forward declarations
class Mesh;

// ============================================================================
// Geometry Data Structures
// ============================================================================

/**
 * @struct Triangle
 * @brief Triangle data
 */
struct Triangle {
    glm::vec3 v0, v1, v2;
    
    glm::vec3 GetCenter() const { return (v0 + v1 + v2) / 3.0f; }
    glm::vec3 GetNormal() const { return glm::normalize(glm::cross(v1 - v0, v2 - v0)); }
    float GetArea() const { return glm::length(glm::cross(v1 - v0, v2 - v0)) * 0.5f; }
};

/**
 * @struct BoundingBox
 * @brief Axis-aligned bounding box
 */
struct BoundingBox {
    glm::vec3 min;
    glm::vec3 max;
    
    BoundingBox() : min(0), max(0) {}
    BoundingBox(const glm::vec3& min_v, const glm::vec3& max_v) : min(min_v), max(max_v) {}
    
    glm::vec3 GetCenter() const { return (min + max) * 0.5f; }
    glm::vec3 GetExtents() const { return (max - min) * 0.5f; }
    float GetRadius() const { return glm::length(GetExtents()); }
    
    bool Contains(const glm::vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }
    
    bool Intersects(const BoundingBox& other) const {
        return !(max.x < other.min.x || min.x > other.max.x ||
                 max.y < other.min.y || min.y > other.max.y ||
                 max.z < other.min.z || min.z > other.max.z);
    }
};

/**
 * @struct Sphere
 * @brief Bounding sphere
 */
struct Sphere {
    glm::vec3 center;
    float radius;
    
    Sphere(const glm::vec3& c = glm::vec3(0), float r = 0.0f) : center(c), radius(r) {}
    
    bool Contains(const glm::vec3& point) const {
        return glm::distance(center, point) <= radius;
    }
    
    bool Intersects(const Sphere& other) const {
        float dist = glm::distance(center, other.center);
        return dist <= (radius + other.radius);
    }
};

/**
 * @struct Plane
 * @brief Infinite plane defined by normal and distance
 */
struct Plane {
    glm::vec3 normal;
    float distance;
    
    Plane() : normal(0, 1, 0), distance(0) {}
    Plane(const glm::vec3& n, float d) : normal(glm::normalize(n)), distance(d) {}
    
    float GetDistance(const glm::vec3& point) const {
        return glm::dot(normal, point) - distance;
    }
};

// ============================================================================
// Mesh Geometry Queries
// ============================================================================

/**
 * Calculate bounding box of mesh vertices
 */
BoundingBox CalculateMeshBounds(const std::vector<glm::vec3>& positions);

/**
 * Calculate bounding sphere of mesh vertices (Welzl's algorithm)
 */
Sphere CalculateMeshBoundingSphere(const std::vector<glm::vec3>& positions);

/**
 * Calculate mesh surface area
 */
float CalculateMeshSurfaceArea(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);

/**
 * Calculate mesh volume (for closed meshes)
 */
float CalculateMeshVolume(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);

/**
 * Calculate mesh center of mass
 */
glm::vec3 CalculateMeshCenterOfMass(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);

// ============================================================================
// Normal and Tangent Calculation
// ============================================================================

/**
 * Calculate vertex normals from triangle indices
 */
std::vector<glm::vec3> CalculateNormals(const std::vector<glm::vec3>& positions, 
                                         const std::vector<uint32_t>& indices);

/**
 * Calculate vertex tangents and bitangents for normal mapping
 */
void CalculateTangents(std::vector<glm::vec4>& tangents,
                       std::vector<glm::vec3>& bitangents,
                       const std::vector<glm::vec3>& positions,
                       const std::vector<glm::vec3>& normals,
                       const std::vector<glm::vec2>& texcoords,
                       const std::vector<uint32_t>& indices);

/**
 * Calculate smooth normals weighted by triangle area
 */
std::vector<glm::vec3> CalculateSmoothedNormals(const std::vector<glm::vec3>& positions, 
                                                 const std::vector<uint32_t>& indices);

/**
 * Calculate face normals (one per triangle)
 */
std::vector<glm::vec3> CalculateFaceNormals(const std::vector<glm::vec3>& positions, 
                                             const std::vector<uint32_t>& indices);

// ============================================================================
// Mesh Optimization
// ============================================================================

/**
 * @struct WeldInfo
 * @brief Result of vertex welding operation
 */
struct WeldInfo {
    std::vector<uint32_t> new_indices;
    std::vector<uint32_t> old_to_new;  // Maps old vertex index to new
    uint32_t removed_count = 0;
};

/**
 * Weld identical vertices together
 * @param positions Vertex positions
 * @param threshold Distance threshold for considering vertices identical
 */
WeldInfo WeldVertices(const std::vector<glm::vec3>& positions, 
                     const std::vector<uint32_t>& indices,
                     float threshold = 0.0001f);

/**
 * Remove degenerate triangles (zero area)
 */
std::vector<uint32_t> RemoveDegenerateTriangles(const std::vector<glm::vec3>& positions, 
                                                 const std::vector<uint32_t>& indices);

/**
 * Calculate vertex normals with angle weighting
 */
std::vector<glm::vec3> CalculateAngleWeightedNormals(const std::vector<glm::vec3>& positions,
                                                      const std::vector<uint32_t>& indices);

// ============================================================================
// Mesh Transformation
// ============================================================================

/**
 * Apply transformation matrix to positions
 */
void TransformPositions(std::vector<glm::vec3>& positions, const glm::mat4& transform);

/**
 * Apply transformation matrix to positions and normals
 */
void TransformPositionsAndNormals(std::vector<glm::vec3>& positions,
                                  std::vector<glm::vec3>& normals,
                                  const glm::mat4& transform);

/**
 * Scale mesh by uniform factor
 */
void ScaleMesh(std::vector<glm::vec3>& positions, float scale);

/**
 * Center mesh at origin
 */
void CenterMesh(std::vector<glm::vec3>& positions);

/**
 * Normalize mesh to unit bounds
 */
void NormalizeMesh(std::vector<glm::vec3>& positions);

// ============================================================================
// Mesh Simplification (LOD)
// ============================================================================

/**
 * Simple mesh simplification using vertex clustering
 * @param positions Vertex positions
 * @param indices Triangle indices
 * @param target_ratio Target reduction ratio (e.g., 0.5 for 50% reduction)
 */
std::vector<uint32_t> SimplifyMesh(const std::vector<glm::vec3>& positions,
                                    const std::vector<uint32_t>& indices,
                                    float target_ratio);

/**
 * Create multiple LOD levels automatically
 */
struct MeshLOD {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    float error_metric = 0.0f;
};

std::vector<MeshLOD> GenerateLODLevels(const std::vector<glm::vec3>& positions,
                                       const std::vector<uint32_t>& indices,
                                       uint32_t level_count = 4);

// ============================================================================
// Procedural Geometry Generation
// ============================================================================

/**
 * Generate terrain heightmap mesh
 * @param width Grid width
 * @param height Grid height
 * @param scale Height scale factor
 * @param heightmap Height values (must be width * height)
 */
void GenerateTerrainMesh(std::vector<glm::vec3>& out_positions,
                        std::vector<uint32_t>& out_indices,
                        uint32_t width, uint32_t height,
                        const std::vector<float>& heightmap,
                        float scale = 1.0f);

/**
 * Generate simplex noise-based terrain
 */
void GenerateNoiseTerrain(std::vector<glm::vec3>& out_positions,
                         std::vector<uint32_t>& out_indices,
                         uint32_t width, uint32_t height,
                         float scale = 1.0f,
                         float frequency = 1.0f,
                         float amplitude = 1.0f);

/**
 * Generate tube/path mesh following a curve
 */
void GenerateTube(std::vector<glm::vec3>& out_positions,
                 std::vector<uint32_t>& out_indices,
                 const std::vector<glm::vec3>& curve_points,
                 float radius, uint32_t segments = 8);

/**
 * Generate text mesh from glyph outlines
 */
void GenerateTextMesh(std::vector<glm::vec3>& out_positions,
                     std::vector<uint32_t>& out_indices,
                     const std::string& text,
                     float size = 1.0f);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Check if mesh is closed (valid for volume calculation)
 */
bool IsMeshClosed(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);

/**
 * Check if mesh is manifold
 */
bool IsMeshManifold(const std::vector<uint32_t>& indices);

/**
 * Invert mesh winding order
 */
void InvertWinding(std::vector<uint32_t>& indices);

/**
 * Calculate mesh quality metrics
 */
struct MeshQuality {
    float min_edge_length = 0.0f;
    float max_edge_length = 0.0f;
    float avg_triangle_area = 0.0f;
    float aspect_ratio = 0.0f;  // Best case = 1.0
};

MeshQuality CalculateMeshQuality(const std::vector<glm::vec3>& positions, 
                                 const std::vector<uint32_t>& indices);

} // namespace schizo::geometry

#endif // GEOMETRY_H
