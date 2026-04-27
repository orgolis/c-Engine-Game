/**
 * @file profiler.cpp
 * @brief Profiling implementation.
 */

#include "profiler.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace gws::profiler {

// ============================================================================
// CPUTimer
// ============================================================================

CPUTimer::CPUTimer(const std::string& name)
    : name_(name), start_(std::chrono::high_resolution_clock::now()) {}

CPUTimer::~CPUTimer() {
    double elapsed = get_elapsed_ms();
    CPUProfiler::instance().record_timing(name_, elapsed);
}

double CPUTimer::get_elapsed_ms() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - start_);
    return duration.count() / 1000.0;
}

// ============================================================================
// CPUProfiler
// ============================================================================

CPUProfiler& CPUProfiler::instance() {
    static CPUProfiler profiler;
    return profiler;
}

void CPUProfiler::record_timing(const std::string& name, double time_ms) {
    current_frame_timings_[name] = time_ms;
}

void CPUProfiler::begin_frame() {
    frame_start_ = std::chrono::high_resolution_clock::now();
}

void CPUProfiler::end_frame() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - frame_start_);
    double frame_time_ms = duration.count() / 1000.0;

    // Build frame stats from current timings
    CPUFrameStats stats;
    stats.frame_number = frame_number_++;
    stats.frame_time_ms = frame_time_ms;

    for (const auto& [name, time] : current_frame_timings_) {
        stats.passes.push_back({name, time});
    }

    // Sort by time descending
    std::sort(stats.passes.begin(), stats.passes.end(),
              [](const CPUFrameStats::Pass& a, const CPUFrameStats::Pass& b) {
                  return a.time_ms > b.time_ms;
              });

    frame_history_.push_back(stats);
    if (frame_history_.size() > MAX_FRAME_HISTORY) {
        frame_history_.pop_front();
    }

    current_frame_timings_.clear();
}

const CPUFrameStats* CPUProfiler::get_frame_stats(size_t frame_index) const {
    if (frame_index >= frame_history_.size()) {
        return nullptr;
    }
    return &frame_history_[frame_history_.size() - 1 - frame_index];
}

double CPUProfiler::get_average_pass_time(const std::string& name) const {
    double total = 0.0;
    size_t count = 0;

    for (const auto& frame : frame_history_) {
        for (const auto& pass : frame.passes) {
            if (pass.name == name) {
                total += pass.time_ms;
                count++;
                break;
            }
        }
    }

    return count > 0 ? total / count : 0.0;
}

void CPUProfiler::clear_history() {
    frame_history_.clear();
    current_frame_timings_.clear();
    frame_number_ = 0;
}

// ============================================================================
// GPUProfiler
// ============================================================================

GPUProfiler& GPUProfiler::instance() {
    static GPUProfiler profiler;
    return profiler;
}

void GPUProfiler::record_pass_time(const std::string& name, double time_ms) {
    current_frame_timings_[name] = time_ms;
}

const GPUFrameStats* GPUProfiler::get_frame_stats(size_t frame_index) const {
    if (frame_index >= frame_history_.size()) {
        return nullptr;
    }
    return &frame_history_[frame_history_.size() - 1 - frame_index];
}

double GPUProfiler::get_average_pass_time(const std::string& name) const {
    double total = 0.0;
    size_t count = 0;

    for (const auto& frame : frame_history_) {
        for (const auto& pass : frame.passes) {
            if (pass.name == name) {
                total += pass.time_ms;
                count++;
                break;
            }
        }
    }

    return count > 0 ? total / count : 0.0;
}

void GPUProfiler::clear_history() {
    frame_history_.clear();
    current_frame_timings_.clear();
    frame_number_ = 0;
}

void GPUProfiler::end_frame() {
    GPUFrameStats stats;
    stats.frame_number = frame_number_++;
    stats.frame_time_ms = 0.0;

    for (const auto& [name, time] : current_frame_timings_) {
        stats.passes.push_back({name, time});
        stats.frame_time_ms += time;
    }

    // Sort by time descending
    std::sort(stats.passes.begin(), stats.passes.end(),
              [](const GPUFrameStats::Pass& a, const GPUFrameStats::Pass& b) {
                  return a.time_ms > b.time_ms;
              });

    frame_history_.push_back(stats);
    if (frame_history_.size() > MAX_FRAME_HISTORY) {
        frame_history_.pop_front();
    }

    current_frame_timings_.clear();
}

} // namespace gws::profiler
