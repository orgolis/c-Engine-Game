#include "gpu_profiler_panels.h"
#include "gpu_profiler.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace engine::vulkan {

void GPUProfilerPanels::draw_gpu_stats_panel() {
  if (!visible_) return;

  auto& profiler = GPUProfiler::instance();

  if (ImGui::Begin("GPU Profiler Statistics", &visible_)) {
    // Frame time and load
    float total_gpu_time = profiler.get_total_gpu_time_ms();
    float gpu_load = profiler.get_gpu_load_percent();

    ImGui::Text("Total GPU Time: %.2f ms", total_gpu_time);
    ImGui::SameLine(300);
    ImGui::Text("Budget: %.2f ms @ 60 FPS", frame_budget_ms_);

    // GPU load bar
    float load_ratio = gpu_load / 100.0f;
    ImVec4 bar_color;
    if (load_ratio > 1.0f) {
      bar_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);  // Red: over budget
    } else if (load_ratio > 0.9f) {
      bar_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // Yellow: near budget
    } else {
      bar_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);  // Green: healthy
    }

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_color);
    ImGui::ProgressBar(std::min(load_ratio, 1.0f), ImVec2(-1, 0), "");
    ImGui::PopStyleColor();

    ImGui::Text("GPU Load: %.1f%%", gpu_load);

    ImGui::Separator();

    // Per-pass timing table
    if (ImGui::BeginTable("GPU Pass Timings", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Pass Name");
      ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed, 100);
      ImGui::TableSetupColumn("% of Budget", ImGuiTableColumnFlags_WidthFixed, 100);
      ImGui::TableSetupColumn("Avg 10-frame", ImGuiTableColumnFlags_WidthFixed, 120);
      ImGui::TableHeadersRow();

      const auto& timings = profiler.get_all_timings();
      for (const auto& [pass_name, timing] : timings) {
        ImGui::TableNextRow();

        // Pass name
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", pass_name.c_str());

        // Current duration
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f", timing.duration_ms);

        // Percentage of budget
        ImGui::TableSetColumnIndex(2);
        float percent = (timing.duration_ms / frame_budget_ms_) * 100.0f;
        
        // Color code based on percentage
        if (percent > 100.0f) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        } else if (percent > 50.0f) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
        }
        ImGui::Text("%.1f%%", percent);
        if (percent > 50.0f) {
          ImGui::PopStyleColor();
        }

        // 10-frame average
        ImGui::TableSetColumnIndex(3);
        float avg_10 = timing.get_average_ms(10);
        ImGui::Text("%.2f ms", avg_10);
      }

      ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::Checkbox("Show Timeline", &show_timeline_);
    ImGui::Checkbox("Highlight Bottlenecks", &highlight_bottlenecks_);

    ImGui::End();
  }
}

void GPUProfilerPanels::draw_gpu_timeline_panel() {
  if (!visible_ || !show_timeline_) return;

  auto& profiler = GPUProfiler::instance();
  const auto& timings = profiler.get_all_timings();

  if (ImGui::Begin("GPU Timeline", nullptr, ImGuiWindowFlags_NoMove)) {
    ImGui::Text("Historical GPU Timing (last 100 frames)");
    ImGui::Separator();

    // Plot each pass's history
    for (const auto& [pass_name, timing] : timings) {
      if (timing.history.empty()) continue;

      // Create plot label with current value
      char label[128];
      ImFormatString(label, IM_ARRAYSIZE(label), "%s (%.2f ms)", pass_name.c_str(), timing.duration_ms);

      ImGui::PlotLines(label, timing.history.data(), timing.history.size(),
                       timing.history_index, nullptr, 0.0f, 20.0f,
                       ImVec2(-1, 80));
    }

    ImGui::End();
  }
}

void GPUProfilerPanels::draw_gpu_profiler_panel() {
  draw_gpu_stats_panel();
  if (show_timeline_) {
    draw_gpu_timeline_panel();
  }
}

}  // namespace engine::vulkan
