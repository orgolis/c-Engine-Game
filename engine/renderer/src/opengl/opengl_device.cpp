#include <glad/glad.h>
#define GLFW_INCLUDE_NONE  // Prevent GLFW from including OpenGL headers
#include <GLFW/glfw3.h>
#include "opengl_device.h"
#include "render_device.h"
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace schizo::renderer {

bool OpenGLDevice::Initialize() {
    spdlog::info("Initializing OpenGL device");
    
    // Load all OpenGL function pointers using GLFW's function loader
    if (!gladLoadGL((GLADloadproc)glfwGetProcAddress)) {
        spdlog::error("Failed to initialize GLAD - could not load OpenGL functions");
        return false;
    }
    
    spdlog::info("OpenGL device initialized successfully: {}", GetDeviceInfo());
    return true;
}

void OpenGLDevice::Shutdown() {
    spdlog::info("Shutting down OpenGL device");
    // TODO: Cleanup any remaining resources
}

std::string OpenGLDevice::GetDeviceInfo() const {
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    
    if (!vendor || !renderer || !version) {
        return "OpenGL (version info unavailable)";
    }
    
    return std::string("OpenGL ") + version + " - " + renderer + " (" + vendor + ")";
}

void OpenGLDevice::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    glViewport((GLint)x, (GLint)y, (GLsizei)width, (GLsizei)height);
    spdlog::debug("SetViewport({}x{} @ {},{}", width, height, x, y);
}

void OpenGLDevice::BindFramebuffer(Framebuffer* fb) {
    // TODO: Implement framebuffer binding
    (void)fb;
}

void OpenGLDevice::Clear(bool color, bool depth, bool stencil, const glm::vec4& color_value) {
    glClearColor(color_value.r, color_value.g, color_value.b, color_value.a);
    GLbitfield mask = 0;
    if (color) mask |= GL_COLOR_BUFFER_BIT;
    if (depth) mask |= GL_DEPTH_BUFFER_BIT;
    if (stencil) mask |= GL_STENCIL_BUFFER_BIT;
    glClear(mask);
    spdlog::debug("Clear(color={}, depth={}, stencil={})", color, depth, stencil);
}

uint32_t OpenGLDevice::CompileShader(ShaderStage stage, const std::string& source,
                                     const char** defines, int define_count) {
    // Suppress unused parameter warnings
    (void)defines;
    (void)define_count;
    
    GLenum gl_stage = GL_VERTEX_SHADER;
    switch (stage) {
        case ShaderStage::Vertex:      gl_stage = GL_VERTEX_SHADER; break;
        case ShaderStage::Fragment:    gl_stage = GL_FRAGMENT_SHADER; break;
        case ShaderStage::Geometry:    gl_stage = GL_GEOMETRY_SHADER; break;
        case ShaderStage::TessControl: gl_stage = GL_TESS_CONTROL_SHADER; break;
        case ShaderStage::TessEval:    gl_stage = GL_TESS_EVALUATION_SHADER; break;
        case ShaderStage::Compute:     gl_stage = GL_COMPUTE_SHADER; break;
    }
    
    GLuint shader = glCreateShader(gl_stage);
    if (!shader) {
        last_error_ = "Failed to create shader";
        return 0;
    }
    
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        if (log_len > 0) {
            std::string log(log_len, '\0');
            glGetShaderInfoLog(shader, log_len, nullptr, &log[0]);
            last_error_ = "Shader compilation failed: " + log;
            spdlog::error("{}", last_error_);
        }
        glDeleteShader(shader);
        return 0;
    }
    
    spdlog::debug("CompileShader: stage={}, success", (int)stage);
    return shader;
}

uint32_t OpenGLDevice::LinkProgram(const uint32_t* stages, int stage_count) {
    GLuint program = glCreateProgram();
    if (!program) {
        last_error_ = "Failed to create program";
        return 0;
    }
    
    for (int i = 0; i < stage_count; ++i) {
        if (stages[i]) {
            glAttachShader(program, stages[i]);
        }
    }
    
    glLinkProgram(program);
    
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint log_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        if (log_len > 0) {
            std::string log(log_len, '\0');
            glGetProgramInfoLog(program, log_len, nullptr, &log[0]);
            last_error_ = "Program linking failed: " + log;
            spdlog::error("{}", last_error_);
        }
        glDeleteProgram(program);
        return 0;
    }
    
    spdlog::debug("LinkProgram: success");
    return program;
}

void OpenGLDevice::UseProgram(uint32_t program) {
    current_program_ = program;
    glUseProgram(program);
    spdlog::debug("UseProgram: {}", program);
}

void OpenGLDevice::DeleteShader(uint32_t handle) {
    glDeleteShader(handle);
    spdlog::debug("DeleteShader: {}", handle);
}

uint32_t OpenGLDevice::CreateBuffer(uint32_t target, size_t size, const void* data, bool dynamic_draw) {
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(target, buffer);
    GLenum usage = dynamic_draw ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    glBufferData(target, (GLsizeiptr)size, data, usage);
    spdlog::debug("CreateBuffer: id={}, size={}, dynamic={}", buffer, size, dynamic_draw);
    return buffer;
}

void OpenGLDevice::UpdateBuffer(uint32_t handle, size_t offset, size_t size, const void* data) {
    glBindBuffer(GL_COPY_WRITE_BUFFER, handle);
    glBufferSubData(GL_COPY_WRITE_BUFFER, (GLintptr)offset, (GLsizeiptr)size, data);
    spdlog::debug("UpdateBuffer: handle={}, offset={}, size={}", handle, offset, size);
}

void OpenGLDevice::DeleteBuffer(uint32_t handle) {
    glDeleteBuffers(1, (const GLuint*)&handle);
    spdlog::debug("DeleteBuffer: {}", handle);
}

