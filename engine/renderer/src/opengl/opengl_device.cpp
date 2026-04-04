#include "render_device.h"
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <string>

// OpenGL headers (via GLFW)
#include <GLFW/glfw3.h>

namespace schizo::renderer {

/**
 * @class OpenGLDevice
 * @brief OpenGL 4.5+ implementation of RenderDevice
 */
class OpenGLDevice : public RenderDevice {
public:
    OpenGLDevice() = default;
    ~OpenGLDevice() override = default;
    
    bool Initialize() override;
    void Shutdown() override;
    
    RenderAPI GetAPI() const override { return RenderAPI::OpenGL45; }
    std::string GetDeviceInfo() const override;
    
    void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    void BindFramebuffer(Framebuffer* fb) override;
    void Clear(bool color, bool depth, bool stencil, const glm::vec4& color_value) override;
    
    uint32_t CompileShader(ShaderStage stage, const std::string& source,
                          const char** defines = nullptr, int define_count = 0) override;
    uint32_t LinkProgram(const uint32_t* stages, int stage_count) override;
    void UseProgram(uint32_t program) override;
    void DeleteShader(uint32_t handle) override;
    
    uint32_t CreateBuffer(uint32_t target, size_t size, const void* data, bool dynamic_draw) override;
    void UpdateBuffer(uint32_t handle, size_t offset, size_t size, const void* data) override;
    void DeleteBuffer(uint32_t handle) override;
    void* MapBuffer(uint32_t handle, bool readonly = false) override;
    void UnmapBuffer(uint32_t handle) override;
    
    uint32_t CreateTexture2D(uint32_t width, uint32_t height, uint32_t format, const void* data) override;
    uint32_t CreateTextureCube(uint32_t size, uint32_t format, const void** faces) override;
    void BindTexture(uint32_t texture, uint32_t unit) override;
    void DeleteTexture(uint32_t handle) override;
    
    uint32_t CreateVertexArray() override;
    void BindVertexArray(uint32_t vao) override;
    void SetVertexAttribute(uint32_t attrib_index, int component_count, uint32_t type,
                           bool normalized, uint32_t stride, const void* offset) override;
    void AttachVertexBuffer(uint32_t vao, uint32_t vbo, uint32_t binding_index) override;
    void DeleteVertexArray(uint32_t vao) override;
    
    void DrawIndexed(uint32_t mode, uint32_t index_count, uint32_t index_type,
                    size_t index_offset, uint32_t instance_count = 1) override;
    void Draw(uint32_t mode, uint32_t vertex_count, uint32_t vertex_offset = 0) override;
    
    void SetDepthTest(bool enabled) override;
    void SetBlending(bool enabled) override;
    void SetBlendFunc(uint32_t src_factor, uint32_t dst_factor) override;
    void SetFaceCulling(bool enabled, bool cull_back = true) override;
    
    RenderStats GetStats() const override { return stats_; }
    void ResetStats() override { stats_ = RenderStats(); }
    
    void EnableDebugOutput(bool enabled) override;
    std::string GetLastError() const override { return last_error_; }

private:
    mutable std::string last_error_;
    RenderStats stats_;
    uint32_t current_program_ = 0;
    uint32_t current_vao_ = 0;
};

bool OpenGLDevice::Initialize() {
    spdlog::info("Initializing OpenGL device");
    
    // The OpenGL context should already be created by GLFW at this point
    // Set default OpenGL state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Set clear color
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    
    spdlog::info("OpenGL device initialized successfully: {}", GetDeviceInfo());
    return true;
}

void OpenGLDevice::Shutdown() {
    spdlog::info("Shutting down OpenGL device");
    // TODO: Cleanup any remaining resources
}

std::string OpenGLDevice::GetDeviceInfo() const {
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    
    std::string info = "OpenGL ";
    if (version) info += version;
    if (vendor) info += " (" + std::string(vendor);
    if (renderer) info += " " + std::string(renderer);
    if (vendor || renderer) info += ")";
    
    return info;
}

void OpenGLDevice::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    glViewport(static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height));
}

void OpenGLDevice::BindFramebuffer(Framebuffer* fb) {
    // TODO: Implement framebuffer binding
    (void)fb;
}

