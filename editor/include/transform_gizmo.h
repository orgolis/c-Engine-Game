#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// Forward declarations
namespace schizo::editor {
    class SimpleRenderer;
}

namespace schizo::editor {

enum class GizmoMode {
    None = 0,
    Translate = 1,
    Rotate = 2,
    Scale = 3
};

enum class GizmoAxis {
    None = 0,
    X = 1,
    Y = 2,
    Z = 4
};

/**
 * @class TransformGizmo
 * @brief Handles transform gizmo interaction and visualization
 */
class TransformGizmo {
public:
    TransformGizmo();
    
    /**
     * Set current gizmo mode
     */
    void SetMode(GizmoMode mode) { mode_ = mode; }
    GizmoMode GetMode() const { return mode_; }
    
    /**
     * Cycle through gizmo modes (T for translate, R for rotate, S for scale)
     */
    void CycleMode();
    
    /**
     * Check if dragging gizmo
     */
    bool IsDragging() const { return is_dragging_; }
    
    /**
     * Get currently selected axis
     */
    GizmoAxis GetSelectedAxis() const { return selected_axis_; }
    
    /**
     * Start dragging on an axis
     */
    void BeginDrag(GizmoAxis axis, const glm::vec2& mouse_pos);
    
    /**
     * Update drag
     */
    glm::vec3 UpdateDrag(const glm::vec2& mouse_pos, const glm::vec3& current_value);
    
    /**
     * End dragging
     */
    void EndDrag();
    
    /**
     * Get delta accumulated during drag
     */
    glm::vec3 GetDeltaValue() const { return delta_value_; }
    
    /**
     * @brief Render the gizmo in the 3D viewport
     * @param renderer The renderer to use
     * @param position Gizmo position in world space
     * @param rotation Gizmo rotation in world space
     * @param scale Gizmo scale
     * @param view View matrix
     * @param proj Projection matrix
     * @param gizmo_size Size of the gizmo in units
     */
    void Render(SimpleRenderer* renderer, const glm::vec3& position, const glm::vec3& rotation,
                const glm::vec3& scale, const glm::mat4& view, const glm::mat4& proj,
                float gizmo_size = 2.0f);
    
    /**
     * @brief Check mouse proximity to gizmo axis and set hover state
     * @param mouse_pos Normalized mouse position (in viewport space)
     * @param viewport_size Viewport size in pixels
     * @param camera_pos Camera position
     * @param gizmo_pos Gizmo position
     * @param view View matrix
     * @param proj Projection matrix
     * @return The axis closest to the mouse, or None if no close axis
     */
    GizmoAxis CheckAxisProximity(const glm::vec2& mouse_pos, const glm::vec2& viewport_size,
                                const glm::vec3& camera_pos, const glm::vec3& gizmo_pos,
                                const glm::mat4& view, const glm::mat4& proj);
    
    /**
     * @brief Get the hovered axis (for visual feedback)
     */
    GizmoAxis GetHoveredAxis() const { return hovered_axis_; }

private:
    GizmoMode mode_ = GizmoMode::Translate;
    GizmoAxis selected_axis_ = GizmoAxis::None;
    GizmoAxis hovered_axis_ = GizmoAxis::None;
    bool is_dragging_ = false;
    glm::vec2 last_mouse_pos_;
    glm::vec3 delta_value_;
    float drag_sensitivity_ = 0.01f;
};

} // namespace schizo::editor
