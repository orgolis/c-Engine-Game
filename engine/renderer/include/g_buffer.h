#pragma once

#include <memory>
#include <string>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace schizo::renderer {

// Forward declarations
class RenderDevice;
class Framebuffer;
class Texture2D;

// ============================================================================
// G-Buffer Configuration
// ============================================================================

/**
 * @enum GBufferFormat
 * @brief Storage format for G-Buffer textures
 */
enum class GBufferFormat {
    RGBA8,          // 8-bit RGBA per channel
    RGBA16F,        // 16-bit float RGBA
    RGBA32F,        // 32-bit float RGBA
    RGB16F,         // 16-bit float RGB
    RG16F,          // 16-bit float RG
    R32F,           // 32-bit float R
    R8,             // 8-bit single channel
};

/**
 * @struct GBufferAttachment
 * @brief Describes a single G-Buffer texture attachment
 */
struct GBufferAttachment {
    std::string name;
    GBufferFormat format;
    uint32_t width;
    uint32_t height;
    
    GBufferAttachment(const std::string& n, GBufferFormat f, uint32_t w, uint32_t h)
        : name(n), format(f), width(w), height(h) {}
};

/**
 * @struct GBufferConfig
 * @brief Configuration for G-Buffer creation
 */
struct GBufferConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    
    // Attachment formats
    GBufferFormat position_format = GBufferFormat::RGBA16F;      // World position + padding
    GBufferFormat normal_format = GBufferFormat::RGBA16F;        // Normal + roughness
    GBufferFormat albedo_format = GBufferFormat::RGBA8;          // Albedo + metallic
    GBufferFormat material_format = GBufferFormat::RGBA8;        // Material ID + AO + emission + padding
    GBufferFormat depth_format = GBufferFormat::R32F;            // Depth texture
    
    // Options
    bool use_msaa = false;
    uint32_t msaa_samples = 4;
    bool use_half_resolution = false;  // For optimization
};

// ============================================================================
// G-Buffer Class
// ============================================================================

/**
 * @class GBuffer
 * @brief Manages deferred rendering G-Buffer with multiple render targets
 * 
 * Standard layout:
 * - Attachment 0: Position (world space)
 * - Attachment 1: Normal (world space)
 * - Attachment 2: Albedo + Metallic
 * - Attachment 3: Material ID + AO + Emission
 * - Depth: Linear depth or standard depth buffer
 */
class GBuffer {
public:
    GBuffer();
    ~GBuffer();
    
    // Deleted copy operations
    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;
    
    // Allow move operations
    GBuffer(GBuffer&&) = default;
    GBuffer& operator=(GBuffer&&) = default;
    
    /**
     * @brief Create G-Buffer with specified configuration
     * @param device Render device
     * @param config G-Buffer configuration
     * @return Unique pointer to G-Buffer
     */
    static std::unique_ptr<GBuffer> Create(RenderDevice* device, const GBufferConfig& config);
    
    // Lifecycle
    /**
     * @brief Begin rendering to G-Buffer
     */
    void BeginGeometryPass(RenderDevice* device);
    
    /**
     * @brief End rendering to G-Buffer
     */
    void EndGeometryPass(RenderDevice* device);
    
    /**
     * @brief Begin lighting pass using G-Buffer data
     */
    void BeginLightingPass(RenderDevice* device);
    
    /**
     * @brief End lighting pass
     */
    void EndLightingPass(RenderDevice* device);
    
    /**
     * @brief Clear all G-Buffer attachments
     */
    void Clear(RenderDevice* device, const glm::vec4& clear_color = glm::vec4(0.0f));
    
    /**
     * @brief Resize G-Buffer to new dimensions
     */
    void Resize(RenderDevice* device, uint32_t width, uint32_t height);
    
    // Attachment access
    /**
     * @brief Get position texture (world space coordinates)
     */
    std::shared_ptr<Texture2D> GetPositionTexture() const { return position_texture_; }
    
    /**
     * @brief Get normal texture (world space normals)
     */
    std::shared_ptr<Texture2D> GetNormalTexture() const { return normal_texture_; }
    
    /**
     * @brief Get albedo texture (surface color)
     */
    std::shared_ptr<Texture2D> GetAlbedoTexture() const { return albedo_texture_; }
    
    /**
     * @brief Get material properties texture (ID, AO, emission, etc.)
     */
    std::shared_ptr<Texture2D> GetMaterialTexture() const { return material_texture_; }
    
    /**
     * @brief Get depth texture (linear depth)
     */
    std::shared_ptr<Texture2D> GetDepthTexture() const { return depth_texture_; }
    
    /**
     * @brief Get framebuffer for rendering
     */
    Framebuffer* GetFramebuffer() const { return framebuffer_.get(); }
    
    /**
     * @brief Get all color attachments for binding
     */
    std::vector<std::shared_ptr<Texture2D>> GetColorAttachments() const;
    
    // Configuration
    /**
     * @brief Get G-Buffer configuration
     */
    const GBufferConfig& GetConfig() const { return config_; }
    
    /**
     * @brief Get width
     */
    uint32_t GetWidth() const { return config_.width; }
    
    /**
     * @brief Get height
     */
    uint32_t GetHeight() const { return config_.height; }
    
    /**
     * @brief Get aspect ratio
     */
    float GetAspectRatio() const { return static_cast<float>(config_.width) / config_.height; }
    
    // Debug/Visualization
    /**
     * @brief Get a specific attachment by index for visualization
     */
    std::shared_ptr<Texture2D> GetAttachment(uint32_t index) const;
    
    /**
     * @brief Get attachment count
     */
    uint32_t GetAttachmentCount() const { return 4; }  // Position, Normal, Albedo, Material
    
    /**
     * @brief Get attachment name for debugging
     */
    std::string GetAttachmentName(uint32_t index) const;

protected:
    GBufferConfig config_;
    std::unique_ptr<Framebuffer> framebuffer_;
    
    // Color attachments
    std::shared_ptr<Texture2D> position_texture_;
    std::shared_ptr<Texture2D> normal_texture_;
    std::shared_ptr<Texture2D> albedo_texture_;
    std::shared_ptr<Texture2D> material_texture_;
    
    // Depth
    std::shared_ptr<Texture2D> depth_texture_;
};

} // namespace schizo::renderer
