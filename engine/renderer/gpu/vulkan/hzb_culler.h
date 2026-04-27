#pragma once

#include <vulkan/vulkan.h>
#include "math/math.hpp"
#include <vector>
#include <memory>

namespace gws {

// Forward declarations
class Device;
class VulkanImage;
struct DrawCall;

// HZBCuller: GPU-based occlusion culling using hierarchical Z-buffer
// Complements CPU frustum culling for additional draw-call reduction
// Architecture:
//   1. Build HZB mipmap chain from depth texture (compute shader)
//   2. For each mesh, test AABB visibility against HZB
//   3. Generate indirect draw buffer with visible items only
//   4. Submit with vkCmdDrawIndirectCount for GPU-driven rendering
class HZBCuller {
public:
    struct Config {
        uint32_t max_draw_calls = 4096;  // Max concurrent drawcalls
        uint32_t mip_levels = 12;        // HZB mipmap levels (supports up to 4096x4096)
        bool enabled = false;
    };

    HZBCuller(Device* device, const Config& cfg);
    ~HZBCuller();

    // Initialize HZB resources (called once)
    void initialize();

    // Build HZB from depth texture (called per frame)
    // Returns VkImage of HZB chain (can be sampled for culling)
    VkImage build_hzb(VkCommandBuffer cmd, VkImage depth_texture, VkExtent2D depth_size);

    // Perform occlusion culling: filter draw calls and generate indirect buffer
    // Returns number of visible items
    uint32_t cull_draw_calls(
        VkCommandBuffer cmd,
        const std::vector<DrawCall>& draw_calls,
        const math::mat4& view_proj,
        VkBuffer& out_indirect_buffer
    );

    // Get HZB texture for debugging/visualization
    VkImage get_hzb_texture() const { return hzb_texture_.get(); }

    // Enable/disable HZB culling
    void set_enabled(bool enabled) { config_.enabled = enabled; }
    bool is_enabled() const { return config_.enabled; }

private:
    Device* device_;
    Config config_;

    // HZB mipmap chain
    std::unique_ptr<VulkanImage> hzb_texture_;
    VkImageView hzb_view_ = VK_NULL_HANDLE;

    // Compute pipeline for HZB mipmap building
    VkPipeline hzb_build_pipeline_ = VK_NULL_HANDLE;
    VkPipeline hzb_test_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout hzb_build_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout hzb_test_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout hzb_descriptor_layout_ = VK_NULL_HANDLE;

    // Indirect draw buffer (GPU-generated)
    std::unique_ptr<class VulkanBuffer> indirect_buffer_;
    std::unique_ptr<class VulkanBuffer> visible_count_buffer_;  // GPU counter
    std::unique_ptr<class VulkanBuffer> aabb_buffer_;           // AABB bounds for culling

    // Helper functions
    void create_hzb_texture(VkExtent2D initial_size);
    void create_compute_pipelines();
    void create_descriptor_layout();
    void build_hzb_mipmap_level(VkCommandBuffer cmd, uint32_t src_level, uint32_t dst_level,
                                VkImage depth_texture, VkExtent2D src_size, VkExtent2D dst_size);
};

}  // namespace gws
