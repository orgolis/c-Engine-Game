#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <memory>

namespace gws {

// Forward declarations
class Entity;
class Camera;
class Device;

// Transform Gizmo: 3D manipulators for transform editing in viewport
// Features:
//   - Translate (XYZ axes + plane constraints)
//   - Rotate (ring gizmo around each axis)
//   - Scale (box manipulators at corners)
//   - Mouse picking for axis/plane selection
//   - Real-time visual feedback
//
// Integration:
//   - Rendered on top of scene in deferred pipeline
//   - Mouse input routing via UIManager
//   - Transform applied directly to entity

class TransformGizmo {
public:
    enum class Mode {
        Translate,
        Rotate,
        Scale,
        None
    };

    enum class Axis {
        X,  // Red
        Y,  // Green
        Z,  // Blue
        XY, // YZ plane
        XZ, // XZ plane
        YZ, // XY plane
        None
    };

    struct Config {
        float axis_length = 0.3f;       // Length of axis arrows
        float axis_thickness = 0.01f;   // Line thickness
        float hover_scale = 1.2f;       // Scale when mouse hovers
        bool show_gizmo = true;
    };

    TransformGizmo(Device* device, const Config& cfg = Config{});
    ~TransformGizmo();

    // Initialize gizmo rendering resources
    void initialize();

    // Update gizmo position to entity transform
    void set_target_entity(Entity* entity) { target_entity_ = entity; }
    Entity* get_target_entity() const { return target_entity_; }

    // Set gizmo mode (translate/rotate/scale)
    void set_mode(Mode mode) { mode_ = mode; }
    Mode get_mode() const { return mode_; }

    // Mouse interaction
    bool handle_mouse_down(glm::vec2 screen_pos, const Camera* camera);
    bool handle_mouse_move(glm::vec2 screen_pos, const Camera* camera);
    bool handle_mouse_up();

    // Render gizmo overlay (called from deferred render stage)
    void render(VkCommandBuffer cmd, const Camera* camera);

    // Get current transform after manipulation
    glm::mat4 get_local_transform() const;

    // Visibility
    void show() { config_.show_gizmo = true; }
    void hide() { config_.show_gizmo = false; }
    bool is_visible() const { return config_.show_gizmo && target_entity_ != nullptr; }

private:
    Device* device_;
    Config config_;
    Entity* target_entity_ = nullptr;
    Mode mode_ = Mode::Translate;
    Axis hovered_axis_ = Axis::None;
    Axis selected_axis_ = Axis::None;
    bool is_dragging_ = false;

    // Gizmo state during manipulation
    glm::vec3 gizmo_origin_;         // Center of gizmo in world space
    glm::vec3 drag_start_world_;     // World position at drag start
    glm::vec3 drag_start_local_;     // Local transform at drag start
    glm::mat4 drag_start_matrix_;    // Full transform at drag start

    // Rendering resources (deferred to Phase 6+ full implementation)
    VkPipeline gizmo_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout gizmo_layout_ = VK_NULL_HANDLE;

    // Helper functions
    Axis pick_axis(glm::vec2 screen_pos, const Camera* camera);
    void apply_translation(glm::vec2 screen_pos, const Camera* camera);
    void apply_rotation(glm::vec2 screen_pos, const Camera* camera);
    void apply_scale(glm::vec2 screen_pos, const Camera* camera);
    void render_axis_arrow(VkCommandBuffer cmd, glm::vec3 origin, glm::vec3 direction, 
                          glm::vec3 color, bool hovered);
    void render_rotation_ring(VkCommandBuffer cmd, glm::vec3 origin, Axis axis, 
                             glm::vec3 color, bool hovered);
};

}  // namespace gws
