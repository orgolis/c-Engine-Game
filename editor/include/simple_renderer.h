#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <map>
#include <string>

// Forward declarations
namespace schizo::assets {
    class MeshAsset;
}

namespace schizo::editor {

/**
 * @struct Vertex
 * @brief Simple vertex structure: position + color
 */
struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

/**
 * @struct Mesh
 * @brief GPU mesh data
 */
struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    uint32_t index_count = 0;
    GLenum primitive_mode = 0x0004;  // GL_TRIANGLES (0x0001 = GL_LINES)
};

/**
 * @class SimpleRenderer
 * @brief Clean, minimal 3D viewport renderer
 */
class SimpleRenderer {
public:
    SimpleRenderer();
    ~SimpleRenderer();
    
    // Initialize renderer
    void Initialize();
    
    // Framebuffer management
    void CreateFramebuffer(uint32_t width, uint32_t height);
    void ResizeFramebuffer(uint32_t width, uint32_t height);
    void BindFramebuffer();
    void UnbindFramebuffer();
    GLuint GetFramebufferTexture() const { return fb_color_texture_; }
    
    // Rendering
    void ClearFramebuffer(const glm::vec4& color);
    void RenderMesh(const Mesh& mesh, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj);
    void RenderMesh(const Mesh& mesh, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj, const glm::vec4& color);
    
    // Built-in mesh creation
    Mesh CreateGridMesh();
    Mesh CreateAxesMesh();
    Mesh CreateCubeMesh();
    Mesh CreateRotationGizmoMesh();
    Mesh CreateWireframeBoxMesh();
    Mesh CreateTranslationGizmoMesh(char axis);  // 'x', 'y', 'z' - creates arrow for that axis
    Mesh CreateScaleGizmoMesh(char axis);        // 'x', 'y', 'z' - creates cube handle for that axis
    
    // Mesh asset support
    Mesh GetOrCreateMeshFromAsset(const schizo::assets::MeshAsset& asset);
    void ClearMeshAssetCache();
    
    // Public mesh references
    Mesh grid_mesh;
    Mesh axes_mesh;
    Mesh cube_mesh;
    Mesh rotation_gizmo_mesh;
    Mesh wireframe_box_mesh;

private:
    // Shader program
    GLuint shader_program_ = 0;
    
    // Framebuffer
    GLuint fb_ = 0;
    GLuint fb_color_texture_ = 0;
    GLuint fb_depth_texture_ = 0;
    uint32_t fb_width_ = 0;
    uint32_t fb_height_ = 0;
    
    // Mesh asset cache (filepath -> compiled mesh)
    std::map<std::string, Mesh> mesh_asset_cache_;
    
    // Helper: Create shader
    GLuint CreateShaderProgram(const char* vs_src, const char* fs_src);
    
    // Helper: Create mesh from vertices and indices
    Mesh CreateMeshInternal(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
};

} // namespace schizo::editor
