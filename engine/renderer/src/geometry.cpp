#include "geometry.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <set>
#include <limits>
#include <cstring>

namespace schizo::geometry {

// ============================================================================
// Mesh Geometry Queries
// ============================================================================

BoundingBox CalculateMeshBounds(const std::vector<glm::vec3>& positions) {
    if (positions.empty()) {
        return BoundingBox(glm::vec3(0), glm::vec3(0));
    }
    
    glm::vec3 min = positions[0];
    glm::vec3 max = positions[0];
    
    for (const auto& p : positions) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    
    return BoundingBox(min, max);
}

Sphere CalculateMeshBoundingSphere(const std::vector<glm::vec3>& positions) {
    if (positions.empty()) {
        return Sphere();
    }
    
    // Welzl's algorithm for minimum bounding sphere
    // Simplified version: use AABB center and farthest point
    BoundingBox bbox = CalculateMeshBounds(positions);
    glm::vec3 center = bbox.GetCenter();
    
    float max_dist = 0.0f;
    for (const auto& p : positions) {
        float dist = glm::distance(center, p);
        if (dist > max_dist) {
            max_dist = dist;
        }
    }
    
    return Sphere(center, max_dist);
}

float CalculateMeshSurfaceArea(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices) {
    float area = 0.0f;
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        const auto& v0 = positions[indices[i]];
        const auto& v1 = positions[indices[i + 1]];
        const auto& v2 = positions[indices[i + 2]];
        
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        
        area += glm::length(glm::cross(edge1, edge2)) * 0.5f;
    }
    
    return area;
}

float CalculateMeshVolume(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices) {
    float volume = 0.0f;
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        const auto& v0 = positions[indices[i]];
        const auto& v1 = positions[indices[i + 1]];
        const auto& v2 = positions[indices[i + 2]];
        
        volume += glm::dot(v0, glm::cross(v1, v2)) / 6.0f;
    }
    
    return std::abs(volume);
}

glm::vec3 CalculateMeshCenterOfMass(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices) {
    glm::vec3 com(0.0f);
    float total_volume = 0.0f;
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        const auto& v0 = positions[indices[i]];
        const auto& v1 = positions[indices[i + 1]];
        const auto& v2 = positions[indices[i + 2]];
        
        float vol = glm::dot(v0, glm::cross(v1, v2)) / 6.0f;
        glm::vec3 center = (v0 + v1 + v2) / 3.0f;
        
        com += center * vol;
        total_volume += vol;
    }
    
    if (total_volume != 0.0f) {
        com /= total_volume;
    }
    
    return com;
}

// ============================================================================
// Normal and Tangent Calculation
// ============================================================================

std::vector<glm::vec3> CalculateNormals(const std::vector<glm::vec3>& positions, 
                                         const std::vector<uint32_t>& indices) {
    std::vector<glm::vec3> normals(positions.size(), glm::vec3(0));
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        
        const auto& p0 = positions[i0];
        const auto& p1 = positions[i1];
        const auto& p2 = positions[i2];
        
        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        glm::vec3 face_normal = glm::cross(edge1, edge2);
        
        normals[i0] += face_normal;
        normals[i1] += face_normal;
        normals[i2] += face_normal;
    }
    
    for (auto& n : normals) {
        if (glm::length(n) > 0.0001f) {
            n = glm::normalize(n);
        } else {
            n = glm::vec3(0, 1, 0);
        }
    }
    
    return normals;
}

