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

  /// Additive frame accounting (performance audit F2) -- the ONLY way to
  /// report resolved per-pass timings (name, milliseconds).
  ///
  /// A frame's timings come from two producers: the render graph's five
  /// render-pass stages, and GpuZones for every compute pass dispatched
  /// between them. There used to be a submit_frame() as well, which reset the
  /// total and zeroed every pass absent from its argument -- so it was only
  /// correct with exactly one producer, and with two, whichever called it
  /// second erased the other. F2 moved the graph off it; GpuZones stayed on
  /// it, and erased the graph's stages every frame, including a 23 ms
  /// lighting pass that then read as 0.00. It has been deleted rather than
  /// documented, because the documentation was already there and did not
  /// stop it. gpuzones_check asserts it stays gone.
  ///
  /// Bracket the frame with begin_gpu_frame()/end_gpu_frame() and let each
  /// producer call submit_partial(). A producer that knows a pass stopped
  /// reports it as 0; absence means "no news", not "it stopped".
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
