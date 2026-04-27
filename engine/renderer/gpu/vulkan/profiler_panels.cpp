/**
 * @file profiler_panels.cpp
 * @brief Profiler panel implementations.
 */

#include "profiler_panels.h"
#include "ui_manager.h"
#include "profiler.h"
#include <imgui.h>

namespace gws::renderer::gpu {

using gws::profiler::CPUProfiler;
using gws::profiler::GPUProfiler;

void ProfilerPanels::register_all(UIManager* ui) {
    if (!ui) return;
    register_cpu_profiler_panel(ui);
    register_gpu_profiler_panel(ui);
}

void ProfilerPanels::register_cpu_profiler_panel(UIManager* ui) {
    if (!ui) return;

    ui->register_panel("cpu_profiler", []() {
        ImGui::SetNextWindowPos(ImVec2(10, 630), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("CPU Profiler", nullptr, ImGuiWindowFlags_NoMove)) {
            auto& profiler = CPUProfiler::instance();
            auto stats = profiler.get_frame_stats(0);

            if (!stats) {
                ImGui::TextDisabled("No profiling data");
            } else {
                // Frame info
                ImGui::Text("Frame: %u", stats->frame_number);
                ImGui::Text("Frame Time: %.2f ms", stats->frame_time_ms);
                ImGui::Separator();

                // Per-pass breakdown
                ImGui::Text("Passes (sorted by time):");
                ImGui::Spacing();

                float total_time = stats->frame_time_ms;
                for (const auto& pass : stats->passes) {
                    float percentage = total_time > 0.0f ? (pass.time_ms / total_time) * 100.0f : 0.0f;
                    ImGui::Text("%-20s %.2f ms (%.1f%%)", pass.name.c_str(), pass.time_ms, percentage);
                }

                // Statistics
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Statistics:");

                double avg_frame_time = 0.0;
                size_t frame_count = 0;
                double min_frame_time = 1000.0;
                double max_frame_time = 0.0;

                for (const auto& frame : profiler.get_frame_history()) {
                    avg_frame_time += frame.frame_time_ms;
                    min_frame_time = std::min(min_frame_time, frame.frame_time_ms);
                    max_frame_time = std::max(max_frame_time, frame.frame_time_ms);
                    frame_count++;
                }

                if (frame_count > 0) {
                    avg_frame_time /= frame_count;
                }

                ImGui::Text("Avg: %.2f ms | Min: %.2f ms | Max: %.2f ms",
                            avg_frame_time, min_frame_time, max_frame_time);
            }

            ImGui::End();
        }
    });
}

void ProfilerPanels::register_gpu_profiler_panel(UIManager* ui) {
    if (!ui) return;

    ui->register_panel("gpu_profiler", []() {
        ImGui::SetNextWindowPos(ImVec2(370, 630), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("GPU Profiler", nullptr, ImGuiWindowFlags_NoMove)) {
            auto& profiler = GPUProfiler::instance();
            auto stats = profiler.get_frame_stats(0);

            if (!stats) {
                ImGui::TextDisabled("No profiling data");
            } else {
                // Frame info
                ImGui::Text("Frame: %u", stats->frame_number);
                ImGui::Text("Frame Time: %.2f ms", stats->frame_time_ms);
                ImGui::Separator();

                // Per-pass breakdown
                ImGui::Text("Passes (sorted by time):");
                ImGui::Spacing();

                float total_time = stats->frame_time_ms;
                for (const auto& pass : stats->passes) {
                    float percentage = total_time > 0.0f ? (pass.time_ms / total_time) * 100.0f : 0.0f;
                    ImGui::Text("%-20s %.2f ms (%.1f%%)", pass.name.c_str(), pass.time_ms, percentage);
                }

                // Statistics
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Statistics:");

                double avg_frame_time = 0.0;
                size_t frame_count = 0;
                double min_frame_time = 1000.0;
                double max_frame_time = 0.0;

                for (const auto& frame : profiler.get_frame_history()) {
                    avg_frame_time += frame.frame_time_ms;
                    min_frame_time = std::min(min_frame_time, frame.frame_time_ms);
                    max_frame_time = std::max(max_frame_time, frame.frame_time_ms);
                    frame_count++;
                }

                if (frame_count > 0) {
                    avg_frame_time /= frame_count;
                }

                ImGui::Text("Avg: %.2f ms | Min: %.2f ms | Max: %.2f ms",
                            avg_frame_time, min_frame_time, max_frame_time);
            }

            ImGui::End();
        }
    });
}

} // namespace gws::renderer::gpu
