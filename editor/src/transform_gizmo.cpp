#include "transform_gizmo.h"
#include "simple_renderer.h"
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
    glm::vec3 new_value = current_value;
    
    // Use appropriate mouse axis based on gizmo axis being dragged
    // X-axis: use horizontal mouse movement
    // Y-axis: use vertical mouse movement (inverted, since screen Y increases downward)
    // Z-axis: use horizontal mouse movement (depth typically represented horizontally)
    switch (selected_axis_) {
        case GizmoAxis::X: {
            float delta_distance = delta.x * drag_sensitivity_;
            delta_value_ = glm::vec3(delta_distance, 0.0f, 0.0f);
            new_value.x += delta_distance;
            break;
        }
        case GizmoAxis::Y: {
            float delta_distance = -delta.y * drag_sensitivity_;  // Invert Y (screen coordinates)
            delta_value_ = glm::vec3(0.0f, delta_distance, 0.0f);
            new_value.y += delta_distance;
            break;
        }
        case GizmoAxis::Z: {
            // For Z axis, use X mouse movement with shift to disambiguate from X-axis
            // Without modifier, use horizontal movement; this gives intuitive depth control
            float delta_distance = delta.x * drag_sensitivity_;
            delta_value_ = glm::vec3(0.0f, 0.0f, delta_distance);
            new_value.z += delta_distance;
            break;
        }
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

void TransformGizmo::Render(SimpleRenderer* renderer, const glm::vec3& position, const glm::vec3& rotation,
                            const glm::vec3& scale, const glm::mat4& view, const glm::mat4& proj,
                            float gizmo_size) {
    if (!renderer || mode_ == GizmoMode::None) return;
    
    // Create model matrix for gizmo
    glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
    // Note: We'll ignore rotation for now - gizmos are typically world-aligned
    model = glm::scale(model, glm::vec3(gizmo_size));
    
    switch (mode_) {
        case GizmoMode::Translate: {
            // Render red X arrow
            glm::vec4 x_color(1.0f, 0.0f, 0.0f, 1.0f);
            if (hovered_axis_ == GizmoAxis::X || selected_axis_ == GizmoAxis::X) {
                x_color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow highlight
            }
            Mesh x_arrow = renderer->CreateTranslationGizmoMesh('x');
            renderer->RenderMesh(x_arrow, model, view, proj, x_color);
            
            // Render green Y arrow
            glm::vec4 y_color(0.0f, 1.0f, 0.0f, 1.0f);
            if (hovered_axis_ == GizmoAxis::Y || selected_axis_ == GizmoAxis::Y) {
                y_color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow highlight
            }
            Mesh y_arrow = renderer->CreateTranslationGizmoMesh('y');
            renderer->RenderMesh(y_arrow, model, view, proj, y_color);
            
            // Render blue Z arrow
            glm::vec4 z_color(0.0f, 0.0f, 1.0f, 1.0f);
            if (hovered_axis_ == GizmoAxis::Z || selected_axis_ == GizmoAxis::Z) {
                z_color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow highlight
            }
            Mesh z_arrow = renderer->CreateTranslationGizmoMesh('z');
            renderer->RenderMesh(z_arrow, model, view, proj, z_color);
            break;
        }
        
        case GizmoMode::Rotate: {
            // Render rotation rings (from rotation gizmo mesh)
            glm::vec4 red(1.0f, 0.0f, 0.0f, 1.0f);
            glm::vec4 green(0.0f, 1.0f, 0.0f, 1.0f);
            glm::vec4 blue(0.0f, 0.0f, 1.0f, 1.0f);
            
            // Highlight on hover
            if (selected_axis_ == GizmoAxis::X) red = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            if (selected_axis_ == GizmoAxis::Y) green = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            if (selected_axis_ == GizmoAxis::Z) blue = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            
            renderer->RenderMesh(renderer->rotation_gizmo_mesh, model, view, proj, red);
            break;
        }
        
        case GizmoMode::Scale: {
            // Render scale handles (cubes on each axis)
            glm::vec4 x_color(1.0f, 0.0f, 0.0f, 1.0f);
            if (selected_axis_ == GizmoAxis::X) {
                x_color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            }
            Mesh x_scale = renderer->CreateScaleGizmoMesh('x');
            renderer->RenderMesh(x_scale, model, view, proj, x_color);
            
            glm::vec4 y_color(0.0f, 1.0f, 0.0f, 1.0f);
            if (selected_axis_ == GizmoAxis::Y) {
                y_color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            }
            Mesh y_scale = renderer->CreateScaleGizmoMesh('y');
            renderer->RenderMesh(y_scale, model, view, proj, y_color);
            
            glm::vec4 z_color(0.0f, 0.0f, 1.0f, 1.0f);
            if (selected_axis_ == GizmoAxis::Z) {
                z_color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            }
            Mesh z_scale = renderer->CreateScaleGizmoMesh('z');
            renderer->RenderMesh(z_scale, model, view, proj, z_color);
            break;
        }
        
        default:
            break;
    }
}

GizmoAxis TransformGizmo::CheckAxisProximity(const glm::vec2& mouse_pos, const glm::vec2& viewport_size,
                                             const glm::vec3& camera_pos, const glm::vec3& gizmo_pos,
                                             const glm::mat4& view, const glm::mat4& proj) {
    // Convert normalized mouse coordinates to screen space
    glm::vec2 mouse_screen = mouse_pos * viewport_size;
    
    // Project gizmo position to screen space
    glm::vec4 gizmo_screen = proj * view * glm::vec4(gizmo_pos, 1.0f);
    gizmo_screen /= gizmo_screen.w;
    glm::vec2 gizmo_2d((gizmo_screen.x + 1.0f) * 0.5f * viewport_size.x,
                       (1.0f - gizmo_screen.y) * 0.5f * viewport_size.y);
    
    // Define axis directions in world space
    glm::vec3 x_axis(1.0f, 0.0f, 0.0f);
    glm::vec3 y_axis(0.0f, 1.0f, 0.0f);
    glm::vec3 z_axis(0.0f, 0.0f, 1.0f);
    
    // Project axis endpoints to screen space (2 units away in each direction)
    auto project_axis_end = [&](const glm::vec3& axis_dir) -> glm::vec2 {
        glm::vec3 end_pos = gizmo_pos + axis_dir * 2.0f;
        glm::vec4 end_screen = proj * view * glm::vec4(end_pos, 1.0f);
        end_screen /= end_screen.w;
        return glm::vec2((end_screen.x + 1.0f) * 0.5f * viewport_size.x,
                        (1.0f - end_screen.y) * 0.5f * viewport_size.y);
    };
    
    glm::vec2 x_end = project_axis_end(x_axis);
    glm::vec2 y_end = project_axis_end(y_axis);
    glm::vec2 z_end = project_axis_end(z_axis);
    
    // Calculate distance from mouse to each axis line
    const float threshold = 10.0f;  // Pixel distance threshold
    
    auto point_to_line_distance = [](glm::vec2 p, glm::vec2 a, glm::vec2 b) -> float {
        glm::vec2 pa = p - a;
        glm::vec2 ba = b - a;
        float t = glm::dot(pa, ba) / glm::dot(ba, ba);
        t = glm::clamp(t, 0.0f, 1.0f);
        return glm::length(pa - ba * t);
    };
    
    float x_dist = point_to_line_distance(mouse_screen, gizmo_2d, x_end);
    float y_dist = point_to_line_distance(mouse_screen, gizmo_2d, y_end);
    float z_dist = point_to_line_distance(mouse_screen, gizmo_2d, z_end);
    
    // Return the closest axis within threshold
    if (x_dist < y_dist && x_dist < z_dist && x_dist < threshold) {
        hovered_axis_ = GizmoAxis::X;
        return GizmoAxis::X;
    } else if (y_dist < z_dist && y_dist < threshold) {
        hovered_axis_ = GizmoAxis::Y;
        return GizmoAxis::Y;
    } else if (z_dist < threshold) {
        hovered_axis_ = GizmoAxis::Z;
        return GizmoAxis::Z;
    }
    
    hovered_axis_ = GizmoAxis::None;
    return GizmoAxis::None;
}

} // namespace schizo::editor
