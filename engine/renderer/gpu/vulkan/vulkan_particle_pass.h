#pragma once
// ============================================================================
// VulkanParticlePass — draws the CPU particle simulation's billboards (3.9).
//
// The simulation half of 3.9 has been done for a while: emitters are entities,
// seeded per entity, honouring the floating-origin rebase, producing a
// world-space triangle list of BillboardVertex. Nothing drew it, so particles
// existed entirely in memory and the feature was invisible.
//
// WHERE THIS SITS, and why it is not negotiable: AFTER the transparent pass has
// composited, not between Lighting and Transparent. The transparent pass is
// WBOIT and owns its own accumulation targets; a pass inserted between them
// breaks it, and this engine has a recorded instance of exactly that mistake.
// This one loads the HDR colour that Transparent already wrote and adds to it.
//
// Depth TEST on, depth WRITE off. Particles must be occluded by geometry in
// front of them but must not occlude each other -- writing depth would make
// whichever quad drew first punch a hole in the ones behind it, which reads as
// flickering squares rather than as a depth bug.
//
// No sorting. These are additively-blended sprites where draw order does not
// change the result. If a future emitter needs alpha-over blending, sorting
// becomes a real requirement rather than an oversight -- saying so here so the
// absence is not mistaken for completeness.
// ============================================================================

#include "vfx/particle_system.h"      // BillboardVertex

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanShaderRegistry;

class VulkanParticlePass {
public:
    /// @param hdr_color_view  HDR colour view the transparent pass composited into.
    /// @param depth_view      G-Buffer depth, sampled for occlusion, never written.
    static std::unique_ptr<VulkanParticlePass> create(
        VulkanDevice* device,
        VkImageView   hdr_color_view,
        VkFormat      hdr_format,
        VkImageView   depth_view,
        VkFormat      depth_format,
        uint32_t      width,
        uint32_t      height);

    VulkanParticlePass() = default;
    ~VulkanParticlePass();

    VulkanParticlePass(const VulkanParticlePass&)            = delete;
    VulkanParticlePass& operator=(const VulkanParticlePass&) = delete;

    /// Upload `verts` and draw them. A no-op when there is nothing to draw, so
    /// the common case of a scene without emitters costs one branch.
    void execute(VkCommandBuffer cmd,
                 const std::vector<schizo::vfx::BillboardVertex>& verts,
                 const glm::mat4& view_proj);

    /// Indices drawn last frame. build_billboards emits FOUR corner vertices
    /// per particle with no indices, so this pass supplies the two triangles.
    uint32_t last_index_count() const { return last_index_count_; }

    /// Vertices drawn last frame. Exposed because "the pass ran" and "the pass
    /// drew something" are different claims, and only the second one means the
    /// feature works -- a render path can pass validation and draw nothing.
    uint32_t last_vertex_count() const { return last_vertex_count_; }
    uint32_t draw_calls()        const { return draw_calls_; }

private:
    bool init(VulkanDevice* device, VkImageView hdr_color_view, VkFormat hdr_format,
              VkImageView depth_view, VkFormat depth_format,
              uint32_t width, uint32_t height);
    bool create_render_pass(VkFormat hdr_format, VkFormat depth_format);
    bool create_framebuffer(VkImageView hdr_color_view, VkImageView depth_view);
    bool create_pipeline();
    bool ensure_vertex_capacity(size_t bytes);
    bool ensure_index_capacity(uint32_t quads);

    VulkanDevice* device_ = nullptr;
    VkDevice      vk_     = VK_NULL_HANDLE;

    VkRenderPass     render_pass_     = VK_NULL_HANDLE;
    VkFramebuffer    framebuffer_     = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_        = VK_NULL_HANDLE;

    VkBuffer       ibo_        = VK_NULL_HANDLE;
    VkDeviceMemory ibo_memory_ = VK_NULL_HANDLE;
    void*          ibo_mapped_ = nullptr;
    uint32_t       ibo_quads_  = 0;

    VkBuffer       vbo_        = VK_NULL_HANDLE;
    VkDeviceMemory vbo_memory_ = VK_NULL_HANDLE;
    void*          vbo_mapped_ = nullptr;
    size_t         vbo_bytes_  = 0;

    std::unique_ptr<VulkanShaderRegistry> shader_registry_;

    uint32_t width_  = 0;
    uint32_t height_ = 0;
    uint32_t last_vertex_count_ = 0;
    uint32_t last_index_count_  = 0;
    uint32_t draw_calls_        = 0;
};

}  // namespace gws::renderer::gpu