void CalculateTangents(std::vector<glm::vec4>& tangents,
                       std::vector<glm::vec3>& bitangents,
                       const std::vector<glm::vec3>& positions,
                       const std::vector<glm::vec3>& normals,
                       const std::vector<glm::vec2>& texcoords,
                       const std::vector<uint32_t>& indices) {
    tangents.assign(positions.size(), glm::vec4(0));
    bitangents.assign(positions.size(), glm::vec3(0));
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        
        const auto& p0 = positions[i0];
        const auto& p1 = positions[i1];
        const auto& p2 = positions[i2];
        
        const auto& uv0 = texcoords[i0];
        const auto& uv1 = texcoords[i1];
        const auto& uv2 = texcoords[i2];
        
        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        glm::vec2 duv1 = uv1 - uv0;
        glm::vec2 duv2 = uv2 - uv0;
        
        float f = 1.0f / (duv1.x * duv2.y - duv2.x * duv1.y + 0.0001f);
        
        glm::vec3 tangent = (duv2.y * edge1 - duv1.y * edge2) * f;
        glm::vec3 bitangent = (duv1.x * edge2 - duv2.x * edge1) * f;
        
        tangent = glm::normalize(tangent);
        bitangent = glm::normalize(bitangent);
        
        tangents[i0] += glm::vec4(tangent, 0);
        tangents[i1] += glm::vec4(tangent, 0);
        tangents[i2] += glm::vec4(tangent, 0);
        
        bitangents[i0] += bitangent;
        bitangents[i1] += bitangent;
        bitangents[i2] += bitangent;
    }
    
    // Apply Gram-Schmidt orthogonalization
    for (size_t i = 0; i < positions.size(); ++i) {
        const auto& n = normals[i];
        auto& t = tangents[i];
        
        // Orthogonalize tangent
        glm::vec3 t_vec(t);
        t_vec = t_vec - n * glm::dot(n, t_vec);
        if (glm::length(t_vec) > 0.0001f) {
            t_vec = glm::normalize(t_vec);
        } else {
            t_vec = glm::vec3(1, 0, 0);
        }
        
        // Calculate handedness
        float handedness = glm::dot(glm::cross(n, t_vec), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
        
        t = glm::vec4(t_vec, handedness);
        bitangents[i] = glm::normalize(n) * handedness;
    }
}

std::vector<glm::vec3> CalculateSmoothedNormals(const std::vector<glm::vec3>& positions,
                                                 const std::vector<uint32_t>& indices) {
    std::vector<glm::vec3> normals(positions.size(), glm::vec3(0));
    std::vector<float> total_area(positions.size(), 0.0f);
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        
        const auto& p0 = positions[i0];
        const auto& p1 = positions[i1];
        const auto& p2 = positions[i2];
        
        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        glm::vec3 face_normal = glm::cross(edge1, edge2);
        float area = glm::length(face_normal) * 0.5f;
        
        normals[i0] += face_normal * area;
        normals[i1] += face_normal * area;
        normals[i2] += face_normal * area;
        
        total_area[i0] += area;
        total_area[i1] += area;
        total_area[i2] += area;
    }
    
    for (size_t i = 0; i < normals.size(); ++i) {
        if (total_area[i] > 0.0001f) {
            normals[i] = glm::normalize(normals[i]);
        } else {
            normals[i] = glm::vec3(0, 1, 0);
        }
    }
    
    return normals;
}

std::vector<glm::vec3> CalculateFaceNormals(const std::vector<glm::vec3>& positions, 
                                             const std::vector<uint32_t>& indices) {
    std::vector<glm::vec3> face_normals;
    face_normals.reserve(indices.size() / 3);
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        const auto& p0 = positions[indices[i]];
        const auto& p1 = positions[indices[i + 1]];
        const auto& p2 = positions[indices[i + 2]];
        
        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
        
        face_normals.push_back(normal);
    }
    
    return face_normals;
}

// ============================================================================
// Mesh Transformation
// ============================================================================

void TransformPositions(std::vector<glm::vec3>& positions, const glm::mat4& transform) {
    for (auto& p : positions) {
        glm::vec4 p4(p, 1.0f);
        p4 = transform * p4;
        p = glm::vec3(p4) / p4.w;
    }
}

void TransformPositionsAndNormals(std::vector<glm::vec3>& positions,
                                  std::vector<glm::vec3>& normals,
                                  const glm::mat4& transform) {
    glm::mat3 normal_matrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    
    for (auto& p : positions) {
        glm::vec4 p4(p, 1.0f);
        p4 = transform * p4;
        p = glm::vec3(p4) / p4.w;
    }
    
    for (auto& n : normals) {
        n = glm::normalize(normal_matrix * n);
    }
}

void ScaleMesh(std::vector<glm::vec3>& positions, float scale) {
    for (auto& p : positions) {
        p *= scale;
    }
}

void CenterMesh(std::vector<glm::vec3>& positions) {
    if (positions.empty()) return;
    
    glm::vec3 center(0);
    for (const auto& p : positions) {
        center += p;
    }
    center /= positions.size();
    
    for (auto& p : positions) {
        p -= center;
    }
}

