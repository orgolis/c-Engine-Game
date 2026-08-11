/**
 * @file vulkan_ddgi_pass.h
 * @brief DDGI — dynamic diffuse GI via irradiance probes (Master Plan §3.2).
 *
 * Two stages, self-contained (no lighting-pass surgery — same pattern as the
 * froxel-fog / SSR passes):
 *
 *   1. Trace (compute): one workgroup per probe, 64 rays/frame against the
 *      scene TLAS (ray query). Hits are re-shaded from the RT instance SSBO
 *      (albedo x (ambient + sun.N.L x shadow-ray) + emissive); misses sample
 *      the env cubemap. The workgroup rebuilds the probe's octahedral
 *      irradiance tile (8x8 + 1px border) and Chebyshev depth tile
 *      (16x16 + border) with temporal hysteresis.
 *   2. Composite (fullscreen, additive ONE/ONE onto the lit HDR target):
 *      per pixel, 8-probe trilinear gather weighted by smooth-backface and
 *      Chebyshev visibility → adds albedo x irradiance x intensity.
 *
 * Requires hardware ray tracing (creation fails cleanly without it). Probe
 * counts are fixed at creation (they size the atlases); origin/spacing/
 * intensity are live-tunable. Runs: trace after the TLAS update (any point
 * before Lighting), composite right after the Lighting stage.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanGBuffer;

struct DdgiConfig {
    glm::vec3  origin       = glm::vec3(-15.0f, -0.5f, -15.0f);
    glm::vec3  spacing      = glm::vec3(2.5f, 2.0f, 2.5f);
    glm::ivec3 counts       = glm::ivec3(12, 6, 12);   // fixed at creation
    float      intensity    = 1.0f;    // composite scale on the GI term
    float      hysteresis   = 0.97f;   // temporal blend (higher = smoother)
    float      normal_bias  = 0.3f;    // surface offset for probe sampling
    float      max_ray_dist = 80.0f;   // probe ray tmax
    glm::vec3  ambient      = glm::vec3(0.25f);  // flat ambient at ray hits
};

class VulkanDdgiPass {
public:
    /// Returns nullptr when the device lacks hardware ray tracing.
    static std::unique_ptr<VulkanDdgiPass> create(VulkanDevice* device,
                                                  VulkanGBuffer* g_buffer,
                                                  VkImageView hdr_color_view,
                                                  VkFormat    hdr_format,
                                                  VkImageView env_cubemap_view,
                                                  VkSampler   env_cubemap_sampler,
                                                  uint32_t    width,
                                                  uint32_t    height,
                                                  const DdgiConfig& config = {});

    VulkanDdgiPass() = default;
    ~VulkanDdgiPass();
    VulkanDdgiPass(const VulkanDdgiPass&) = delete;
    VulkanDdgiPass& operator=(const VulkanDdgiPass&) = delete;

    /// Record the probe trace + atlas update. Call after the frame's TLAS
    /// build, before the composite. No-op until TLAS + instance buffer are set.
    void execute_trace(VkCommandBuffer cmd);

    /// Record the additive fullscreen composite onto the HDR target. Call
    /// right after the Lighting stage.
    void execute_composite(VkCommandBuffer cmd);

    /// Bind the scene TLAS (rebinds the descriptor only on change).
    void set_tlas(VkAccelerationStructureKHR tlas);
    /// Bind the RT instance shading-data SSBO (VulkanRtScene::get_instance_data_buffer).
    void set_instance_data_buffer(VkBuffer buffer);
    /// Sun for probe-hit shading. `direction` points light → scene.
    void set_sun(const glm::vec3& direction, const glm::vec3& color) {
        sun_direction_ = direction; sun_color_ = color;
    }

    DdgiConfig& mutable_config() { return config_; }
    const DdgiConfig& config() const { return config_; }
    void set_enabled(bool e) { enabled_ = e; }
    bool is_enabled() const { return enabled_; }
    uint32_t probe_count() const {
        return static_cast<uint32_t>(config_.counts.x * config_.counts.y * config_.counts.z);
    }

private:
    bool initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                    VkImageView hdr_color_view, VkFormat hdr_format,
                    VkImageView env_cubemap_view, VkSampler env_cubemap_sampler,
                    uint32_t width, uint32_t height);
    bool create_atlas(VkFormat format, uint32_t w, uint32_t h,
                      VkImage& img, VkDeviceMemory& mem, VkImageView& view);
    void destroy();

    VulkanDevice*  device_   = nullptr;
    VulkanGBuffer* g_buffer_ = nullptr;
    uint32_t width_  = 0;
    uint32_t height_ = 0;
    bool     enabled_ = false;              // opt-in (Post-Processing panel)
    bool     atlases_initialized_ = false;  // UNDEFINED->GENERAL done?
    uint32_t frame_index_ = 0;
    DdgiConfig config_{};

    VkAccelerationStructureKHR tlas_ = VK_NULL_HANDLE;
    VkBuffer  instance_data_buffer_  = VK_NULL_HANDLE;
    glm::vec3 sun_direction_ = glm::vec3(0.3f, -1.0f, 0.2f);
    glm::vec3 sun_color_     = glm::vec3(1.0f, 0.95f, 0.85f);

    VkSampler gbuffer_sampler_ = VK_NULL_HANDLE;  // nearest, clamp
    VkSampler atlas_sampler_   = VK_NULL_HANDLE;  // linear, clamp

    VkBuffer       ubo_        = VK_NULL_HANDLE;
    VkDeviceMemory ubo_memory_ = VK_NULL_HANDLE;
    void*          ubo_mapped_ = nullptr;

    // Probe atlases (2D, kept in GENERAL layout).
    VkImage        irr_image_  = VK_NULL_HANDLE;   // rgba16f
    VkDeviceMemory irr_memory_ = VK_NULL_HANDLE;
    VkImageView    irr_view_   = VK_NULL_HANDLE;
    VkImage        dep_image_  = VK_NULL_HANDLE;   // rg16f
    VkDeviceMemory dep_memory_ = VK_NULL_HANDLE;
    VkImageView    dep_view_   = VK_NULL_HANDLE;

    // Trace (compute).
    VkDescriptorSetLayout trace_dsl_      = VK_NULL_HANDLE;
    VkDescriptorSet       trace_set_      = VK_NULL_HANDLE;
    VkPipelineLayout      trace_layout_   = VK_NULL_HANDLE;
    VkPipeline            trace_pipeline_ = VK_NULL_HANDLE;

    // Composite.
    VkRenderPass          composite_render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer         composite_framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout composite_dsl_         = VK_NULL_HANDLE;
    VkDescriptorSet       composite_set_         = VK_NULL_HANDLE;
    VkPipelineLayout      composite_layout_      = VK_NULL_HANDLE;
    VkPipeline            composite_pipeline_    = VK_NULL_HANDLE;

    VkDescriptorPool pool_ = VK_NULL_HANDLE;
};

} // namespace gws::renderer::gpu
