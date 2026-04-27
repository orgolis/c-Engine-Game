/**
 * @file inspector_panel.h
 * @brief Inspector panel for editing entity properties (Phase 6 Week 22).
 *
 * Displays properties of the selected entity with editable controls.
 * Uses reflection system to automatically generate UI from component metadata.
 * Integrates with transform gizmo for interactive 3D manipulation.
 */

#pragma once

#include <memory>
#include <glm/glm.hpp>

namespace schizo::scene {
class Entity;
}

namespace gws::renderer::gpu {

class SceneHierarchyPanel;

/**
 * @class InspectorPanel
 * @brief ImGui panel for inspecting and editing entity properties.
 *
 * Displays:
 * - Entity name and ID
 * - Transform (position, rotation, scale) with live gizmo feedback
 * - Active/inactive toggle
 * - Components list (mesh, light, etc.)
 * - Component-specific properties
 *
 * Usage:
 * ```cpp
 *   auto inspector = InspectorPanel::create(hierarchy_panel);
 *   ui->register_panel("inspector", [inspector]() {
 *       inspector->draw();
 *   });
 * ```
 */
class InspectorPanel {
public:
    InspectorPanel() = default;
    ~InspectorPanel() = default;

    InspectorPanel(const InspectorPanel&) = delete;
    InspectorPanel& operator=(const InspectorPanel&) = delete;

    /// Create an inspector panel connected to a scene hierarchy panel.
    /// The inspector automatically displays properties of the hierarchy's selected entity.
    static std::unique_ptr<InspectorPanel> create(
        std::shared_ptr<SceneHierarchyPanel> hierarchy);

    /// Draw the inspector panel.
    /// Call this from your UI panel draw function.
    void draw();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Internal methods
    void draw_entity_info();
    void draw_transform_editor();
    void draw_components();
    void draw_component_properties(std::shared_ptr<schizo::scene::Entity> entity);

    explicit InspectorPanel(std::unique_ptr<Impl> impl);
};

} // namespace gws::renderer::gpu
