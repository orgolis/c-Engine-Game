#pragma once

#include <glm/glm.hpp>
#include <cstdint>

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

private:
    GizmoMode mode_ = GizmoMode::Translate;
    GizmoAxis selected_axis_ = GizmoAxis::None;
    bool is_dragging_ = false;
    glm::vec2 last_mouse_pos_;
    glm::vec3 delta_value_;
    float drag_sensitivity_ = 0.01f;
};

} // namespace schizo::editor
