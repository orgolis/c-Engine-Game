/**
 * @file vulkan_volumetric_light_pass.h
 * @brief Volumetric sun lighting / light shafts (crepuscular rays) — two stages.
 *
 *   1. Compute pass: ray-marches the world-space view ray from camera to the
 *      scene surface (or a max distance for sky), sampling the sun's shadow
 *      map per step to accumulate single-scattered in-scattering with a
 *      Henyey-Greenstein phase. Runs at HALF resolution, dithered.
 *   2. Composite pass: fullscreen triangle ADDITIVELY blends the (bilinearly
 *      upsampled) in-scatter onto the HDR target.
 *
 * Reuses the existing directional shadow map + sun light. Runs between
 * Lighting and Transparent, before tone-mapping (it is an HDR effect).
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanGBuffer;

struct VolumetricLightConfig {
    // Homogeneous medium extinction/scatter coefficient (per world unit).
    // Higher = denser haze, brighter shafts, shorter visible range.
    float density      = 0.03f;
    // Henyey-Greenstein anisotropy. 0 = isotropic; >0 forward-scatters so
    // shafts bloom when looking toward the sun. 0.6–0.8 reads well.
    float anisotropy   = 0.80f;
    // Overall scale on the composited in-scatter.
    float intensity    = 1.0f;
    // World-space ray length cap (also the march distance for sky pixels).
    float max_distance = 80.0f;
    // Ray-march sample count. Higher = crisper, less-banded shafts (the
    // bilateral upsample cleans residual dither). 80 at half-res reads well.
    int   num_steps    = 80;
    // Shadow-map depth comparison bias.
    float shadow_bias  = 0.0015f;
};

class VulkanVolumetricLightPass {
public:
    static std::unique_ptr<VulkanVolumetricLightPass> create(
        VulkanDevice* device,
        VulkanGBuffer* g_buffer,
        VkImageView hdr_color_view,
        VkFormat    hdr_format,
        VkImageView shadow_view,
        VkSampler   shadow_sampler,
        uint32_t    width,
        uint32_t    height);

    VulkanVolumetricLightPass() = default;
    ~VulkanVolumetricLightPass();
    VulkanVolumetricLightPass(const VulkanVolumetricLightPass&) = delete;
    VulkanVolumetricLightPass& operator=(const VulkanVolumetricLightPass&) = delete;

    /// Run compute + composite back-to-back.
    void execute(VkCommandBuffer cmd,
                 const glm::mat4& view,
                 const glm::mat4& proj,
                 const glm::vec3& camera_position);

    /// Sun direction (light -> scene) + colour (folds in intensity).
    void set_sun(const glm::vec3& direction, const glm::vec3& color) {
        sun_direction_ = direction; sun_color_ = color;
    }
    /// Sun light view-projection matrix (the shadow map's matrix).
    void set_shadow_matrix(const glm::mat4& m) { shadow_matrix_ = m; }

    /// Bind the cloud shadow map so shafts are occluded by clouds (god rays
    /// through gaps). Call once after the cloud pass is created.
    void set_cloud_shadow(VkImageView view, VkSampler sampler);
    /// (center.x, center.z, half_extent, enabled) — update each frame.
    void set_cloud_shadow_params(const glm::vec4& p) { cloud_params_ = p; }

    VolumetricLightConfig& mutable_config() { return config_; }
    const VolumetricLightConfig& config() const { return config_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

private:
    bool initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                    VkImageView hdr_color_view, VkFormat hdr_format,
                    VkImageView shadow_view, VkSampler shadow_sampler,
                    uint32_t width, uint32_t height);
    bool create_scatter_target();
    bool create_uniform_buffer();
    bool create_compute_pipeline();
    bool create_composite_pipeline(VkImageView hdr_color_view, VkFormat hdr_format);
    void destroy();

    VulkanDevice*  device_   = nullptr;
    VulkanGBuffer* g_buffer_ = nullptr;
    uint32_t       width_    = 0;   // full-res (HDR target)
    uint32_t       height_   = 0;
    uint32_t       scatter_w_ = 0;  // half-res (scatter target)
    uint32_t       scatter_h_ = 0;
    bool           enabled_  = true;
    VolumetricLightConfig config_{};

    glm::vec3 sun_direction_ = glm::vec3(0.3f, -1.0f, 0.2f);
    glm::vec3 sun_color_     = glm::vec3(1.0f, 0.95f, 0.85f);
    glm::mat4 shadow_matrix_ = glm::mat4(1.0f);
    VkImageView cloud_shadow_view_    = VK_NULL_HANDLE; // borrowed from cloud pass
    VkSampler   cloud_shadow_sampler_ = VK_NULL_HANDLE;
    glm::vec4   cloud_params_ = glm::vec4(0.0f); // center.xz, half_extent, enabled

    VkSampler   gbuffer_sampler_ = VK_NULL_HANDLE;  // nearest (position/depth)
    VkSampler   scatter_sampler_ = VK_NULL_HANDLE;  // linear (upsample)
    VkImageView hdr_color_view_  = VK_NULL_HANDLE;  // borrowed from lighting pass
    VkImageView shadow_view_     = VK_NULL_HANDLE;  // borrowed from shadow map
    VkSampler   shadow_sampler_  = VK_NULL_HANDLE;  // borrowed from shadow map

    // Per-frame uniform buffer (matrices + params), persistently mapped.
    VkBuffer       ubo_         = VK_NULL_HANDLE;
    VkDeviceMemory ubo_memory_  = VK_NULL_HANDLE;
    void*          ubo_mapped_  = nullptr;

    // Half-res scatter target — written by compute, sampled by composite.
    VkImage        scatter_image_  = VK_NULL_HANDLE;
    VkDeviceMemory scatter_memory_ = VK_NULL_HANDLE;
    VkImageView    scatter_view_   = VK_NULL_HANDLE;

    // Compute resources.
    VkDescriptorSetLayout compute_dsl_      = VK_NULL_HANDLE;
    VkDescriptorPool      compute_pool_     = VK_NULL_HANDLE;
    VkDescriptorSet       compute_set_      = VK_NULL_HANDLE;
    VkPipelineLayout      compute_layout_   = VK_NULL_HANDLE;
    VkPipeline            compute_pipeline_ = VK_NULL_HANDLE;

    // Composite resources.
    VkRenderPass          composite_render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer         composite_framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout composite_dsl_         = VK_NULL_HANDLE;
    VkDescriptorPool      composite_pool_        = VK_NULL_HANDLE;
    VkDescriptorSet       composite_set_         = VK_NULL_HANDLE;
    VkPipelineLayout      composite_layout_      = VK_NULL_HANDLE;
    VkPipeline            composite_pipeline_    = VK_NULL_HANDLE;
};

} // namespace gws::renderer::gpu
