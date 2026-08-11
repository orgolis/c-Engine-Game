/**
 * @file vulkan_water_pass.h
 * @brief Animated water surfaces (terrain expansion).
 *
 * Renders each registered water rect as a procedural grid (no vertex buffer)
 * displaced by directional waves, shaded with fresnel environment-cubemap
 * reflection, sun glint, and depth-based shallow->deep color + shoreline
 * softness. Alpha-blends into the lighting pass's HDR target via its own
 * single-attachment LOAD render pass (the volumetrics/clouds pattern) and
 * depth-tests MANUALLY against the G-buffer world position — no depth
 * attachment, no barrier surgery.
 *
 * Records between SSR and the cloud/shaft composites (water is a surface;
 * atmospherics layer on top). Per-frame data goes through a persistently
 * mapped UBO; per-surface data through 64-byte push constants.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <cstdint>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanGBuffer;

class VulkanWaterPass {
public:
    static std::unique_ptr<VulkanWaterPass> create(VulkanDevice* device,
                                                   VulkanGBuffer* g_buffer,
                                                   VkImageView hdr_color_view,
                                                   VkFormat    hdr_format,
                                                   VkImageView env_view,
                                                   VkSampler   env_sampler,
                                                   uint32_t    width,
                                                   uint32_t    height);

    VulkanWaterPass() = default;
    ~VulkanWaterPass();
    VulkanWaterPass(const VulkanWaterPass&) = delete;
    VulkanWaterPass& operator=(const VulkanWaterPass&) = delete;

    /// Reset the per-frame surface list (call once per frame before add_water).
    void begin_frame() { draws_.clear(); }

    /// Register one water surface for this frame.
    void add_water(const glm::vec3& center, const glm::vec2& size,
                   const glm::vec3& deep_color, const glm::vec3& shallow_color,
                   float wave_height, float wave_speed, float wave_scale,
                   float clarity, float reflectivity) {
        WaterDraw d;
        d.center_sx    = glm::vec4(center, size.x);
        d.p0           = glm::vec4(size.y, wave_height, wave_speed, wave_scale);
        d.deep_clarity = glm::vec4(deep_color, clarity);
        d.shallow_refl = glm::vec4(shallow_color, reflectivity);
        draws_.push_back(d);
    }

    /// Sun direction (light -> scene) + colour, for the specular glint.
    void set_sun(const glm::vec3& direction, const glm::vec3& color) {
        sun_direction_ = direction; sun_color_ = color;
    }

    /// Record all registered surfaces into the HDR target. No-op when the
    /// frame has no water or the pass is disabled.
    void execute(VkCommandBuffer cmd,
                 const glm::mat4& view,
                 const glm::mat4& proj,
                 const glm::vec3& camera_position,
                 float time_seconds);

    void set_enabled(bool e) { enabled_ = e; }
    bool is_enabled() const { return enabled_; }
    size_t surface_count() const { return draws_.size(); }

private:
    // Must match the shader's push-constant block (64 bytes).
    struct WaterDraw {
        glm::vec4 center_sx;     // center.xyz, size.x
        glm::vec4 p0;            // size.y, wave_height, wave_speed, wave_scale
        glm::vec4 deep_clarity;  // deep rgb, clarity
        glm::vec4 shallow_refl;  // shallow rgb, reflectivity
    };
    // Must match the shader's FrameUBO (std140, 192 bytes).
    struct FrameUBO {
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 cam_time;
        glm::vec4 sun_dir;
        glm::vec4 sun_color;
        glm::vec4 resolution;
    };

    bool initialize(VulkanDevice* device, VulkanGBuffer* g_buffer,
                    VkImageView hdr_color_view, VkFormat hdr_format,
                    VkImageView env_view, VkSampler env_sampler,
                    uint32_t width, uint32_t height);
    void destroy();

    VulkanDevice*  device_   = nullptr;
    VulkanGBuffer* g_buffer_ = nullptr;
    uint32_t width_  = 0;
    uint32_t height_ = 0;
    bool enabled_    = true;

    glm::vec3 sun_direction_ = glm::vec3(0.3f, -1.0f, 0.2f);
    glm::vec3 sun_color_     = glm::vec3(1.0f, 0.95f, 0.85f);
    std::vector<WaterDraw> draws_;

    VkSampler gbuffer_sampler_ = VK_NULL_HANDLE;   // nearest, for position tex

    VkBuffer       ubo_        = VK_NULL_HANDLE;   // persistently mapped
    VkDeviceMemory ubo_memory_ = VK_NULL_HANDLE;
    void*          ubo_mapped_ = nullptr;

    VkRenderPass          render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer         framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl_         = VK_NULL_HANDLE;
    VkDescriptorPool      pool_        = VK_NULL_HANDLE;
    VkDescriptorSet       set_         = VK_NULL_HANDLE;
    VkPipelineLayout      layout_      = VK_NULL_HANDLE;
    VkPipeline            pipeline_    = VK_NULL_HANDLE;
};

} // namespace gws::renderer::gpu
