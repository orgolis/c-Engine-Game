#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <vector>
#include <memory>
#include <cstdint>

// Forward declarations
namespace schizo::assets {
    class MeshAsset;
}

namespace schizo::editor {

/**
 * @struct SimpleMesh
 * @brief Simple mesh data for rendering
 */
struct SimpleMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    uint32_t index_count = 0;
    
    SimpleMesh() = default;
    ~SimpleMesh();
    
    // Delete copy, allow move
    SimpleMesh(const SimpleMesh&) = delete;
    SimpleMesh& operator=(const SimpleMesh&) = delete;
    SimpleMesh(SimpleMesh&&) = default;
    SimpleMesh& operator=(SimpleMesh&&) = default;
};

/**
 * @class SimpleRenderer
 * @brief Minimal OpenGL 3D renderer for editor viewport
 */
class SimpleRenderer {
public:
    SimpleRenderer();
    ~SimpleRenderer();
    
    /**
     * Initialize renderer (set up shaders)
     */
    void Initialize();
    
    /**
     * Create framebuffer for viewport rendering
     */
    void CreateFramebuffer(uint32_t width, uint32_t height);
    
    /**
     * Resize framebuffer
     */
    void ResizeFramebuffer(uint32_t width, uint32_t height);
    
    /**
     * Bind framebuffer for rendering
     */
    void BindFramebuffer();
    
    /**
     * Unbind framebuffer (render to screen)
     */
    void UnbindFramebuffer();
    
    /**
     * Get framebuffer texture ID for ImGui display
     */
    GLuint GetFramebufferTexture() const { return framebuffer_texture_; }
    
    /**
     * Clear framebuffer
     */
    void ClearFramebuffer(const glm::vec4& color);
    
    /**
     * Upload mesh data to GPU
     */
    SimpleMesh CreateMesh(const std::vector<glm::vec3>& positions, 
                         const std::vector<glm::vec3>& colors,
                         const std::vector<uint32_t>& indices);
    
    /**
     * Render a mesh at a given transform
     */
    void RenderMesh(const SimpleMesh& mesh, 
                    const glm::mat4& transform,
                    const glm::mat4& view,
                    const glm::mat4& projection);
    
    /**
     * Render a mesh with custom color
     */
    void RenderMeshSolid(const SimpleMesh& mesh,
                        const glm::mat4& transform,
                        const glm::mat4& view,
                        const glm::mat4& projection,
                        const glm::vec4& color);
    
    /**
     * Render wireframe mesh
     */
    void RenderMeshWireframe(const SimpleMesh& mesh,
                            const glm::mat4& transform,
                            const glm::mat4& view,
                            const glm::mat4& projection,
                            const glm::vec4& color);
    
    /**
     * Render 3D grid
     */
    void RenderGrid(const glm::mat4& view,
                   const glm::mat4& projection,
                   float spacing = 1.0f,
                   int grid_size = 20);
    
    /**
     * Render 3D axes at origin
     */
    void RenderAxes(const glm::mat4& view,
                   const glm::mat4& projection,
                   float length = 1.0f);
    
    /**
     * Render a mesh asset
     */
    void RenderMeshAsset(const schizo::assets::MeshAsset& asset,
                        const glm::mat4& transform,
                        const glm::mat4& view,
                        const glm::mat4& projection,
                        bool wireframe = false);
    
    /**
     * Get simple cube mesh (cached)
     */
    const SimpleMesh& GetCubeMesh() const { return cube_mesh_; }
    
    /**
     * Get simple sphere mesh (cached)
     */
    const SimpleMesh& GetSphereMesh() const { return sphere_mesh_; }

private:
    GLuint shader_program_ = 0;
    GLuint color_shader_program_ = 0;
    GLuint line_shader_program_ = 0;
    SimpleMesh cube_mesh_;
    SimpleMesh sphere_mesh_;
    SimpleMesh grid_mesh_;
    SimpleMesh axes_mesh_;
    
    // Framebuffer objects
    GLuint framebuffer_ = 0;
    GLuint framebuffer_texture_ = 0;
    GLuint framebuffer_depth_ = 0;
    uint32_t framebuffer_width_ = 0;
    uint32_t framebuffer_height_ = 0;
    
    void CreateShaders();
    void CreatePrimitives();
    void CreateGridMesh();
    void CreateAxesMesh();
};

} // namespace schizo::editor
