/**
 * @file debug_panels.h
 * @brief Debug UI panels for profiling and diagnostics (Phase 6 Week 21).
 *
 * Provides built-in debug panels:
 * - FPS counter and frame timing
 * - Draw call profiler
 * - Memory usage
 * - Culling statistics
 */

#pragma once

namespace gws::renderer::gpu {

class UIManager;
class VulkanRenderGraph;

/**
 * @class DebugPanels
 * @brief Registry of built-in debug visualization panels.
 *
 * Register debug panels with UIManager to enable real-time profiling:
 * - FPS and frame timing histogram
 * - Per-pass GPU timing (if profiler available)
 * - Draw call count and triangle count
 * - Culling statistics (frustum + occlusion)
 * - Memory allocation stats
 */
class DebugPanels {
public:
    DebugPanels() = default;
    ~DebugPanels() = default;

    DebugPanels(const DebugPanels&) = delete;
    DebugPanels& operator=(const DebugPanels&) = delete;

    /// Register all built-in debug panels with the UI manager.
    /// Panels are initially hidden; use ui->show_panel() to enable.
    static void register_all(UIManager* ui, VulkanRenderGraph* graph);

    /// Register the FPS and frame timing panel.
    static void register_frame_timing_panel(UIManager* ui);

    /// Register the draw call counter panel.
    static void register_draw_stats_panel(UIManager* ui, VulkanRenderGraph* graph);

    /// Register the culling statistics panel.
    static void register_culling_stats_panel(UIManager* ui, VulkanRenderGraph* graph);
};

} // namespace gws::renderer::gpu
