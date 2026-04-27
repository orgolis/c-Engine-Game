/**
 * @file profiler.h
 * @brief CPU and GPU profiling infrastructure (Phase 6 Week 23).
 *
 * Provides:
 * - CPU timing markers (scope-based RAII)
 * - GPU timing queries (render pass profiling)
 * - Frame history tracking
 * - Per-pass breakdown display
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <deque>

namespace gws::profiler {

/**
 * @class CPUTimer
 * @brief RAII timer for CPU profiling.
 *
 * Usage:
 * ```cpp
 *   {
 *       CPUTimer timer("MyFunction");
 *       // ... work happens here ...
 *   }  // Timer automatically records when scope exits
 * ```
 */
class CPUTimer {
public:
    explicit CPUTimer(const std::string& name);
    ~CPUTimer();

    CPUTimer(const CPUTimer&) = delete;
    CPUTimer& operator=(const CPUTimer&) = delete;

    double get_elapsed_ms() const;

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
};

/**
 * @struct CPUFrameStats
 * @brief CPU timing statistics for a frame.
 */
struct CPUFrameStats {
    struct Pass {
        std::string name;
        double time_ms = 0.0;
    };

    std::vector<Pass> passes;
    double frame_time_ms = 0.0;
    uint32_t frame_number = 0;

    double get_pass_time(const std::string& name) const {
        for (const auto& pass : passes) {
            if (pass.name == name) return pass.time_ms;
        }
        return 0.0;
    }
};

/**
 * @class CPUProfiler
 * @brief Central CPU profiling controller.
 */
class CPUProfiler {
public:
    static CPUProfiler& instance();

    /// Record a timing marker
    void record_timing(const std::string& name, double time_ms);

    /// Start a new frame
    void begin_frame();

    /// End current frame and record stats
    void end_frame();

    /// Get statistics for frame index (0 = latest)
    const CPUFrameStats* get_frame_stats(size_t frame_index = 0) const;

    /// Get all recorded frames (last N)
    const std::deque<CPUFrameStats>& get_frame_history() const { return frame_history_; }

    /// Get average time for a pass (across history)
    double get_average_pass_time(const std::string& name) const;

    /// Clear history
    void clear_history();

private:
    CPUProfiler() = default;

    std::unordered_map<std::string, double> current_frame_timings_;
    std::deque<CPUFrameStats> frame_history_;
    std::chrono::high_resolution_clock::time_point frame_start_;
    uint32_t frame_number_ = 0;

    static constexpr size_t MAX_FRAME_HISTORY = 300;  // ~5 seconds at 60 FPS
};

/**
 * @struct GPUFrameStats
 * @brief GPU timing statistics for a frame.
 */
struct GPUFrameStats {
    struct Pass {
        std::string name;
        double time_ms = 0.0;
    };

    std::vector<Pass> passes;
    double frame_time_ms = 0.0;
    uint32_t frame_number = 0;

    double get_pass_time(const std::string& name) const {
        for (const auto& pass : passes) {
            if (pass.name == name) return pass.time_ms;
        }
        return 0.0;
    }
};

/**
 * @class GPUProfiler
 * @brief GPU profiling via Vulkan timestamp queries.
 *
 * Note: Requires VkQueryPool setup and vkGetQueryPoolResults calls.
 * Simplified implementation for Week 23.
 */
class GPUProfiler {
public:
    static GPUProfiler& instance();

    /// Record GPU timing for a pass (typically called after vkGetQueryPoolResults)
    void record_pass_time(const std::string& name, double time_ms);

    /// Get statistics for frame
    const GPUFrameStats* get_frame_stats(size_t frame_index = 0) const;

    /// Get all recorded frames
    const std::deque<GPUFrameStats>& get_frame_history() const { return frame_history_; }

    /// Get average time for a pass
    double get_average_pass_time(const std::string& name) const;

    /// Clear history
    void clear_history();

    /// Mark frame boundary (call once per frame)
    void end_frame();

private:
    GPUProfiler() = default;

    std::unordered_map<std::string, double> current_frame_timings_;
    std::deque<GPUFrameStats> frame_history_;
    uint32_t frame_number_ = 0;

    static constexpr size_t MAX_FRAME_HISTORY = 300;
};

} // namespace gws::profiler

// Convenience macros
#define GWS_PROFILE_CPU_SCOPE(name) \
    gws::profiler::CPUTimer __gws_cpu_timer(name)

#define GWS_PROFILE_CPU_SCOPED(name) \
    gws::profiler::CPUTimer __gws_cpu_timer_##__LINE__(name)
