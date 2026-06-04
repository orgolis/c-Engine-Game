/**
 * @file vulkan_transparent_pass.h
 * @brief Forward-shaded transparent pass with Weighted-Blended Order-
 *        Independent Transparency (McGuire-Bavoil 2013).
 *
 * Two internal sub-passes:
 *
 * 1. **Accumulation** — geometry rasterised into two extra render targets
 *    (accum RGBA16F + revealage R16F) using additive / multiplicative blend
 *    states. Reads the G-Buffer depth as a read-only attachment so opaque
 *    geometry still occludes transparent fragments. Order of submission
 *    does NOT matter — the weight function (depth-driven) handles
 *    compositing.
 *
 * 2. **Composite** — full-screen pass that samples accum + revealage and
 *    blends the weighted average over the lit HDR target.
 *
 * Caller (typically VulkanRenderGraph) is expected to invoke `execute`
 * inside a frame's command buffer. The caller no longer needs to sort —
 * the pass is order-independent — but pre-sorting back-to-front is still
 * harmless and slightly improves quality for non-intersecting content.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanShaderRegistry;
struct DrawItem;
struct CameraData;

class VulkanTransparentPass {
public:
    /// @param device           Vulkan device.
    /// @param hdr_color_view   The HDR colour image view from VulkanLightingPass.
    /// @param hdr_format       Format of that HDR image (R16G16B16A16_SFLOAT today).
    /// @param depth_view       The G-Buffer's depth image view.
    /// @param depth_format     Depth format (D32_SFLOAT today).
    /// @param width / height   Render extent.
    /// @param material_layout  Same descriptor-set layout used by the G-Buffer
    ///                         scene pipeline so existing Material instances
    ///                         can be re-bound here at set=1.
    static std::unique_ptr<VulkanTransparentPass> create(
        VulkanDevice* device,
        VkImageView   hdr_color_view,
        VkFormat      hdr_format,
        VkImageView   depth_view,
        VkFormat      depth_format,
        uint32_t      width,
        uint32_t      height,
        VkDescriptorSetLayout material_layout);

    VulkanTransparentPass() = default;
    ~VulkanTransparentPass();

    VulkanTransparentPass(const VulkanTransparentPass&) = delete;
    VulkanTransparentPass& operator=(const VulkanTransparentPass&) = delete;

    /// Begin the render pass, bind the pipeline, draw every item in `items`
    /// in iteration order (the caller is responsible for sorting), end the
    /// render pass. `items` is expected to contain *only* transparent
    /// (alpha-blend) draws.
    void execute(VkCommandBuffer cmd,
                 const std::vector<DrawItem>& items,
                 const CameraData& camera);

    /// Set the per-frame ambient term. The directional/point lights are
    /// pulled from the shared lighting SSBO bound via set_lighting_resources.
    void set_ambient(const glm::vec3& color, float intensity);

    /// Wire up the shared resources from VulkanLightingPass so the forward
    /// shader can compute PBR + receive shadows from the same light list
    /// the deferred shader uses. Call once after both passes are created.
    /// Passing VK_NULL_HANDLE for any view falls back to a dummy texture
    /// (the lighting pass owns the dummies).
    void set_lighting_resources(VkBuffer    light_buffer,
                                uint32_t    max_lights,
                                VkImageView directional_shadow_view,
                                VkSampler   directional_shadow_sampler,
                                VkImageView point_shadow_view,
                                VkSampler   point_shadow_sampler);

    /// Per-frame: how many entries in the light SSBO are valid. The shader
    /// iterates 0..light_count - 1 and skips the rest.
    void set_light_count(uint32_t count) { light_count_ = count; }

    /// Recreate the framebuffer for a new HDR view / depth view at new
    /// extents. The render pass is reusable across resizes; only the
    /// framebuffer and stored dimensions change.
    void resize(VkImageView hdr_color_view,
                VkImageView depth_view,
                uint32_t width, uint32_t height);

private:
    bool initialize(VulkanDevice* device,
                    VkImageView hdr_color_view, VkFormat hdr_format,
                    VkImageView depth_view,     VkFormat depth_format,
                    uint32_t width, uint32_t height,
                    VkDescriptorSetLayout material_layout);

    bool create_light_env_resources();
    bool create_wboit_targets();
    bool create_composite_resources(VkImageView hdr_color_view, VkFormat hdr_format);
    void upload_light_env(const glm::vec3& camera_position);
    void destroy_wboit_targets();
    void destroy();

    VulkanDevice* device_ = nullptr;
    std::unique_ptr<VulkanShaderRegistry> shader_registry_;

    // ---- Accumulation sub-pass (geometry) ----------------------------
    VkRenderPass     accum_render_pass_      = VK_NULL_HANDLE;
    VkFramebuffer    accum_framebuffer_      = VK_NULL_HANDLE;
    VkPipeline       accum_pipeline_         = VK_NULL_HANDLE;
    VkPipelineLayout accum_pipeline_layout_  = VK_NULL_HANDLE;

    // Two extra render targets owned by the pass.
    VkImage        accum_image_     = VK_NULL_HANDLE;
    VkImageView    accum_view_      = VK_NULL_HANDLE;
    VkDeviceMemory accum_memory_    = VK_NULL_HANDLE;
    VkImage        reveal_image_    = VK_NULL_HANDLE;
    VkImageView    reveal_view_     = VK_NULL_HANDLE;
    VkDeviceMemory reveal_memory_   = VK_NULL_HANDLE;

    // ---- Composite sub-pass (full-screen over HDR) -------------------
    VkRenderPass          composite_render_pass_     = VK_NULL_HANDLE;
    VkFramebuffer         composite_framebuffer_     = VK_NULL_HANDLE;
    VkPipeline            composite_pipeline_        = VK_NULL_HANDLE;
    VkPipelineLayout      composite_pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout composite_set_layout_      = VK_NULL_HANDLE;
    VkDescriptorPool      composite_pool_            = VK_NULL_HANDLE;
    VkDescriptorSet       composite_set_             = VK_NULL_HANDLE;
    VkSampler             composite_sampler_         = VK_NULL_HANDLE;

    // ---- Per-frame lighting env UBO (set=0 of the accum pipeline) ----
    VkBuffer              light_env_buffer_  = VK_NULL_HANDLE;
    VkDeviceMemory        light_env_memory_  = VK_NULL_HANDLE;
    void*                 light_env_mapped_  = nullptr;
    VkDescriptorSetLayout light_env_layout_  = VK_NULL_HANDLE;
    VkDescriptorPool      light_env_pool_    = VK_NULL_HANDLE;
    VkDescriptorSet       light_env_set_     = VK_NULL_HANDLE;

    glm::vec3 ambient_color_     = glm::vec3(1.0f);
    float     ambient_intensity_ = 0.3f;
    uint32_t  light_count_       = 0;

    VkFormat hdr_format_   = VK_FORMAT_UNDEFINED;
    VkFormat depth_format_ = VK_FORMAT_UNDEFINED;
    uint32_t width_  = 0;
    uint32_t height_ = 0;
};

} // namespace gws::renderer::gpu
