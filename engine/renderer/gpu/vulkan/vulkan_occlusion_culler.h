/**
 * @file vulkan_occlusion_culler.h
 * @brief Per-draw GPU occlusion culling using VK_QUERY_TYPE_OCCLUSION.
 *
 * Each Geometry-stage draw is wrapped in vkCmdBeginQuery/EndQuery. After the
 * frame's fence is signalled, vkGetQueryPoolResults gives us a per-draw
 * "samples passed" count (binary precision — we just need >0 vs 0). The
 * results then drive the NEXT frame's draw filtering: draws whose previous
 * sample count was zero are skipped (they were fully occluded by what was in
 * front, the depth-test rejected every fragment).
 *
 * Limitations:
 *   - One frame of latency (an object that becomes visible takes one frame
 *     to start being drawn). Acceptable for editor / level-edit use.
 *   - Draw list ordering must be stable across frames or visibility info
 *     becomes useless. The culler detects list-size changes and invalidates
 *     accordingly; large per-frame index reshuffling will hurt accuracy
 *     until the order stabilises.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace gws::renderer::gpu {

class VulkanDevice;

class VulkanOcclusionCuller {
public:
    struct Config {
        uint32_t max_draws = 4096;  ///< Hard upper bound on per-frame draws.
    };

    /// Build a culler. Returns nullptr if the device lacks occlusion-query
    /// support on the graphics queue.
    static std::unique_ptr<VulkanOcclusionCuller> create(VulkanDevice* device,
                                                         const Config& cfg);

    VulkanOcclusionCuller() = default;
    ~VulkanOcclusionCuller();

    VulkanOcclusionCuller(const VulkanOcclusionCuller&) = delete;
    VulkanOcclusionCuller& operator=(const VulkanOcclusionCuller&) = delete;

    /// Call once per frame before any draws. Resets the GPU query pool and
    /// records the draw list size; size changes invalidate the visibility
    /// cache (everything counts as visible).
    void begin_frame(VkCommandBuffer cmd, uint32_t draw_count);

    /// True when the draw at `index` was visible on the previous frame (or
    /// when we have no info — first frame, after a list-size change, or when
    /// the culler is disabled). The Geometry recorder uses this to decide
    /// whether to submit the draw.
    bool was_visible(uint32_t draw_index) const;

    /// vkCmdBeginQuery wrapper. The matching vkCmdEndQuery is `end_draw`.
    /// Indexes the query pool slot for this draw — the slot equals the
    /// draw's position in the *original* (pre-skip) draw list, so subsequent
    /// frame's `was_visible(i)` lookups remain coherent even when we skip
    /// some indices.
    void begin_draw(VkCommandBuffer cmd, uint32_t draw_index);
    void end_draw(VkCommandBuffer cmd, uint32_t draw_index);

    /// Pull the previous frame's query results from the GPU into the
    /// internal visibility buffer. MUST be called after the previous
    /// frame's fence has been waited on so the queries are available.
    /// No-op when the culler is disabled or the device dropped queries.
    void resolve_results();

    /// On/off switch (e.g. for A/B perf comparison).
    void set_enabled(bool e) { enabled_ = e; }
    bool is_enabled() const  { return enabled_; }

    uint32_t get_visible_count() const { return last_visible_count_; }
    uint32_t get_culled_count()  const { return last_culled_count_; }

private:
    bool initialize(VulkanDevice* device, const Config& cfg);
    void destroy();

    VulkanDevice* device_       = nullptr;
    VkQueryPool   query_pool_   = VK_NULL_HANDLE;
    uint32_t      max_draws_    = 0;

    /// Visibility bitmap from the previous frame. true = was visible.
    std::vector<uint8_t> visibility_;
    uint32_t pending_draw_count_ = 0;  ///< count submitted this frame
    uint32_t last_visible_count_ = 0;
    uint32_t last_culled_count_  = 0;
    bool     have_prev_results_  = false;
    bool     enabled_            = true;
};

} // namespace gws::renderer::gpu
