#pragma once

namespace engine::vulkan {

/**
 * GPU Profiler UI Panels - ImGui visualization for GPU profiling
 * 
 * Displays per-pass GPU timing from GPUProfiler with:
 * - Real-time statistics table
 * - Historical timeline visualization
 * - GPU load percentage
 * - Bottleneck highlighting
 */
class GPUProfilerPanels {
public:
  GPUProfilerPanels() = default;
  ~GPUProfilerPanels() = default;

  /**
   * Draw GPU statistics panel
   * Shows per-pass timing in a table with current duration and percentage of frame
   */
  void draw_gpu_stats_panel();

  /**
   * Draw GPU timeline panel
   * Shows historical per-pass timing as line graphs
   */
  void draw_gpu_timeline_panel();

  /**
   * Draw combined GPU profiler panel
   * Shows stats and timeline in one window
   */
  void draw_gpu_profiler_panel();

  /**
   * Toggle panel visibility
   */
  void set_visible(bool visible) { visible_ = visible; }
  bool is_visible() const { return visible_; }

private:
  bool visible_ = true;
  bool show_timeline_ = false;
  bool highlight_bottlenecks_ = true;

  // Configuration
  float frame_budget_ms_ = 16.67f;  // 60 FPS target
};

}  // namespace engine::vulkan
