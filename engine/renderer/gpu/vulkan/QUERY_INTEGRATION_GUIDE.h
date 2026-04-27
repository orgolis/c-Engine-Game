/**
 * Week 25 - GPU Profiler Render Graph Integration
 * 
 * This file documents how GPU query timestamps are integrated into the
 * Vulkan render graph to enable per-pass GPU performance profiling.
 * 
 * INTEGRATION POINTS:
 * 1. query_pool_ member in VulkanRenderGraph
 * 2. Query index tracking for each render pass
 * 3. Timestamp writes at pass start/end
 * 4. Result retrieval and profiler update after frame completion
 */

#pragma once

#include "query_pool.h"
#include "gpu_profiler.h"

namespace engine::vulkan {

/**
 * VulkanRenderGraph GPU Profiler Integration
 * 
 * Add these members to VulkanRenderGraph class:
 * 
 * private:
 *   VulkanQueryPool query_pool_;           // Query pool for frame timestamps
 *   uint32_t current_query_index_;         // Tracks next available query slot
 *   static constexpr uint32_t MAX_QUERIES = 512;  // 256 passes × 2 timestamps each
 * 
 * Key integration points:
 */

// In VulkanRenderGraph::initialize():
// query_pool_.create(device_, MAX_QUERIES);
// GPUProfiler::instance().initialize(device_properties_.limits.timestampPeriod);

// In VulkanRenderGraph::record_frame():
// current_query_index_ = 0;  // Reset for new frame

// In each render pass (e.g., record_geometry):
void example_render_pass_integration(VkCommandBuffer cmd) {
  // Start query for this pass
  uint32_t query_start = current_query_index_++;
  uint32_t query_end = current_query_index_++;
  
  query_pool_.write_timestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE, query_start);
  GPUProfiler::instance().record_pass_start("geometry", query_start);
  
  // ... render geometry ...
  
  query_pool_.write_timestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE, query_end);
  GPUProfiler::instance().record_pass_end("geometry", query_end);
}

// In VulkanRenderGraph::present_frame() (after all rendering complete):
// std::vector<uint64_t> timestamps;
// if (query_pool_.get_results(timestamps, true)) {  // Wait for GPU
//   GPUProfiler::instance().update_from_query_results(timestamps);
// }

// For UI display in debug panels:
// GPUProfilerPanels panels_;
// panels_.draw_gpu_profiler_panel();  // Call in ImGui render loop

/**
 * RENDER PASS QUERY LAYOUT (for 256-pass scenes):
 * 
 * Query Index Range    | Pass Name
 * ==================   | ===================
 * 0-1                  | Geometry (start, end)
 * 2-3                  | Shadow (start, end)
 * 4-5                  | HZB Build (start, end)
 * 6-7                  | HZB Test (start, end)
 * 8-9                  | G-Buffer (start, end)
 * 10-11                | Lighting (start, end)
 * 12-13                | Post-Processing (start, end)
 * ...
 * 510-511              | Final Present (start, end)
 * 
 * Total capacity: 512 timestamps = 256 passes
 */

/**
 * GPU TO CPU SYNCHRONIZATION:
 * 
 * Frame N-2: Results available (2 frame latency)
 * Frame N-1: Recording queries
 * Frame N:   GPU executing, CPU retrieving N-2 results
 * 
 * This latency hiding pattern prevents GPU→CPU stalls
 * by reading results from 2 frames back while current frame runs.
 */

/**
 * TIMESTAMP VALIDITY CHECKS:
 * 
 * - Verify timestamp_start < timestamp_end
 * - Check for counter wraparound (64-bit timestamps unlikely to wrap)
 * - Validate delta_ms > 0 (clock must advance)
 * - Skip passes with invalid timestamps (report warning)
 */

}  // namespace engine::vulkan
