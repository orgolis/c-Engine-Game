#pragma once

#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "render_device.h"

namespace schizo::renderer {

// Forward declarations (RenderDevice is already included above)

/**
 * @struct Transform
 * @brief 3D position, rotation, scale
 */
struct Transform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    
    glm::mat4 GetMatrix() const;
};

/**
 * @struct Camera
 * @brief Camera definition with projection
 */
struct Camera {
    glm::vec3 position = glm::vec3(0.0f, 1.0f, 5.0f);
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    
    float fov = 45.0f;       // Field of view in degrees
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
    
    uint32_t viewport_width = 1280;
    uint32_t viewport_height = 720;
    
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix() const;
    glm::mat4 GetViewProjectionMatrix() const;
};

/**
 * @struct LightData
 * @brief Simple light source data for shader parameters
 */
struct LightData {
    enum class Type : uint8_t {
        Directional,
        Point,
        Spot,
    };
    
    Type type = Type::Directional;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    
    // Point & Spot light parameters
    float range = 10.0f;
    float inner_angle = 30.0f;  // Spot light inner cone angle
    float outer_angle = 45.0f;  // Spot light outer cone angle
    
    // Shadow parameters
    bool cast_shadow = false;
    float shadow_near = 0.1f;
    float shadow_far = 100.0f;
};

/**
 * @class Renderer
 * @brief High-level rendering interface
 * 
 * Provides a simple API for rendering scenes with multiple lighting models
 * (forward, deferred) without exposing low-level GPU details.
 */
class Renderer {
public:
    /**
     * Create a renderer for the current platform
     * @return Unique pointer to new Renderer
     */
    static std::unique_ptr<Renderer> Create();
    
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = default;
    Renderer& operator=(Renderer&&) = default;
    
    virtual ~Renderer() = default;

    // Initialization
    /**
     * Initialize the renderer
     * Must be called with a valid graphics context and AFTER RenderDevice::Initialize
     */
    virtual bool Initialize() = 0;
    
    /**
     * Shutdown and cleanup
     */
    virtual void Shutdown() = 0;
    
    /**
     * Access the underlying RenderDevice
     */
    virtual RenderDevice* GetDevice() = 0;
    virtual const RenderDevice* GetDevice() const = 0;

    // Frame Management
    /**
     * Begin a new frame
     * Call this once per frame before any draw calls
     */
    virtual void BeginFrame(const Camera& camera) = 0;
    
    /**
     * End the current frame and submit all rendering to GPU
     */
    virtual void EndFrame() = 0;
    
    /**
     * Get performance statistics from the last frame
     */
    virtual RenderStats GetStats() const = 0;

    // Simple Drawing API
    /**
     * Draw a colored triangle (for testing/debugging)
     */
    virtual void DrawTriangle(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                             const glm::vec4& color) = 0;
    
    /**
     * Draw a wireframe cube
     */
    virtual void DrawCube(const Transform& transform, const glm::vec4& color) = 0;
    
    /**
     * Draw a wireframe sphere
     */
    virtual void DrawSphere(const glm::vec3& center, float radius, const glm::vec4& color,
                           uint32_t segments = 16) = 0;
    
    // Debug Visualization
    /**
     * Enable or disable debug rendering (wireframes, bounds, axes)
     */
    virtual void SetDebugRenderingEnabled(bool enabled) = 0;
    
    /**
     * Get vertex shader source template for current API
     */
    virtual std::string GetVertexShaderTemplate() const = 0;
    
    /**
     * Get fragment shader source template for current API
     */
    virtual std::string GetFragmentShaderTemplate() const = 0;

protected:
    Renderer() = default;
};

} // namespace schizo::renderer
