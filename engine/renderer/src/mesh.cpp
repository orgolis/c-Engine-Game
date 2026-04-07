#include "mesh.h"
#include "render_device.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <iostream>
#include <unordered_map>

namespace schizo::renderer {

Mesh::Mesh() = default;

Mesh::~Mesh() = default;

void Mesh::SetData(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    vertices_ = vertices;
    indices_ = indices;
    vertex_count_ = static_cast<uint32_t>(vertices_.size());
    index_count_ = static_cast<uint32_t>(indices_.size());
}

void Mesh::UploadToGPU(RenderDevice* device) {
    if (!device || vertices_.empty() || indices_.empty()) {
        spdlog::warn("Cannot upload mesh to GPU: invalid device or empty data");
        return;
    }

    // Create VAO
    vao_ = device->CreateVertexArray();
    device->BindVertexArray(vao_);

    // Create and upload vertex buffer
    const size_t vertex_size = vertices_.size() * sizeof(Vertex);
    vbo_ = device->CreateBuffer(0x8892, vertex_size, vertices_.data(), false);  // GL_ARRAY_BUFFER = 0x8892

    // Create and upload index buffer
    const size_t index_size = indices_.size() * sizeof(uint32_t);
    ibo_ = device->CreateBuffer(0x8893, index_size, indices_.data(), false);  // GL_ELEMENT_ARRAY_BUFFER = 0x8893

    // Setup vertex attributes
    // Position (location 0)
    device->SetVertexAttribute(0, 3, 0x1406, false, sizeof(Vertex), (const void*)offsetof(Vertex, position));  // GL_FLOAT = 0x1406
    
    // Color (location 1)
    device->SetVertexAttribute(1, 4, 0x1406, false, sizeof(Vertex), (const void*)offsetof(Vertex, color));

    spdlog::debug("Mesh uploaded to GPU: VAO={}, VBO={}, IBO={}, vertices={}, indices={}",
        vao_, vbo_, ibo_, vertex_count_, index_count_);
}

void Mesh::Cleanup(RenderDevice* device) {
    if (device) {
        if (vao_) device->DeleteVertexArray(vao_);
        if (vbo_) device->DeleteBuffer(vbo_);
        if (ibo_) device->DeleteBuffer(ibo_);
    }
    vao_ = vbo_ = ibo_ = 0;
}

void Mesh::Draw(RenderDevice* device, uint32_t mode) {
    if (!device || !vao_ || index_count_ == 0) {
        std::cerr << "DEBUG: Draw() skipped - device=" << (device ? "valid" : "null") 
                  << " vao=" << vao_ << " index_count=" << index_count_ << std::endl;
        return;
    }

    std::cerr << "DEBUG: Draw() called - vao=" << vao_ << " indices=" << index_count_ << " mode=" << mode << std::endl;
    device->BindVertexArray(vao_);
    device->DrawIndexed(mode, index_count_, 0x1403, 0);  // GL_UNSIGNED_INT = 0x1403
    std::cerr << "DEBUG: DrawIndexed() complete" << std::endl;
}

// Primitive Generators

std::unique_ptr<Mesh> Mesh::CreateTriangle(RenderDevice* device) {
    auto mesh = std::make_unique<Mesh>();
    
    std::vector<Vertex> vertices = {
        { glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) },  // Red
        { glm::vec3( 0.5f, -0.5f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) },  // Green
        { glm::vec3( 0.0f,  0.5f, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) },  // Blue
    };
    
