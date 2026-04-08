#include "simple_renderer.h"
#include "mesh_generator.h"
#include "mesh_asset.h"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

namespace schizo::editor {

SimpleMesh::~SimpleMesh() {
    if (vao != 0) glDeleteVertexArrays(1, &vao);
    if (vbo != 0) glDeleteBuffers(1, &vbo);
    if (ebo != 0) glDeleteBuffers(1, &ebo);
}

SimpleRenderer::SimpleRenderer() = default;

SimpleRenderer::~SimpleRenderer() {
    if (shader_program_ != 0) glDeleteProgram(shader_program_);
    if (color_shader_program_ != 0) glDeleteProgram(color_shader_program_);
}

void SimpleRenderer::CreateShaders() {
    const char* vertex_src = R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 color;
        
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        
        out vec3 vertex_color;
        
        void main() {
            gl_Position = projection * view * model * vec4(position, 1.0);
            vertex_color = color;
        }
    )";
    
    const char* fragment_src = R"(
        #version 330 core
        in vec3 vertex_color;
        out vec4 FragColor;
        
        void main() {
            FragColor = vec4(vertex_color, 1.0);
        }
    )";
    
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertex_src, nullptr);
    glCompileShader(vertex);
    
    GLint success;
    GLchar infoLog[512];
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertex, 512, nullptr, infoLog);
        spdlog::error("Vertex shader compilation failed: {}", infoLog);
    }
    
    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragment_src, nullptr);
    glCompileShader(fragment);
    
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragment, 512, nullptr, infoLog);
        spdlog::error("Fragment shader compilation failed: {}", infoLog);
    }
    
    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vertex);
    glAttachShader(shader_program_, fragment);
    glLinkProgram(shader_program_);
    
    glGetProgramiv(shader_program_, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader_program_, 512, nullptr, infoLog);
        spdlog::error("Shader program linking failed: {}", infoLog);
    }
    
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    
    spdlog::info("Basic shader program created");
}

void SimpleRenderer::CreatePrimitives() {
    // Create cube
    auto cube_data = MeshGenerator::GenerateCube(1.0f);
    cube_mesh_ = CreateMesh(
        [&cube_data]() {
            std::vector<glm::vec3> positions;
            for (const auto& v : cube_data.first) {
                positions.push_back(v.position);
            }
            return positions;
        }(),
        [&cube_data]() {
            std::vector<glm::vec3> colors;
            for (const auto& v : cube_data.first) {
                colors.push_back(glm::vec3(v.color));
            }
            return colors;
        }(),
        cube_data.second
    );
    
    // Create sphere
    auto sphere_data = MeshGenerator::GenerateSphere(1.0f, 24, 16);
    sphere_mesh_ = CreateMesh(
        [&sphere_data]() {
            std::vector<glm::vec3> positions;
            for (const auto& v : sphere_data.first) {
                positions.push_back(v.position);
            }
            return positions;
        }(),
        [&sphere_data]() {
            std::vector<glm::vec3> colors;
            for (const auto& v : sphere_data.first) {
                colors.push_back(glm::vec3(v.color));
            }
            return colors;
        }(),
        sphere_data.second
    );
}

void SimpleRenderer::Initialize() {
    CreateShaders();
    CreatePrimitives();
    CreateGridMesh();
    CreateAxesMesh();
    spdlog::info("SimpleRenderer initialized");
}

