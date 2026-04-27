/**
 * @file debug_visualization.cpp
 * @brief Debug visualization implementation.
 */

#include "debug_visualization.h"
#include "ui_manager.h"
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace gws::renderer::gpu {

void DebugVisualization::register_all(UIManager* ui) {
    if (!ui) return;
    register_light_viz_panel(ui);
    register_physics_viz_panel(ui);
}

void DebugVisualization::register_light_viz_panel(UIManager* ui) {
    if (!ui) return;

    ui->register_panel("light_visualization", []() {
        ImGui::SetNextWindowPos(ImVec2(730, 220), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Light Visualization", nullptr, ImGuiWindowFlags_NoMove)) {
            ImGui::Text("Light Debug Visualization");
            ImGui::Separator();

            static bool show_directional_frustum = true;
            static bool show_point_volumes = true;
            static bool show_spot_cones = true;

            ImGui::Checkbox("Show Directional Frustum", &show_directional_frustum);
            ImGui::Checkbox("Show Point Volumes", &show_point_volumes);
            ImGui::Checkbox("Show Spot Cones", &show_spot_cones);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Visualization Options:");

            static float frustum_opacity = 0.3f;
            ImGui::SliderFloat("Frustum Opacity", &frustum_opacity, 0.0f, 1.0f);

            static float volume_opacity = 0.2f;
            ImGui::SliderFloat("Volume Opacity", &volume_opacity, 0.0f, 1.0f);

            ImGui::Spacing();
            ImGui::TextDisabled("(Rendering integration deferred to Week 24)");

            ImGui::End();
        }
    });
}

void DebugVisualization::register_physics_viz_panel(UIManager* ui) {
    if (!ui) return;

    ui->register_panel("physics_visualization", []() {
        ImGui::SetNextWindowPos(ImVec2(730, 530), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Physics Visualization", nullptr, ImGuiWindowFlags_NoMove)) {
            ImGui::Text("Physics Debug Visualization");
            ImGui::Separator();

            static bool show_rigidbodies = true;
            static bool show_colliders = true;
            static bool show_contacts = true;
            static bool show_constraints = false;

            ImGui::Checkbox("Show Rigidbodies (AABB)", &show_rigidbodies);
            ImGui::Checkbox("Show Colliders", &show_colliders);
            ImGui::Checkbox("Show Contacts", &show_contacts);
            ImGui::Checkbox("Show Constraints", &show_constraints);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Visualization Options:");

            static float aabb_opacity = 0.2f;
            ImGui::SliderFloat("AABB Opacity", &aabb_opacity, 0.0f, 1.0f);

            static bool highlight_contacts = true;
            ImGui::Checkbox("Highlight Contact Points", &highlight_contacts);

            ImGui::Spacing();
            ImGui::TextDisabled("(Rendering integration deferred to Week 24)");

            ImGui::End();
        }
    });
}

void DebugVisualization::draw_aabb(const glm::vec3& min, const glm::vec3& max,
                                   const glm::vec3& color) {
    // TODO: Render AABB wireframe
    // Would integrate with render graph's debug drawing stage
    spdlog::debug("Draw AABB: min={},{},{} max={},{},{} color={},{},{}",
                  min.x, min.y, min.z, max.x, max.y, max.z,
                  color.x, color.y, color.z);
}

void DebugVisualization::draw_sphere(const glm::vec3& center, float radius,
                                     const glm::vec3& color) {
    // TODO: Render sphere wireframe
    spdlog::debug("Draw Sphere: center={},{},{} radius={} color={},{},{}",
                  center.x, center.y, center.z, radius,
                  color.x, color.y, color.z);
}

void DebugVisualization::draw_frustum(const glm::mat4& view_proj,
                                      const glm::vec3& color) {
    // TODO: Render frustum edges
    spdlog::debug("Draw Frustum: color={},{},{}", color.x, color.y, color.z);
}

} // namespace gws::renderer::gpu