void NormalizeMesh(std::vector<glm::vec3>& positions) {
    BoundingBox bbox = CalculateMeshBounds(positions);
    CenterMesh(positions);
    
    glm::vec3 extents = bbox.GetExtents();
    float scale = 1.0f / std::max({extents.x, extents.y, extents.z});
    ScaleMesh(positions, scale);
}

// ============================================================================
// Mesh Simplification
// ============================================================================

std::vector<uint32_t> SimplifyMesh(const std::vector<glm::vec3>& positions,
                                    const std::vector<uint32_t>& indices,
                                    float target_ratio) {
    // Simplified vertex clustering approach
    BoundingBox bbox = CalculateMeshBounds(positions);
    glm::vec3 extents = bbox.GetExtents();
    
    float cluster_size = std::max({extents.x, extents.y, extents.z}) / std::sqrt(1.0f / target_ratio);
    
    std::unordered_map<uint32_t, uint32_t> grid_to_vertex;
    std::vector<uint32_t> vertex_map(positions.size());
    uint32_t new_vertex_count = 0;
    
    for (uint32_t i = 0; i < positions.size(); ++i) {
        glm::vec3 rel = positions[i] - bbox.min;
        uint32_t gx = static_cast<uint32_t>(rel.x / cluster_size);
        uint32_t gy = static_cast<uint32_t>(rel.y / cluster_size);
        uint32_t gz = static_cast<uint32_t>(rel.z / cluster_size);
        
        uint32_t grid_hash = gx * 73856093 ^ gy * 19349663 ^ gz * 83492791;
        
        auto it = grid_to_vertex.find(grid_hash);
        if (it != grid_to_vertex.end()) {
            vertex_map[i] = it->second;
        } else {
            vertex_map[i] = new_vertex_count;
            grid_to_vertex[grid_hash] = new_vertex_count++;
        }
    }
    
    std::vector<uint32_t> simplified_indices;
    std::set<uint64_t> seen;
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t a = vertex_map[indices[i]];
        uint32_t b = vertex_map[indices[i + 1]];
        uint32_t c = vertex_map[indices[i + 2]];
        
        if (a != b && b != c && a != c) {
            uint64_t hash = ((uint64_t)a << 32) | b;
            if (seen.find(hash) == seen.end()) {
                seen.insert(hash);
                simplified_indices.push_back(a);
                simplified_indices.push_back(b);
                simplified_indices.push_back(c);
            }
        }
    }
    
    spdlog::info("Simplified mesh: {} -> {} triangles", indices.size() / 3, simplified_indices.size() / 3);
    return simplified_indices;
}

std::vector<MeshLOD> GenerateLODLevels(const std::vector<glm::vec3>& positions,
                                       const std::vector<uint32_t>& indices,
                                       uint32_t level_count) {
    std::vector<MeshLOD> lods;
    
    std::vector<glm::vec3> current_positions = positions;
    std::vector<uint32_t> current_indices = indices;
    
    for (uint32_t level = 0; level < level_count; ++level) {
        float ratio = 1.0f - (level * 0.25f);  // 100%, 75%, 50%, 25%
        ratio = std::max(ratio, 0.1f);
        
        MeshLOD lod;
        lod.positions = current_positions;
        lod.indices = SimplifyMesh(current_positions, current_indices, ratio);
        lod.error_metric = 1.0f / (level + 1);
        
        lods.push_back(lod);
        
        current_indices = lod.indices;
    }
    
    return lods;
}

// ============================================================================
// Procedural Geometry
// ============================================================================

void GenerateTerrainMesh(std::vector<glm::vec3>& out_positions,
                        std::vector<uint32_t>& out_indices,
                        uint32_t width, uint32_t height,
                        const std::vector<float>& heightmap,
                        float scale) {
    out_positions.clear();
    out_indices.clear();
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            float h = heightmap[y * width + x] * scale;
            out_positions.emplace_back(x, h, y);
        }
    }
    
    for (uint32_t y = 0; y < height - 1; ++y) {
        for (uint32_t x = 0; x < width - 1; ++x) {
            uint32_t v0 = y * width + x;
            uint32_t v1 = v0 + 1;
            uint32_t v2 = v0 + width;
            uint32_t v3 = v2 + 1;
            
            out_indices.push_back(v0); out_indices.push_back(v2); out_indices.push_back(v1);
            out_indices.push_back(v1); out_indices.push_back(v2); out_indices.push_back(v3);
        }
    }
}