void* OpenGLDevice::MapBuffer(uint32_t handle, bool readonly) {
    glBindBuffer(GL_COPY_READ_BUFFER, handle);
    GLenum access = readonly ? GL_READ_ONLY : GL_READ_WRITE;
    void* ptr = glMapBuffer(GL_COPY_READ_BUFFER, access);
    spdlog::debug("MapBuffer: handle={}, readonly={}", handle, readonly);
    return ptr;
}

void OpenGLDevice::UnmapBuffer(uint32_t handle) {
    glBindBuffer(GL_COPY_READ_BUFFER, handle);
    glUnmapBuffer(GL_COPY_READ_BUFFER);
    spdlog::debug("UnmapBuffer: {}", handle);
}

uint32_t OpenGLDevice::CreateTexture2D(uint32_t width, uint32_t height, uint32_t format, const void* data) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, (GLsizei)width, (GLsizei)height, 0, format, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    spdlog::debug("CreateTexture2D: id={}, {}x{}", texture, width, height);
    return texture;
}

uint32_t OpenGLDevice::CreateTextureCube(uint32_t size, uint32_t format, const void** faces) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    for (int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, (GLint)format, (GLsizei)size, (GLsizei)size, 0, format, GL_UNSIGNED_BYTE, faces[i]);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    spdlog::debug("CreateTextureCube: id={}, {}x{}", texture, size, size);
    return texture;
}

void OpenGLDevice::BindTexture(uint32_t texture, uint32_t unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    spdlog::debug("BindTexture: texture={}, unit={}", texture, unit);
}

void OpenGLDevice::DeleteTexture(uint32_t handle) {
    glDeleteTextures(1, (const GLuint*)&handle);
    spdlog::debug("DeleteTexture: {}", handle);
}

uint32_t OpenGLDevice::CreateVertexArray() {
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    spdlog::debug("CreateVertexArray: id={}", vao);
    return vao;
}

void OpenGLDevice::BindVertexArray(uint32_t vao) {
    current_vao_ = vao;
    glBindVertexArray(vao);
    spdlog::debug("BindVertexArray: {}", vao);
}

void OpenGLDevice::SetVertexAttribute(uint32_t attrib_index, int component_count, uint32_t type,
                           bool normalized, uint32_t stride, const void* offset) {
    glVertexAttribPointer((GLuint)attrib_index, component_count, type, normalized, (GLsizei)stride, offset);
    glEnableVertexAttribArray((GLuint)attrib_index);
    spdlog::debug("SetVertexAttribute: index={}, components={}, normalized={}", attrib_index, component_count, normalized);
}

void OpenGLDevice::AttachVertexBuffer(uint32_t vao, uint32_t vbo, uint32_t binding_index) {
    glBindVertexArray(vao);
    glBindVertexBuffer((GLuint)binding_index, vbo, 0, 0);  // offset=0, stride=0 (use attrib pointer stride)
    spdlog::debug("AttachVertexBuffer: vao={}, vbo={}, binding={}", vao, vbo, binding_index);
}

void OpenGLDevice::DeleteVertexArray(uint32_t vao) {
    glDeleteVertexArrays(1, (const GLuint*)&vao);
    spdlog::debug("DeleteVertexArray: {}", vao);
}

void OpenGLDevice::DrawIndexed(uint32_t mode, uint32_t index_count, uint32_t index_type,
                               size_t index_offset, uint32_t instance_count) {
    if (instance_count > 1) {
        glDrawElementsInstanced(mode, (GLsizei)index_count, index_type, (const void*)index_offset, (GLsizei)instance_count);
    } else {
        glDrawElements(mode, (GLsizei)index_count, index_type, (const void*)index_offset);
    }
    spdlog::debug("DrawIndexed: mode={}, count={}, instances={}", mode, index_count, instance_count);
    stats_.draw_calls++;
}

void OpenGLDevice::Draw(uint32_t mode, uint32_t vertex_count, uint32_t vertex_offset) {
    glDrawArrays(mode, (GLint)vertex_offset, (GLsizei)vertex_count);
    spdlog::debug("Draw: mode={}, count={}, offset={}", mode, vertex_count, vertex_offset);
    stats_.draw_calls++;
}

void OpenGLDevice::SetDepthTest(bool enabled) {
    if (enabled) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    spdlog::debug("SetDepthTest: {}", enabled);
}

void OpenGLDevice::SetBlending(bool enabled) {
    if (enabled) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    spdlog::debug("SetBlending: {}", enabled);
}

void OpenGLDevice::SetBlendFunc(uint32_t src_factor, uint32_t dst_factor) {
    glBlendFunc(src_factor, dst_factor);
    spdlog::debug("SetBlendFunc: src={}, dst={}", src_factor, dst_factor);
}

void OpenGLDevice::SetFaceCulling(bool enabled, bool cull_back) {
    if (enabled) {
        glEnable(GL_CULL_FACE);
        glCullFace(cull_back ? GL_BACK : GL_FRONT);
    } else {
        glDisable(GL_CULL_FACE);
    }
    spdlog::debug("SetFaceCulling: enabled={}, cull_back={}", enabled, cull_back);
}

void OpenGLDevice::EnableDebugOutput(bool enabled) {
    (void)enabled;
    // TODO: Implement glDebugMessageControl once extension is loaded
    spdlog::debug("EnableDebugOutput: {}", enabled);
}

// Factory function
std::unique_ptr<RenderDevice> RenderDevice::Create(RenderAPI api) {
    switch (api) {
        case RenderAPI::OpenGL45:
            return std::make_unique<OpenGLDevice>();
        default:
            spdlog::error("Unsupported render API requested");
            return nullptr;
    }
}

}  // namespace schizo::renderer
