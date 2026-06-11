/**
 * @file vulkan_environment_map.h
 * @brief HDR cubemap environment map — owned by the engine, sampled by the
 *        sky pass and (eventually) the IBL precompute passes.
 *
 * Two creation paths:
 *   - `create_procedural`: builds a simple zenith/horizon gradient cubemap
 *     on the CPU and uploads it. Default fallback when no `.hdr` asset
 *     exists. Cheap and lets the engine boot without external content.
 *   - `create_from_hdr`: loads an equirectangular HDR file via stb_image,
 *     uploads it as a 2D source texture, then dispatches a compute shader
 *     to project each face of the cubemap from that source.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace gws::renderer::gpu {

class VulkanDevice;

class VulkanEnvironmentMap {
public:
    /// Build a procedural-gradient cubemap. `face_size` is the side length
    /// of each cubemap face in pixels.
    static std::unique_ptr<VulkanEnvironmentMap> create_procedural(
        VulkanDevice* device,
        uint32_t      face_size = 256);

    /// Load an equirectangular HDR file (any format stb_image supports —
    /// `.hdr`, `.exr` via add-on, etc.) and project it onto a cubemap.
    /// Returns nullptr on failure (file missing, decode error). The caller
    /// can fall back to `create_procedural`.
    static std::unique_ptr<VulkanEnvironmentMap> create_from_hdr(
        VulkanDevice* device,
        const std::string& path,
        uint32_t      face_size = 256);

    VulkanEnvironmentMap() = default;
    ~VulkanEnvironmentMap();
    VulkanEnvironmentMap(const VulkanEnvironmentMap&) = delete;
    VulkanEnvironmentMap& operator=(const VulkanEnvironmentMap&) = delete;

    VkImageView get_view() const    { return cubemap_view_; }
    VkSampler   get_sampler() const { return sampler_; }
    uint32_t    get_face_size() const { return face_size_; }

    /// Bake the three IBL precompute textures from the base cubemap:
    ///   1. Diffuse-IBL irradiance cubemap (small, low-frequency convolution).
    ///   2. Prefiltered specular environment (GGX-importance-sampled, mip
    ///      chain encodes the roughness sweep).
    ///   3. BRDF integration LUT (Karis split-sum specular term).
    /// Idempotent — calling more than once does nothing after the first.
    /// Returns false if the bake failed (e.g. the cubemap was never built).
    bool bake_ibl(uint32_t irradiance_size   = 32,
                  uint32_t prefilter_size    = 256,
                  uint32_t prefilter_mips    = 5,
                  uint32_t brdf_lut_size     = 256);

    bool        ibl_ready() const     { return ibl_baked_; }
    VkImageView get_irradiance_view() const  { return irradiance_view_; }
    VkImageView get_prefiltered_view() const { return prefiltered_view_; }
    VkImageView get_brdf_lut_view() const    { return brdf_lut_view_; }
    VkSampler   get_brdf_lut_sampler() const { return brdf_lut_sampler_; }
    uint32_t    get_prefilter_mips() const   { return prefilter_mips_; }

private:
    bool create_cubemap_image(VkFormat format);
    bool create_sampler();
    bool build_from_equirect_buffer(const float* rgba_data,
                                     int src_width,
                                     int src_height);
    void upload_procedural_faces();
    bool bake_irradiance(uint32_t size);
    bool bake_prefiltered(uint32_t size, uint32_t mips);
    bool bake_brdf_lut(uint32_t size);
    void destroy();

    VulkanDevice* device_   = nullptr;
    uint32_t      face_size_ = 0;

    VkImage        cubemap_image_  = VK_NULL_HANDLE;
    VkDeviceMemory cubemap_memory_ = VK_NULL_HANDLE;
    VkImageView    cubemap_view_   = VK_NULL_HANDLE;  // viewType = CUBE
    VkSampler      sampler_        = VK_NULL_HANDLE;

    // ---- IBL precompute outputs ------------------------------------
    bool ibl_baked_ = false;
    uint32_t prefilter_mips_ = 0;

    VkImage        irradiance_image_   = VK_NULL_HANDLE;
    VkDeviceMemory irradiance_memory_  = VK_NULL_HANDLE;
    VkImageView    irradiance_view_    = VK_NULL_HANDLE; // viewType = CUBE

    VkImage        prefiltered_image_  = VK_NULL_HANDLE;
    VkDeviceMemory prefiltered_memory_ = VK_NULL_HANDLE;
    VkImageView    prefiltered_view_   = VK_NULL_HANDLE; // viewType = CUBE, all mips

    VkImage        brdf_lut_image_     = VK_NULL_HANDLE;
    VkDeviceMemory brdf_lut_memory_    = VK_NULL_HANDLE;
    VkImageView    brdf_lut_view_      = VK_NULL_HANDLE; // viewType = 2D
    VkSampler      brdf_lut_sampler_   = VK_NULL_HANDLE;
};

} // namespace gws::renderer::gpu
