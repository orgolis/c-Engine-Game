/**
 * @file profiler_panels.h
 * @brief Profiler visualization panels (Phase 6 Week 23).
 *
 * Displays CPU and GPU profiling data via ImGui:
 * - Frame time history (graph)
 * - Per-pass breakdown (sorted by time)
 * - Statistics (min/max/avg)
 */

#pragma once

#include <memory>

namespace gws::renderer::gpu {

class UIManager;

/**
 * @class ProfilerPanels
 * @brief Registry of profiler visualization panels.
 */
class ProfilerPanels {
public:
    static void register_all(UIManager* ui);
    static void register_cpu_profiler_panel(UIManager* ui);
    static void register_gpu_profiler_panel(UIManager* ui);
};

} // namespace gws::renderer::gpu
