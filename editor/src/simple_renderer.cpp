#include "simple_renderer.h"
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <glad/glad.h>
#include <cmath>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "../../engine/core/assets/mesh_asset.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace schizo::editor {

SimpleRenderer::SimpleRenderer() = default;

SimpleRenderer::~SimpleRenderer() {
    if (shader_program_ != 0) glDeleteProgram(shader_program_);
    if (fb_ != 0) glDeleteFramebuffers(1, &fb_);
    if (fb_color_texture_ != 0) glDeleteTextures(1, &fb_color_texture_);
    if (fb_depth_texture_ != 0) glDeleteTextures(1, &fb_depth_texture_);
}

GLuint SimpleRenderer::CreateShaderProgram(const char* vs_src, const char* fs_src) {
    // Compile vertex shader
    GLuint vs = glCreateShader(0x8B31);  // GL_VERTEX_SHADER
    glShaderSource(vs, 1, &vs_src, nullptr);
    glCompileShader(vs);
    
    GLint success;
    GLchar info_log[512];
    glGetShaderiv(vs, 0x8B81, &success);  // GL_COMPILE_STATUS
    if (!success) {
        glGetShaderInfoLog(vs, 512, nullptr, info_log);
        spdlog::error("[Shader] Vertex compile failed: {}", info_log);
        return 0;
    }
    
    // Compile fragment shader
    GLuint fs = glCreateShader(0x8B30);  // GL_FRAGMENT_SHADER
    glShaderSource(fs, 1, &fs_src, nullptr);
    glCompileShader(fs);
    
    glGetShaderiv(fs, 0x8B81, &success);  // GL_COMPILE_STATUS
    if (!success) {
        glGetShaderInfoLog(fs, 512, nullptr, info_log);
        spdlog::error("[Shader] Fragment compile failed: {}", info_log);
        return 0;
    }
    
    // Link program
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    
    glGetProgramiv(prog, 0x8B82, &success);  // GL_LINK_STATUS
    if (!success) {
        glGetProgramInfoLog(prog, 512, nullptr, info_log);
        spdlog::error("[Shader] Link failed: {}", info_log);
        return 0;
    }
    
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    spdlog::info("[Shader] Program created: {}", prog);
    return prog;
}

void SimpleRenderer::Initialize() {
    // Load OpenGL function pointers
    if (!gladLoadGL((GLADloadproc)glfwGetProcAddress)) {
        spdlog::error("[Renderer] GLAD initialization failed!");
        return;
    }
    spdlog::info("[Renderer] GLAD initialized");
    
    // Create shader program
    const char* vs = R"(
        #version 330 core
        layout(location=0) in vec3 position;
        layout(location=1) in vec3 color;
        
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        
        out vec3 vert_color;
        void main() {
            vert_color = color;
            gl_Position = projection * view * model * vec4(position, 1.0);
        }
    )";
    
    const char* fs = R"(
        #version 330 core
        in vec3 vert_color;
        out vec4 frag_color;
        void main() {
            frag_color = vec4(vert_color, 1.0);
        }
    )";
    
    shader_program_ = CreateShaderProgram(vs, fs);
    
    // Create initial framebuffer (1x1, will be resized)
    CreateFramebuffer(800, 600);
    
    // Create built-in meshes
    grid_mesh = CreateGridMesh();
    axes_mesh = CreateAxesMesh();
    cube_mesh = CreateCubeMesh();
    rotation_gizmo_mesh = CreateRotationGizmoMesh();
    wireframe_box_mesh = CreateWireframeBoxMesh();
    
    spdlog::info("[Renderer] Initialization complete");
}