void SimpleRenderer::CreateFramebuffer(uint32_t width, uint32_t height) {
    // Clean up existing framebuffer
    if (framebuffer_ != 0) {
        glDeleteFramebuffers(1, &framebuffer_);
        if (framebuffer_texture_ != 0) glDeleteTextures(1, &framebuffer_texture_);
        if (framebuffer_depth_ != 0) glDeleteTextures(1, &framebuffer_depth_);
        framebuffer_ = 0;
        framebuffer_texture_ = 0;
        framebuffer_depth_ = 0;
    }
    
    framebuffer_width_ = width;
    framebuffer_height_ = height;
    
    // Create framebuffer
    glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(0x8D40, framebuffer_);  // GL_FRAMEBUFFER
    
    // Create color texture
    glGenTextures(1, &framebuffer_texture_);
    glBindTexture(0x0DE1, framebuffer_texture_);  // GL_TEXTURE_2D
    glTexImage2D(0x0DE1, 0, 0x8058, width, height, 0, 0x1908, 0x1401, nullptr);  // GL_RGBA, GL_UNSIGNED_BYTE
    glTexParameteri(0x0DE1, 0x2801, 0x2601);  // GL_TEXTURE_MAG_FILTER, GL_LINEAR
    glTexParameteri(0x0DE1, 0x2800, 0x2601);  // GL_TEXTURE_MIN_FILTER, GL_LINEAR
    glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, framebuffer_texture_, 0);  // GL_COLOR_ATTACHMENT0
    
    // Create depth texture instead of renderbuffer
    glGenTextures(1, &framebuffer_depth_);
    glBindTexture(0x0DE1, framebuffer_depth_);  // GL_TEXTURE_2D
    glTexImage2D(0x0DE1, 0, 0x1902, width, height, 0, 0x1902, 0x1405, nullptr);  // GL_DEPTH_COMPONENT
    glTexParameteri(0x0DE1, 0x2801, 0x2601);  // GL_TEXTURE_MAG_FILTER, GL_LINEAR
    glTexParameteri(0x0DE1, 0x2800, 0x2601);  // GL_TEXTURE_MIN_FILTER, GL_LINEAR  
    glFramebufferTexture2D(0x8D40, 0x8D00, 0x0DE1, framebuffer_depth_, 0);  // GL_DEPTH_ATTACHMENT
    
    // Check framebuffer status
    GLenum status = glCheckFramebufferStatus(0x8D40);  // GL_FRAMEBUFFER
    if (status != 0x8CD5) {  // GL_FRAMEBUFFER_COMPLETE
        spdlog::error("Framebuffer creation failed! Status: 0x{:X}", status);
    } else {
        spdlog::info("Framebuffer created successfully: {}x{}", width, height);
    }
    
    glBindFramebuffer(0x8D40, 0);  // Unbind
}

void SimpleRenderer::ResizeFramebuffer(uint32_t width, uint32_t height) {
    if (width != framebuffer_width_ || height != framebuffer_height_) {
        CreateFramebuffer(width, height);
    }
}

void SimpleRenderer::BindFramebuffer() {
    if (framebuffer_ != 0) {
        glBindFramebuffer(0x8D40, framebuffer_);  // GL_FRAMEBUFFER
        glViewport(0, 0, framebuffer_width_, framebuffer_height_);
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

void SimpleRenderer::CreateGridMesh() {
    // Create a grid mesh for rendering
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> colors;
    std::vector<uint32_t> indices;
    
    float spacing = 1.0f;
    int grid_size = 20;
    
    uint32_t idx = 0;
    glm::vec3 grid_color(0.4f, 0.4f, 0.4f);
    glm::vec3 axis_color_x(1.0f, 0.0f, 0.0f);  // Red
    glm::vec3 axis_color_z(0.0f, 0.0f, 1.0f);  // Blue
    
    // X-axis lines
    for (int i = -grid_size; i <= grid_size; ++i) {
        float z = i * spacing;
        positions.push_back(glm::vec3(-grid_size * spacing, 0.0f, z));
        positions.push_back(glm::vec3(grid_size * spacing, 0.0f, z));
        
        glm::vec3 color = (i == 0) ? axis_color_z : grid_color;
        colors.push_back(color);
        colors.push_back(color);
        
        indices.push_back(idx++);
        indices.push_back(idx++);
    }
    
    // Z-axis lines
    for (int i = -grid_size; i <= grid_size; ++i) {
        float x = i * spacing;
        positions.push_back(glm::vec3(x, 0.0f, -grid_size * spacing));
        positions.push_back(glm::vec3(x, 0.0f, grid_size * spacing));
        
        glm::vec3 color = (i == 0) ? axis_color_x : grid_color;
        colors.push_back(color);
        colors.push_back(color);
        
        indices.push_back(idx++);
        indices.push_back(idx++);
    }
    
    grid_mesh_ = CreateMesh(positions, colors, indices);
}

void SimpleRenderer::CreateAxesMesh() {
    // Create axes for origin visualization
    std::vector<glm::vec3> positions = {
        glm::vec3(0.0f, 0.0f, 0.0f),  // Origin
        glm::vec3(1.0f, 0.0f, 0.0f),  // X axis
        glm::vec3(0.0f, 0.0f, 0.0f),  // Origin
        glm::vec3(0.0f, 1.0f, 0.0f),  // Y axis
        glm::vec3(0.0f, 0.0f, 0.0f),  // Origin
        glm::vec3(0.0f, 0.0f, 1.0f),  // Z axis
    };
    
    std::vector<glm::vec3> colors = {
        glm::vec3(1.0f, 0.0f, 0.0f),  // Red X
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),  // Green Y
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),  // Blue Z
        glm::vec3(0.0f, 0.0f, 1.0f),
    };
    
    std::vector<uint32_t> indices = { 0, 1, 2, 3, 4, 5 };
    
    axes_mesh_ = CreateMesh(positions, colors, indices);
}

