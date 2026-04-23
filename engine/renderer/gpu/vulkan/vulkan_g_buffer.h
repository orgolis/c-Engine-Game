/**
 * @file vulkan_g_buffer.h
 * @brief Vulkan implementation of G-Buffer for deferred rendering
 * 
 * Manages multiple render targets for geometry pass in deferred rendering pipeline.
 * Standard layout:
 * - Attachment 0: Position (world space) - RGBA16F
 * - Attachment 1: Normal (world space) + Roughness - RGBA16F
 * - Attachment 2: Albedo + Metallic - RGBA8
 * - Attachment 3: Material ID + AO + Emission + Depth - RGBA8
 * - Depth: Standard depth buffer - D32F
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

namespace gws::renderer::gpu {

class VulkanImage;
class VulkanRenderPass;
class VulkanDevice;

/**
 * @enum GBufferFormat
 * @brief Storage format for G-Buffer textures
 */
enum class GBufferFormat {
    RGBA8,          // 8-bit RGBA
    RGBA16F,        // 16-bit float RGBA
    RGBA32F,        // 32-bit float RGBA
    RGB16F,         // 16-bit float RGB
    RG16F,          // 16-bit float RG
    R32F,           // 32-bit float R
    R8,             // 8-bit single channel
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
    GBufferFormat material_format = GBufferFormat::RGBA8;        // Material ID + AO + emission
    
    // Options
    bool use_msaa = false;
    uint32_t msaa_samples = 4;
};

/**
 * @class VulkanGBuffer
 * @brief Vulkan G-Buffer implementation for deferred rendering
 * 
 * Manages 4 color attachments + 1 depth attachment for deferred rendering geometry pass.
 */
class VulkanGBuffer {
public:
    VulkanGBuffer() = default;
    ~VulkanGBuffer();
    
    // Deleted copy operations
    VulkanGBuffer(const VulkanGBuffer&) = delete;
    VulkanGBuffer& operator=(const VulkanGBuffer&) = delete;
    
    // Allow move operations
    VulkanGBuffer(VulkanGBuffer&&) = default;
    VulkanGBuffer& operator=(VulkanGBuffer&&) = default;
    
    /**
     * @brief Create G-Buffer with specified configuration
     * @param device Vulkan device
     * @param config G-Buffer configuration
     * @return Unique pointer to G-Buffer
     */
    static std::unique_ptr<VulkanGBuffer> create(VulkanDevice* device, 
                                                 const GBufferConfig& config);
    
    // Lifecycle
    /**
     * @brief Begin geometry pass (render to G-Buffer)
     */
    void begin_geometry_pass(VkCommandBuffer cmd);
    
    /**
     * @brief End geometry pass
     */
    void end_geometry_pass(VkCommandBuffer cmd);
    
    /**
     * @brief Begin lighting pass (read from G-Buffer)
     */
    void begin_lighting_pass(VkCommandBuffer cmd);
    
    /**
     * @brief End lighting pass
     */
    void end_lighting_pass(VkCommandBuffer cmd);
    
    /**
     * @brief Clear all G-Buffer attachments
     */
    void clear(VkCommandBuffer cmd, const glm::vec4& clear_color = glm::vec4(0.0f));
    
    /**
     * @brief Resize G-Buffer to new dimensions
     */
    void resize(VulkanDevice* device, uint32_t width, uint32_t height);
    
    // Attachment access
    /**
     * @brief Get position texture handle
     */
    VkImageView get_position_view() const { return position_view_; }
    
    /**
     * @brief Get normal texture handle
     */
    VkImageView get_normal_view() const { return normal_view_; }
    
    /**
     * @brief Get albedo texture handle
     */
    VkImageView get_albedo_view() const { return albedo_view_; }
    
    /**
     * @brief Get material texture handle
     */
    VkImageView get_material_view() const { return material_view_; }
    
    /**
     * @brief Get depth texture handle
     */
    VkImageView get_depth_view() const { return depth_view_; }
    
    /**
     * @brief Get framebuffer handle
     */
    VkFramebuffer get_framebuffer() const { return framebuffer_; }
    
    /**
     * @brief Get render pass handle
     */
    VkRenderPass get_render_pass() const { return render_pass_; }
    
    /**
     * @brief Get width
     */
    uint32_t get_width() const { return config_.width; }
    
    /**
     * @brief Get height
     */
    uint32_t get_height() const { return config_.height; }

private:
    // Configuration
    GBufferConfig config_;
    VulkanDevice* device_ = nullptr;
    
    // Images
    VkImage position_image_ = VK_NULL_HANDLE;
    VkImage normal_image_ = VK_NULL_HANDLE;
    VkImage albedo_image_ = VK_NULL_HANDLE;
    VkImage material_image_ = VK_NULL_HANDLE;
    VkImage depth_image_ = VK_NULL_HANDLE;
    
    // Image views
    VkImageView position_view_ = VK_NULL_HANDLE;
    VkImageView normal_view_ = VK_NULL_HANDLE;
    VkImageView albedo_view_ = VK_NULL_HANDLE;
    VkImageView material_view_ = VK_NULL_HANDLE;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    
    // Memory
    VkDeviceMemory position_memory_ = VK_NULL_HANDLE;
    VkDeviceMemory normal_memory_ = VK_NULL_HANDLE;
    VkDeviceMemory albedo_memory_ = VK_NULL_HANDLE;
    VkDeviceMemory material_memory_ = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
    
    // Render pass and framebuffer
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    
    // Helper functions
    VkFormat format_to_vk(GBufferFormat fmt);
    void create_render_pass();
    void create_framebuffer();
    void cleanup();
};

} // namespace gws::renderer::gpu