void SimpleRenderer::CreateFramebuffer(uint32_t width, uint32_t height) {
    // Clean up old framebuffer
    if (fb_ != 0) {
        glDeleteFramebuffers(1, &fb_);
        if (fb_color_texture_ != 0) glDeleteTextures(1, &fb_color_texture_);
        if (fb_depth_texture_ != 0) glDeleteTextures(1, &fb_depth_texture_);
    }
    
    fb_width_ = width;
    fb_height_ = height;
    
    // Create framebuffer
    glGenFramebuffers(1, &fb_);
    glBindFramebuffer(0x8D40, fb_);  // GL_FRAMEBUFFER
    
    // Color attachment
    glGenTextures(1, &fb_color_texture_);
    glBindTexture(0x0DE1, fb_color_texture_);  // GL_TEXTURE_2D
    glTexImage2D(0x0DE1, 0, 0x8058, width, height, 0, 0x1908, 0x1401, nullptr);  // GL_RGBA, GL_UNSIGNED_BYTE
    glTexParameteri(0x0DE1, 0x2801, 0x2601);  // GL_TEXTURE_MAG_FILTER, GL_LINEAR
    glTexParameteri(0x0DE1, 0x2800, 0x2601);  // GL_TEXTURE_MIN_FILTER, GL_LINEAR
    glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, fb_color_texture_, 0);  // GL_COLOR_ATTACHMENT0
    
    // Depth attachment
    glGenTextures(1, &fb_depth_texture_);
    glBindTexture(0x0DE1, fb_depth_texture_);  // GL_TEXTURE_2D
    glTexImage2D(0x0DE1, 0, 0x81A5, width, height, 0, 0x1902, 0x1406, nullptr);  // GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT
    glTexParameteri(0x0DE1, 0x2801, 0x2601);  // GL_LINEAR
    glTexParameteri(0x0DE1, 0x2800, 0x2601);  // GL_LINEAR
    glFramebufferTexture2D(0x8D40, 0x8D00, 0x0DE1, fb_depth_texture_, 0);  // GL_DEPTH_ATTACHMENT
    
    // Verify
    GLenum status = glCheckFramebufferStatus(0x8D40);  // GL_FRAMEBUFFER
    if (status != 0x8CD5) {  // GL_FRAMEBUFFER_COMPLETE
        spdlog::error("[Framebuffer] Status: 0x{:X} (not complete!)", status);
    } else {
        spdlog::info("[Framebuffer] Created {}x{}: COMPLETE", width, height);
    }
    
    glBindFramebuffer(0x8D40, 0);  // Unbind
}

void SimpleRenderer::ResizeFramebuffer(uint32_t width, uint32_t height) {
    if (width == fb_width_ && height == fb_height_) return;
    CreateFramebuffer(width, height);
}

void SimpleRenderer::BindFramebuffer() {
    if (fb_ != 0) {
        glBindFramebuffer(0x8D40, fb_);  // GL_FRAMEBUFFER
        glViewport(0, 0, fb_width_, fb_height_);
    }
}

void SimpleRenderer::UnbindFramebuffer() {
    glBindFramebuffer(0x8D40, 0);  // GL_FRAMEBUFFER
}

void SimpleRenderer::ClearFramebuffer(const glm::vec4& color) {
    glClearColor(color.x, color.y, color.z, color.w);
    glClear(0x4000 | 0x100);  // GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
    glEnable(0x0B71);  // GL_DEPTH_TEST
}

void SimpleRenderer::RenderMesh(const Mesh& mesh, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj) {
    if (mesh.vao == 0 || mesh.index_count == 0) {
        return;
    }
    
    glUseProgram(shader_program_);
    glBindVertexArray(mesh.vao);
    
    GLint model_loc = glGetUniformLocation(shader_program_, "model");
    GLint view_loc = glGetUniformLocation(shader_program_, "view");
    GLint proj_loc = glGetUniformLocation(shader_program_, "projection");
    
    glUniformMatrix4fv(model_loc, 1, 0, glm::value_ptr(model));  // GL_FALSE = 0
    glUniformMatrix4fv(view_loc, 1, 0, glm::value_ptr(view));
    glUniformMatrix4fv(proj_loc, 1, 0, glm::value_ptr(proj));
    
    // Use the mesh's primitive mode (GL_TRIANGLES or GL_LINES)
    glDrawElements(mesh.primitive_mode, mesh.index_count, 0x1405, nullptr);  // GL_UNSIGNED_INT
    
    glBindVertexArray(0);
}

void SimpleRenderer::RenderMesh(const Mesh& mesh, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj, const glm::vec4& color) {
    // Colored mesh rendering (for selection highlights, etc.)
    // Note: This currently renders with the default colors from the mesh
    // To apply custom color, we'd need to modify the shader to accept a color uniform
    // For now, just call the standard render
    RenderMesh(mesh, model, view, proj);
}

