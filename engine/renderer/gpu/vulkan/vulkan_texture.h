/**
 * @file vulkan_texture.h
 * @brief Per-material 2D texture (image + view + sampler) for the deferred
 *        scene pipeline (Phase 5 Week 1).
 *
 * Lives in `gws::renderer::gpu::` so that `VulkanGBuffer`, `Material`, and the
 * new `glTFLoader` can pass these around without the older `vks::` namespace.
 * Owns its `VkImage`, `VkImageView`, `VkSampler`, and the `VkDeviceMemory` they
 * back. Backed by stb_image for file/memory loading.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <string>

namespace gws::renderer::gpu {

class VulkanDevice;

class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) noexcept;
    Texture& operator=(Texture&&) noexcept;

    /// Create a texture from raw RGBA8 pixels (4 bytes/texel).
    /// `srgb` selects R8G8B8A8_SRGB vs R8G8B8A8_UNORM.
    static std::unique_ptr<Texture> create_from_pixels(VulkanDevice* device,
                                                       const uint8_t* rgba_pixels,
                                                       uint32_t width,
                                                       uint32_t height,
                                                       bool srgb);

    /// Create an RGBA32F texture from raw float pixels (16 bytes/texel) with a
    /// linear, clamp-to-edge sampler. For LUTs (e.g. the LTC area-light tables)
    /// whose values fall outside [0,1] and need float precision.
    static std::unique_ptr<Texture> create_from_float_pixels(VulkanDevice* device,
                                                             const float* rgba_pixels,
                                                             uint32_t width,
                                                             uint32_t height);

    /// Decode an in-memory PNG/JPG/BMP via stb_image and upload.
    static std::unique_ptr<Texture> create_from_memory(VulkanDevice* device,
                                                       const uint8_t* data,
                                                       size_t size,
                                                       bool srgb);

    /// Decode a file via stb_image and upload.
    static std::unique_ptr<Texture> create_from_file(VulkanDevice* device,
                                                     const std::string& path,
                                                     bool srgb);

    /// Upload an already block-compressed (BCn) mip chain straight to the GPU
    /// — no CPU decode. `format` is a BC VkFormat (e.g. VK_FORMAT_BC7_*_BLOCK);
    /// `block_data` is all `mip_count` mips concatenated (the cooked texture
    /// blob's block region). Returns nullptr if the device can't sample that
    /// format (caller should fall back). This is the Stage-2 cooked-texture
    /// runtime path: cook produces BC7, the GPU samples it directly.
    static std::unique_ptr<Texture> create_compressed(VulkanDevice* device,
                                                      VkFormat format,
                                                      uint32_t width,
                                                      uint32_t height,
                                                      uint32_t mip_count,
                                                      const uint8_t* block_data,
                                                      size_t data_size);

    /// 1×1 opaque white texture. Standard fallback for unbound color slots.
    static std::unique_ptr<Texture> create_default_white(VulkanDevice* device);

    /// 1×1 (0.5, 0.5, 1.0) tangent-space "flat" normal map.
    static std::unique_ptr<Texture> create_default_normal(VulkanDevice* device);

    /// 1×1 black opaque texture. For emissive defaults.
    static std::unique_ptr<Texture> create_default_black(VulkanDevice* device);

    VkImage     image() const   { return image_; }
    VkImageView view() const    { return view_; }
    VkSampler   sampler() const { return sampler_; }
    uint32_t    width() const   { return width_; }
    uint32_t    height() const  { return height_; }

private:
    VulkanDevice*  device_  = nullptr;
    VkImage        image_   = VK_NULL_HANDLE;
    VkImageView    view_    = VK_NULL_HANDLE;
    VkSampler      sampler_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_  = VK_NULL_HANDLE;
    uint32_t       width_   = 0;
    uint32_t       height_  = 0;

    void destroy();
};

} // namespace gws::renderer::gpu
