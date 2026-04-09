#include "simple_renderer_new.h"
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_src, nullptr);
    glCompileShader(vs);
    
    GLint success;
    GLchar info_log[512];
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vs, 512, nullptr, info_log);
        spdlog::error("[Shader] Vertex compile failed: {}", info_log);
        return 0;
    }
    
    // Compile fragment shader
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_src, nullptr);
    glCompileShader(fs);
    
    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
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
    
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
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
    spdlog::info("[Renderer] GLAD initialized - OpenGL {}.{}", GLVersion.major, GLVersion.minor);
    
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
    glBindFramebuffer(GL_FRAMEBUFFER, fb_);
    
    // Color attachment
    glGenTextures(1, &fb_color_texture_);
    glBindTexture(GL_TEXTURE_2D, fb_color_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_color_texture_, 0);
    
    // Depth attachment
    glGenTextures(1, &fb_depth_texture_);
    glBindTexture(GL_TEXTURE_2D, fb_depth_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb_depth_texture_, 0);
    
    // Verify
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("[Framebuffer] Status: 0x{:X} (not complete!)", status);
    } else {
        spdlog::info("[Framebuffer] Created {}x{}: COMPLETE", width, height);
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SimpleRenderer::ResizeFramebuffer(uint32_t width, uint32_t height) {
    if (width == fb_width_ && height == fb_height_) return;
    CreateFramebuffer(width, height);
}

void SimpleRenderer::BindFramebuffer() {
    if (fb_ != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fb_);
        glViewport(0, 0, fb_width_, fb_height_);
    }
}

void SimpleRenderer::UnbindFramebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SimpleRenderer::ClearFramebuffer(const glm::vec4& color) {
    glClearColor(color.x, color.y, color.z, color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
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
    
    glUniformMatrix4fv(model_loc, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(view_loc, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(proj_loc, 1, GL_FALSE, glm::value_ptr(proj));
    
    glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
    
    glBindVertexArray(0);
}

Mesh SimpleRenderer::CreateMeshInternal(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    Mesh mesh;
    
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
    
    glBindVertexArray(mesh.vao);
    
    // Upload vertices
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);
    
    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Color attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(1);
    
    // Upload indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
    
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
    return mesh;
}

Mesh SimpleRenderer::CreateAxesMesh() {
    std::vector<Vertex> vertices = {
        {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)},  // Red X
        {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)},
        
        {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)},  // Green Y
        {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)},
        
        {glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)},  // Blue Z
        {glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 1.0f)},
    };
    
    std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5};
    
    spdlog::info("[Axes] Created: {} vertices", vertices.size());
    return CreateMeshInternal(vertices, indices);
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

} // namespace schizo::editor
