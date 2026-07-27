/**
 * @file vulkan_texture.h
 * @brief GPU 2D texture (image + view + sampler) for the deferred scene
 *        pipeline. (Master Plan Stage 2 — texture support & handling.)
 *
 * Lives in `gws::renderer::gpu::` so `VulkanGBuffer`, `Material`, the glTF
 * loader, and the TextureManager can pass these around. Owns its `VkImage`,
 * `VkImageView`, and backing `VkDeviceMemory`. The `VkSampler` is either owned
 * (standalone factories bake a default sampler) or borrowed from a shared
 * `SamplerCache` (the managed path) — see `owns_sampler_`.
 *
 * Runtime decode is via stb_image; block-compressed (BCn) cooked textures are
 * uploaded straight to the GPU with no CPU decode via `create_compressed`.
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
    /// `gen_mips` generates a full mip chain on the GPU (vkCmdBlit); requires
    /// the format to support linear blit (falls back to a single mip if not).
    /// `shared_sampler` (optional) borrows a cache-owned sampler instead of
    /// baking one — the texture then does NOT destroy it.
    static std::unique_ptr<Texture> create_from_pixels(VulkanDevice* device,
                                                       const uint8_t* rgba_pixels,
                                                       uint32_t width,
                                                       uint32_t height,
                                                       bool srgb,
                                                       bool gen_mips = false,
                                                       VkSampler shared_sampler = VK_NULL_HANDLE);

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
                                                       bool srgb,
                                                       bool gen_mips = false,
                                                       VkSampler shared_sampler = VK_NULL_HANDLE);

    /// Decode a file via stb_image and upload.
    static std::unique_ptr<Texture> create_from_file(VulkanDevice* device,
                                                     const std::string& path,
                                                     bool srgb,
                                                     bool gen_mips = false,
                                                     VkSampler shared_sampler = VK_NULL_HANDLE);

    /// Upload an already block-compressed (BCn) mip chain straight to the GPU
    /// — no CPU decode. `format` is a BC VkFormat (e.g. VK_FORMAT_BC7_*_BLOCK);
    /// `block_data` is all `mip_count` mips concatenated (the cooked texture
    /// blob's block region). Returns nullptr if the device can't sample that
    /// format (caller should fall back). `shared_sampler` optionally borrows a
    /// cache-owned sampler. This is the Stage-2 cooked-texture runtime path.
    static std::unique_ptr<Texture> create_compressed(VulkanDevice* device,
                                                      VkFormat format,
                                                      uint32_t width,
                                                      uint32_t height,
                                                      uint32_t mip_count,
                                                      const uint8_t* block_data,
                                                      size_t data_size,
                                                      VkSampler shared_sampler = VK_NULL_HANDLE);

    /// 1×1 opaque white texture. Standard fallback for unbound color slots.
    static std::unique_ptr<Texture> create_default_white(VulkanDevice* device);

    /// 1×1 (0.5, 0.5, 1.0) tangent-space "flat" normal map.
    static std::unique_ptr<Texture> create_default_normal(VulkanDevice* device);

    /// 1×1 black opaque texture. For emissive defaults.
    static std::unique_ptr<Texture> create_default_black(VulkanDevice* device);

    /// Rebuild this texture's image/view from new RGBA8 pixels IN PLACE, keeping
    /// the same object identity and sampler so a descriptor set that references
    /// `view()` + `sampler()` only needs re-writing (not re-pointering). Bumps
    /// `generation()`. Used by the TextureManager for hot reload. The caller
    /// MUST ensure the GPU is idle (no in-flight use of the old image).
    bool reload_from_pixels(const uint8_t* rgba_pixels, uint32_t width,
                            uint32_t height, bool srgb, bool gen_mips);

    VkImage     image() const   { return image_; }
    VkImageView view() const    { return view_; }
    VkSampler   sampler() const { return sampler_; }
    uint32_t    width() const   { return width_; }
    uint32_t    height() const  { return height_; }
    uint32_t    mip_levels() const { return mip_levels_; }
    /// Incremented on every in-place reload — descriptor sets can compare the
    /// value they last bound against this to detect a hot-reload.
    uint32_t    generation() const { return generation_; }
    bool        is_valid() const   { return image_ != VK_NULL_HANDLE; }

private:
    VulkanDevice*  device_       = nullptr;
    VkImage        image_        = VK_NULL_HANDLE;
    VkImageView    view_         = VK_NULL_HANDLE;
    VkSampler      sampler_      = VK_NULL_HANDLE;
    VkDeviceMemory memory_       = VK_NULL_HANDLE;
    uint32_t       width_        = 0;
    uint32_t       height_       = 0;
    uint32_t       mip_levels_   = 1;
    uint32_t       generation_   = 0;
    bool           owns_sampler_ = true;  // false when borrowing from SamplerCache

    // Build the device-local RGBA8 image (+ optional generated mip chain) into
    // image_/view_/memory_/width_/height_/mip_levels_. Sampler handled by the
    // caller. `device_` must already be set. Returns false on failure.
    bool build_rgba_(const uint8_t* rgba_pixels, uint32_t width, uint32_t height,
                     bool srgb, bool gen_mips);

    // Free image/view/memory (but keep sampler + device_) for in-place reload.
    void destroy_image_only_();

    void destroy();
};

} // namespace gws::renderer::gpu
