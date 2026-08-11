#include "framebuffer.h"
#include "render_device.h"
#include <spdlog/spdlog.h>
#include <cstdint>

namespace schizo::renderer {

// ============================================================================
// OpenGL Framebuffer Implementation
// ============================================================================

class OpenGLFramebuffer : public Framebuffer {
public:
    OpenGLFramebuffer(uint32_t handle, uint32_t width, uint32_t height)
        : handle_(handle), width_(width), height_(height) {}
    
    ~OpenGLFramebuffer() override {}
    
    uint32_t GetHandle() const override { return handle_; }
    uint32_t GetWidth() const override { return width_; }
    uint32_t GetHeight() const override { return height_; }
    
    std::shared_ptr<Texture> GetColorAttachment(uint32_t index = 0) const override {
        if (index < color_attachments_.size()) {
            return color_attachments_[index];
        }
        return nullptr;
    }
    
    std::shared_ptr<Texture> GetDepthAttachment() const override {
        return depth_attachment_;
    }
    
    void Bind(RenderDevice* device) override {
        if (device) {
            device->BindFramebuffer(this);
            spdlog::debug("Framebuffer::Bind - Bound framebuffer {}", handle_);
        }
    }
    
    void Unbind(RenderDevice* device) override {
        if (device) {
            device->BindFramebuffer(nullptr);
            spdlog::debug("Framebuffer::Unbind - Unbound framebuffer");
        }
    }
    
    void Invalidate(RenderDevice* device, bool color = true, bool depth = true) override {
        if (!device) return;
        
        // Invalidate framebuffer contents (optimization for tile-based deferred renderers)
        spdlog::debug("Framebuffer::Invalidate - Invalidating framebuffer {} (color={}, depth={})", 
                      handle_, color, depth);
        
        // Note: Actual implementation would call glInvalidateFramebuffer
    }
    
    void Resize(RenderDevice* device, uint32_t width, uint32_t height) override {
        if (!device) return;
        
        spdlog::info("Framebuffer::Resize - {}x{} -> {}x{}", width_, height_, width, height);
        
        width_ = width;
        height_ = height;
        
        // Resize all attachments
        for (auto& attachment : color_attachments_) {
            if (auto tex = std::dynamic_pointer_cast<Texture2D>(attachment)) {
                tex->Resize(device, width, height);
            }
        }
        
        if (auto tex = std::dynamic_pointer_cast<Texture2D>(depth_attachment_)) {
            tex->Resize(device, width, height);
        }
    }
    
    void AttachColor(RenderDevice* device, uint32_t index, std::shared_ptr<Texture2D> texture) override {
        if (!device || !texture) {
            spdlog::warn("Framebuffer::AttachColor - Invalid device or texture");
            return;
        }
        
        if (index >= 8) {
            spdlog::warn("Framebuffer::AttachColor - Index {} exceeds max attachments (8)", index);
            return;
        }
        
        // Resize array if needed
        if (color_attachments_.size() <= index) {
            color_attachments_.resize(index + 1);
        }
        
        color_attachments_[index] = texture;
        spdlog::debug("Framebuffer::AttachColor - Attached color texture at index {}", index);
    }
    
    void AttachDepth(RenderDevice* device, std::shared_ptr<Texture2D> texture) override {
        if (!device || !texture) {
            spdlog::warn("Framebuffer::AttachDepth - Invalid device or texture");
            return;
        }
        
        depth_attachment_ = texture;
        spdlog::debug("Framebuffer::AttachDepth - Attached depth texture");
    }
    
    bool IsValid() const override {
        if (handle_ == 0) {
            validation_error_ = "Invalid framebuffer handle";
            return false;
        }
        
        if (color_attachments_.empty() && !depth_attachment_) {
            validation_error_ = "No attachments";
            return false;
        }
        
        return true;
    }
    
    std::string GetValidationError() const override {
        return validation_error_;
    }

private:
    uint32_t handle_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    
    std::vector<std::shared_ptr<Texture>> color_attachments_;
    std::shared_ptr<Texture> depth_attachment_;
    
    mutable std::string validation_error_;
};

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<Framebuffer> Framebuffer::Create(RenderDevice* device,
                                                  uint32_t width, uint32_t height,
                                                  TextureFormat color_format,
                                                  TextureFormat depth_format) {
    if (!device) {
        spdlog::error("Framebuffer::Create - No render device");
        return nullptr;
    }
    
    // Create color attachment
    auto color_tex = Texture2D::Create(device, width, height, color_format);
    if (!color_tex) {
        spdlog::error("Framebuffer::Create - Failed to create color attachment");
        return nullptr;
    }
    
    // Create depth attachment
    auto depth_tex = Texture2D::Create(device, width, height, depth_format);
    if (!depth_tex) {
        spdlog::error("Framebuffer::Create - Failed to create depth attachment");
        return nullptr;
    }
    
    // Create framebuffer
    // Note: In real implementation, would call device->CreateFramebuffer()
    uint32_t fb_handle = 1;  // Placeholder
    
    auto framebuffer = std::make_unique<OpenGLFramebuffer>(fb_handle, width, height);
    framebuffer->AttachColor(device, 0, std::dynamic_pointer_cast<Texture2D>(color_tex));
    framebuffer->AttachDepth(device, std::dynamic_pointer_cast<Texture2D>(depth_tex));
    
    spdlog::info("Framebuffer::Create - Created {}x{} framebuffer with color and depth attachments", 
                 width, height);
    
    return framebuffer;
}

} // namespace schizo::renderer
