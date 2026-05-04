/**
 * @file scene_hierarchy_panel.cpp
 * @brief Scene hierarchy panel implementation.
 */

#include "scene_hierarchy_panel.h"
#include "scene.h"
#include "entity.h"
#include <imgui.h>
#include <spdlog/spdlog.h>

namespace gws::renderer::gpu {

using schizo::scene::Scene;
using schizo::scene::Entity;

std::unique_ptr<SceneHierarchyPanel> SceneHierarchyPanel::create() {
    return std::make_unique<SceneHierarchyPanel>();
}

std::vector<std::shared_ptr<SceneHierarchyPanel::EntityTreeNode>>
SceneHierarchyPanel::build_tree(std::shared_ptr<Scene> scene) {
    if (!scene) return {};

    // Build a map of all entities
    std::unordered_map<uint32_t, std::shared_ptr<EntityTreeNode>> entity_nodes;

    for (const auto& entity : scene->GetEntities()) {
        auto node = std::make_shared<EntityTreeNode>();
        node->entity = entity;
        entity_nodes[entity->GetId()] = node;
    }

    // Connect parent-child relationships
    std::vector<std::shared_ptr<EntityTreeNode>> root_nodes;
    for (const auto& entity : scene->GetEntities()) {
        auto parent = entity->GetParent();
        if (!parent) {
            // Root node
            root_nodes.push_back(entity_nodes[entity->GetId()]);
        } else {
            // Add as child to parent node
            auto parent_node = entity_nodes[parent->GetId()];
            if (parent_node) {
                parent_node->children.push_back(entity_nodes[entity->GetId()]);
            }
        }
    }

    return root_nodes;
}

void SceneHierarchyPanel::draw(std::shared_ptr<Scene> scene) {
    if (!scene) return;

    ImGui::SetNextWindowPos(ImVec2(10, 220), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scene Hierarchy", nullptr, ImGuiWindowFlags_NoMove)) {
        // Scene name header
        ImGui::Text("Scene: %s", scene->GetName().c_str());
        ImGui::Separator();

        // Entity count
        ImGui::Text("Entities: %u", scene->GetEntityCount());
        ImGui::Spacing();

        // Draw all root entities
        auto root_nodes = build_tree(scene);
        for (const auto& node : root_nodes) {
            draw_entity_node(node);
        }

        ImGui::End();
    }
}

void SceneHierarchyPanel::draw_entity_node(const std::shared_ptr<EntityTreeNode>& node) {
    if (!node || !node->entity) return;

    const auto& entity = node->entity;
    uint32_t entity_id = entity->GetId();
    bool is_expanded = expanded_nodes_.count(entity_id) > 0;
    bool has_children = !node->children.empty();

    // Determine node flags
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (!has_children) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (selected_entity_ && selected_entity_->GetId() == entity_id) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Entity icon (simplified - use emoji for now)
    std::string display_name = entity->GetName();
    if (entity->GetMeshComponent()->GetMesh()) {
        display_name = "📦 " + display_name;  // Mesh icon
    } else {
        display_name = "⬜ " + display_name;  // Empty entity icon
    }

    // Draw tree node
    bool node_open = ImGui::TreeNodeEx(
        reinterpret_cast<void*>(entity_id),
        flags,
        "%s",
        display_name.c_str()
    );

    // Handle selection
    if (ImGui::IsItemClicked()) {
        selected_entity_ = entity;
    }

    // Context menu
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        context_menu_entity_ = entity;
        ImGui::OpenPopup("entity_context_menu");
    }

    // Entity context menu
    if (ImGui::BeginPopup("entity_context_menu")) {
        if (context_menu_entity_ && context_menu_entity_->GetId() == entity_id) {
            draw_entity_context_menu(context_menu_entity_);
        }
        ImGui::EndPopup();
    }

    // Draw children
    if (node_open) {
        for (const auto& child_node : node->children) {
            draw_entity_node(child_node);
        }
        ImGui::TreePop();

        expanded_nodes_.insert(entity_id);
    } else {
        expanded_nodes_.erase(entity_id);
    }
}

void SceneHierarchyPanel::draw_entity_context_menu(
    std::shared_ptr<Entity> entity) {
    if (!entity) return;

    if (ImGui::MenuItem("Rename", nullptr)) {
        // TODO: Open rename dialog
        spdlog::debug("Rename entity: {}", entity->GetName());
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Delete", nullptr)) {
        // TODO: Delete entity from scene
        spdlog::debug("Delete entity: {}", entity->GetName());
    }

    if (ImGui::MenuItem("Duplicate", nullptr)) {
        // TODO: Duplicate entity
        spdlog::debug("Duplicate entity: {}", entity->GetName());
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Deactivate", nullptr)) {
        if (entity->IsActive()) {
            entity->SetActive(false);
        }
    }

    if (ImGui::MenuItem("Activate", nullptr)) {
        if (!entity->IsActive()) {
            entity->SetActive(true);
        }
    }
}

std::shared_ptr<Entity> SceneHierarchyPanel::get_selected_entity() const {
    return selected_entity_;
}

void SceneHierarchyPanel::set_selected_entity(std::shared_ptr<Entity> entity) {
    selected_entity_ = entity;
}

bool SceneHierarchyPanel::is_expanded(uint32_t entity_id) const {
    return expanded_nodes_.count(entity_id) > 0;
}

void SceneHierarchyPanel::toggle_expanded(uint32_t entity_id) {
    if (expanded_nodes_.count(entity_id)) {
        expanded_nodes_.erase(entity_id);
    } else {
        expanded_nodes_.insert(entity_id);
    }
}

void SceneHierarchyPanel::reset() {
    selected_entity_.reset();
    expanded_nodes_.clear();
    context_menu_entity_.reset();
}

} // namespace gws::renderer::gpu