Mesh SimpleRenderer::CreateMeshInternal(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    Mesh mesh;
    
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
    
    glBindVertexArray(mesh.vao);
    
    // Upload vertices
    glBindBuffer(0x8892, mesh.vbo);  // GL_ARRAY_BUFFER
    glBufferData(0x8892, vertices.size() * sizeof(Vertex), vertices.data(), 0x88E4);  // GL_STATIC_DRAW
    
    // Position attribute
    glVertexAttribPointer(0, 3, 0x1406, 0, sizeof(Vertex), (void*)0);  // GL_FLOAT
    glEnableVertexAttribArray(0);
    
    // Color attribute
    glVertexAttribPointer(1, 3, 0x1406, 0, sizeof(Vertex), (void*)offsetof(Vertex, color));  // GL_FLOAT
    glEnableVertexAttribArray(1);
    
    // Upload indices
    glBindBuffer(0x8893, mesh.ebo);  // GL_ELEMENT_ARRAY_BUFFER
    glBufferData(0x8893, indices.size() * sizeof(uint32_t), indices.data(), 0x88E4);  // GL_STATIC_DRAW
    
    mesh.index_count = indices.size();
    
    glBindVertexArray(0);
    
    spdlog::info("[Mesh] Created: VAO={}, VBO={}, EBO={}, indices={}", mesh.vao, mesh.vbo, mesh.ebo, mesh.index_count);
    return mesh;
}

Mesh SimpleRenderer::CreateGridMesh() {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t idx = 0;
    
    const int grid_size = 20;
    const float spacing = 1.0f;
    const glm::vec3 main_color(0.6f, 0.6f, 0.6f);
    const glm::vec3 axis_color(0.9f, 0.9f, 0.9f);
    
    // X-direction lines
    for (int i = -grid_size; i <= grid_size; ++i) {
        float x = i * spacing;
        vertices.push_back({glm::vec3(x, 0.0f, -grid_size * spacing), i == 0 ? axis_color : main_color});
        vertices.push_back({glm::vec3(x, 0.0f, grid_size * spacing), i == 0 ? axis_color : main_color});
        indices.push_back(idx++);
        indices.push_back(idx++);
    }
    
    // Z-direction lines
    for (int i = -grid_size; i <= grid_size; ++i) {
        float z = i * spacing;
        vertices.push_back({glm::vec3(-grid_size * spacing, 0.0f, z), i == 0 ? axis_color : main_color});
        vertices.push_back({glm::vec3(grid_size * spacing, 0.0f, z), i == 0 ? axis_color : main_color});
        indices.push_back(idx++);
        indices.push_back(idx++);
    }
    
    spdlog::info("[Grid] {} vertices, {} indices", vertices.size(), indices.size());
    // Render as lines for grid
    Mesh mesh = CreateMeshInternal(vertices, indices);
    mesh.primitive_mode = 0x0001;  // GL_LINES
    return mesh;
}

