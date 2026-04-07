#pragma once

#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <cstdint>

namespace schizo::renderer {

// Forward declarations
class RenderDevice;
class GBuffer;
class Light;
class LightManager;
class Framebuffer;
class Shader;
class Mesh;
class Material;

// ============================================================================
// Deferred Rendering Configuration
// ============================================================================

/**
 * @enum RenderingMode
 * @brief Rendering pipeline mode
 */
enum class RenderingMode {
    Forward,        // Traditional forward rendering
    Deferred,       // Deferred rendering
    Hybrid,         // Hybrid: deferred for opaque, forward for transparent
};

/**
 * @struct DeferredConfig
 * @brief Configuration for deferred rendering pipeline
 */
struct DeferredConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    
    RenderingMode mode = RenderingMode::Hybrid;
    
    // Optimization
    bool enable_light_culling = true;           // Spatial light culling
    bool enable_tile_based_deferred = false;    // Tile-based light binning
    uint32_t tile_size = 16;                    // Tile size for light binning
    
    // Light limits
    uint32_t max_lights_per_tile = 32;
    uint32_t max_shadow_casting_lights = 8;
    
    // Debug visualization
    bool debug_gbuffer = false;                 // Visualize G-Buffer attachments
    bool debug_light_count = false;             // Visualize light count heatmap
    bool debug_tile_heatmap = false;            // Visualize tile-based lighting
};

// ============================================================================
// Deferred Rendering Pass
// ============================================================================

/**
 * @struct DeferredPass
 * @brief Represents a single rendering pass in deferred pipeline
 */
struct DeferredPass {
    std::string name;
    std::shared_ptr<Shader> shader;
    uint32_t attachment_count = 0;
    bool clear_before = true;
    bool depth_test = true;
    bool depth_write = false;
};

// ============================================================================
// Deferred Renderer
// ============================================================================

/**
 * @class DeferredRenderer
 * @brief Main deferred rendering pipeline implementation
 * 
 * Architecture:
 * 1. Geometry Pass: Render all opaque objects to G-Buffer
 * 2. Light Culling: Determine which lights affect which tiles/pixels
 * 3. Lighting Pass: Render full-screen quad with lighting calculations
 * 4. Forward Pass: Render transparent objects on top (if hybrid mode)
 * 5. Post-Processing: Apply effects (bloom, tone mapping, etc.)
 */
class DeferredRenderer {
public:
    DeferredRenderer();
    ~DeferredRenderer();
    
    // Deleted copy operations
    DeferredRenderer(const DeferredRenderer&) = delete;
    DeferredRenderer& operator=(const DeferredRenderer&) = delete;
    
    // Allow move operations
    DeferredRenderer(DeferredRenderer&&) = default;
    DeferredRenderer& operator=(DeferredRenderer&&) = default;
    
    /**
     * @brief Create deferred renderer
     * @param device Render device
     * @param config Configuration
     * @return Unique pointer to deferred renderer
     */
    static std::unique_ptr<DeferredRenderer> Create(RenderDevice* device, const DeferredConfig& config);
    
    // Lifecycle
    /**
     * @brief Initialize renderer (compile shaders, allocate buffers)
     */
    void Initialize(RenderDevice* device);
    
    /**
     * @brief Shutdown renderer and cleanup resources
     */
    void Shutdown(RenderDevice* device);
    
    /**
     * @brief Resize renderer to new dimensions
     */
    void Resize(RenderDevice* device, uint32_t width, uint32_t height);
    
    // Rendering
    /**
     * @brief Begin frame rendering
     */
    void BeginFrame(RenderDevice* device);
    
    /**
     * @brief End frame rendering
     */
    void EndFrame(RenderDevice* device);
    
    /**
     * @brief Geometry pass: render to G-Buffer
     */
    void GeometryPass(RenderDevice* device, const std::vector<std::pair<std::shared_ptr<Mesh>, std::shared_ptr<Material>>>& objects);
    
    /**
     * @brief Light culling: determine visible lights
     */
    void LightCullingPass(RenderDevice* device, LightManager* light_manager);
    
    /**
     * @brief Lighting pass: calculate final lighting
     */
    void LightingPass(RenderDevice* device, LightManager* light_manager, const glm::mat4& view_matrix, const glm::mat4& proj_matrix);
    
    /**
     * @brief Forward pass: render transparent objects
     */
    void ForwardPass(RenderDevice* device, const std::vector<std::pair<std::shared_ptr<Mesh>, std::shared_ptr<Material>>>& transparent_objects);
    
    /**
     * @brief Composite final result to screen
     */
    void CompositePass(RenderDevice* device);
    
    // Light management
    /**
     * @brief Set light manager for rendering
     */
    void SetLightManager(LightManager* light_manager) { light_manager_ = light_manager; }
    
    /**
     * @brief Get light manager
     */
    LightManager* GetLightManager() const { return light_manager_; }
    
    /**
     * @brief Get visible light count (after culling)
     */
    uint32_t GetVisibleLightCount() const { return visible_light_count_; }
    
    /**
     * @brief Get max lights per tile
     */
    uint32_t GetMaxLightsPerTile() const { return config_.max_lights_per_tile; }
    
    // G-Buffer access
    /**
     * @brief Get G-Buffer
     */
    GBuffer* GetGBuffer() const { return gbuffer_.get(); }
    
