#include "transform_gizmo.h"
#include "../device.h"
#include "../../scene/include/entity.h"
#include "../camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace gws {

TransformGizmo::TransformGizmo(Device* device, const Config& cfg)
    : device_(device), config_(cfg) {
}

TransformGizmo::~TransformGizmo() {
    if (gizmo_pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_->get_device(), gizmo_pipeline_, nullptr);
    }
    if (gizmo_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_->get_device(), gizmo_layout_, nullptr);
    }
}

void TransformGizmo::initialize() {
    // Create rendering pipeline for gizmo
    // Deferred: full pipeline setup with shaders
    // For now, infrastructure in place
}

bool TransformGizmo::handle_mouse_down(glm::vec2 screen_pos, const Camera* camera) {
    if (!is_visible() || !camera) return false;

    // Pick axis under mouse
    Axis picked = pick_axis(screen_pos, camera);
    if (picked == Axis::None) return false;

    selected_axis_ = picked;
    is_dragging_ = true;
    drag_start_world_ = glm::vec3(0);  // Computed from screen_pos
    drag_start_local_ = target_entity_->GetTransform().GetPosition();
    drag_start_matrix_ = target_entity_->GetTransform().GetMatrix();

    return true;  // Consumed input
}

bool TransformGizmo::handle_mouse_move(glm::vec2 screen_pos, const Camera* camera) {
    if (!is_visible() || !camera) return false;

    if (!is_dragging_) {
        // Update hover
        hovered_axis_ = pick_axis(screen_pos, camera);
        return false;  // Not consuming
    }

    // Apply transformation based on mode
    switch (mode_) {
        case Mode::Translate:
            apply_translation(screen_pos, camera);
            break;
        case Mode::Rotate:
            apply_rotation(screen_pos, camera);
            break;
        case Mode::Scale:
            apply_scale(screen_pos, camera);
            break;
        default:
            break;
    }

    return true;  // Consuming input
}

bool TransformGizmo::handle_mouse_up() {
    if (!is_dragging_) return false;

    is_dragging_ = false;
    selected_axis_ = Axis::None;
    return true;  // Consumed input
}

TransformGizmo::Axis TransformGizmo::pick_axis(glm::vec2 screen_pos, const Camera* camera) {
    if (!target_entity_ || !camera) return Axis::None;

    // Get gizmo center in world space
    gizmo_origin_ = target_entity_->GetTransform().GetPosition();

    // Project axis endpoints to screen space
    // X axis: (gizmo_origin, gizmo_origin + X * length)
    // Y axis: (gizmo_origin, gizmo_origin + Y * length)
    // Z axis: (gizmo_origin, gizmo_origin + Z * length)

    glm::vec3 x_end = gizmo_origin_ + glm::vec3(config_.axis_length, 0, 0);
    glm::vec3 y_end = gizmo_origin_ + glm::vec3(0, config_.axis_length, 0);
    glm::vec3 z_end = gizmo_origin_ + glm::vec3(0, 0, config_.axis_length);

    // Project to screen (deferred: use camera's project function)
    // glm::vec4 x_proj = camera->project(x_end);
    // glm::vec2 x_screen = glm::vec2(x_proj.x, x_proj.y);
    // ... similar for Y, Z

    // Ray-line distance testing (pick closest)
    // For now, return placeholder
    return Axis::None;
}

void TransformGizmo::apply_translation(glm::vec2 screen_pos, const Camera* camera) {
    if (!target_entity_) return;

    // Constrain movement to selected axis/plane
    // Project screen_pos onto world plane perpendicular to camera direction
    // Deferred: full raycasting implementation
    
    // Placeholder: simple offset based on screen movement
    // Real implementation would use geometric raycasting
}

void TransformGizmo::apply_rotation(glm::vec2 screen_pos, const Camera* camera) {
    if (!target_entity_) return;

    // Rotate around selected axis
    // Rotation axis: X, Y, or Z in world space
    // Angle: computed from mouse delta
    // Deferred: full rotation calculation
}

void TransformGizmo::apply_scale(glm::vec2 screen_pos, const Camera* camera) {
    if (!target_entity_) return;

    // Scale in selected direction
    // If corner manipulator: uniform scale
    // If axis manipulator: scale along that axis
    // Deferred: full scale calculation
}

void TransformGizmo::render(VkCommandBuffer cmd, const Camera* camera) {
    if (!is_visible() || !target_entity_) return;

    gizmo_origin_ = target_entity_->GetTransform().GetPosition();

    // Render three axis arrows
    render_axis_arrow(cmd, gizmo_origin_, glm::vec3(1, 0, 0), glm::vec3(1, 0, 0), 
                     hovered_axis_ == Axis::X);
    render_axis_arrow(cmd, gizmo_origin_, glm::vec3(0, 1, 0), glm::vec3(0, 1, 0), 
                     hovered_axis_ == Axis::Y);
    render_axis_arrow(cmd, gizmo_origin_, glm::vec3(0, 0, 1), glm::vec3(0, 0, 1), 
                     hovered_axis_ == Axis::Z);

    // Render constraint planes (if mode supports)
    if (mode_ == Mode::Translate) {
        // Draw translucent planes for XY, XZ, YZ constraints
        // Deferred: plane rendering
    }

    // Render rotation rings (if in rotation mode)
    if (mode_ == Mode::Rotate) {
        render_rotation_ring(cmd, gizmo_origin_, Axis::X, glm::vec3(1, 0, 0), 
                            hovered_axis_ == Axis::X);
        render_rotation_ring(cmd, gizmo_origin_, Axis::Y, glm::vec3(0, 1, 0), 
                            hovered_axis_ == Axis::Y);
        render_rotation_ring(cmd, gizmo_origin_, Axis::Z, glm::vec3(0, 0, 1), 
                            hovered_axis_ == Axis::Z);
    }
}

void TransformGizmo::render_axis_arrow(VkCommandBuffer cmd, glm::vec3 origin, 
                                     glm::vec3 direction, glm::vec3 color, bool hovered) {
    // Render arrow along direction
    // Deferred: implement with line/cone primitives
    // For now: placeholder
}

void TransformGizmo::render_rotation_ring(VkCommandBuffer cmd, glm::vec3 origin, 
                                         Axis axis, glm::vec3 color, bool hovered) {
    // Render rotation ring for axis
    // Deferred: implement with circle/torus primitives
    // For now: placeholder
}

glm::mat4 TransformGizmo::get_local_transform() const {
    if (!target_entity_) {
        return glm::mat4(1.0f);
    }
    return target_entity_->GetTransform().GetMatrix();
}

}  // namespace gws
