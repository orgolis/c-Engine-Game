#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <glm/glm.hpp>

namespace schizo::renderer {

// Forward declarations
class Shader;
class Mesh;
class Texture;
class Framebuffer;

/**
 * @enum RenderAPI
 * @brief Available graphics APIs
 */
enum class RenderAPI : uint8_t {
    OpenGL45,
    Vulkan,
    DirectX12,
    Metal,
    Unknown
};

/**
 * @enum ShaderStage
 * @brief Graphics pipeline shader stages
 */
enum class ShaderStage : uint8_t {
    Vertex,      // Vertex shader
    Fragment,    // Fragment/Pixel shader
    Geometry,    // Geometry shader
    TessControl, // Tessellation control
    TessEval,    // Tessellation evaluation
    Compute,     // Compute shader
};

/**
 * @struct RenderStats
 * @brief Performance metrics from last frame
 */
struct RenderStats {
    uint32_t draw_calls = 0;
    uint32_t vertices_rendered = 0;
    uint32_t triangles_rendered = 0;
    float gpu_time_ms = 0.0f;
    uint32_t texture_bindings = 0;
    uint32_t buffer_bindings = 0;
};

/**
 * @class RenderDevice
 * @brief Abstract interface to the graphics API
 * 
 * This abstraction layer allows rendering code to work with multiple graphics
 * APIs (OpenGL, Vulkan, DirectX12) without changing game/editor code.
 * 
 * All methods are virtual to support backend switching at runtime.
 * In practice, a single backend is selected at compile/startup time.
 */
class RenderDevice {
public:
    /**
     * Create a RenderDevice for the current platform
     * @param api Which graphics API to use
     * @return Unique pointer to new RenderDevice
     */
    static std::unique_ptr<RenderDevice> Create(RenderAPI api = RenderAPI::OpenGL45);
    
    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;
    RenderDevice(RenderDevice&&) = default;
    RenderDevice& operator=(RenderDevice&&) = default;
    
    virtual ~RenderDevice() = default;

    // Initialization
    /**
     * Initialize the graphics device
     * Must be called after a valid graphics context exists
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shut down and release all GPU resources
     */
    virtual void Shutdown() = 0;
    
    /**
     * Get the graphics API this device uses
     */
    virtual RenderAPI GetAPI() const = 0;
    
    /**
     * Get GPU vendor name and driver version
     */
    virtual std::string GetDeviceInfo() const = 0;

    // Viewport & Rendertarget Management
    /**
     * Set the viewport rectangle within the render target
     */
    virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
    
    /**
     * Bind a framebuffer as render target (null = back buffer)
     */
    virtual void BindFramebuffer(Framebuffer* fb) = 0;
    
    /**
     * Clear the current render target
     */
    virtual void Clear(bool color, bool depth, bool stencil, 
                       const glm::vec4& color_value = glm::vec4(0.0f)) = 0;

    // Shader Operations
    /**
     * Compile a shader stage from source
     * @param stage Which shader stage (vertex, fragment, etc)
     * @param source GLSL/HLSL source code
     * @param defines Preprocessor defines array (key=value pairs)
     * @return Compiled shader handle, or 0 on error
     */
    virtual uint32_t CompileShader(ShaderStage stage, const std::string& source,
                                   const char** defines = nullptr, int define_count = 0) = 0;
    
    /**
     * Link compiled shaders into a program
     * @param stages Array of compiled shader handles
     * @param stage_count Number of shaders
     * @return Program handle
     */
    virtual uint32_t LinkProgram(const uint32_t* stages, int stage_count) = 0;
    
    /**
     * Bind a shader program for rendering
     */
    virtual void UseProgram(uint32_t program) = 0;
    
    /**
     * Delete a compiled shader or program
     */
    virtual void DeleteShader(uint32_t handle) = 0;

    // Buffer Operations
    /**
     * Create a GPU buffer
     * @param target Buffer target (GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, etc)
     * @param size Buffer size in bytes
     * @param data Initial data (can be null)
     * @param dynamic_draw true = will be updated frequently
     * @return Buffer handle
     */
    virtual uint32_t CreateBuffer(uint32_t target, size_t size, const void* data, bool dynamic_draw) = 0;
    
    /**
     * Update buffer contents
     * @param handle Buffer handle
     * @param offset Offset in buffer
     * @param size Data size in bytes
     * @param data New data
     */
    virtual void UpdateBuffer(uint32_t handle, size_t offset, size_t size, const void* data) = 0;
    
