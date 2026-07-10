#include "gpu_profiler.h"
#include <algorithm>
#include <numeric>

namespace engine::vulkan {

GPUProfiler* GPUProfiler::instance_ = nullptr;

GPUProfiler::GPUProfiler() {
  // Initialize history buffers (100 frames)
}

GPUProfiler& GPUProfiler::instance() {
  if (!instance_) {
    instance_ = new GPUProfiler();
  }
  return *instance_;
}

void GPUProfiler::initialize(float gpu_timestamp_period) {
  // gpu_timestamp_period is in nanoseconds per tick
  // Convert to milliseconds: ns_per_tick / 1e6 = ms_per_tick
  timestamp_to_ms_ = gpu_timestamp_period / 1e6f;
}

void GPUProfiler::record_pass_start(const std::string& pass_name, uint32_t query_index) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (pass_timings_.find(pass_name) == pass_timings_.end()) {
    PassTiming timing;
    timing.name = pass_name;
    timing.history.resize(100, 0.0f);
    pass_timings_[pass_name] = timing;
  }

  pass_query_indices_[pass_name] = query_index;
}

void GPUProfiler::record_pass_end(const std::string& pass_name, uint32_t query_index) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (pass_timings_.find(pass_name) != pass_timings_.end()) {
    // Query index pair stored as (start, end)
    // We'll process this when update_from_query_results is called
  }
}

void GPUProfiler::update_from_query_results(const std::vector<uint64_t>& timestamps) {
  std::lock_guard<std::mutex> lock(mutex_);

  total_gpu_time_ms_ = 0.0f;

  // Process each pass's start/end timestamps
  for (auto& [pass_name, timing] : pass_timings_) {
    if (pass_query_indices_.find(pass_name) != pass_query_indices_.end()) {
      uint32_t query_idx = pass_query_indices_[pass_name];

      // Assuming query indices are sequential: start at 2*idx, end at 2*idx+1
      if (2 * query_idx + 1 < timestamps.size()) {
        uint64_t start_ts = timestamps[2 * query_idx];
        uint64_t end_ts = timestamps[2 * query_idx + 1];

        if (start_ts <= end_ts) {
          uint64_t delta_ticks = end_ts - start_ts;
          float delta_ms = static_cast<float>(delta_ticks) * timestamp_to_ms_;

          // Update current duration
          timing.duration_ms = delta_ms;

          // Add to history
          timing.history[timing.history_index] = delta_ms;
          timing.history_index = (timing.history_index + 1) % timing.history.size();

          total_gpu_time_ms_ += delta_ms;
        }
      }
    }
  }
}

void GPUProfiler::submit_frame(const std::vector<std::pair<std::string, float>>& passes) {
  std::lock_guard<std::mutex> lock(mutex_);

  total_gpu_time_ms_ = 0.0f;

  // Apply this frame's measured passes (create on first sight, extend history).
  for (const auto& [name, ms] : passes) {
    PassTiming& timing = pass_timings_[name];
    if (timing.name.empty()) {
      timing.name = name;
      timing.history.resize(100, 0.0f);
    }
    timing.duration_ms = ms;
    if (!timing.history.empty()) {
      timing.history[timing.history_index] = ms;
      timing.history_index = (timing.history_index + 1) % static_cast<int>(timing.history.size());
    }
    total_gpu_time_ms_ += ms;
  }

  // Zero any pass that did NOT run this frame so a disabled stage shows 0
  // instead of lingering at last frame's value.
  for (auto& [name, timing] : pass_timings_) {
    bool present = false;
    for (const auto& p : passes) {
      if (p.first == name) { present = true; break; }
    }
    if (!present) timing.duration_ms = 0.0f;
  }
}

const GPUProfiler::PassTiming* GPUProfiler::get_pass_timing(const std::string& pass_name) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = pass_timings_.find(pass_name);
  if (it != pass_timings_.end()) {
    return &it->second;
  }
  return nullptr;
}

float GPUProfiler::get_total_gpu_time_ms() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_gpu_time_ms_;
}

void GPUProfiler::reset_frame() {
  std::lock_guard<std::mutex> lock(mutex_);
  total_gpu_time_ms_ = 0.0f;
  pass_query_indices_.clear();
}

float GPUProfiler::get_gpu_load_percent() const {
  std::lock_guard<std::mutex> lock(mutex_);
  // Frame budget at 60 FPS = 16.67ms
  float frame_budget_ms = 16.67f;
  return (total_gpu_time_ms_ / frame_budget_ms) * 100.0f;
}

float GPUProfiler::PassTiming::get_average_ms(int frame_count) const {
  if (history.empty() || frame_count <= 0) return 0.0f;

  int samples = std::min(frame_count, static_cast<int>(history.size()));
  float sum = 0.0f;
  for (int i = 0; i < samples; ++i) {
    sum += history[i];
  }
  return sum / samples;
}

}  // namespace engine::vulkan