    /**
     * @brief Get output framebuffer
     */
    Framebuffer* GetOutputFramebuffer() const { return output_framebuffer_.get(); }
    
    // Configuration
    /**
     * @brief Get renderer configuration
     */
    const DeferredConfig& GetConfig() const { return config_; }
    
    /**
     * @brief Set rendering mode
     */
    void SetRenderingMode(RenderingMode mode) { config_.mode = mode; }
    
    /**
     * @brief Get current rendering mode
     */
    RenderingMode GetRenderingMode() const { return config_.mode; }
    
    /**
     * @brief Enable/disable light culling
     */
    void SetLightCullingEnabled(bool enabled) { config_.enable_light_culling = enabled; }
    
    /**
     * @brief Enable/disable tile-based deferred
     */
    void SetTileBasedDeferredEnabled(bool enabled) { config_.enable_tile_based_deferred = enabled; }
    
    /**
     * @brief Set debug visualization mode
     */
    void SetDebugMode(bool gbuffer, bool light_count, bool tile_heatmap) {
        config_.debug_gbuffer = gbuffer;
        config_.debug_light_count = light_count;
        config_.debug_tile_heatmap = tile_heatmap;
    }
    
    // Performance queries
    /**
     * @brief Get geometry pass time (ms)
     */
    float GetGeometryPassTime() const { return geometry_pass_time_; }
    
    /**
     * @brief Get light culling time (ms)
     */
    float GetLightCullingTime() const { return light_culling_time_; }
    
    /**
     * @brief Get lighting pass time (ms)
     */
    float GetLightingPassTime() const { return lighting_pass_time_; }
    
    /**
     * @brief Get total frame time (ms)
     */
    float GetTotalFrameTime() const { 
        return geometry_pass_time_ + light_culling_time_ + lighting_pass_time_; 
    }

protected:
    // Helper methods
    void CreateShaders(RenderDevice* device);
    void CreateBuffers(RenderDevice* device);
    void SetupGeometryPass(RenderDevice* device);
    void SetupLightingPass(RenderDevice* device);
    void PerformLightCulling(RenderDevice* device);
    void ComputeLightTiles(RenderDevice* device);
    
    DeferredConfig config_;
    RenderDevice* device_ = nullptr;
    LightManager* light_manager_ = nullptr;
    
    // G-Buffer
    std::unique_ptr<GBuffer> gbuffer_;
    std::unique_ptr<Framebuffer> output_framebuffer_;
    
    // Shaders for each pass
    std::shared_ptr<Shader> geometry_shader_;
    std::shared_ptr<Shader> lighting_shader_;
    std::shared_ptr<Shader> forward_shader_;
    std::shared_ptr<Shader> composite_shader_;
    std::shared_ptr<Shader> debug_shader_;
    
    // Full-screen quad for lighting pass
    std::shared_ptr<Mesh> fullscreen_quad_;
    
    // Light data buffers
    std::vector<GLuint> light_list_buffers_;
    std::vector<GLuint> light_grid_buffers_;
    GLuint light_index_buffer_ = 0;
    
    // Performance timing
    float geometry_pass_time_ = 0.0f;
    float light_culling_time_ = 0.0f;
    float lighting_pass_time_ = 0.0f;
    
    // State tracking
    uint32_t visible_light_count_ = 0;
    uint32_t total_light_count_ = 0;
    bool is_rendering_ = false;
};

// ============================================================================
// Utility Classes
// ============================================================================

/**
 * @class TileLightBinner
 * @brief Performs tile-based light binning for efficient culling
 */
class TileLightBinner {
public:
    struct Tile {
        std::vector<uint32_t> light_indices;
        uint32_t light_count = 0;
    };
    
    TileLightBinner(uint32_t width, uint32_t height, uint32_t tile_size);
    
    /**
     * @brief Bin lights into tiles
     */
    void BinLights(const std::vector<std::shared_ptr<Light>>& lights, 
                   const glm::mat4& proj_matrix);
    
    /**
     * @brief Get tile at screen position
     */
    const Tile& GetTile(uint32_t x, uint32_t y) const;
    
    /**
     * @brief Get all tiles
     */
    const std::vector<Tile>& GetTiles() const { return tiles_; }
    
    /**
     * @brief Get max light count in any tile
     */
    uint32_t GetMaxTileLightCount() const;

private:
    uint32_t width_, height_, tile_size_;
    uint32_t tiles_x_, tiles_y_;
    std::vector<Tile> tiles_;
};

/**
 * @class DeferredDebugger
 * @brief Visualization utilities for deferred rendering
 */
class DeferredDebugger {
public:
    /**
     * @brief Visualize G-Buffer attachment
     */
    static void VisualizeGBuffer(RenderDevice* device, GBuffer* gbuffer, uint32_t attachment_index);
    
    /**
     * @brief Visualize light count heatmap
     */
    static void VisualizeLightCount(RenderDevice* device, const std::vector<glm::vec4>& light_count_data, 
                                   uint32_t width, uint32_t height);
    
    /**
     * @brief Visualize tile-based lighting heatmap
     */
    static void VisualizeTileHeatmap(RenderDevice* device, const TileLightBinner& binner, 
                                    uint32_t width, uint32_t height);
};

} // namespace schizo::renderer
