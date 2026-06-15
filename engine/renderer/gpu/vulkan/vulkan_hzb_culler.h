/**
 * @file vulkan_hzb_culler.h
 * @brief Hierarchical Z-Buffer occlusion culler.
 *
 * Each frame:
 *   1. (CPU, frame start) `test_visibility()` projects every draw's world-
 *      space AABB into NDC, samples the CPU-side HZB (from the previous
 *      frame's depth) at the appropriate mip level, and decides visible /
 *      occluded.
 *   2. (GPU, after geometry stage) `build_and_readback()` dispatches the
 *      compute downsample chain to populate HZB[0..N] from the current
 *      frame's depth buffer, then copies one mip level (the "readback mip"
 *      — half-resolution by default for a 32x18 pixel HZB at 1920x1080)
 *      to a host-visible buffer.
 *   3. (CPU, next frame start) `pull_readback()` swaps the readback buffer
 *      contents into the CPU-side HZB array used by `test_visibility`.
 *
 * One frame of latency in steady state — culled objects re-appear one
 * frame after their occluder clears. Acceptable for editor / WYSIWYG use.
 * Unlike the per-draw occlusion-query approach this replaces, HZB doesn't
 * have the "wait forever on an unwritten query slot" hazard: the AABB test
 * happens on the CPU against a fully-populated HZB, independent of whether
 * the draw runs.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace gws::renderer::gpu {

class VulkanDevice;

class VulkanHzbCuller {
public:
    struct Config {
        VkFormat depth_format  = VK_FORMAT_D32_SFLOAT;
        uint32_t depth_width   = 0;
        uint32_t depth_height  = 0;
        uint32_t max_draws     = 4096;
        /// Mip level the readback buffer captures. Smaller mip = lower CPU
        /// cost, coarser test. Defaults to mip 4 (~120x67 px at 1920x1080).
        uint32_t readback_mip  = 4;
    };

    static std::unique_ptr<VulkanHzbCuller> create(VulkanDevice* device, const Config& cfg);

    VulkanHzbCuller() = default;
    ~VulkanHzbCuller();
    VulkanHzbCuller(const VulkanHzbCuller&) = delete;
    VulkanHzbCuller& operator=(const VulkanHzbCuller&) = delete;

    /// Bind/rebind the depth view that mip 0 is built from. Must be called
    /// after the G-Buffer is built (depth view exists) and after every
    /// G-Buffer resize.
    void set_depth_view(VkImageView depth_view);

    /// CPU side: project each draw's world AABB to NDC, sample the
    /// CPU-side HZB at the right mip, set visibility flags. Call before
    /// the Geometry stage.
    // Pointer + count form — lets callers pass per-frame scratch (e.g. a
    // frame-allocator array) without building a std::vector. The vector
    // overload below forwards to it.
    void test_visibility(const glm::vec3* aabb_mins,
                         const glm::vec3* aabb_maxs,
                         size_t count,
                         const glm::mat4& view_proj);
    void test_visibility(const std::vector<glm::vec3>& aabb_mins,
                         const std::vector<glm::vec3>& aabb_maxs,
                         const glm::mat4& view_proj) {
        test_visibility(aabb_mins.data(), aabb_maxs.data(),
                        aabb_mins.size(), view_proj);
    }

    /// Query a draw's visibility from the last `test_visibility` call.
    /// Returns true on the first frame or when the culler is disabled.
    bool was_visible(uint32_t draw_index) const;

    /// Append GPU commands: HZB build (compute downsample chain) + copy
    /// readback mip to host buffer. Call after Geometry stage finishes
    /// writing depth, while still inside the same frame's command buffer.
    void build_and_readback(VkCommandBuffer cmd);

    /// CPU side: copy the host-visible readback buffer contents into the
    /// CPU-side HZB array used by test_visibility. Call after the frame
    /// fence is signalled so the GPU has finished the readback copy.
    void pull_readback();

    void     set_enabled(bool e) { enabled_ = e; }
    bool     is_enabled() const  { return enabled_; }
    uint32_t get_visible_count() const { return last_visible_count_; }
    uint32_t get_culled_count()  const { return last_culled_count_; }
    uint32_t get_mip_count() const { return mip_count_; }

private:
    bool initialize(VulkanDevice* device, const Config& cfg);
    void destroy();

    bool create_hzb_image();
    bool create_pipeline();
    bool create_descriptor_sets();
    bool create_readback_buffer();
    void write_descriptor_for_mip(uint32_t mip);

    VulkanDevice* device_  = nullptr;
    Config        config_  = {};
    bool          enabled_ = true;

    // ---- HZB image (R32_SFLOAT, mip chain) ----
    VkImage        hzb_image_   = VK_NULL_HANDLE;
    VkDeviceMemory hzb_memory_  = VK_NULL_HANDLE;
    std::vector<VkImageView> hzb_mip_views_;  // one writeable view per mip
    VkImageView    hzb_sample_view_ = VK_NULL_HANDLE; // full-chain sampler view

    uint32_t hzb_width_  = 0;   // mip 0 width (half of depth_width)
    uint32_t hzb_height_ = 0;   // mip 0 height
    uint32_t mip_count_  = 0;

    // Depth view bound as compute source for mip 0 build.
    VkImageView depth_view_ = VK_NULL_HANDLE;

    // Samplers
    VkSampler hzb_sampler_   = VK_NULL_HANDLE; // point-sample HZB
    VkSampler depth_sampler_ = VK_NULL_HANDLE; // point-sample depth (depth aspect)

    // ---- Compute pipeline ----
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout      pipeline_layout_       = VK_NULL_HANDLE;
    VkPipeline            pipeline_              = VK_NULL_HANDLE;
    VkDescriptorPool      descriptor_pool_       = VK_NULL_HANDLE;
    // One descriptor set per dispatch (per output mip). Index 0 = "depth →
    // HZB[0]"; index k>0 = "HZB[k-1] → HZB[k]".
    std::vector<VkDescriptorSet> descriptor_sets_;

    // ---- Readback ----
    VkBuffer       readback_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
    void*          readback_mapped_ = nullptr;
    uint32_t       readback_width_  = 0;
    uint32_t       readback_height_ = 0;

    // ---- CPU-side HZB used by test_visibility ----
    std::vector<float> cpu_hzb_;     // size = readback_width_ * readback_height_
    bool               cpu_hzb_valid_ = false;

    // ---- Per-frame visibility ----
    std::vector<uint8_t> visibility_;
    uint32_t             last_visible_count_ = 0;
    uint32_t             last_culled_count_  = 0;
    bool                 have_results_       = false;
};

} // namespace gws::renderer::gpu
