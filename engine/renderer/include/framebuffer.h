#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "texture.h"

namespace schizo::renderer {

// Forward declaration
class RenderDevice;

/**
 * @enum AttachmentType
 * @brief Framebuffer attachment types
 */
enum class AttachmentType : uint8_t {
    Color0,
    Color1,
    Color2,
    Color3,
    Color4,
    Color5,
    Color6,
    Color7,
    Depth,
    DepthStencil,
    Stencil,
};

/**
 * @struct FramebufferAttachment
 * @brief Describes a framebuffer attachment
 */
struct FramebufferAttachment {
    AttachmentType type;
    std::shared_ptr<Texture> texture;
    uint32_t mip_level = 0;
    uint32_t layer = 0;  // For array textures/cubemaps
};

/**
 * @class Framebuffer
 * @brief Abstract interface for off-screen rendering targets
 * 
 * Framebuffers allow rendering to textures instead of the screen.
 * Common uses:
 * - Post-processing effects
 * - Shadow mapping
 * - Deferred rendering (G-buffers)
 * - Scene capture
 * - Off-screen UI rendering
 */
class Framebuffer {
public:
    /**
     * Create a framebuffer with specified attachments
     * @param width Framebuffer width in pixels
     * @param height Framebuffer height in pixels
     * @param color_format Format for color attachments
     * @param depth_format Format for depth attachment (Depth24 or Depth32F)
     */
    static std::unique_ptr<Framebuffer> Create(RenderDevice* device,
                                               uint32_t width, uint32_t height,
                                               TextureFormat color_format = TextureFormat::RGBA8,
                                               TextureFormat depth_format = TextureFormat::Depth24);
    
    virtual ~Framebuffer() = default;
    
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&&) = default;
    Framebuffer& operator=(Framebuffer&&) = default;
    
    /**
     * Get framebuffer GPU handle
     */
    virtual uint32_t GetHandle() const = 0;
    
    /**
     * Get framebuffer dimensions
     */
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    
    /**
     * Get color attachment texture
     * @param index Color attachment index (0-7)
     */
    virtual std::shared_ptr<Texture> GetColorAttachment(uint32_t index = 0) const = 0;
    
    /**
     * Get depth attachment texture
     */
    virtual std::shared_ptr<Texture> GetDepthAttachment() const = 0;
    
    /**
     * Bind framebuffer as render target
     */
    virtual void Bind(RenderDevice* device) = 0;
    
    /**
     * Unbind framebuffer (return to back buffer)
     */
    virtual void Unbind(RenderDevice* device) = 0;
    
    /**
     * Invalidate framebuffer contents (optimization for tile-based deferred renderers)
     */
    virtual void Invalidate(RenderDevice* device, bool color = true, bool depth = true) = 0;
    
    /**
     * Resize framebuffer and its attachments
     */
    virtual void Resize(RenderDevice* device, uint32_t width, uint32_t height) = 0;
    
    /**
     * Add a color attachment
     * @param index Attachment index (0-7)
     * @param texture Color texture to attach
     */
    virtual void AttachColor(RenderDevice* device, uint32_t index, std::shared_ptr<Texture2D> texture) = 0;
    
    /**
     * Add a depth attachment
     * @param texture Depth texture to attach
     */
    virtual void AttachDepth(RenderDevice* device, std::shared_ptr<Texture2D> texture) = 0;
    
    /**
     * Check if framebuffer is valid (all attachments present)
     */
    virtual bool IsValid() const = 0;
    
    /**
     * Get validation error message
     */
    virtual std::string GetValidationError() const = 0;

protected:
    Framebuffer() = default;
};

} // namespace schizo::renderer