Mesh SimpleRenderer::CreateAxesMesh() {
    // Create axes as thin long lines for visual feedback
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float thickness = 0.01f;  // Thin for visual feedback
    float length = 0.5f;       // Long axes
    
    // X axis (red) - box from (0,0,0) to (length,±thickness,±thickness)
    uint32_t x_start = vertices.size();
    vertices.push_back({glm::vec3(0, -thickness, -thickness), glm::vec3(1.0f, 0.0f, 0.0f)});
    vertices.push_back({glm::vec3(0, thickness, -thickness), glm::vec3(1.0f, 0.0f, 0.0f)});
    vertices.push_back({glm::vec3(0, thickness, thickness), glm::vec3(1.0f, 0.0f, 0.0f)});
    vertices.push_back({glm::vec3(0, -thickness, thickness), glm::vec3(1.0f, 0.0f, 0.0f)});
    vertices.push_back({glm::vec3(length, -thickness, -thickness), glm::vec3(1.0f, 0.0f, 0.0f)});
    vertices.push_back({glm::vec3(length, thickness, -thickness), glm::vec3(1.0f, 0.0f, 0.0f)});
    vertices.push_back({glm::vec3(length, thickness, thickness), glm::vec3(1.0f, 0.0f, 0.0f)});
    vertices.push_back({glm::vec3(length, -thickness, thickness), glm::vec3(1.0f, 0.0f, 0.0f)});
    
    // Y axis (green) - box from (0,0,0) to (±thickness,length,±thickness)
    uint32_t y_start = vertices.size();
    vertices.push_back({glm::vec3(-thickness, 0, -thickness), glm::vec3(0.0f, 1.0f, 0.0f)});
    vertices.push_back({glm::vec3(thickness, 0, -thickness), glm::vec3(0.0f, 1.0f, 0.0f)});
    vertices.push_back({glm::vec3(thickness, 0, thickness), glm::vec3(0.0f, 1.0f, 0.0f)});
    vertices.push_back({glm::vec3(-thickness, 0, thickness), glm::vec3(0.0f, 1.0f, 0.0f)});
    vertices.push_back({glm::vec3(-thickness, length, -thickness), glm::vec3(0.0f, 1.0f, 0.0f)});
    vertices.push_back({glm::vec3(thickness, length, -thickness), glm::vec3(0.0f, 1.0f, 0.0f)});
    vertices.push_back({glm::vec3(thickness, length, thickness), glm::vec3(0.0f, 1.0f, 0.0f)});
    vertices.push_back({glm::vec3(-thickness, length, thickness), glm::vec3(0.0f, 1.0f, 0.0f)});
    
    // Z axis (blue) - box from (0,0,0) to (±thickness,±thickness,length)
    uint32_t z_start = vertices.size();
    vertices.push_back({glm::vec3(-thickness, -thickness, 0), glm::vec3(0.0f, 0.0f, 1.0f)});
    vertices.push_back({glm::vec3(thickness, -thickness, 0), glm::vec3(0.0f, 0.0f, 1.0f)});
    vertices.push_back({glm::vec3(thickness, thickness, 0), glm::vec3(0.0f, 0.0f, 1.0f)});
    vertices.push_back({glm::vec3(-thickness, thickness, 0), glm::vec3(0.0f, 0.0f, 1.0f)});
    vertices.push_back({glm::vec3(-thickness, -thickness, length), glm::vec3(0.0f, 0.0f, 1.0f)});
    vertices.push_back({glm::vec3(thickness, -thickness, length), glm::vec3(0.0f, 0.0f, 1.0f)});
    vertices.push_back({glm::vec3(thickness, thickness, length), glm::vec3(0.0f, 0.0f, 1.0f)});
    vertices.push_back({glm::vec3(-thickness, thickness, length), glm::vec3(0.0f, 0.0f, 1.0f)});
    
    // Helper lambda to add cube face indices
    auto AddCube = [&](uint32_t start) {
        uint32_t i = start;
        // Front
        indices.push_back(i+0); indices.push_back(i+1); indices.push_back(i+2);
        indices.push_back(i+0); indices.push_back(i+2); indices.push_back(i+3);
        // Back
        indices.push_back(i+4); indices.push_back(i+6); indices.push_back(i+5);
        indices.push_back(i+4); indices.push_back(i+7); indices.push_back(i+6);
        // Left
        indices.push_back(i+0); indices.push_back(i+3); indices.push_back(i+7);
        indices.push_back(i+0); indices.push_back(i+7); indices.push_back(i+4);
        // Right
        indices.push_back(i+1); indices.push_back(i+5); indices.push_back(i+6);
        indices.push_back(i+1); indices.push_back(i+6); indices.push_back(i+2);
        // Top
        indices.push_back(i+3); indices.push_back(i+2); indices.push_back(i+6);
        indices.push_back(i+3); indices.push_back(i+6); indices.push_back(i+7);
        // Bottom
        indices.push_back(i+0); indices.push_back(i+4); indices.push_back(i+5);
        indices.push_back(i+0); indices.push_back(i+5); indices.push_back(i+1);
    };
    
    AddCube(x_start);
    AddCube(y_start);
    AddCube(z_start);
    
    spdlog::info("[Axes] Created: {} vertices, {} indices", vertices.size(), indices.size());
    Mesh mesh = CreateMeshInternal(vertices, indices);
    mesh.primitive_mode = 0x0004;  // GL_TRIANGLES
    return mesh;
}

