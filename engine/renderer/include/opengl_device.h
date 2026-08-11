#pragma once

#include "render_device.h"
#include <string>
#include <cstdint>

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

}  // namespace schizo::renderer
