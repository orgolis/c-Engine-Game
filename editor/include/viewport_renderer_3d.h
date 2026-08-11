#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include <cstdint>

// Forward declarations
namespace schizo::renderer {
    class RenderDevice;
}

namespace schizo::scene {
    class Scene;
    class Entity;
}

namespace schizo::editor {

/**
 * @class ViewportRenderer3D
 * @brief Full 3D viewport renderer using deferred rendering pipeline
 * 
 * Features:
 * - Deferred rendering for high-quality lighting
 * - G-Buffer based rendering
 * - Real-time shadows
 * - Entity mesh rendering
 * - Multiple rendering modes (lit, wireframe, etc.)
 * - ImGui texture integration
 */
class ViewportRenderer3D {
public:
    ViewportRenderer3D();
    ~ViewportRenderer3D();
    
    // Delete copy operations
    ViewportRenderer3D(const ViewportRenderer3D&) = delete;
    ViewportRenderer3D& operator=(const ViewportRenderer3D&) = delete;
    
    /**
     * @brief Initialize viewport renderer
     * @param render_device Render device to use (optional, nullptr is acceptable)
     * @param width Initial viewport width
     * @param height Initial viewport height
     */
    void Initialize(schizo::renderer::RenderDevice* render_device, uint32_t width, uint32_t height);
    
    /**
     * @brief Shutdown renderer and cleanup resources
     */
    void Shutdown();
    
    /**
     * @brief Render scene to viewport framebuffer
     * @param scene Scene to render
     * @param view_matrix Camera view matrix
     * @param proj_matrix Camera projection matrix
     * @param viewport_width Current viewport width
     * @param viewport_height Current viewport height
     */
    void Render(
        const std::shared_ptr<schizo::scene::Scene>& scene,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix,
        uint32_t viewport_width,
        uint32_t viewport_height
    );
    
    /**
     * @brief Resize viewport framebuffer
     * @param width New width
     * @param height New height
     */
    void Resize(uint32_t width, uint32_t height);
    
    /**
     * @brief Get framebuffer color texture for ImGui
     */
    GLuint GetFramebufferTexture() const;
    
    /**
     * @brief Set viewport clear color
     */
    void SetClearColor(const glm::vec4& color) { clear_color_ = color; }
    
    /**
     * @brief Get viewport clear color
     */
    const glm::vec4& GetClearColor() const { return clear_color_; }
    
    /**
     * @brief Enable/disable wireframe rendering
     */
    void SetWireframeMode(bool enabled) { wireframe_mode_ = enabled; }
    
    /**
     * @brief Get wireframe mode
     */
    bool GetWireframeMode() const { return wireframe_mode_; }
    
    /**
     * @brief Enable/disable grid rendering
     */
    void SetShowGrid(bool enabled) { show_grid_ = enabled; }
    
    /**
     * @brief Get grid visibility
     */
    bool GetShowGrid() const { return show_grid_; }
    
    /**
     * @brief Enable/disable axes rendering
     */
    void SetShowAxes(bool enabled) { show_axes_ = enabled; }
    
    /**
     * @brief Get axes visibility
     */
    bool GetShowAxes() const { return show_axes_; }
    
    /**
     * @brief Enable/disable G-Buffer debug visualization
     */
    void SetDebugGBuffer(bool enabled) { debug_gbuffer_ = enabled; }
    
    /**
     * @brief Get G-Buffer debug state
     */
    bool GetDebugGBuffer() const { return debug_gbuffer_; }
    
    /**
     * @brief Enable/disable lighting
     */
    void SetEnableLighting(bool enabled) { enable_lighting_ = enabled; }
    
    /**
     * @brief Get lighting state
     */
    bool GetEnableLighting() const { return enable_lighting_; }

private:
    // Resources (rendering currently stubbed, not using these)
    schizo::renderer::RenderDevice* render_device_ = nullptr;
    // TODO: Add rendering resources when viewport rendering is implemented
    // std::unique_ptr<schizo::renderer::DeferredRenderer> deferred_renderer_;
    // std::unique_ptr<schizo::renderer::GBuffer> gbuffer_;
    // std::unique_ptr<schizo::renderer::Framebuffer> final_framebuffer_;
    // std::unique_ptr<schizo::renderer::Mesh> grid_mesh_;
    // std::unique_ptr<schizo::renderer::Mesh> axes_mesh_;
    // std::unique_ptr<schizo::renderer::Mesh> fullscreen_quad_;
    
    // Viewport state
    GLuint framebuffer_texture_ = 0;
    uint32_t framebuffer_width_ = 0;
    uint32_t framebuffer_height_ = 0;
    glm::vec4 clear_color_ = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    
    // Rendering options
    bool wireframe_mode_ = false;
    bool show_grid_ = true;
    bool show_axes_ = true;
    bool debug_gbuffer_ = false;
    bool enable_lighting_ = true;
    
    // Helper methods
    void CreateFramebuffer(uint32_t width, uint32_t height);
    void DestroyFramebuffer();
    void CreateGridMesh();
    void CreateAxesMesh();
    void RenderGridAndAxes(const glm::mat4& view_matrix, const glm::mat4& proj_matrix);
    void RenderEntities(
        const std::shared_ptr<schizo::scene::Scene>& scene,
        const glm::mat4& view_matrix,
        const glm::mat4& proj_matrix
    );
    void CopyGBufferToFramebuffer();
};

} // namespace schizo::editor
