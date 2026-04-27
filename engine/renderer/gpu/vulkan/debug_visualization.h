/**
 * @file debug_visualization.h
 * @brief Debug visualization for lights and physics (Phase 6 Week 23).
 */

#pragma once

#include <memory>
#include <glm/glm.hpp>

namespace schizo::scene {
class Scene;
}

namespace gws::renderer::gpu {

class UIManager;

/**
 * @class DebugVisualization
 * @brief Registry of debug visualization panels and rendering helpers.
 *
 * Provides:
 * - Light frustum visualization
 * - Physics AABB/collision visualization
 * - Gizmo rendering helpers
 */
class DebugVisualization {
public:
    static void register_all(UIManager* ui);
    static void register_light_viz_panel(UIManager* ui);
    static void register_physics_viz_panel(UIManager* ui);

    // Scene rendering helpers (would integrate with render graph)
    static void draw_aabb(const glm::vec3& min, const glm::vec3& max,
                         const glm::vec3& color);
    static void draw_sphere(const glm::vec3& center, float radius,
                           const glm::vec3& color);
    static void draw_frustum(const glm::mat4& view_proj, const glm::vec3& color);
};

} // namespace gws::renderer::gpu
