#include "mesh_generator.h"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

namespace schizo::editor {

std::pair<std::vector<Vertex>, std::vector<uint32_t>> MeshGenerator::GenerateCube(float size) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float s = size * 0.5f;
    
    // Define 8 corners of the cube with colors
    glm::vec3 colors[] = {
        glm::vec3(1, 0, 0),  // Red
        glm::vec3(0, 1, 0),  // Green
        glm::vec3(0, 0, 1),  // Blue
        glm::vec3(1, 1, 0),  // Yellow
        glm::vec3(1, 0, 1),  // Magenta
        glm::vec3(0, 1, 1),  // Cyan
        glm::vec3(0.5f, 0.5f, 0.5f), // Gray
        glm::vec3(1, 1, 1)   // White
    };
    
    // Vertices
    vertices.emplace_back(glm::vec3(-s, -s, -s), colors[0]);
    vertices.emplace_back(glm::vec3(s, -s, -s), colors[1]);
    vertices.emplace_back(glm::vec3(s, s, -s), colors[2]);
    vertices.emplace_back(glm::vec3(-s, s, -s), colors[3]);
    vertices.emplace_back(glm::vec3(-s, -s, s), colors[4]);
    vertices.emplace_back(glm::vec3(s, -s, s), colors[5]);
    vertices.emplace_back(glm::vec3(s, s, s), colors[6]);
    vertices.emplace_back(glm::vec3(-s, s, s), colors[7]);
    
    // Indices (6 faces, 2 triangles each)
    uint32_t cube_indices[] = {
        // Front face
        0, 1, 2,  2, 3, 0,
        // Back face
        5, 4, 7,  7, 6, 5,
        // Left face
        4, 0, 3,  3, 7, 4,
        // Right face
        1, 5, 6,  6, 2, 1,
        // Top face
        3, 2, 6,  6, 7, 3,
        // Bottom face
        4, 5, 1,  1, 0, 4
    };
    
    for (uint32_t idx : cube_indices) {
        indices.push_back(idx);
    }
    
    return {vertices, indices};
}

std::pair<std::vector<Vertex>, std::vector<uint32_t>> MeshGenerator::GenerateSphere(float radius, uint32_t segments, uint32_t rings) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    const float pi = 3.14159265359f;
    
    // Generate vertices
    for (uint32_t i = 0; i <= rings; ++i) {
        float phi = pi * static_cast<float>(i) / static_cast<float>(rings);
        
        for (uint32_t j = 0; j <= segments; ++j) {
            float theta = 2.0f * pi * static_cast<float>(j) / static_cast<float>(segments);
            
            float x = radius * sin(phi) * cos(theta);
            float y = radius * cos(phi);
            float z = radius * sin(phi) * sin(theta);
            
            glm::vec3 color(
                (sin(theta) + 1.0f) * 0.5f,
                (cos(phi) + 1.0f) * 0.5f,
                (cos(theta) + 1.0f) * 0.5f
            );
            
            vertices.emplace_back(glm::vec3(x, y, z), color);
        }
    }
    
    // Generate indices
    for (uint32_t i = 0; i < rings; ++i) {
        for (uint32_t j = 0; j < segments; ++j) {
            uint32_t a = i * (segments + 1) + j;
            uint32_t b = a + segments + 1;
            
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(a + 1);
            
            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(b + 1);
        }
    }
    
    return {vertices, indices};
}

std::pair<std::vector<Vertex>, std::vector<uint32_t>> MeshGenerator::GenerateCylinder(float radius, float height, uint32_t segments) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    const float pi = 3.14159265359f;
    float h = height * 0.5f;
    
    // Top center
    vertices.emplace_back(glm::vec3(0, h, 0), glm::vec3(1, 0, 0));
    // Bottom center
    vertices.emplace_back(glm::vec3(0, -h, 0), glm::vec3(0, 1, 0));
    
    // Top circle
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
        float x = radius * cos(angle);
        float z = radius * sin(angle);
        vertices.emplace_back(glm::vec3(x, h, z), glm::vec3(1, 1, 0));
    }
    
    // Bottom circle
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
        float x = radius * cos(angle);
        float z = radius * sin(angle);
        vertices.emplace_back(glm::vec3(x, -h, z), glm::vec3(0, 1, 1));
    }
    
    // Top cap
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(0);
        indices.push_back(2 + (i + 1) % (segments + 1));
        indices.push_back(2 + i);
    }
    
    // Bottom cap
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(1);
        indices.push_back(2 + segments + 1 + i);
        indices.push_back(2 + segments + 1 + (i + 1) % (segments + 1));
    }
    
    // Side faces
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t top_a = 2 + i;
        uint32_t top_b = 2 + (i + 1);
        uint32_t bot_a = 2 + segments + 1 + i;
        uint32_t bot_b = 2 + segments + 1 + (i + 1);
        
        indices.push_back(top_a);
        indices.push_back(bot_a);
        indices.push_back(top_b);
        
        indices.push_back(top_b);
        indices.push_back(bot_a);
        indices.push_back(bot_b);
    }
    
    return {vertices, indices};
}

