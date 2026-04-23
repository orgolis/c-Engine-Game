/**
 * @file image.h
 * @brief Vulkan Image abstraction - RAII wrapper for textures and render targets
 * 
 * Provides unified interface for 2D textures, cubemaps, and attachments
 * with automatic GPU memory management and layout tracking.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>

namespace vks {

/**
 * @enum ImageFormat
 * @brief Supported image formats
 */
enum class ImageFormat {
    RGBA8_SRGB,       ///< 8-bit SRGB color (for sRGB textures)
    RGBA8_UNORM,      ///< 8-bit linear color
    RGBA16F,          ///< 16-bit float HDR
    RGBA32F,          ///< 32-bit float high precision
    DEPTH32F,         ///< 32-bit depth
    DEPTH24_STENCIL8, ///< 24-bit depth + 8-bit stencil
};

/**
 * @enum ImageUsage
 * @brief How the image will be used
 */
enum class ImageUsage {
    TEXTURE,          ///< Sampled texture (VK_IMAGE_USAGE_SAMPLED_BIT)
    ATTACHMENT,       ///< Render target attachment
    STORAGE,          ///< Storage image for compute
};

/**
 * @class VulkanImage
 * @brief RAII wrapper for GPU images/textures
 * 
 * Automatically manages VkImage, VkImageView, and optionally VkSampler.
 * Tracks layout for barrier management.
 */
class VulkanImage {
public:
    VulkanImage() = default;
    ~VulkanImage() { destroy(); }
    
    /**
     * @brief Create an empty image
     * @param device Vulkan logical device
     * @param pdev Physical device
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param format Image format
     * @param usage How the image will be used
     * @return Created image
     */
    static VulkanImage create(VkDevice device, VkPhysicalDevice pdev,
                             uint32_t width, uint32_t height,
                             ImageFormat format, ImageUsage usage);
    
    /**
     * @brief Load image from file (PNG, JPG, etc.)
     * @param device Vulkan logical device
     * @param pdev Physical device
     * @param path File path
     * @param usage Image usage type
     * @return Loaded image
     */
    static VulkanImage load_from_file(VkDevice device, VkPhysicalDevice pdev,
                                     const std::string& path,
                                     ImageUsage usage = ImageUsage::TEXTURE);
    
    /**
     * @brief Get the underlying VkImage handle
     */
    VkImage get_image() const { return image; }
    
    /**
     * @brief Get the image view
     */
    VkImageView get_view() const { return view; }
    
    /**
     * @brief Get the sampler (for textures)
     */
    VkSampler get_sampler() const { return sampler; }
    
    /**
     * @brief Get image width in pixels
     */
    uint32_t get_width() const { return width; }
    
    /**
     * @brief Get image height in pixels
     */
    uint32_t get_height() const { return height; }
    
    /**
     * @brief Get Vulkan format
     */
    VkFormat get_vk_format() const { return vk_format; }
    
    /**
     * @brief Get current image layout
     */
    VkImageLayout get_layout() const { return current_layout; }
    
    /**
     * @brief Set current image layout (for barrier tracking)
     */
    void set_layout(VkImageLayout layout) { current_layout = layout; }
    
    /**
     * @brief Check if image is valid
     */
    bool is_valid() const { return image != VK_NULL_HANDLE; }
    
    // Move semantics
    VulkanImage(VulkanImage&& other) noexcept;
    VulkanImage& operator=(VulkanImage&& other) noexcept;
    
    // Delete copies
    VulkanImage(const VulkanImage&) = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice pdev = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    
    uint32_t width = 0, height = 0;
    VkFormat vk_format = VK_FORMAT_UNDEFINED;
    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    ImageUsage usage = ImageUsage::TEXTURE;
    
    void destroy();
    static VkFormat format_to_vk(ImageFormat fmt);
    static VkImageAspectFlags format_to_aspect(ImageFormat fmt);
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) const;
};

}  // namespace vks
