#include "transform_gizmo.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/vector_angle.hpp>

namespace schizo::editor {

TransformGizmo::TransformGizmo()
    : mode_(GizmoMode::Translate), selected_axis_(GizmoAxis::None),
      is_dragging_(false), delta_value_(0.0f) {}

void TransformGizmo::CycleMode() {
    switch (mode_) {
        case GizmoMode::Translate:
            mode_ = GizmoMode::Rotate;
            break;
        case GizmoMode::Rotate:
            mode_ = GizmoMode::Scale;
            break;
        case GizmoMode::Scale:
            mode_ = GizmoMode::Translate;
            break;
        default:
            mode_ = GizmoMode::Translate;
    }
}

void TransformGizmo::BeginDrag(GizmoAxis axis, const glm::vec2& mouse_pos) {
    is_dragging_ = true;
    selected_axis_ = axis;
    last_mouse_pos_ = mouse_pos;
    delta_value_ = glm::vec3(0.0f);
}

glm::vec3 TransformGizmo::UpdateDrag(const glm::vec2& mouse_pos, const glm::vec3& current_value) {
    if (!is_dragging_) return current_value;
    
    glm::vec2 delta = mouse_pos - last_mouse_pos_;
    float delta_distance = glm::length(delta) * drag_sensitivity_;
    if (delta.x < 0 || delta.y > 0) delta_distance *= -1.0f; // Move opposite direction
    
    glm::vec3 new_value = current_value;
    
    switch (selected_axis_) {
        case GizmoAxis::X:
            delta_value_ = glm::vec3(delta_distance, 0.0f, 0.0f);
            new_value.x += delta_distance;
            break;
        case GizmoAxis::Y:
            delta_value_ = glm::vec3(0.0f, delta_distance, 0.0f);
            new_value.y += delta_distance;
            break;
        case GizmoAxis::Z:
            delta_value_ = glm::vec3(0.0f, 0.0f, delta_distance);
            new_value.z += delta_distance;
            break;
        default:
            break;
    }
    
    last_mouse_pos_ = mouse_pos;
    return new_value;
}

void TransformGizmo::EndDrag() {
    is_dragging_ = false;
    selected_axis_ = GizmoAxis::None;
    delta_value_ = glm::vec3(0.0f);
}

} // namespace schizo::editor
