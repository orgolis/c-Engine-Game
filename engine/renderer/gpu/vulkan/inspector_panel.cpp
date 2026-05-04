/**
 * @file inspector_panel.cpp
 * @brief Inspector panel implementation.
 */

#include "inspector_panel.h"
#include "scene_hierarchy_panel.h"
#include "entity.h"
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace gws::renderer::gpu {

using schizo::scene::Entity;

struct InspectorPanel::Impl {
    std::shared_ptr<SceneHierarchyPanel> hierarchy;
};

InspectorPanel::InspectorPanel(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

std::unique_ptr<InspectorPanel> InspectorPanel::create(
    std::shared_ptr<SceneHierarchyPanel> hierarchy) {
    auto impl = std::make_unique<Impl>();
    impl->hierarchy = hierarchy;
    return std::make_unique<InspectorPanel>(std::move(impl));
}

void InspectorPanel::draw() {
    if (!impl_ || !impl_->hierarchy) return;

    auto selected_entity = impl_->hierarchy->get_selected_entity();

    ImGui::SetNextWindowPos(ImVec2(320, 220), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove)) {
        if (!selected_entity) {
            ImGui::TextDisabled("No entity selected");
        } else {
            draw_entity_info();
            ImGui::Separator();
            draw_transform_editor();
            ImGui::Separator();
            draw_components();
        }

        ImGui::End();
    }
}

void InspectorPanel::draw_entity_info() {
    auto entity = impl_->hierarchy->get_selected_entity();
    if (!entity) return;

    // Entity ID (read-only)
    ImGui::Text("ID: %u", entity->GetId());

    // Entity name (editable)
    static char name_buffer[256];
    strncpy(name_buffer, entity->GetName().c_str(), sizeof(name_buffer) - 1);
    name_buffer[sizeof(name_buffer) - 1] = '\0';

    if (ImGui::InputText("Name", name_buffer, sizeof(name_buffer))) {
        entity->SetName(name_buffer);
    }

    // Entity tag
    static char tag_buffer[256];
    strncpy(tag_buffer, entity->GetTag().c_str(), sizeof(tag_buffer) - 1);
    tag_buffer[sizeof(tag_buffer) - 1] = '\0';

    if (ImGui::InputText("Tag", tag_buffer, sizeof(tag_buffer))) {
        entity->SetTag(tag_buffer);
    }

    // Active toggle
    bool is_active = entity->IsActive();
    if (ImGui::Checkbox("Active", &is_active)) {
        entity->SetActive(is_active);
    }

    ImGui::SameLine();

    // Active in hierarchy (read-only)
    bool active_in_hierarchy = entity->IsActiveInHierarchy();
    ImGui::BeginDisabled();
    ImGui::Checkbox("Active in Hierarchy", &active_in_hierarchy);
    ImGui::EndDisabled();
}

void InspectorPanel::draw_transform_editor() {
    auto entity = impl_->hierarchy->get_selected_entity();
    if (!entity) return;

    auto* transform = entity->GetTransform();
    if (!transform) return;

    ImGui::Text("Transform");

    // Position
    glm::vec3 pos = transform->GetPosition();
    float pos_array[3] = {pos.x, pos.y, pos.z};
    if (ImGui::DragFloat3("Position", pos_array, 0.1f)) {
        transform->SetPosition(glm::vec3(pos_array[0], pos_array[1], pos_array[2]));
    }

    // Rotation (Euler angles)
    glm::vec3 rotation = transform->GetRotation();
    float rot_array[3] = {rotation.x, rotation.y, rotation.z};
    if (ImGui::DragFloat3("Rotation", rot_array, 1.0f)) {
        transform->SetRotation(glm::vec3(rot_array[0], rot_array[1], rot_array[2]));
    }

    // Scale
    glm::vec3 scale = transform->GetScale();
    float scale_array[3] = {scale.x, scale.y, scale.z};
    if (ImGui::DragFloat3("Scale", scale_array, 0.01f, 0.0f, 10.0f)) {
        transform->SetScale(glm::vec3(scale_array[0], scale_array[1], scale_array[2]));
    }

    // World position (read-only)
    glm::vec3 world_pos = transform->GetWorldPosition();
    ImGui::BeginDisabled();
    float world_pos_array[3] = {world_pos.x, world_pos.y, world_pos.z};
    ImGui::DragFloat3("World Position", world_pos_array, 0.1f);
    ImGui::EndDisabled();
}

void InspectorPanel::draw_components() {
    auto entity = impl_->hierarchy->get_selected_entity();
    if (!entity) return;

    ImGui::Text("Components");
    ImGui::Spacing();

    // Mesh component
    auto mesh_comp = entity->GetMeshComponent();
    if (mesh_comp && ImGui::TreeNodeEx("MeshComponent", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto mesh = mesh_comp->GetMesh();
        if (mesh) {
            ImGui::Text("Mesh: %s", mesh->GetName().c_str());
            ImGui::Text("Vertex Count: %u", mesh->GetVertexCount());
            ImGui::Text("Index Count: %u", mesh->GetIndexCount());
        } else {
            ImGui::TextDisabled("No mesh assigned");
        }

        ImGui::TreePop();
    }

    // Transform component
    auto transform = entity->GetTransform();
    if (transform && ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        glm::vec3 pos = transform->GetPosition();
        ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);

        glm::vec3 scale = transform->GetScale();
        ImGui::Text("Scale: (%.2f, %.2f, %.2f)", scale.x, scale.y, scale.z);

        ImGui::TreePop();
    }

    // Add component button
    ImGui::Spacing();
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("add_component_popup");
    }

    if (ImGui::BeginPopup("add_component_popup")) {
        ImGui::Text("Add Component:");
        ImGui::Separator();

        if (ImGui::MenuItem("Light")) {
            // TODO: Add light component
            spdlog::debug("Add light component to entity {}", entity->GetId());
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem("Animator")) {
            // TODO: Add animator component
            spdlog::debug("Add animator component to entity {}", entity->GetId());
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

} // namespace gws::renderer::gpu
