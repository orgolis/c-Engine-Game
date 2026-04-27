/**
 * @file debug_panels.cpp
 * @brief Debug panel implementations.
 */

#include "debug_panels.h"
#include "ui_manager.h"
#include "vulkan_render_graph.h"
#include <imgui.h>
#include <algorithm>
#include <deque>

namespace gws::renderer::gpu {

// Frame timing history (last 60 frames)
static std::deque<float> g_frame_times;
static constexpr size_t FRAME_HISTORY_SIZE = 60;

void DebugPanels::register_all(UIManager* ui, VulkanRenderGraph* graph) {
    if (!ui) return;

    register_frame_timing_panel(ui);
    if (graph) {
        register_draw_stats_panel(ui, graph);
        register_culling_stats_panel(ui, graph);
        register_gpu_profiler_panel(ui, graph);
    }
}

void DebugPanels::register_frame_timing_panel(UIManager* ui) {
    if (!ui) return;

    ui->register_panel("debug_frame_timing", []() {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Frame Timing", nullptr, ImGuiWindowFlags_NoMove)) {
            ImGuiIO& io = ImGui::GetIO();
            float frame_time = 1.0f / (io.Framerate > 0.0f ? io.Framerate : 60.0f);

            // Keep history of frame times
            g_frame_times.push_back(frame_time);
            if (g_frame_times.size() > FRAME_HISTORY_SIZE) {
                g_frame_times.pop_front();
            }

            // Display current FPS
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Text("Frame Time: %.2f ms", frame_time * 1000.0f);

            // Display average of last 10 frames
            float avg_time = 0.0f;
            int sample_count = std::min(size_t(10), g_frame_times.size());
            for (size_t i = g_frame_times.size() - sample_count; i < g_frame_times.size(); ++i) {
                avg_time += g_frame_times[i];
            }
            if (sample_count > 0) {
                avg_time /= sample_count;
                ImGui::Text("Avg (10 frames): %.2f ms", avg_time * 1000.0f);
            }

            // Plot frame time history
            if (!g_frame_times.empty()) {
                ImGui::PlotLines("###frame_time_graph",
                    [](void* data, int idx) -> float {
                        auto* history = static_cast<std::deque<float>*>(data);
                        return (*history)[idx] * 1000.0f;  // Convert to ms for display
                    },
                    &g_frame_times,
                    g_frame_times.size(),
                    0,
                    "Frame Time (ms)",
                    0.0f,
                    FLT_MAX,
                    ImVec2(280, 100));
            }

            ImGui::End();
        }
    });
}

void DebugPanels::register_draw_stats_panel(UIManager* ui, VulkanRenderGraph* graph) {
    if (!ui || !graph) return;

    ui->register_panel("debug_draw_stats", [graph]() {
        ImGui::SetNextWindowPos(ImVec2(320, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 240), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Draw Statistics", nullptr, ImGuiWindowFlags_NoMove)) {
            const auto& s = graph->get_stats();
            const uint32_t total_draws = s.geometry_draw_calls + s.shadow_draw_calls;
            const uint32_t total_tris  = s.geometry_triangles  + s.shadow_triangles;

            ImGui::Text("Frame: %u", s.frame_index);
            ImGui::Text("Draw Calls (total): %u", total_draws);
            ImGui::Text("Triangles (total): %u", total_tris);
            ImGui::Separator();
            ImGui::Text("Geometry: %u draws / %u tris (%.2f us)",
                        s.geometry_draw_calls, s.geometry_triangles, s.geometry_us);
            ImGui::Text("Shadow:   %u draws / %u tris (%.2f us)",
                        s.shadow_draw_calls,   s.shadow_triangles,   s.shadow_us);
            ImGui::Text("Lighting: %.2f us", s.lighting_us);
            ImGui::Text("Post-FX:  %.2f us", s.post_process_us);

            ImGui::End();
        }
    });
}

void DebugPanels::register_culling_stats_panel(UIManager* ui, VulkanRenderGraph* graph) {
    if (!ui || !graph) return;

    ui->register_panel("debug_culling_stats", [graph]() {
        ImGui::SetNextWindowPos(ImVec2(630, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Culling Statistics", nullptr, ImGuiWindowFlags_NoMove)) {
            const auto& s = graph->get_stats();
            const bool on = graph->is_frustum_culling_enabled();
            ImGui::Text("Frustum Culling: %s", on ? "ON" : "OFF");
            ImGui::Text("Input items:  %u", s.frustum_input_items);
            ImGui::Text("Visible:      %u", s.frustum_visible_items);
            ImGui::Text("Culled:       %u", s.frustum_culled_items);
            if (s.frustum_input_items > 0) {
                const float pct = 100.0f * s.frustum_culled_items
                                  / static_cast<float>(s.frustum_input_items);
                ImGui::Text("Cull rate:    %.1f%%", pct);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Occlusion (HZB): pending Phase 6 Week 24");

            ImGui::End();
        }
    });
}

// ----------------------------------------------------------------------------
// GPU profiler chart
// ----------------------------------------------------------------------------

namespace {

constexpr size_t kProfilerHistory = 60;

struct StageHistory {
    std::deque<float> shadow_us;
    std::deque<float> geometry_us;
    std::deque<float> lighting_us;
    std::deque<float> post_us;
};

StageHistory& stage_history() {
    static StageHistory h;
    return h;
}

void push_capped(std::deque<float>& d, float v) {
    d.push_back(v);
    while (d.size() > kProfilerHistory) d.pop_front();
}

float deque_max(const std::deque<float>& d) {
    float m = 0.0f;
    for (float v : d) if (v > m) m = v;
    return m;
}

void plot_stage(const char* label, std::deque<float>& history, ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
    ImGui::PlotLines(label,
        [](void* data, int idx) -> float {
            auto* h = static_cast<std::deque<float>*>(data);
            return (*h)[idx];
        },
        &history,
        static_cast<int>(history.size()),
        0,
        nullptr,
        0.0f,
        std::max(1.0f, deque_max(history) * 1.2f),
        ImVec2(280, 40));
    ImGui::PopStyleColor();
}

} // namespace

void DebugPanels::register_gpu_profiler_panel(UIManager* ui, VulkanRenderGraph* graph) {
    if (!ui || !graph) return;

    ui->register_panel("debug_gpu_profiler", [graph]() {
        ImGui::SetNextWindowPos(ImVec2(10, 220), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(310, 280), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("GPU Profiler", nullptr, ImGuiWindowFlags_NoMove)) {
            const auto& s = graph->get_stats();
            auto& h = stage_history();
            push_capped(h.shadow_us,   static_cast<float>(s.shadow_us));
            push_capped(h.geometry_us, static_cast<float>(s.geometry_us));
            push_capped(h.lighting_us, static_cast<float>(s.lighting_us));
            push_capped(h.post_us,     static_cast<float>(s.post_process_us));

            const float total_us = static_cast<float>(
                s.shadow_us + s.geometry_us + s.lighting_us + s.post_process_us);
            ImGui::Text("Total GPU: %.2f us / frame", total_us);
            ImGui::Separator();

            // Each stage gets its own y-axis (auto-scaled to its own peak)
            // so a 5x heavier stage doesn't squash the others.
            plot_stage("Shadow",   h.shadow_us,   ImVec4(0.6f, 0.4f, 1.0f, 1.0f));
            plot_stage("Geometry", h.geometry_us, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            plot_stage("Lighting", h.lighting_us, ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
            plot_stage("Post-FX",  h.post_us,     ImVec4(1.0f, 0.4f, 0.4f, 1.0f));

            ImGui::End();
        }
    });
}

} // namespace gws::renderer::gpu