void GenerateTube(std::vector<glm::vec3>& out_positions,
                 std::vector<uint32_t>& out_indices,
                 const std::vector<glm::vec3>& curve_points,
                 float radius, uint32_t segments) {
    out_positions.clear();
    out_indices.clear();
    
    if (curve_points.size() < 2) return;
    
    for (size_t i = 0; i < curve_points.size(); ++i) {
        const auto& p = curve_points[i];
        
        // Calculate tangent
        glm::vec3 tangent;
        if (i == 0) {
            tangent = glm::normalize(curve_points[i + 1] - curve_points[i]);
        } else if (i == curve_points.size() - 1) {
            tangent = glm::normalize(curve_points[i] - curve_points[i - 1]);
        } else {
            tangent = glm::normalize(curve_points[i + 1] - curve_points[i - 1]);
        }
        
        // Generate circle at this point
        glm::vec3 normal(0, 1, 0);
        if (std::abs(glm::dot(tangent, normal)) > 0.9f) {
            normal = glm::vec3(1, 0, 0);
        }
        
        glm::vec3 binormal = glm::normalize(glm::cross(tangent, normal));
        normal = glm::normalize(glm::cross(binormal, tangent));
        
        for (uint32_t s = 0; s < segments; ++s) {
            float angle = 2.0f * glm::pi<float>() * s / segments;
            float x = radius * std::cos(angle);
            float y = radius * std::sin(angle);
            
            out_positions.push_back(p + x * normal + y * binormal);
        }
    }
    
    // Generate indices
    for (size_t i = 0; i < curve_points.size() - 1; ++i) {
        for (uint32_t s = 0; s < segments; ++s) {
            uint32_t v0 = i * segments + s;
            uint32_t v1 = v0 + 1;
            if (s == segments - 1) v1 = i * segments;
            
            uint32_t v2 = (i + 1) * segments + s;
            uint32_t v3 = v2 + 1;
            if (s == segments - 1) v3 = (i + 1) * segments;
            
            out_indices.push_back(v0); out_indices.push_back(v2); out_indices.push_back(v1);
            out_indices.push_back(v1); out_indices.push_back(v2); out_indices.push_back(v3);
        }
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

bool IsMeshClosed(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices) {
    std::unordered_map<uint64_t, int> edge_count;
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        
        if (i0 > i1) std::swap(i0, i1);
        if (i1 > i2) std::swap(i1, i2);
        if (i0 > i1) std::swap(i0, i1);
        
        edge_count[((uint64_t)i0 << 32) | i1]++;
        edge_count[((uint64_t)i1 << 32) | i2]++;
        edge_count[((uint64_t)i2 << 32) | i0]++;
    }
    
    for (const auto& [edge, count] : edge_count) {
        if (count != 2) return false;
    }
    
    return true;
}

MeshQuality CalculateMeshQuality(const std::vector<glm::vec3>& positions, 
                                 const std::vector<uint32_t>& indices) {
    MeshQuality quality;
    quality.min_edge_length = std::numeric_limits<float>::max();
    quality.max_edge_length = 0.0f;
    
    float total_area = 0.0f;
    
    for (size_t i = 0; i < indices.size(); i += 3) {
        const auto& p0 = positions[indices[i]];
        const auto& p1 = positions[indices[i + 1]];
        const auto& p2 = positions[indices[i + 2]];
        
        float len01 = glm::distance(p0, p1);
        float len12 = glm::distance(p1, p2);
        float len20 = glm::distance(p2, p0);
        
        quality.min_edge_length = std::min(quality.min_edge_length, std::min(len01, std::min(len12, len20)));
        quality.max_edge_length = std::max(quality.max_edge_length, std::max(len01, std::max(len12, len20)));
        
        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;
        total_area += glm::length(glm::cross(edge1, edge2)) * 0.5f;
    }
    
    quality.avg_triangle_area = total_area / (indices.size() / 3);
    
    return quality;
}

void InvertWinding(std::vector<uint32_t>& indices) {
    for (size_t i = 0; i < indices.size(); i += 3) {
        std::swap(indices[i + 1], indices[i + 2]);
    }
}

} // namespace schizo::geometry
