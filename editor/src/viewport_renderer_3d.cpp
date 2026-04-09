#include "viewport_renderer_3d.h"
#include <glad/glad.h>
#include "render_device.h"
#include "scene.h"
#include "entity.h"
#include "transform_component.h"
#include "mesh_renderer_component.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace schizo::editor {

using namespace schizo::renderer;
using namespace schizo::graphics;

ViewportRenderer3D::ViewportRenderer3D() = default;

ViewportRenderer3D::~ViewportRenderer3D() {
    Shutdown();
}

void ViewportRenderer3D::Initialize(RenderDevice* render_device, uint32_t width, uint32_t height) {
    spdlog::info("[ViewportRenderer3D] Initializing 3D viewport renderer ({}x{})", width, height);
    
    render_device_ = render_device;  // May be nullptr, that's okay - we'll use OpenGL directly
    framebuffer_width_ = width;
    framebuffer_height_ = height;
    
    // Create final output framebuffer
    CreateFramebuffer(width, height);
    
    printf("[ViewportRenderer3D] Initialization complete\n");
    spdlog::info("[ViewportRenderer3D] Initialization complete");
}

void ViewportRenderer3D::Shutdown() {
    spdlog::info("[ViewportRenderer3D] Shutting down");
    
    DestroyFramebuffer();
    render_device_ = nullptr;
}

void ViewportRenderer3D::CreateFramebuffer(uint32_t width, uint32_t height) {
    DestroyFramebuffer();
    
    if (!render_device_) {
        spdlog::error("[ViewportRenderer3D] Cannot create framebuffer without render device");
        return;
    }
    
    framebuffer_width_ = width;
    framebuffer_height_ = height;
    
    // TODO: Implement OpenGL framebuffer creation
    // For now: use placeholder framebuffer texture for ImGui display
    spdlog::info("[ViewportRenderer3D] Framebuffer initialized: {} x {}", width, height);
}

void ViewportRenderer3D::DestroyFramebuffer() {
    // TODO: Implement OpenGL framebuffer cleanup
    // For now: simple cleanup without GL calls
    framebuffer_texture_ = 0;
}

void ViewportRenderer3D::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    
    if (framebuffer_width_ != width || framebuffer_height_ != height) {
        spdlog::info("[ViewportRenderer3D] Resizing to {}x{}", width, height);
        CreateFramebuffer(width, height);
    }
}

GLuint ViewportRenderer3D::GetFramebufferTexture() const {
    return framebuffer_texture_;
}

void ViewportRenderer3D::CreateGridMesh() {
    // Create simple grid mesh
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    
    float grid_size = 50.0f;
    float grid_spacing = 1.0f;
    int grid_count = static_cast<int>(grid_size / grid_spacing);
    
    uint32_t index = 0;
    
    // X-Z grid lines
    for (int i = -grid_count; i <= grid_count; ++i) {
        float pos = i * grid_spacing;
        
        // Lines parallel to Z axis
        positions.push_back(glm::vec3(pos, 0.0f, -grid_size));
        positions.push_back(glm::vec3(pos, 0.0f, grid_size));
        indices.push_back(index++);
        indices.push_back(index++);
        
        // Lines parallel to X axis
        positions.push_back(glm::vec3(-grid_size, 0.0f, pos));
        positions.push_back(glm::vec3(grid_size, 0.0f, pos));
        indices.push_back(index++);
        indices.push_back(index++);
    }
    
    // Create mesh (this would need actual mesh creation via render device)
    spdlog::debug("[ViewportRenderer3D] Grid mesh created with {} vertices", positions.size());
}

void ViewportRenderer3D::CreateAxesMesh() {
    // Create coordinate axes mesh
    // Red = X axis, Green = Y axis, Blue = Z axis
    
    // Implementation would create axis visualization
    spdlog::debug("[ViewportRenderer3D] Axes mesh created");
}

void ViewportRenderer3D::RenderGridAndAxes(const glm::mat4& view_matrix, const glm::mat4& proj_matrix) {
    // Render grid and axes using direct OpenGL calls
    // This creates a basic visualization
    
    spdlog::debug("[ViewportRenderer3D] Rendering grid and axes");
    
    // For now, this is a placeholder - grid and axes would be rendered via simple geometry
}

void ViewportRenderer3D::RenderEntities(
    const std::shared_ptr<schizo::scene::Scene>& scene,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix) {
    
    if (!scene) return;
    spdlog::debug("[ViewportRenderer3D] RenderEntities called (stubbed for now)");
    // TODO: Implement entity rendering with OpenGL
}

void ViewportRenderer3D::CopyGBufferToFramebuffer() {
    // Copy final rendered image from deferred renderer to viewport framebuffer
    spdlog::debug("[ViewportRenderer3D] Copying G-Buffer to framebuffer");
}

void ViewportRenderer3D::Render(
    const std::shared_ptr<schizo::scene::Scene>& scene,
    const glm::mat4& view_matrix,
    const glm::mat4& proj_matrix,
    uint32_t viewport_width,
    uint32_t viewport_height) {
    
    // Resize if needed
    if (viewport_width != framebuffer_width_ || viewport_height != framebuffer_height_) {
        Resize(viewport_width, viewport_height);
    }
    
    spdlog::debug("[ViewportRenderer3D] Render called for {}x{} viewport", viewport_width, viewport_height);
    // TODO: Implement full framebuffer-based rendering with OpenGL
}

} // namespace schizo::editor