Mesh SimpleRenderer::CreateCubeMesh() {
    std::vector<Vertex> vertices = {
        // Front face
        {glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f)},
        {glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f)},
        {glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f)},
        {glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 1.0f)},
        
        // Back face
        {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f)},
        {glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f)},
        {glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f)},
        {glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f)},
    };
    
    std::vector<uint32_t> indices = {
        0, 1, 2, 2, 3, 0,  // Front
        4, 6, 5, 4, 7, 6,  // Back
        4, 5, 1, 1, 0, 4,  // Bottom
        3, 2, 6, 6, 7, 3,  // Top
        4, 0, 3, 3, 7, 4,  // Left
        1, 5, 6, 6, 2, 1,  // Right
    };
    
    return CreateMeshInternal(vertices, indices);
}

Mesh SimpleRenderer::CreateRotationGizmoMesh() {
    // Create ring-shaped rotation gizmos for X, Y, Z axes
    // Each ring is perpendicular to its axis
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float radius = 1.0f;
    int segments = 32;
    uint32_t idx = 0;
    
    // Helper lambda to create a ring
    auto AddRing = [&](const glm::vec3& axis_color, int axis_type) {
        // axis_type: 0 = X (YZ ring), 1 = Y (XZ ring), 2 = Z (XY ring)
        uint32_t ring_start = idx;
        
        for (int i = 0; i < segments; ++i) {
            float angle = 2.0f * 3.14159265f * i / segments;
            float cos_a = cos(angle);
            float sin_a = sin(angle);
            
            glm::vec3 pos;
            if (axis_type == 0) {  // X axis - ring in YZ plane
                pos = glm::vec3(0, cos_a * radius, sin_a * radius);
            } else if (axis_type == 1) {  // Y axis - ring in XZ plane
                pos = glm::vec3(cos_a * radius, 0, sin_a * radius);
            } else {  // Z axis - ring in XY plane
                pos = glm::vec3(cos_a * radius, sin_a * radius, 0);
            }
            
            vertices.push_back({pos, axis_color});
            indices.push_back(ring_start + i);
            indices.push_back(ring_start + (uint32_t)((i + 1) % segments));
            idx += 1;
        }
    };
    
    // Add three rings: X (red), Y (green), Z (blue)
    AddRing(glm::vec3(1.0f, 0.0f, 0.0f), 0);  // X ring (red)
    AddRing(glm::vec3(0.0f, 1.0f, 0.0f), 1);  // Y ring (green)
    AddRing(glm::vec3(0.0f, 0.0f, 1.0f), 2);  // Z ring (blue)
    
    spdlog::info("[RotationGizmo] Created: {} vertices, {} indices", vertices.size(), indices.size());
    Mesh mesh = CreateMeshInternal(vertices, indices);
    mesh.primitive_mode = 0x0001;  // GL_LINES
    return mesh;
}

Mesh SimpleRenderer::CreateWireframeBoxMesh() {
    // Create wireframe box outline (for selection highlighting)
    std::vector<Vertex> vertices = {
        // Front face
        {glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 0.0f)},  // 0 - yellow
        {glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 0.0f)},   // 1
        {glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 0.0f)},    // 2
        {glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 1.0f, 0.0f)},   // 3
        
        // Back face
        {glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(1.0f, 1.0f, 0.0f)}, // 4
        {glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(1.0f, 1.0f, 0.0f)},  // 5
        {glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(1.0f, 1.0f, 0.0f)},   // 6
        {glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(1.0f, 1.0f, 0.0f)},  // 7
    };
    
    std::vector<uint32_t> indices = {
        // Front face edges
        0, 1,  1, 2,  2, 3,  3, 0,
        // Back face edges
        4, 5,  5, 6,  6, 7,  7, 4,
        // Connecting edges
        0, 4,  1, 5,  2, 6,  3, 7,
    };
    
    Mesh mesh = CreateMeshInternal(vertices, indices);
    mesh.primitive_mode = 0x0001;  // GL_LINES
    return mesh;
}