SimpleMesh SimpleRenderer::CreateMesh(const std::vector<glm::vec3>& positions,
                                     const std::vector<glm::vec3>& colors,
                                     const std::vector<uint32_t>& indices) {
    SimpleMesh mesh;
    
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
    
    glBindVertexArray(mesh.vao);
    
    // Combine position and color data
    std::vector<float> vertex_data;
    for (size_t i = 0; i < positions.size(); ++i) {
        vertex_data.push_back(positions[i].x);
        vertex_data.push_back(positions[i].y);
        vertex_data.push_back(positions[i].z);
        if (i < colors.size()) {
            vertex_data.push_back(colors[i].x);
            vertex_data.push_back(colors[i].y);
            vertex_data.push_back(colors[i].z);
        } else {
            vertex_data.push_back(1.0f);
            vertex_data.push_back(1.0f);
            vertex_data.push_back(1.0f);
        }
    }
    
    glBindBuffer(0x8892, mesh.vbo);  // GL_ARRAY_BUFFER
    glBufferData(0x8892, vertex_data.size() * sizeof(float), vertex_data.data(), 0x88E4);  // GL_STATIC_DRAW
    
    // Position attribute  
    glVertexAttribPointer(0, 3, 0x1406, 0, 6 * sizeof(float), (void*)0);  // GL_FLOAT
    glEnableVertexAttribArray(0);
    
    // Color attribute
    glVertexAttribPointer(1, 3, 0x1406, 0, 6 * sizeof(float), (void*)(3 * sizeof(float)));  // GL_FLOAT
    glEnableVertexAttribArray(1);
    
    glBindBuffer(0x8893, mesh.ebo);  // GL_ELEMENT_ARRAY_BUFFER
    glBufferData(0x8893, indices.size() * sizeof(uint32_t), indices.data(), 0x88E4);  // GL_STATIC_DRAW
    
    mesh.index_count = static_cast<uint32_t>(indices.size());
    
    glBindVertexArray(0);
    
    return mesh;
}

void SimpleRenderer::RenderMesh(const SimpleMesh& mesh,
                                const glm::mat4& transform,
                                const glm::mat4& view,
                                const glm::mat4& projection) {
    glUseProgram(shader_program_);
    
    GLint model_loc = glGetUniformLocation(shader_program_, "model");
    GLint view_loc = glGetUniformLocation(shader_program_, "view");
    GLint proj_loc = glGetUniformLocation(shader_program_, "projection");
    
    glUniformMatrix4fv(model_loc, 1, 0, glm::value_ptr(transform));  // 0 = GL_FALSE
    glUniformMatrix4fv(view_loc, 1, 0, glm::value_ptr(view));
    glUniformMatrix4fv(proj_loc, 1, 0, glm::value_ptr(projection));
    
    glBindVertexArray(mesh.vao);
    glDrawElements(0x0004, mesh.index_count, 0x1405, nullptr);  // GL_TRIANGLES, GL_UNSIGNED_INT
    glBindVertexArray(0);
}

void SimpleRenderer::RenderMeshSolid(const SimpleMesh& mesh,
                                    const glm::mat4& transform,
                                    const glm::mat4& view,
                                    const glm::mat4& projection,
                                    const glm::vec4& color) {
    // For now, just render with the normal shader (colored vertices)
    RenderMesh(mesh, transform, view, projection);
}

void SimpleRenderer::RenderMeshWireframe(const SimpleMesh& mesh,
                                        const glm::mat4& transform,
                                        const glm::mat4& view,
                                        const glm::mat4& projection,
                                        const glm::vec4& color) {
    glUseProgram(shader_program_);
    glPolygonMode(0x0408, 0x1B01);  // GL_FRONT_AND_BACK, GL_LINE
    
    GLint model_loc = glGetUniformLocation(shader_program_, "model");
    GLint view_loc = glGetUniformLocation(shader_program_, "view");
    GLint proj_loc = glGetUniformLocation(shader_program_, "projection");
    
    glUniformMatrix4fv(model_loc, 1, 0, glm::value_ptr(transform));
    glUniformMatrix4fv(view_loc, 1, 0, glm::value_ptr(view));
    glUniformMatrix4fv(proj_loc, 1, 0, glm::value_ptr(projection));
    
    glBindVertexArray(mesh.vao);
    glDrawElements(0x0004, mesh.index_count, 0x1405, nullptr);  // GL_TRIANGLES, GL_UNSIGNED_INT
    glBindVertexArray(0);
    
    glPolygonMode(0x0408, 0x1B02);  // GL_FILL
}