    std::vector<uint32_t> indices = { 0, 1, 2 };
    
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created triangle mesh");
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateCube(RenderDevice* device) {
    auto mesh = std::make_unique<Mesh>();
    
    const float s = 0.5f;  // Half-size
    
    std::vector<Vertex> vertices = {
        // Front face (Z+)
        { glm::vec3(-s, -s,  s), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) },
        { glm::vec3( s, -s,  s), glm::vec4(1.0f, 0.5f, 0.0f, 1.0f) },
        { glm::vec3( s,  s,  s), glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) },
        { glm::vec3(-s,  s,  s), glm::vec4(0.5f, 1.0f, 0.0f, 1.0f) },
        // Back face (Z-)
        { glm::vec3(-s, -s, -s), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) },
        { glm::vec3(-s,  s, -s), glm::vec4(0.0f, 1.0f, 0.5f, 1.0f) },
        { glm::vec3( s,  s, -s), glm::vec4(0.0f, 1.0f, 1.0f, 1.0f) },
        { glm::vec3( s, -s, -s), glm::vec4(0.0f, 0.5f, 1.0f, 1.0f) },
        // Top face (Y+)
        { glm::vec3(-s,  s, -s), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) },
        { glm::vec3(-s,  s,  s), glm::vec4(0.5f, 0.0f, 1.0f, 1.0f) },
        { glm::vec3( s,  s,  s), glm::vec4(1.0f, 0.0f, 1.0f, 1.0f) },
        { glm::vec3( s,  s, -s), glm::vec4(1.0f, 0.0f, 0.5f, 1.0f) },
        // Bottom face (Y-)
        { glm::vec3(-s, -s, -s), glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) },
        { glm::vec3( s, -s, -s), glm::vec4(0.6f, 0.6f, 0.6f, 1.0f) },
        { glm::vec3( s, -s,  s), glm::vec4(0.7f, 0.7f, 0.7f, 1.0f) },
        { glm::vec3(-s, -s,  s), glm::vec4(0.8f, 0.8f, 0.8f, 1.0f) },
        // Right face (X+)
        { glm::vec3( s, -s, -s), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) },
        { glm::vec3( s,  s, -s), glm::vec4(1.0f, 0.0f, 0.5f, 1.0f) },
        { glm::vec3( s,  s,  s), glm::vec4(1.0f, 0.0f, 1.0f, 1.0f) },
        { glm::vec3( s, -s,  s), glm::vec4(1.0f, 0.5f, 1.0f, 1.0f) },
        // Left face (X-)
        { glm::vec3(-s, -s, -s), glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) },
        { glm::vec3(-s, -s,  s), glm::vec4(0.0f, 1.0f, 0.5f, 1.0f) },
        { glm::vec3(-s,  s,  s), glm::vec4(0.0f, 1.0f, 1.0f, 1.0f) },
        { glm::vec3(-s,  s, -s), glm::vec4(0.0f, 0.5f, 1.0f, 1.0f) },
    };
    
    std::vector<uint32_t> indices = {
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
        20, 22, 21, 20, 23, 22,
    };
    
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created cube mesh");
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateGrid(RenderDevice* device, uint32_t width, uint32_t height) {
    auto mesh = std::make_unique<Mesh>();
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Generate grid vertices
    for (uint32_t y = 0; y <= height; ++y) {
        for (uint32_t x = 0; x <= width; ++x) {
            float fx = (x / static_cast<float>(width)) * 2.0f - 1.0f;
            float fy = (y / static_cast<float>(height)) * 2.0f - 1.0f;
            
            glm::vec4 color(0.5f + 0.5f * fx, 0.5f + 0.5f * fy, 0.5f, 1.0f);
            vertices.emplace_back(glm::vec3(fx, 0.0f, fy), color);
        }
    }
    
    // Generate grid indices
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t a = y * (width + 1) + x;
            uint32_t b = a + 1;
            uint32_t c = a + (width + 1);
            uint32_t d = c + 1;
            
            // First triangle
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            
            // Second triangle
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }
    
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created grid mesh: {}x{}", width, height);
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateSphere(RenderDevice* device, float radius, uint32_t segments, uint32_t rings) {
    auto mesh = std::make_unique<Mesh>();
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Generate sphere vertices
    for (uint32_t r = 0; r <= rings; ++r) {
        float phi = glm::pi<float>() * r / rings;
        float sin_phi = std::sin(phi);
        float cos_phi = std::cos(phi);
        
        for (uint32_t s = 0; s <= segments; ++s) {
            float theta = 2.0f * glm::pi<float>() * s / segments;
            float sin_theta = std::sin(theta);
            float cos_theta = std::cos(theta);
            
            float x = radius * sin_phi * cos_theta;
            float y = radius * cos_phi;
            float z = radius * sin_phi * sin_theta;
            
            glm::vec4 color(
                0.5f + 0.5f * (x / radius),
                0.5f + 0.5f * (y / radius),
                0.5f + 0.5f * (z / radius),
                1.0f
            );
            
            vertices.emplace_back(glm::vec3(x, y, z), color);
        }
    }
    
    // Generate sphere indices
    for (uint32_t r = 0; r < rings; ++r) {
        for (uint32_t s = 0; s < segments; ++s) {
            uint32_t a = r * (segments + 1) + s;
            uint32_t b = a + 1;
            uint32_t c = a + (segments + 1);
            uint32_t d = c + 1;
            
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }
    
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created sphere mesh: radius={}, segments={}, rings={}", radius, segments, rings);
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateTorus(RenderDevice* device, float major_radius, float minor_radius, 
                                        uint32_t segments, uint32_t rings) {
    auto mesh = std::make_unique<Mesh>();
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    for (uint32_t ring = 0; ring <= rings; ++ring) {
        float u = 2.0f * glm::pi<float>() * ring / rings;
        float cu = std::cos(u);
        float su = std::sin(u);
        
        for (uint32_t seg = 0; seg <= segments; ++seg) {
            float v = 2.0f * glm::pi<float>() * seg / segments;
            float cv = std::cos(v);
            float sv = std::sin(v);
            
            float x = (major_radius + minor_radius * cv) * cu;
            float y = minor_radius * sv;
            float z = (major_radius + minor_radius * cv) * su;
            
            glm::vec4 color(0.5f + 0.5f * cu, 0.5f + 0.5f * cv, 0.5f, 1.0f);
            vertices.emplace_back(glm::vec3(x, y, z), color);
        }
    }
    
    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t seg = 0; seg < segments; ++seg) {
            uint32_t a = ring * (segments + 1) + seg;
            uint32_t b = a + 1;
            uint32_t c = a + (segments + 1);
            uint32_t d = c + 1;
            
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }
    
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created torus mesh: major={}, minor={}, segments={}, rings={}", 
                 major_radius, minor_radius, segments, rings);
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateCylinder(RenderDevice* device, float radius, float height,
                                          uint32_t segments, bool include_caps) {
    auto mesh = std::make_unique<Mesh>();
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float h = height * 0.5f;
    
    // Side vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = 2.0f * glm::pi<float>() * i / segments;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        
        glm::vec4 color(0.5f + 0.5f * (x / radius), 0.5f, 0.5f + 0.5f * (z / radius), 1.0f);
        
        // Top vertex
        vertices.emplace_back(glm::vec3(x, h, z), color);
        // Bottom vertex
        vertices.emplace_back(glm::vec3(x, -h, z), color);
    }
    
    // Side indices
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t top1 = i * 2;
        uint32_t bot1 = top1 + 1;
        uint32_t top2 = ((i + 1) * 2) % vertices.size();
        uint32_t bot2 = top2 + 1;
        
        indices.push_back(top1);
        indices.push_back(top2);
        indices.push_back(bot1);
        indices.push_back(bot1);
        indices.push_back(top2);
        indices.push_back(bot2);
    }
    
    if (include_caps) {
        uint32_t top_center = vertices.size();
        uint32_t bot_center = top_center + 1;
        
        vertices.emplace_back(glm::vec3(0, h, 0), glm::vec4(1, 1, 1, 1));
        vertices.emplace_back(glm::vec3(0, -h, 0), glm::vec4(1, 1, 1, 1));
        
        // Top cap
        for (uint32_t i = 0; i < segments; ++i) {
            uint32_t v0 = i * 2;
            uint32_t v1 = ((i + 1) % segments) * 2;
            
            indices.push_back(top_center);
            indices.push_back(v1);
            indices.push_back(v0);
        }
        
        // Bottom cap
        for (uint32_t i = 0; i < segments; ++i) {
            uint32_t v0 = i * 2 + 1;
            uint32_t v1 = ((i + 1) % segments) * 2 + 1;
            
            indices.push_back(bot_center);
            indices.push_back(v0);
            indices.push_back(v1);
        }
    }
    
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created cylinder mesh: radius={}, height={}, segments={}", radius, height, segments);
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateCapsule(RenderDevice* device, float radius, float height,
                                         uint32_t segments, uint32_t rings) {
    auto mesh = std::make_unique<Mesh>();
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float h = (height - 2 * radius) * 0.5f;
    
    // Top hemisphere
    for (uint32_t r = 0; r <= rings / 2; ++r) {
        float phi = glm::pi<float>() * r / (rings / 2);
        float sin_phi = std::sin(phi);
        float cos_phi = std::cos(phi);
        
        for (uint32_t s = 0; s <= segments; ++s) {
            float theta = 2.0f * glm::pi<float>() * s / segments;
            float x = radius * sin_phi * std::cos(theta);
            float y = h + radius * cos_phi;
            float z = radius * sin_phi * std::sin(theta);
            
            glm::vec4 color(0.5f + 0.5f * (x / radius), 0.5f + 0.5f * (y / (h + radius)), 
                           0.5f + 0.5f * (z / radius), 1.0f);
            vertices.emplace_back(glm::vec3(x, y, z), color);
        }
    }
    
    // Bottom hemisphere
    uint32_t bottom_start = vertices.size();
    for (uint32_t r = rings / 2; r <= rings; ++r) {
        float phi = glm::pi<float>() * r / rings;
        float sin_phi = std::sin(phi);
        float cos_phi = std::cos(phi);
        
        for (uint32_t s = 0; s <= segments; ++s) {
            float theta = 2.0f * glm::pi<float>() * s / segments;
            float x = radius * sin_phi * std::cos(theta);
            float y = -h + radius * cos_phi;
            float z = radius * sin_phi * std::sin(theta);
            
            glm::vec4 color(0.5f + 0.5f * (x / radius), 0.5f - 0.5f * (y / (h + radius)), 
                           0.5f + 0.5f * (z / radius), 1.0f);
            vertices.emplace_back(glm::vec3(x, y, z), color);
        }
    }
    
    // Generate indices
    for (uint32_t i = 0; i < vertices.size() - (segments + 1); i += (segments + 1)) {
        for (uint32_t j = 0; j < segments; ++j) {
            uint32_t a = i + j;
            uint32_t b = a + 1;
            uint32_t c = a + segments + 1;
            uint32_t d = c + 1;
            
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }
    
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created capsule mesh: radius={}, height={}, segments={}, rings={}", 
                 radius, height, segments, rings);
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateCone(RenderDevice* device, float radius, float height,
                                      uint32_t segments, bool include_cap) {
    auto mesh = std::make_unique<Mesh>();
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Base vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = 2.0f * glm::pi<float>() * i / segments;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        
        glm::vec4 color(0.5f + 0.5f * (x / radius), 0.0f, 0.5f + 0.5f * (z / radius), 1.0f);
        vertices.emplace_back(glm::vec3(x, 0.0f, z), color);
    }
    
    // Apex
    uint32_t apex_idx = vertices.size();
    vertices.emplace_back(glm::vec3(0, height, 0), glm::vec4(1, 1, 1, 1));
    
    // Side faces
    for (uint32_t i = 0; i < segments; ++i) {
        uint32_t base1 = i;
        uint32_t base2 = i + 1;
        
        indices.push_back(base1);
        indices.push_back(apex_idx);
        indices.push_back(base2);
    }
    
    // Base cap
    if (include_cap) {
        uint32_t center = vertices.size();
        vertices.emplace_back(glm::vec3(0, 0, 0), glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        
        for (uint32_t i = 0; i < segments; ++i) {
            uint32_t v0 = i;
            uint32_t v1 = i + 1;
            
            indices.push_back(center);
            indices.push_back(v1);
            indices.push_back(v0);
        }
    }
    
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created cone mesh: radius={}, height={}, segments={}", radius, height, segments);
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreatePyramid(RenderDevice* device, float base_size, float height) {
    auto mesh = std::make_unique<Mesh>();
    
    float hs = base_size * 0.5f;
    
    std::vector<Vertex> vertices = {
        // Base
        { glm::vec3(-hs, 0, -hs), glm::vec4(1, 0, 0, 1) },
        { glm::vec3( hs, 0, -hs), glm::vec4(1, 1, 0, 1) },
        { glm::vec3( hs, 0,  hs), glm::vec4(0, 1, 0, 1) },
        { glm::vec3(-hs, 0,  hs), glm::vec4(0, 1, 1, 1) },
        
        // Apex
        { glm::vec3(0, height, 0), glm::vec4(1, 1, 1, 1) },
    };
    
    std::vector<uint32_t> indices = {
        // Base
        0, 2, 1, 0, 3, 2,
        
        // Sides
        0, 4, 1,
        1, 4, 2,
        2, 4, 3,
        3, 4, 0,
    };
    
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created pyramid mesh: base_size={}, height={}", base_size, height);
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateIcosphere(RenderDevice* device, float radius, uint32_t subdivisions) {
    // Create initial icosahedron
    const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
    const float inv_len = 1.0f / std::sqrt(phi * phi + 1.0f);
    
    std::vector<Vertex> vertices = {
        { glm::vec3(-1,  phi, 0) * inv_len, glm::vec4(1, 0, 0, 1) },
        { glm::vec3( 1,  phi, 0) * inv_len, glm::vec4(1, 1, 0, 1) },
        { glm::vec3(-1, -phi, 0) * inv_len, glm::vec4(0, 1, 0, 1) },
        { glm::vec3( 1, -phi, 0) * inv_len, glm::vec4(0, 1, 1, 1) },
        
        { glm::vec3(0, -1,  phi) * inv_len, glm::vec4(1, 0, 1, 1) },
        { glm::vec3(0,  1,  phi) * inv_len, glm::vec4(0, 0, 1, 1) },
        { glm::vec3(0, -1, -phi) * inv_len, glm::vec4(1, 1, 1, 1) },
        { glm::vec3(0,  1, -phi) * inv_len, glm::vec4(0.5f, 0.5f, 0.5f, 1) },
        
        { glm::vec3( phi, 0, -1) * inv_len, glm::vec4(0.5f, 0, 0, 1) },
        { glm::vec3( phi, 0,  1) * inv_len, glm::vec4(0, 0.5f, 0, 1) },
        { glm::vec3(-phi, 0, -1) * inv_len, glm::vec4(0, 0, 0.5f, 1) },
        { glm::vec3(-phi, 0,  1) * inv_len, glm::vec4(0.5f, 0.5f, 0, 1) },
    };
    
    std::vector<uint32_t> indices = {
        0, 11, 5,  0, 5, 1,  0, 1, 7,  0, 7, 10, 0, 10, 11,
        1, 5, 9,  5, 11, 4,  11, 10, 2,  10, 7, 6,  7, 1, 8,
        3, 9, 4,  3, 4, 2,  3, 2, 6,  3, 6, 8,  3, 8, 9,
        4, 9, 5,  2, 4, 11, 6, 2, 10, 8, 6, 7,  9, 8, 1,
    };
    
    // Subdivisions
    for (uint32_t i = 0; i < subdivisions; ++i) {
        std::vector<uint32_t> new_indices;
        std::unordered_map<uint64_t, uint32_t> edge_map;
        
        auto get_mid = [&](uint32_t a, uint32_t b) -> uint32_t {
            if (a > b) std::swap(a, b);
            uint64_t key = (static_cast<uint64_t>(a) << 32) | b;
            
            auto it = edge_map.find(key);
            if (it != edge_map.end()) {
                return it->second;
            }
            
            glm::vec3 mid = glm::normalize((vertices[a].position + vertices[b].position) * 0.5f) * radius;
            uint32_t idx = vertices.size();
            vertices.emplace_back(mid, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
            edge_map[key] = idx;
            return idx;
        };
        
        for (size_t j = 0; j < indices.size(); j += 3) {
            uint32_t a = indices[j];
            uint32_t b = indices[j + 1];
            uint32_t c = indices[j + 2];
            
            uint32_t ab = get_mid(a, b);
            uint32_t bc = get_mid(b, c);
            uint32_t ca = get_mid(c, a);
            
            new_indices.push_back(a);
            new_indices.push_back(ab);
            new_indices.push_back(ca);
            
            new_indices.push_back(ab);
            new_indices.push_back(b);
            new_indices.push_back(bc);
            
            new_indices.push_back(bc);
            new_indices.push_back(c);
            new_indices.push_back(ca);
            
            new_indices.push_back(ab);
            new_indices.push_back(bc);
            new_indices.push_back(ca);
        }
        
        indices = new_indices;
    }
    
    // Scale to desired radius
    for (auto& v : vertices) {
        v.position = glm::normalize(v.position) * radius;
    }
    
    auto mesh = std::make_unique<Mesh>();
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created icosphere mesh: radius={}, subdivisions={}", radius, subdivisions);
    return mesh;
}

std::unique_ptr<Mesh> Mesh::CreateQuad(RenderDevice* device) {
    // Create a full-screen quad in normalized device coordinates
    // Covers from -1 to 1 in both X and Y
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Four corners of the quad
    glm::vec4 white(1.0f);
    
    // Bottom-left (-1, -1)
    vertices.push_back(Vertex(glm::vec3(-1.0f, -1.0f, 0.0f), white));
    
    // Bottom-right (1, -1)
    vertices.push_back(Vertex(glm::vec3(1.0f, -1.0f, 0.0f), white));
    
    // Top-right (1, 1)
    vertices.push_back(Vertex(glm::vec3(1.0f, 1.0f, 0.0f), white));
    
    // Top-left (-1, 1)
    vertices.push_back(Vertex(glm::vec3(-1.0f, 1.0f, 0.0f), white));
    
    // Two triangles (CCW)
    // First triangle
    indices.push_back(0);  // Bottom-left
    indices.push_back(1);  // Bottom-right
    indices.push_back(2);  // Top-right
    
    // Second triangle
    indices.push_back(0);  // Bottom-left
    indices.push_back(2);  // Top-right
    indices.push_back(3);  // Top-left
    
    auto mesh = std::make_unique<Mesh>();
    mesh->SetData(vertices, indices);
    mesh->UploadToGPU(device);
    
    spdlog::info("Created fullscreen quad mesh");
    return mesh;
}

void Mesh::CalculateBounds() {
    if (vertices_.empty()) {
        bounds_center_ = glm::vec3(0);
        bounds_extents_ = glm::vec3(0);
        bounds_radius_ = 0.0f;
        return;
    }
    
    glm::vec3 min = vertices_[0].position;
    glm::vec3 max = vertices_[0].position;
    
    for (const auto& v : vertices_) {
        min = glm::min(min, v.position);
        max = glm::max(max, v.position);
    }
    
    bounds_center_ = (min + max) * 0.5f;
    bounds_extents_ = (max - min) * 0.5f;
    bounds_radius_ = glm::length(bounds_extents_);
    
    spdlog::debug("Mesh bounds: center={{{:.2f},{:.2f},{:.2f}}}, radius={:.2f}",
                  bounds_center_.x, bounds_center_.y, bounds_center_.z, bounds_radius_);
}

} // namespace schizo::renderer
