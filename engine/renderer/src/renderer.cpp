#include "renderer.h"
#include "render_device.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>

namespace schizo::renderer {

// Transform implementation
glm::mat4 Transform::GetMatrix() const {
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 rotation_matrix = glm::mat4_cast(rotation);
    glm::mat4 scale_matrix = glm::scale(glm::mat4(1.0f), scale);
    return translation * rotation_matrix * scale_matrix;
}

// Camera implementation
glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(position, position + direction, up);
}

glm::mat4 Camera::GetProjectionMatrix() const {
    float aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
    return glm::perspective(glm::radians(fov), aspect, near_plane, far_plane);
}

glm::mat4 Camera::GetViewProjectionMatrix() const {
    return GetProjectionMatrix() * GetViewMatrix();
}

// Renderer implementation
class RendererImpl : public Renderer {
public:
    RendererImpl();
    ~RendererImpl() override;
    
    bool Initialize() override;
    void Shutdown() override;
    
    RenderDevice* GetDevice() override { return device_.get(); }
    const RenderDevice* GetDevice() const override { return device_.get(); }
    
    void BeginFrame(const Camera& camera) override;
    void EndFrame() override;
    
    RenderStats GetStats() const override;
    
    void DrawTriangle(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                     const glm::vec4& color) override;
    void DrawCube(const Transform& transform, const glm::vec4& color) override;
    void DrawSphere(const glm::vec3& center, float radius, const glm::vec4& color,
                   uint32_t segments = 16) override;
    
    void SetDebugRenderingEnabled(bool enabled) override { debug_rendering_ = enabled; }
    
    std::string GetVertexShaderTemplate() const override;
    std::string GetFragmentShaderTemplate() const override;

private:
    std::unique_ptr<RenderDevice> device_;
    Camera current_camera_;
    RenderStats frame_stats_;
    bool debug_rendering_ = false;
    bool initialized_ = false;
};

RendererImpl::RendererImpl() {
    device_ = RenderDevice::Create();
}

RendererImpl::~RendererImpl() {
    Shutdown();
}

bool RendererImpl::Initialize() {
    if (!device_ || !device_->Initialize()) {
        spdlog::error("Failed to initialize RenderDevice");
        return false;
    }
    
    spdlog::info("Renderer initialized with API: {}", device_->GetDeviceInfo());
    initialized_ = true;
    return true;
}

void RendererImpl::Shutdown() {
    if (device_) {
        device_->Shutdown();
    }
    initialized_ = false;
}

void RendererImpl::BeginFrame(const Camera& camera) {
    if (!initialized_) {
        spdlog::warn("BeginFrame called on uninitialized Renderer");
        return;
    }
    
    current_camera_ = camera;
    device_->ResetStats();
    
    // Set viewport
    device_->SetViewport(0, 0, camera.viewport_width, camera.viewport_height);
    
    // Clear frame
    device_->Clear(true, true, true, glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
}

void RendererImpl::EndFrame() {
    if (!initialized_) return;
    
    frame_stats_ = device_->GetStats();
}

RenderStats RendererImpl::GetStats() const {
    return frame_stats_;
}

void RendererImpl::DrawTriangle(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                               const glm::vec4& color) {
    // Placeholder: simple test rendering
    (void)color;  // Suppress unused parameter warning
    spdlog::debug("DrawTriangle: p1={{{},{},{}}}, p2={{{},{},{}}}, p3={{{},{},{}}}", 
        p1.x, p1.y, p1.z, p2.x, p2.y, p2.z, p3.x, p3.y, p3.z);
}

void RendererImpl::DrawCube(const Transform& transform, const glm::vec4& color) {
    // Simple cube wireframe for testing
    (void)transform; (void)color;
    spdlog::debug("DrawCube called - placeholder");
}

void RendererImpl::DrawSphere(const glm::vec3& center, float radius, const glm::vec4& color,
                             uint32_t segments) {
    // Simple sphere placeholder for testing
    (void)center; (void)radius; (void)color; (void)segments;
    spdlog::debug("DrawSphere called - placeholder");
}

std::string RendererImpl::GetVertexShaderTemplate() const {
    return R"(
#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_texcoord;

layout(std140, binding = 0) uniform Camera {
    mat4 view;
    mat4 projection;
    mat4 view_projection;
};

out vec3 v_position;
out vec3 v_normal;
out vec2 v_texcoord;

void main() {
    v_position = in_position;
    v_normal = in_normal;
    v_texcoord = in_texcoord;
    gl_Position = view_projection * vec4(in_position, 1.0);
}
)";
}

std::string RendererImpl::GetFragmentShaderTemplate() const {
    return R"(
#version 450 core

in vec3 v_position;
in vec3 v_normal;
in vec2 v_texcoord;

out vec4 out_color;

void main() {
    out_color = vec4(1.0);
}
)";
}

// Factory function
std::unique_ptr<Renderer> Renderer::Create() {
    return std::make_unique<RendererImpl>();
}

} // namespace schizo::renderer