Mesh SimpleRenderer::GetOrCreateMeshFromAsset(const schizo::assets::MeshAsset& asset) {
    const std::string& filepath = asset.GetFilePath();
    
    // Check cache
    auto it = mesh_asset_cache_.find(filepath);
    if (it != mesh_asset_cache_.end()) {
        spdlog::debug("[Mesh] Using cached mesh from {}", filepath);
        return it->second;
    }
    
    // Convert asset mesh to simple vertex format
    std::vector<Vertex> all_vertices;
    std::vector<uint32_t> all_indices;
    
    const auto& primitives = asset.GetPrimitives();
    for (const auto& prim : primitives) {
        uint32_t vertex_offset = all_vertices.size();
        
        // Convert asset vertices to editor vertices
        for (const auto& asset_vert : prim.vertices) {
            Vertex v;
            v.position = asset_vert.position;
            v.color = asset_vert.color;
            all_vertices.push_back(v);
        }
        
        // Add indices with offset
        for (uint32_t idx : prim.indices) {
            all_indices.push_back(idx + vertex_offset);
        }
    }
    
    // Create GPU mesh
    Mesh mesh = CreateMeshInternal(all_vertices, all_indices);
    
    // Cache result
    mesh_asset_cache_[filepath] = mesh;
    spdlog::info("[Mesh] Loaded and cached mesh asset: {} ({} vertices, {} indices)", 
        filepath, all_vertices.size(), all_indices.size());
    
    return mesh;
}

void SimpleRenderer::ClearMeshAssetCache() {
    mesh_asset_cache_.clear();
    spdlog::info("[Mesh] Cleared mesh asset cache");
}

Mesh SimpleRenderer::CreateTranslationGizmoMesh(char axis) {
    // Create an arrow pointing along the specified axis
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    glm::vec3 color(1.0f, 1.0f, 1.0f);  // Will be colored by the render call
    
    // Arrow consists of:
    // 1. Shaft (cylinder represented as line or box)
    // 2. Head (cone or pyramid)
    
    // Shaft: simple cylinder along axis (represented as 8-segment approximation)
    float shaft_length = 0.8f;
    float shaft_radius = 0.05f;
    int segments = 8;
    
    // Create shaft (cylinder)
    for (int i = 0; i < segments; ++i) {
        float angle0 = 2.0f * M_PI * i / segments;
        float angle1 = 2.0f * M_PI * (i + 1) / segments;
        
        float x0 = shaft_radius * cos(angle0);
        float z0 = shaft_radius * sin(angle0);
        float x1 = shaft_radius * cos(angle1);
        float z1 = shaft_radius * sin(angle1);
        
        uint32_t base = indices.size();
        
        // Bottom ring
        if (axis == 'x') {
            vertices.push_back({glm::vec3(0.0f, x0, z0), color});   // 0
            vertices.push_back({glm::vec3(0.0f, x1, z1), color});   // 1
            vertices.push_back({glm::vec3(shaft_length, x0, z0), color});  // 2
            vertices.push_back({glm::vec3(shaft_length, x1, z1), color});  // 3
        } else if (axis == 'y') {
            vertices.push_back({glm::vec3(x0, 0.0f, z0), color});   // 0
            vertices.push_back({glm::vec3(x1, 0.0f, z1), color});   // 1
            vertices.push_back({glm::vec3(x0, shaft_length, z0), color});  // 2
            vertices.push_back({glm::vec3(x1, shaft_length, z1), color});  // 3
        } else { // z
            vertices.push_back({glm::vec3(x0, z0, 0.0f), color});   // 0
            vertices.push_back({glm::vec3(x1, z1, 0.0f), color});   // 1
            vertices.push_back({glm::vec3(x0, z0, shaft_length), color});  // 2
            vertices.push_back({glm::vec3(x1, z1, shaft_length), color});  // 3
        }
        
        // Shaft faces
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        
        indices.push_back(base + 1);
        indices.push_back(base + 3);
        indices.push_back(base + 2);
    }
    
    // Arrow head (cone)
    uint32_t head_base = vertices.size();
    float head_length = 0.2f;
    float head_radius = 0.1f;
    
    // Cone apex
    if (axis == 'x') {
        vertices.push_back({glm::vec3(shaft_length + head_length, 0.0f, 0.0f), color});
    } else if (axis == 'y') {
        vertices.push_back({glm::vec3(0.0f, shaft_length + head_length, 0.0f), color});
    } else { // z
        vertices.push_back({glm::vec3(0.0f, 0.0f, shaft_length + head_length), color});
    }
    
    // Cone base ring
    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * M_PI * i / segments;
        float x = head_radius * cos(angle);
        float z = head_radius * sin(angle);
        
        if (axis == 'x') {
            vertices.push_back({glm::vec3(shaft_length, x, z), color});
        } else if (axis == 'y') {
            vertices.push_back({glm::vec3(x, shaft_length, z), color});
        } else { // z
            vertices.push_back({glm::vec3(x, z, shaft_length), color});
        }
    }
    
    // Cone faces (apex to base ring)
    for (int i = 0; i < segments; ++i) {
        uint32_t apex = head_base;
        uint32_t base1 = head_base + 1 + i;
        uint32_t base2 = head_base + 1 + (i + 1) % segments;
        
        indices.push_back(apex);
        indices.push_back(base1);
        indices.push_back(base2);
    }
    
    // Cone base (fan)
    for (int i = 1; i < segments - 1; ++i) {
        indices.push_back(head_base + 1);
        indices.push_back(head_base + 1 + i);
        indices.push_back(head_base + 1 + i + 1);
    }
    
    Mesh mesh = CreateMeshInternal(vertices, indices);
    return mesh;
}