void SimpleRenderer::RenderGrid(const glm::mat4& view,
                               const glm::mat4& projection,
                               float spacing,
                               int grid_size) {
    glUseProgram(shader_program_);
    glEnable(0x0B44);  // GL_BLEND
    glBlendFunc(0x0302, 0x0303);  // GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
    
    GLint model_loc = glGetUniformLocation(shader_program_, "model");
    GLint view_loc = glGetUniformLocation(shader_program_, "view");
    GLint proj_loc = glGetUniformLocation(shader_program_, "projection");
    
    glm::mat4 identity = glm::mat4(1.0f);
    glUniformMatrix4fv(model_loc, 1, 0, glm::value_ptr(identity));
    glUniformMatrix4fv(view_loc, 1, 0, glm::value_ptr(view));
    glUniformMatrix4fv(proj_loc, 1, 0, glm::value_ptr(projection));
    
    glBindVertexArray(grid_mesh_.vao);
    glDrawElements(0x0001, grid_mesh_.index_count, 0x1405, nullptr);  // GL_LINES, GL_UNSIGNED_INT
    glBindVertexArray(0);
    
    glDisable(0x0B44);
}

void SimpleRenderer::RenderAxes(const glm::mat4& view,
                               const glm::mat4& projection,
                               float length) {
    glUseProgram(shader_program_);
    
    GLint model_loc = glGetUniformLocation(shader_program_, "model");
    GLint view_loc = glGetUniformLocation(shader_program_, "view");
    GLint proj_loc = glGetUniformLocation(shader_program_, "projection");
    
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(length));
    glUniformMatrix4fv(model_loc, 1, 0, glm::value_ptr(scale));
    glUniformMatrix4fv(view_loc, 1, 0, glm::value_ptr(view));
    glUniformMatrix4fv(proj_loc, 1, 0, glm::value_ptr(projection));
    
    glBindVertexArray(axes_mesh_.vao);
    glDrawElements(0x0001, axes_mesh_.index_count, 0x1405, nullptr);  // GL_LINES, GL_UNSIGNED_INT
    glBindVertexArray(0);
}

void SimpleRenderer::RenderMeshAsset(const schizo::assets::MeshAsset& asset,
                                     const glm::mat4& transform,
                                     const glm::mat4& view,
                                     const glm::mat4& projection,
                                     bool wireframe) {
    glUseProgram(shader_program_);
    
    GLint model_loc = glGetUniformLocation(shader_program_, "model");
    GLint view_loc = glGetUniformLocation(shader_program_, "view");
    GLint proj_loc = glGetUniformLocation(shader_program_, "projection");
    
    glUniformMatrix4fv(model_loc, 1, 0, glm::value_ptr(transform));
    glUniformMatrix4fv(view_loc, 1, 0, glm::value_ptr(view));
    glUniformMatrix4fv(proj_loc, 1, 0, glm::value_ptr(projection));
    
    if (wireframe) {
        glPolygonMode(0x0408, 0x1B01);  // GL_FRONT_AND_BACK, GL_LINE
    }
    
    // Render each primitive in the asset
    const auto& primitives = asset.GetPrimitives();
    for (const auto& prim : primitives) {
        // For now, we need to create temporary meshes from asset data
        // This is a placeholder - full implementation would cache GPU meshes
        if (!prim.vertices.empty() && !prim.indices.empty()) {
            std::vector<glm::vec3> positions;
            std::vector<glm::vec3> colors;
            std::vector<uint32_t> indices = prim.indices;
            
            for (const auto& vertex : prim.vertices) {
                positions.push_back(vertex.position);
                colors.push_back(vertex.color);
            }
            
            // Create temporary mesh (inefficient - should be cached)
            SimpleMesh temp_mesh = CreateMesh(positions, colors, indices);
            
            // Render the primitive
            glBindVertexArray(temp_mesh.vao);
            glDrawElements(0x0004, temp_mesh.index_count, 0x1405, nullptr);  // GL_TRIANGLES
            glBindVertexArray(0);
            
            // Clean up temporary mesh
            if (temp_mesh.vao != 0) glDeleteVertexArrays(1, &temp_mesh.vao);
            if (temp_mesh.vbo != 0) glDeleteBuffers(1, &temp_mesh.vbo);
            if (temp_mesh.ebo != 0) glDeleteBuffers(1, &temp_mesh.ebo);
        }
    }
    
    if (wireframe) {
        glPolygonMode(0x0408, 0x1B02);  // GL_FILL
    }
}

} // namespace schizo::editor
