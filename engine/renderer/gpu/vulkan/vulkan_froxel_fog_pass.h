/**
 * @file vulkan_froxel_fog_pass.h
 * @brief Froxel volumetric fog (Stage 3.3) — three stages.
 *
 *   1. Scatter (compute): a camera-frustum-aligned 160x90x64 froxel grid;
 *      each froxel gets height-fog density x incident light (sun with shadow
 *      map + HG phase, all local point/spot lights from the SAME light buffer
 *      the deferred pass uses, ambient floor).
 *   2. Integrate (compute): per screen column, front-to-back accumulation so
 *      every slice holds the in-scatter + transmittance up to its distance.
 *   3. Composite: fullscreen triangle, one trilinear tap at each pixel's
 *      depth, blended out = inscatter + hdr * transmittance.
 *
 * Runs after the cloud/shaft composites (nearest medium last), before the
 * Transparent stage. Follows the volumetrics pass pattern: borrowed HDR view,
 * own LOAD render pass, persistently mapped UBO.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanGBuffer;

struct FroxelFogConfig {
    // Defaults retuned 2026-07-23: the original density 0.012 + ambient floor
    // ~0.4 washed the whole frame into a flat grey-blue haze. A subtler base
    // (fog should READ as atmosphere, not paint) — crank per-scene as needed.
    float density        = 0.006f;  // base extinction at height_base (per meter)
    float height_base    = 0.0f;    // fog is densest at/below this world Y
    float height_falloff = 0.10f;   // exponential thinning above height_base
    float max_distance   = 150.0f;  // froxel far plane (fog range)
    float anisotropy     = 0.6f;    // HG g for the sun (forward scattering)
    float sun_intensity  = 1.0f;
    float local_intensity = 1.0f;   // point/spot in-scatter scale
    glm::vec3 ambient    = glm::vec3(0.10f, 0.12f, 0.16f);  // dim sky floor
};

class VulkanFroxelFogPass {
public:
    static std::unique_ptr<VulkanFroxelFogPass> create(VulkanDevice* device,
                                                       VulkanGBuffer* g_buffer,
                                                       VkImageView hdr_color_view,
                                                       VkFormat    hdr_format,
                                                       VkImageView shadow_view,
                                                       VkSampler   shadow_sampler,
                                                       VkBuffer    light_buffer,
                                                       uint32_t    width,
                                                       uint32_t    height);

    VulkanFroxelFogPass() = default;
    ~VulkanFroxelFogPass();
    VulkanFroxelFogPass(const VulkanFroxelFogPass&) = delete;
    VulkanFroxelFogPass& operator=(const VulkanFroxelFogPass&) = delete;

    /// Record scatter + integrate + composite.
    void execute(VkCommandBuffer cmd,
                 const glm::mat4& view,
                 const glm::mat4& proj,
                 const glm::vec3& camera_position);

    void set_sun(const glm::vec3& direction, const glm::vec3& color) {
        sun_direction_ = direction; sun_color_ = color;
    }
    void set_shadow_matrix(const glm::mat4& m) { shadow_matrix_ = m; }
    void set_light_count(uint32_t n) { light_count_ = n; }

    FroxelFogConfig& mutable_config() { return config_; }
    const FroxelFogConfig& config() const { return config_; }
    void set_enabled(bool e) { enabled_ = e; }
    bool is_enabled() const { return enabled_; }

private:
    bool initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                    VkImageView hdr_color_view, VkFormat hdr_format,
                    VkImageView shadow_view, VkSampler shadow_sampler,
                    VkBuffer light_buffer, uint32_t width, uint32_t height);
    bool create_volume(VkImage& img, VkDeviceMemory& mem, VkImageView& view);
    void destroy();

    VulkanDevice*  device_   = nullptr;
    VulkanGBuffer* g_buffer_ = nullptr;
    uint32_t width_  = 0;
    uint32_t height_ = 0;
    bool enabled_    = true;
    bool volumes_initialized_ = false;   // UNDEFINED->GENERAL done?
    FroxelFogConfig config_{};

    glm::vec3 sun_direction_ = glm::vec3(0.3f, -1.0f, 0.2f);
    glm::vec3 sun_color_     = glm::vec3(1.0f, 0.95f, 0.85f);
    glm::mat4 shadow_matrix_ = glm::mat4(1.0f);
    uint32_t  light_count_   = 0;

    VkSampler gbuffer_sampler_ = VK_NULL_HANDLE;   // nearest (position tex)
    VkSampler volume_sampler_  = VK_NULL_HANDLE;   // trilinear (integrated tex)

    VkBuffer       ubo_        = VK_NULL_HANDLE;
    VkDeviceMemory ubo_memory_ = VK_NULL_HANDLE;
    void*          ubo_mapped_ = nullptr;

    // 3D volumes (rgba16f, kept in GENERAL layout).
    VkImage        scatter_image_     = VK_NULL_HANDLE;
    VkDeviceMemory scatter_memory_    = VK_NULL_HANDLE;
    VkImageView    scatter_view_      = VK_NULL_HANDLE;
    VkImage        integrated_image_  = VK_NULL_HANDLE;
    VkDeviceMemory integrated_memory_ = VK_NULL_HANDLE;
    VkImageView    integrated_view_   = VK_NULL_HANDLE;

    // Compute: scatter + integrate.
    VkDescriptorSetLayout scatter_dsl_        = VK_NULL_HANDLE;
    VkDescriptorSet       scatter_set_        = VK_NULL_HANDLE;
    VkPipelineLayout      scatter_layout_     = VK_NULL_HANDLE;
    VkPipeline            scatter_pipeline_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout integrate_dsl_      = VK_NULL_HANDLE;
    VkDescriptorSet       integrate_set_      = VK_NULL_HANDLE;
    VkPipelineLayout      integrate_layout_   = VK_NULL_HANDLE;
    VkPipeline            integrate_pipeline_ = VK_NULL_HANDLE;

    // Composite.
    VkRenderPass          composite_render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer         composite_framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout composite_dsl_         = VK_NULL_HANDLE;
    VkDescriptorSet       composite_set_         = VK_NULL_HANDLE;
    VkPipelineLayout      composite_layout_      = VK_NULL_HANDLE;
    VkPipeline            composite_pipeline_    = VK_NULL_HANDLE;

    VkDescriptorPool pool_ = VK_NULL_HANDLE;   // shared by all three sets
};

} // namespace gws::renderer::gpu