std::pair<std::vector<Vertex>, std::vector<uint32_t>> MeshGenerator::GeneratePyramid(float size) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float s = size * 0.5f;
    
    // Apex
    vertices.emplace_back(glm::vec3(0, s, 0), glm::vec3(1, 1, 1));
    
    // Base vertices
    vertices.emplace_back(glm::vec3(-s, -s, -s), glm::vec3(1, 0, 0));
    vertices.emplace_back(glm::vec3(s, -s, -s), glm::vec3(0, 1, 0));
    vertices.emplace_back(glm::vec3(s, -s, s), glm::vec3(0, 0, 1));
    vertices.emplace_back(glm::vec3(-s, -s, s), glm::vec3(1, 1, 0));
    
    // Side faces
    indices = {
        0, 1, 2,  // Front
        0, 2, 3,  // Right
        0, 3, 4,  // Back
        0, 4, 1,  // Left
        1, 4, 3,  // Bottom - face 1
        1, 3, 2   // Bottom - face 2
    };
    
    return {vertices, indices};
}

std::pair<std::vector<Vertex>, std::vector<uint32_t>> MeshGenerator::GeneratePlane(float size) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float s = size * 0.5f;
    
    vertices.emplace_back(glm::vec3(-s, 0, -s), glm::vec3(1, 0, 0));
    vertices.emplace_back(glm::vec3(s, 0, -s), glm::vec3(0, 1, 0));
    vertices.emplace_back(glm::vec3(s, 0, s), glm::vec3(0, 0, 1));
    vertices.emplace_back(glm::vec3(-s, 0, s), glm::vec3(1, 1, 0));
    
    indices = {0, 1, 2, 2, 3, 0};
    
    return {vertices, indices};
}

std::pair<std::vector<Vertex>, std::vector<uint32_t>> MeshGenerator::GenerateCapsule(float radius, float height, uint32_t segments) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    const float pi = 3.14159265359f;
    float h = height * 0.5f;
    uint32_t rings = segments / 2;  // Fewer rings for hemisphere
    
    // Generate top hemisphere vertices
    for (uint32_t i = 0; i <= rings; ++i) {
        float phi = pi * 0.5f * static_cast<float>(i) / static_cast<float>(rings);
        
        for (uint32_t j = 0; j <= segments; ++j) {
            float theta = 2.0f * pi * static_cast<float>(j) / static_cast<float>(segments);
            
            float x = radius * cos(phi) * cos(theta);
            float z = radius * cos(phi) * sin(theta);
            float y = h + radius * sin(phi);
            
            glm::vec3 color(
                (sin(theta) + 1.0f) * 0.5f,
                (cos(phi) + 1.0f) * 0.5f,
                (cos(theta) + 1.0f) * 0.5f
            );
            
            vertices.emplace_back(glm::vec3(x, y, z), color);
        }
    }
    
    // Generate bottom hemisphere vertices
    for (uint32_t i = rings; i <= 2 * rings; ++i) {
        float phi = pi * 0.5f * static_cast<float>(i - rings) / static_cast<float>(rings);
        
        for (uint32_t j = 0; j <= segments; ++j) {
            float theta = 2.0f * pi * static_cast<float>(j) / static_cast<float>(segments);
            
            float x = radius * cos(phi) * cos(theta);
            float z = radius * cos(phi) * sin(theta);
            float y = -h - radius * sin(phi);
            
            glm::vec3 color(
                (sin(theta) + 1.0f) * 0.5f,
                (cos(phi) + 1.0f) * 0.5f,
                (cos(theta) + 1.0f) * 0.5f
            );
            
            vertices.emplace_back(glm::vec3(x, y, z), color);
        }
    }
    
    // Top hemisphere indices
    for (uint32_t i = 0; i < rings; ++i) {
        for (uint32_t j = 0; j < segments; ++j) {
            uint32_t a = i * (segments + 1) + j;
            uint32_t b = a + segments + 1;
            
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(a + 1);
            
            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(b + 1);
        }
    }
    
    // Bottom hemisphere indices
    uint32_t offset = (rings + 1) * (segments + 1);
    for (uint32_t i = 0; i < rings; ++i) {
        for (uint32_t j = 0; j < segments; ++j) {
            uint32_t a = offset + i * (segments + 1) + j;
            uint32_t b = a + segments + 1;
            
            indices.push_back(a);
            indices.push_back(a + 1);
            indices.push_back(b);
            
            indices.push_back(a + 1);
            indices.push_back(b + 1);
            indices.push_back(b);
        }
    }
    
    return {vertices, indices};
}

} // namespace schizo::editor
