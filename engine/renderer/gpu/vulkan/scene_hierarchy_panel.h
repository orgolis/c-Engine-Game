/**
 * @file scene_hierarchy_panel.h
 * @brief Scene hierarchy tree visualization panel (Phase 6 Week 22).
 *
 * Displays a tree view of all entities in the scene with:
 * - Parent/child hierarchy visualization
 * - Node expand/collapse
 * - Entity selection and highlighting
 * - Drag-drop entity reparenting (Week 22 extension)
 * - Context menu (delete, duplicate, etc.)
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_set>

namespace schizo::scene {
class Scene;
class Entity;
}

namespace gws::renderer::gpu {

/**
 * @class SceneHierarchyPanel
 * @brief ImGui panel for visualizing and manipulating scene hierarchy.
 *
 * Displays all entities in a tree view with selection support.
 * Tracks which nodes are expanded, which entity is selected.
 * Allows renaming, deletion, and parenting operations.
 *
 * Usage:
 * ```cpp
 *   auto hierarchy = SceneHierarchyPanel::create();
 *   ui->register_panel("scene_hierarchy", [hierarchy]() {
 *       hierarchy->draw(scene);
 *   });
 *
 *   // Get selected entity
 *   if (auto selected = hierarchy->get_selected_entity()) {
 *       // Show in inspector
 *   }
 * ```
 */
class SceneHierarchyPanel {
public:
    SceneHierarchyPanel() = default;
    ~SceneHierarchyPanel() = default;

    SceneHierarchyPanel(const SceneHierarchyPanel&) = delete;
    SceneHierarchyPanel& operator=(const SceneHierarchyPanel&) = delete;

    /// Create a scene hierarchy panel instance.
    static std::unique_ptr<SceneHierarchyPanel> create();

    /// Draw the hierarchy panel for a given scene.
    /// Must be called within an ImGui window context.
    void draw(std::shared_ptr<schizo::scene::Scene> scene);

    /// Get the currently selected entity (nullptr if none selected).
    std::shared_ptr<schizo::scene::Entity> get_selected_entity() const;

    /// Set the selected entity by ID.
    void set_selected_entity(std::shared_ptr<schizo::scene::Entity> entity);

    /// Check if an entity is expanded in the tree.
    bool is_expanded(uint32_t entity_id) const;

    /// Toggle expansion state of an entity node.
    void toggle_expanded(uint32_t entity_id);

    /// Clear all selections and expanded states.
    void reset();

private:
    struct EntityTreeNode {
        std::shared_ptr<schizo::scene::Entity> entity;
        std::vector<std::shared_ptr<EntityTreeNode>> children;
    };

    // Build tree structure from flat entity list
    std::vector<std::shared_ptr<EntityTreeNode>> build_tree(
        std::shared_ptr<schizo::scene::Scene> scene);

    // Recursively draw entity tree node
    void draw_entity_node(const std::shared_ptr<EntityTreeNode>& node);

    // Draw context menu for entity
    void draw_entity_context_menu(std::shared_ptr<schizo::scene::Entity> entity);

    std::shared_ptr<schizo::scene::Entity> selected_entity_;
    std::unordered_set<uint32_t> expanded_nodes_;

    // Context menu state
    std::shared_ptr<schizo::scene::Entity> context_menu_entity_;
};

} // namespace gws::renderer::gpu
