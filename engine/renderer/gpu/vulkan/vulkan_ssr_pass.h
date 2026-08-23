/**
 * @file vulkan_ssr_pass.h
 * @brief Screen-space reflections — two stages.
 *
 *   1. Compute pass: linear ray-march in world space, reads G-Buffer
 *      (position/normal/depth) + albedo (metallic) + the lit HDR target,
 *      writes RGBA reflection where A is a blend mask.
 *   2. Composite pass: fullscreen alpha-blend draws the reflection RGBA
 *      onto the HDR target using SRC_ALPHA / ONE_MINUS_SRC_ALPHA.
 *
 * Runs between Lighting and Transparent — transparent fragments composite
 * over reflections, which is the expected order.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanGBuffer;
class VulkanLightingPass;

struct SsrConfig {
    // Total world-space ray length. 50 covers most editor-scale scenes;
    // increase for huge open worlds.
    float max_distance  = 50.0f;
    // World-space thickness (units) applied after binary refinement. A
    // hit is rejected when the ray has punched more than this far past
    // the scene surface (i.e. through a thin object out to the background).
    // Now in WORLD units — the old NDC-z value made the effective
    // tolerance distance-dependent, so only objects farther than the
    // camera-to-surface distance got reflected.
    float thickness     = 0.5f;
    // Coarse marching steps. With 6 refinement bisections per hit, 32
    // coarse steps gives equivalent effective resolution to ~2000
    // unrefined steps.
    int   num_steps     = 32;
    float fresnel_power = 3.0f;
    // Tint a metal's reflection by its own base colour -- correct PBR
    // (gold reflects gold). Off restores the old neutral-white metals, so
    // the two can be compared side by side. A push constant, so flipping it
    // costs nothing: unlike the RT/screen-space switch it rebuilds no
    // pipeline and never waits for device idle.
    bool  tint_by_albedo = true;
};

class VulkanSsrPass {
public:
    static std::unique_ptr<VulkanSsrPass> create(VulkanDevice* device,
                                                  VulkanGBuffer* g_buffer,
                                                  VkImageView hdr_color_view,
                                                  VkFormat    hdr_format,
                                                  VkImageView env_cubemap_view,
                                                  VkSampler   env_cubemap_sampler,
                                                  uint32_t    width,
                                                  uint32_t    height,
                                                  bool        use_rt = false);

    VulkanSsrPass() = default;
    ~VulkanSsrPass();
    VulkanSsrPass(const VulkanSsrPass&) = delete;
    VulkanSsrPass& operator=(const VulkanSsrPass&) = delete;

    /// Run the compute pass + composite pass back-to-back.
    void execute(VkCommandBuffer cmd,
                 const glm::mat4& view,
                 const glm::mat4& proj,
                 const glm::vec3& camera_position);

    /// Bind the scene TLAS for RT mode (no-op in screen-space mode).
    /// Updates the compute descriptor set's binding 7. Cheap when the
    /// handle is unchanged.
    void set_tlas(VkAccelerationStructureKHR tlas);

    /// Bind the per-instance shading-data SSBO (binding 8) for RT hit
    /// re-shading. From VulkanRtScene::get_instance_data_buffer().
    void set_instance_data_buffer(VkBuffer buffer);

    /// How many entries of that SSBO are valid this frame
    /// (VulkanRtScene::get_instance_count()). The shader refuses to read an
    /// instance at or beyond this index: a ray hit can carry an
    /// instanceCustomIndex from an older TLAS than the buffer's contents, and
    /// reading that stale slot yields a stale device address, which faults the
    /// GPU. Must be set every frame, before execute().
    void set_instance_count(uint32_t count) { instance_count_ = count; }

    /// Primary directional light used to re-shade RT reflection hits.
    /// `color` already folds in intensity. `direction` points from the
    /// light toward the scene (same convention as the lighting pass).
    void set_sun(const glm::vec3& direction, const glm::vec3& color) {
        sun_direction_ = direction; sun_color_ = color;
    }
    /// Ambient term added to re-shaded hits (color * intensity).
    void set_ambient(const glm::vec3& color, float intensity) {
        ambient_color_ = color; ambient_intensity_ = intensity;
    }

    /// Lat-long cloud map so clouds appear in reflections (RT mode). Bind once.
    void set_cloud_sky(VkImageView view, VkSampler sampler);
    /// Toggle reflected clouds on/off (each frame).
    void set_cloud_sky_enabled(bool e) { cloud_sky_enabled_ = e; }

    bool uses_rt() const { return use_rt_; }

    /// Tint a metal's reflection by its own base colour. A push constant,
    /// so unlike set_use_rt() this rebuilds nothing and never waits for idle.
    bool tint_by_albedo() const { return config_.tint_by_albedo; }
    void set_tint_by_albedo(bool on) { config_.tint_by_albedo = on; }
    /// Whether this GPU could do ray-traced reflections at all. False means
    /// set_use_rt(true) is a no-op and the UI should say why.
    bool rt_available() const { return rt_available_; }

    /// Switch between the ray-query and screen-space reflection shaders at
    /// runtime. Waits for device idle and rebuilds the compute pipeline +
    /// descriptor set, mirroring VulkanSsaoPass::set_technique().
    ///
    /// This exists because the choice used to be made only from an
    /// environment variable read once at startup -- unreachable for anyone
    /// launching the editor from the Hub, which is everyone who is not
    /// building from source. A capability nobody can switch on is a
    /// capability nobody can test.
    void set_use_rt(bool on);

    SsrConfig& mutable_config() { return config_; }
    const SsrConfig& config() const { return config_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

private:
    bool initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                    VkImageView hdr_color_view, VkFormat hdr_format,
                    VkImageView env_cubemap_view, VkSampler env_cubemap_sampler,
                    uint32_t width, uint32_t height);
    bool create_reflection_target();
    bool create_compute_pipeline();
    bool create_composite_pipeline(VkImageView hdr_color_view, VkFormat hdr_format);
    void destroy();

    VulkanDevice*  device_   = nullptr;
    VulkanGBuffer* g_buffer_ = nullptr;
    uint32_t       width_    = 0;
    uint32_t       height_   = 0;
    bool           enabled_  = true;
    bool           use_rt_   = false;
    bool           rt_available_ = false;
    VkAccelerationStructureKHR tlas_ = VK_NULL_HANDLE;
    VkBuffer       instance_data_buffer_ = VK_NULL_HANDLE;
    uint32_t       instance_count_       = 0;
    glm::vec3      sun_direction_  = glm::vec3(0.3f, -1.0f, 0.2f);
    glm::vec3      sun_color_      = glm::vec3(1.0f, 0.95f, 0.85f);
    glm::vec3      ambient_color_  = glm::vec3(0.3f);
    float          ambient_intensity_ = 1.0f;
    VkImageView    cloud_sky_view_    = VK_NULL_HANDLE; // borrowed from cloud pass
    VkSampler      cloud_sky_sampler_ = VK_NULL_HANDLE;
    bool           cloud_sky_enabled_ = false;
    SsrConfig      config_{};

    VkSampler   gbuffer_sampler_   = VK_NULL_HANDLE;
    VkSampler   hdr_sampler_       = VK_NULL_HANDLE;
    VkImageView hdr_color_view_    = VK_NULL_HANDLE; // borrowed from lighting pass
    VkImageView env_cubemap_view_  = VK_NULL_HANDLE; // borrowed from env map
    VkSampler   env_cubemap_sampler_ = VK_NULL_HANDLE;

    // Reflection target — written by compute, sampled by composite.
    VkImage        reflection_image_  = VK_NULL_HANDLE;
    VkDeviceMemory reflection_memory_ = VK_NULL_HANDLE;
    VkImageView    reflection_view_   = VK_NULL_HANDLE;

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
