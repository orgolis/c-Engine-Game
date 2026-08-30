#pragma once

#include <map>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <mutex>
#include <set>

namespace engine::vulkan {

/**
 * GPUProfiler - Tracks per-pass GPU timing
 * 
 * Records GPU timestamps from Vulkan queries and provides:
 * - Per-pass timing (geometry, shadow, HZB, lighting, etc.)
 * - 10-frame rolling average
 * - 100-frame historical data
 * - Thread-safe access
 */
class GPUProfiler {
public:
  struct PassTiming {
    std::string name;
    uint64_t timestamp_start = 0;      // GPU timestamp
    uint64_t timestamp_end = 0;
    float duration_ms = 0.0f;          // Computed duration
    std::vector<float> history;        // 100-frame history
    int history_index = 0;             // Circular buffer index

    float get_average_ms(int frame_count) const;
  };

  GPUProfiler();
  ~GPUProfiler() = default;

  /**
   * Singleton access
   */
  static GPUProfiler& instance();

  /**
   * Initialize profiler with GPU timestamp period (nanoseconds per tick)
   */
  void initialize(float gpu_timestamp_period);

  /**
   * Mark start of a render pass
   * @param pass_name Unique identifier for pass (e.g., "geometry", "shadow")
   * @param query_index Index of start timestamp in query pool
   */
  void record_pass_start(const std::string& pass_name, uint32_t query_index);

  /**
   * Mark end of a render pass
   * @param pass_name Must match corresponding record_pass_start call
   * @param query_index Index of end timestamp in query pool
   */
  void record_pass_end(const std::string& pass_name, uint32_t query_index);

  /**
   * Process GPU query results and compute pass durations
   * @param timestamps Vector of timestamps retrieved from Vulkan queries
   * Should contain pairs of (start, end) timestamps in order
   */
  void update_from_query_results(const std::vector<uint64_t>& timestamps);

  /**
   * Submit a frame's already-resolved per-pass timings (name, milliseconds).
   * For callers that read their own GPU timestamps (e.g. VulkanRenderGraph,
   * which resolves stage µs itself) instead of going through the query-index
   * model above. Recomputes the frame total; passes absent this frame are
   * zeroed so a disabled stage shows 0 rather than a stale value; each named
   * pass keeps its rolling history.
   */
  void submit_frame(const std::vector<std::pair<std::string, float>>& passes);

  /// Additive frame accounting (performance audit F2).
  ///
  /// submit_frame() resets the total and zeroes anything absent from its
  /// argument, so it can only be called by ONE producer per frame. The frame's
  /// timings now come from two: the render graph's five render-pass stages, and
  /// GpuZones for every compute pass dispatched between them. With the old API
  /// the second call clobbered the first, which is how the reported total came
  /// to exclude the most expensive work in the frame.
  ///
  /// Bracket the frame with begin_gpu_frame()/end_gpu_frame() and let each
  /// producer call submit_partial().
  void begin_gpu_frame();
  void submit_partial(const std::vector<std::pair<std::string, float>>& passes);
  void end_gpu_frame();

  /**
   * Get timing for specific pass
   * @param pass_name Pass to look up
   * @return PassTiming if found, nullptr otherwise
   */
  const PassTiming* get_pass_timing(const std::string& pass_name) const;

  /**
   * Get total GPU time for frame (sum of all passes)
   */
  float get_total_gpu_time_ms() const;

  /**
   * Get all pass timings
   */
  const std::map<std::string, PassTiming>& get_all_timings() const { return pass_timings_; }

  /**
   * Reset for next frame
   */
  void reset_frame();

  /**
   * Get GPU load percentage (GPU time / 16.7ms frame budget * 100)
   */
  float get_gpu_load_percent() const;

private:
  std::map<std::string, PassTiming> pass_timings_;
  std::map<std::string, uint32_t> pass_query_indices_;  // Maps pass_name to query index pair

  float timestamp_to_ms_ = 1.0f / 1e6f;  // GPU timestamp conversion factor
  float total_gpu_time_ms_ = 0.0f;
  std::set<std::string> seen_this_frame_;   // producers that reported this frame

  mutable std::mutex mutex_;

  static GPUProfiler* instance_;
};

}  // namespace engine::vulkan
