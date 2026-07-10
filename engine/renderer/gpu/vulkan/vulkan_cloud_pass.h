/**
 * @file vulkan_cloud_pass.h
 * @brief Volumetric clouds — full pipeline.
 *
 * Stages (all owned by this class, run between Lighting/SSR and Transparent):
 *   0. Noise bake (once, lazily on first execute): two tileable 3D noise
 *      volumes (Perlin-Worley shape 128^3 + Worley detail 32^3) via compute.
 *   1. Raymarch (compute, half-res): march the view ray through a curved-earth
 *      cloud slab, density from the noise volumes, light-march toward the sun
 *      (Beer + powder + HG dual-lobe + multi-scatter octaves). Writes
 *      (scattering, transmittance) only on sky pixels.
 *   2. Temporal resolve (compute): reproject + neighbourhood-clamp + blend with
 *      history (toggle; blend 1.0 = off). Result copied into the history image.
 *   3. Composite (graphics): out = scattering + hdr*transmittance.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanGBuffer;

struct CloudConfig {
    float coverage       = 0.52f;   // 0..1 fraction of sky covered
    float density        = 1.50f;   // overall density multiplier
    float cloud_bottom   = 600.0f;  // slab base altitude (world units)
    float cloud_top      = 1200.0f; // slab top altitude
    float earth_radius   = 50000.0f; // horizon curvature radius (higher = flatter, less domed)
    float extinction     = 0.08f;   // light absorption per unit density
    int   march_steps    = 64;      // primary ray samples
    int   light_steps    = 6;       // light-march samples toward sun
    float shape_scale    = 0.0012f; // base-shape noise frequency (lower = bigger clouds)
    float detail_scale   = 0.020f;  // edge-erosion noise frequency
    float detail_strength = 0.25f;  // erosion amount (lower = less fragmented)
    float brightness     = 2.0f;    // overall cloud lighting gain
    float powder         = 1.00f;   // dark-edge powder term
    float hg_forward     = 0.80f;   // HG forward lobe (silver lining)
    float hg_back        = -0.20f;  // HG back lobe
    float ambient_scale  = 0.55f;   // sky-ambient contribution to clouds
    float max_march      = 3000.0f; // clamp on slab segment length
    float wind_speed     = 8.0f;    // world units / sec
    float reproject_far  = 4000.0f; // temporal reprojection far point
    bool  temporal       = true;    // temporal accumulation on/off
    float temporal_blend = 0.10f;   // current-frame weight when temporal on
    float resolution_scale = 0.5f;  // cloud-buffer res vs screen (0.25/0.5/1.0)
    float shadow_extent  = 3000.0f; // half-size (world units) of the cloud shadow map
    bool  cast_shadows   = true;    // clouds darken ground + feed god rays
};

class VulkanCloudPass {
public:
    static std::unique_ptr<VulkanCloudPass> create(
        VulkanDevice* device, VulkanGBuffer* g_buffer,
        VkImageView hdr_color_view, VkFormat hdr_format,
        uint32_t width, uint32_t height);

    VulkanCloudPass() = default;
    ~VulkanCloudPass();
    VulkanCloudPass(const VulkanCloudPass&) = delete;
    VulkanCloudPass& operator=(const VulkanCloudPass&) = delete;

    /// Produce the cloud shadow map. Call EARLY (before the deferred lighting
    /// pass + the light-shaft pass that consume it), since it only depends on
    /// the cloud volume, not the lit scene.
    void compute_shadow_map(VkCommandBuffer cmd,
                            const glm::vec3& camera_position);

    void execute(VkCommandBuffer cmd,
                 const glm::mat4& view,
                 const glm::mat4& proj,
                 const glm::vec3& camera_position);

    /// Cloud shadow map resources for consumers (lighting + shaft passes).
    VkImageView get_shadow_view() const { return cloud_shadow_.view; }
    VkSampler   get_shadow_sampler() const { return cloud_sampler_; }
    /// (center.x, center.z, half_extent, enabled?1:0) — project a world point:
    /// uv = (P.xz - center) / (2*half_extent) + 0.5.
    glm::vec4 get_shadow_params() const {
        return glm::vec4(shadow_center_.x, shadow_center_.y, config_.shadow_extent,
                         (enabled_ && config_.cast_shadows) ? 1.0f : 0.0f);
    }
    /// Lat-long cloud map for reflections (SSR samples it on a sky-miss).
    VkImageView get_cloud_sky_view() const { return cloud_sky_.view; }
    VkSampler   get_cloud_sky_sampler() const { return noise_sampler_; } // REPEAT for azimuth wrap
    bool clouds_visible() const { return enabled_; }

    void set_sun(const glm::vec3& direction, const glm::vec3& color) {
        sun_direction_ = direction; sun_color_ = color;
    }
    void set_ambient(const glm::vec3& color) { ambient_ = color; }

    CloudConfig& mutable_config() { return config_; }
    const CloudConfig& config() const { return config_; }
    void set_enabled(bool e) { enabled_ = e; }
    bool is_enabled() const { return enabled_; }

    /// Recreate the half-res cloud buffers at config_.resolution_scale and
    /// rewrite the descriptors that reference them. Waits for device idle;
    /// call when the resolution scale changes.
    bool rebuild_targets();

private:
    bool initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                    VkImageView hdr_color_view, VkFormat hdr_format,
                    uint32_t width, uint32_t height);
    bool create_noise_volumes();
    bool create_cloud_targets();
    bool create_uniform_buffers();
    bool create_samplers();
    bool create_noise_pipeline();
    bool create_shadow_target();
    bool create_shadow_pipeline();
    bool create_cloud_sky();
    bool create_raymarch_pipeline();
    bool create_resolve_pipeline();
    bool create_composite_pipeline(VkImageView hdr_color_view, VkFormat hdr_format);
    void generate_noise(VkCommandBuffer cmd);
    void destroy();

    struct Volume { VkImage image = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; uint32_t res = 0; };
    struct Target { VkImage image = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; };

    VulkanDevice*  device_   = nullptr;
    VulkanGBuffer* g_buffer_ = nullptr;
    uint32_t width_ = 0, height_ = 0;   // full-res HDR
    uint32_t cw_ = 0, ch_ = 0;          // half-res cloud buffers
    bool enabled_ = true;
    bool noise_generated_ = false;
    bool history_initialized_ = false;
    bool shadow_initialized_ = false;
    uint32_t frame_ = 0;
    float time_ = 0.0f;
    glm::mat4 prev_view_proj_ = glm::mat4(1.0f);
    glm::vec2 shadow_center_ = glm::vec2(0.0f); // (cam.x, cam.z) of the shadow map

    CloudConfig config_{};
    glm::vec3 sun_direction_ = glm::vec3(0.3f, -1.0f, 0.2f);
    glm::vec3 sun_color_     = glm::vec3(1.0f, 0.95f, 0.85f);
    glm::vec3 ambient_       = glm::vec3(0.5f, 0.6f, 0.8f);

    Volume shape_{}, detail_{};
    Target current_{}, history_{}, resolved_{};
    Target cloud_shadow_{}; // top-down sun-transmittance map (R16F)
    static constexpr uint32_t kShadowRes = 512;
    Target cloud_sky_{};    // lat-long cloud map for reflections (RGBA16F)
    static constexpr uint32_t kSkyW = 256;
    static constexpr uint32_t kSkyH = 128;

    VkSampler noise_sampler_ = VK_NULL_HANDLE;  // 3D linear repeat
    VkSampler cloud_sampler_ = VK_NULL_HANDLE;  // 2D linear clamp
    VkSampler depth_sampler_ = VK_NULL_HANDLE;  // 2D nearest clamp

    VkBuffer raymarch_ubo_ = VK_NULL_HANDLE; VkDeviceMemory raymarch_ubo_mem_ = VK_NULL_HANDLE; void* raymarch_ubo_mapped_ = nullptr;
    VkBuffer resolve_ubo_  = VK_NULL_HANDLE; VkDeviceMemory resolve_ubo_mem_  = VK_NULL_HANDLE; void* resolve_ubo_mapped_  = nullptr;
    VkBuffer shadow_ubo_   = VK_NULL_HANDLE; VkDeviceMemory shadow_ubo_mem_   = VK_NULL_HANDLE; void* shadow_ubo_mapped_   = nullptr;

    VkDescriptorSetLayout shadow_dsl_ = VK_NULL_HANDLE;
    VkDescriptorPool      shadow_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       shadow_set_ = VK_NULL_HANDLE;
    VkPipelineLayout      shadow_layout_ = VK_NULL_HANDLE;
    VkPipeline            shadow_pipeline_ = VK_NULL_HANDLE;

    // Cloud-sky reflection map — reuses shadow_dsl_ + shadow_layout_.
    VkDescriptorPool      cloud_sky_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       cloud_sky_set_ = VK_NULL_HANDLE;
    VkPipeline            cloud_sky_pipeline_ = VK_NULL_HANDLE;

    // Noise bake pipeline (one layout; two descriptor sets — shape + detail).
    VkDescriptorSetLayout noise_dsl_ = VK_NULL_HANDLE;
    VkDescriptorPool      noise_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       noise_set_shape_ = VK_NULL_HANDLE;
    VkDescriptorSet       noise_set_detail_ = VK_NULL_HANDLE;
    VkPipelineLayout      noise_layout_ = VK_NULL_HANDLE;
    VkPipeline            noise_pipeline_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout raymarch_dsl_ = VK_NULL_HANDLE;
    VkDescriptorPool      raymarch_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       raymarch_set_ = VK_NULL_HANDLE;
    VkPipelineLayout      raymarch_layout_ = VK_NULL_HANDLE;
    VkPipeline            raymarch_pipeline_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout resolve_dsl_ = VK_NULL_HANDLE;
    VkDescriptorPool      resolve_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       resolve_set_ = VK_NULL_HANDLE;
    VkPipelineLayout      resolve_layout_ = VK_NULL_HANDLE;
    VkPipeline            resolve_pipeline_ = VK_NULL_HANDLE;

    VkRenderPass          composite_render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer         composite_framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout composite_dsl_ = VK_NULL_HANDLE;
    VkDescriptorPool      composite_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet       composite_set_ = VK_NULL_HANDLE;
    VkPipelineLayout      composite_layout_ = VK_NULL_HANDLE;
    VkPipeline            composite_pipeline_ = VK_NULL_HANDLE;
};

} // namespace gws::renderer::gpu
