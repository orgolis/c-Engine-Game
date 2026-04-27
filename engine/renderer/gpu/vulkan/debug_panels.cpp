/**
 * @file debug_panels.cpp
 * @brief Debug panel implementations.
 */

#include "debug_panels.h"
#include "ui_manager.h"
#include "vulkan_render_graph.h"
#include <imgui.h>
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
        ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Draw Statistics", nullptr, ImGuiWindowFlags_NoMove)) {
            // TODO: Get actual draw stats from render graph
            // For now, display placeholder
            ImGui::Text("Draw Calls: N/A");
            ImGui::Text("Triangles: N/A");
            ImGui::Text("Vertices: N/A");
            ImGui::Separator();
            ImGui::Text("Geometry Pass: N/A");
            ImGui::Text("Shadow Pass: N/A");
            ImGui::Text("Lighting Pass: N/A");
            ImGui::Text("Post Processing: N/A");

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
            // TODO: Get actual culling stats from render graph
            ImGui::Text("Frustum Culling: %s", graph->is_frustum_culling_enabled() ? "ON" : "OFF");
            ImGui::Text("Culled (Frustum): N/A");
            ImGui::Text("Visible: N/A");
            ImGui::Separator();
            ImGui::Text("Occlusion Culling: N/A");
            ImGui::Text("Culled (Occlusion): N/A");

            ImGui::End();
        }
    });
}

} // namespace gws::renderer::gpu