Mesh SimpleRenderer::CreateScaleGizmoMesh(char axis) {
    // Create a cube handle for scaling along an axis
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    glm::vec3 color(1.0f, 1.0f, 1.0f);
    
    float handle_size = 0.15f;
    float handle_pos = 0.85f;  // Position along axis
    
    // Create a small cube at the end of each axis
    auto create_cube = [&](const glm::vec3& offset) {
        uint32_t base = vertices.size();
        
        // All 8 corners of the cube
        vertices.push_back({offset + glm::vec3(-handle_size, -handle_size, -handle_size), color});
        vertices.push_back({offset + glm::vec3(handle_size, -handle_size, -handle_size), color});
        vertices.push_back({offset + glm::vec3(handle_size, handle_size, -handle_size), color});
        vertices.push_back({offset + glm::vec3(-handle_size, handle_size, -handle_size), color});
        vertices.push_back({offset + glm::vec3(-handle_size, -handle_size, handle_size), color});
        vertices.push_back({offset + glm::vec3(handle_size, -handle_size, handle_size), color});
        vertices.push_back({offset + glm::vec3(handle_size, handle_size, handle_size), color});
        vertices.push_back({offset + glm::vec3(-handle_size, handle_size, handle_size), color});
        
        // Cube faces (6 faces, 2 triangles each)
        // Front
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
        // Back
        indices.push_back(base + 4); indices.push_back(base + 6); indices.push_back(base + 5);
        indices.push_back(base + 4); indices.push_back(base + 7); indices.push_back(base + 6);
        // Top
        indices.push_back(base + 3); indices.push_back(base + 2); indices.push_back(base + 6);
        indices.push_back(base + 3); indices.push_back(base + 6); indices.push_back(base + 7);
        // Bottom
        indices.push_back(base + 0); indices.push_back(base + 5); indices.push_back(base + 1);
        indices.push_back(base + 0); indices.push_back(base + 4); indices.push_back(base + 5);
        // Left
        indices.push_back(base + 0); indices.push_back(base + 3); indices.push_back(base + 7);
        indices.push_back(base + 0); indices.push_back(base + 7); indices.push_back(base + 4);
        // Right
        indices.push_back(base + 1); indices.push_back(base + 5); indices.push_back(base + 6);
        indices.push_back(base + 1); indices.push_back(base + 6); indices.push_back(base + 2);
    };
    
    // Create cube at the appropriate position based on axis
    if (axis == 'x') {
        create_cube(glm::vec3(handle_pos, 0.0f, 0.0f));
    } else if (axis == 'y') {
        create_cube(glm::vec3(0.0f, handle_pos, 0.0f));
    } else { // z
        create_cube(glm::vec3(0.0f, 0.0f, handle_pos));
    }
    
    Mesh mesh = CreateMeshInternal(vertices, indices);
    return mesh;
}

} // namespace schizo::editor
