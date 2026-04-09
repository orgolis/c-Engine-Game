#include "viewport_renderer_3d.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

namespace schizo::editor {

ViewportRenderer3D::ViewportRenderer3D() = default;

ViewportRenderer3D::~ViewportRenderer3D() {
    Shutdown();
}

void ViewportRenderer3D::Initialize(schizo::renderer::RenderDevice* render_device, uint32_t width, uint32_t height) {
    spdlog::info("[ViewportRenderer3D] Initializing 3D viewport renderer ({}x{})", width, height);
    render_device_ = render_device;
    framebuffer_width_ = width;
    framebuffer_height_ = height;
    CreateFramebuffer(width, height);
}

void ViewportRenderer3D::Shutdown() {
    spdlog::info("[ViewportRenderer3D] Shutting down");
    DestroyFramebuffer();
    render_device_ = nullptr;
}

void ViewportRenderer3D::CreateFramebuffer(uint32_t width, uint32_t height) {
    DestroyFramebuffer();
    framebuffer_width_ = width;
    framebuffer_height_ = height;
    spdlog::info("[ViewportRenderer3D] Framebuffer created: {}x{}", width, height);
}

void ViewportRenderer3D::DestroyFramebuffer() {
    framebuffer_texture_ = 0;
}

void ViewportRenderer3D::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (framebuffer_width_ != width || framebuffer_height_ != height) {
        CreateFramebuffer(width, height);
    }
}

GLuint ViewportRenderer3D::GetFramebufferTexture() const {
    return framebuffer_texture_;
}

void ViewportRenderer3D::Render(
    const std::shared_ptr<schizo::scene::Scene>& scene,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix,
    uint32_t viewport_width,
    uint32_t viewport_height) {
    // Stubbed - rendering not implemented yet
}

void ViewportRenderer3D::RenderGridAndAxes(
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix) {
    // Stubbed - grid rendering not implemented yet
}

void ViewportRenderer3D::RenderEntities(
    const std::shared_ptr<schizo::scene::Scene>& scene,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix) {
    // Stubbed - entity rendering not implemented yet
}

void ViewportRenderer3D::CopyGBufferToFramebuffer() {
    // Stubbed - g-buffer copy not implemented yet
}

void ViewportRenderer3D::CreateGridMesh() {
    // Stubbed - grid mesh creation not implemented yet
}

void ViewportRenderer3D::CreateAxesMesh() {
    // Stubbed - axes mesh creation not implemented yet
}

}  // namespace schizo::editor