void OpenGLDevice::Clear(bool color, bool depth, bool stencil, const glm::vec4& color_value) {
    GLbitfield mask = 0;
    if (color) {
        glClearColor(color_value.r, color_value.g, color_value.b, color_value.a);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (depth) {
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (stencil) {
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    glClear(mask);
}

uint32_t OpenGLDevice::CompileShader(ShaderStage stage, const std::string& source,
                                     const char** defines, int define_count) {
    // TODO: Implement shader compilation
    (void)stage; (void)source; (void)defines; (void)define_count;
    return 0;
}

uint32_t OpenGLDevice::LinkProgram(const uint32_t* stages, int stage_count) {
    // TODO: Implement program linking
    (void)stages; (void)stage_count;
    return 0;
}

void OpenGLDevice::UseProgram(uint32_t program) {
    current_program_ = program;
    // TODO: Call glUseProgram
}

void OpenGLDevice::DeleteShader(uint32_t handle) {
    // TODO: Implement deletion
    (void)handle;
}

uint32_t OpenGLDevice::CreateBuffer(uint32_t target, size_t size, const void* data, bool dynamic_draw) {
    // TODO: Implement buffer creation
    (void)target; (void)size; (void)data; (void)dynamic_draw;
    return 0;
}

void OpenGLDevice::UpdateBuffer(uint32_t handle, size_t offset, size_t size, const void* data) {
    // TODO: Implement buffer update
    (void)handle; (void)offset; (void)size; (void)data;
}

void OpenGLDevice::DeleteBuffer(uint32_t handle) {
    // TODO: Implement buffer deletion
    (void)handle;
}

void* OpenGLDevice::MapBuffer(uint32_t handle, bool readonly) {
    // TODO: Implement buffer mapping
    (void)handle; (void)readonly;
    return nullptr;
}

void OpenGLDevice::UnmapBuffer(uint32_t handle) {
    // TODO: Implement buffer unmapping
    (void)handle;
}

uint32_t OpenGLDevice::CreateTexture2D(uint32_t width, uint32_t height, uint32_t format, const void* data) {
    // TODO: Implement 2D texture creation
    (void)width; (void)height; (void)format; (void)data;
    return 0;
}

uint32_t OpenGLDevice::CreateTextureCube(uint32_t size, uint32_t format, const void** faces) {
    // TODO: Implement cubemap creation
    (void)size; (void)format; (void)faces;
    return 0;
}

void OpenGLDevice::BindTexture(uint32_t texture, uint32_t unit) {
    // TODO: Implement texture binding
    (void)texture; (void)unit;
}

void OpenGLDevice::DeleteTexture(uint32_t handle) {
    // TODO: Implement texture deletion
    (void)handle;
}

uint32_t OpenGLDevice::CreateVertexArray() {
    // TODO: Implement VAO creation
    return 0;
}

void OpenGLDevice::BindVertexArray(uint32_t vao) {
    current_vao_ = vao;
    // TODO: Call glBindVertexArray
}

void OpenGLDevice::SetVertexAttribute(uint32_t attrib_index, int component_count, uint32_t type,
                                      bool normalized, uint32_t stride, const void* offset) {
    // TODO: Implement vertex attribute configuration
    (void)attrib_index; (void)component_count; (void)type; (void)normalized; (void)stride; (void)offset;
}

void OpenGLDevice::AttachVertexBuffer(uint32_t vao, uint32_t vbo, uint32_t binding_index) {
    // TODO: Implement vertex buffer attachment
    (void)vao; (void)vbo; (void)binding_index;
}

void OpenGLDevice::DeleteVertexArray(uint32_t vao) {
    // TODO: Implement VAO deletion
    (void)vao;
}

void OpenGLDevice::DrawIndexed(uint32_t mode, uint32_t index_count, uint32_t index_type,
                               size_t index_offset, uint32_t instance_count) {
    // TODO: Call glDrawElementsInstanced
    (void)mode; (void)index_count; (void)index_type; (void)index_offset; (void)instance_count;
    stats_.draw_calls++;
}

void OpenGLDevice::Draw(uint32_t mode, uint32_t vertex_count, uint32_t vertex_offset) {
    // TODO: Call glDrawArrays
    (void)mode; (void)vertex_count; (void)vertex_offset;
    stats_.draw_calls++;
}

void OpenGLDevice::SetDepthTest(bool enabled) {
    // TODO: Call glEnable/glDisable(GL_DEPTH_TEST)
    (void)enabled;
}

void OpenGLDevice::SetBlending(bool enabled) {
    // TODO: Call glEnable/glDisable(GL_BLEND)
    (void)enabled;
}

void OpenGLDevice::SetBlendFunc(uint32_t src_factor, uint32_t dst_factor) {
    // TODO: Call glBlendFunc
    (void)src_factor; (void)dst_factor;
}

void OpenGLDevice::SetFaceCulling(bool enabled, bool cull_back) {
    // TODO: Call glEnable/glDisable(GL_CULL_FACE) and glCullFace
    (void)enabled; (void)cull_back;
}

void OpenGLDevice::EnableDebugOutput(bool enabled) {
    // TODO: Enable KHR_debug if available
    (void)enabled;
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

} // namespace schizo::renderer