    /**
     * Delete a GPU buffer
     */
    virtual void DeleteBuffer(uint32_t handle) = 0;
    
    /**
     * Map a buffer to CPU memory (for dynamic updates)
     * @param handle Buffer handle
     * @param readonly true = read-only, false = read-write
     * @return Pointer to GPU buffer memory
     */
    virtual void* MapBuffer(uint32_t handle, bool readonly = false) = 0;
    
    /**
     * Unmap a previously mapped buffer
     */
    virtual void UnmapBuffer(uint32_t handle) = 0;

    // Texture Operations
    /**
     * Create a 2D texture
     * @param width Texture width
     * @param height Texture height
     * @param format Pixel format (GL_RGB, GL_RGBA, etc)
     * @param data Initial pixel data
     * @return Texture handle
     */
    virtual uint32_t CreateTexture2D(uint32_t width, uint32_t height, uint32_t format, const void* data) = 0;
    
    /**
     * Create a cubemap texture
     * @param size Dimensions (all faces are square)
     * @param format Pixel format
     * @param faces Array of 6 face data pointers
     * @return Texture handle
     */
    virtual uint32_t CreateTextureCube(uint32_t size, uint32_t format, const void** faces) = 0;
    
    /**
     * Bind a texture to a texture unit
     */
    virtual void BindTexture(uint32_t texture, uint32_t unit) = 0;
    
    /**
     * Delete a texture
     */
    virtual void DeleteTexture(uint32_t handle) = 0;

    // Vertex Array Object / Vertex Layout
    /**
     * Create a VAO (vertex array object)
     * Associates vertex buffers with shader attributes
     */
    virtual uint32_t CreateVertexArray() = 0;
    
    /**
     * Bind a VAO for configuration
     */
    virtual void BindVertexArray(uint32_t vao) = 0;
    
    /**
     * Configure a vertex attribute pointer
     */
    virtual void SetVertexAttribute(uint32_t attrib_index, int component_count, uint32_t type,
                                    bool normalized, uint32_t stride, const void* offset) = 0;
    
    /**
     * Associate a VBO with the current VAO
     */
    virtual void AttachVertexBuffer(uint32_t vao, uint32_t vbo, uint32_t binding_index) = 0;
    
    /**
     * Delete a VAO
     */
    virtual void DeleteVertexArray(uint32_t vao) = 0;

    // Drawing
    /**
     * Draw indexed geometry
     * @param mode Primitive mode (GL_TRIANGLES, GL_TRIANGLE_STRIP, etc)
     * @param index_count Number of indices to draw
     * @param index_type Index data type (GL_UNSIGNED_INT, GL_UNSIGNED_SHORT, etc)
     * @param index_offset Start offset in index buffer
     * @param instance_count For instanced drawing (1 = non-instanced)
     */
    virtual void DrawIndexed(uint32_t mode, uint32_t index_count, uint32_t index_type,
                             size_t index_offset, uint32_t instance_count = 1) = 0;
    
    /**
     * Draw non-indexed geometry
     */
    virtual void Draw(uint32_t mode, uint32_t vertex_count, uint32_t vertex_offset = 0) = 0;

    // State Management
    /**
     * Enable or disable depth testing
     */
    virtual void SetDepthTest(bool enabled) = 0;
    
    /**
     * Enable or disable blending
     */
    virtual void SetBlending(bool enabled) = 0;
    
    /**
     * Set blend function
     */
    virtual void SetBlendFunc(uint32_t src_factor, uint32_t dst_factor) = 0;
    
    /**
     * Enable or disable face culling
     */
    virtual void SetFaceCulling(bool enabled, bool cull_back = true) = 0;

    // Query & Stats
    /**
     * Get statistics from the last rendered frame
     */
    virtual RenderStats GetStats() const = 0;
    
    /**
     * Reset frame statistics
     */
    virtual void ResetStats() = 0;

    // Debug
    /**
     * Enable debug output (GL_ARB_debug_output)
     * Expensive for release builds
     */
    virtual void EnableDebugOutput(bool enabled) = 0;
    
    /**
     * Get the last error message
     */
    virtual std::string GetLastError() const = 0;

protected:
    RenderDevice() = default;
};

} // namespace schizo::renderer
