#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <cstdint>

// Forward declarations
namespace engine::character {
    class CharacterController;
    class CharacterStats;
}

namespace schizo::editor {

/**
 * @struct CharacterControllerState
 * @brief Editor state for character controller properties
 */
struct CharacterControllerState {
    // Movement parameters
    float walk_speed = 5.0f;
    float sprint_speed = 10.0f;
    float jump_force = 15.0f;
    float stamina_drain_rate = 20.0f;
    float stamina_regen_rate = 10.0f;
    
    // Ground detection
    float ground_check_distance = 0.5f;
    float slope_limit = 45.0f;
    
    // Display flags
    bool show_velocity_debug = false;
    bool show_state_name = true;
    bool show_ground_detection = false;
};

/**
 * @class CharacterControllerPanel
 * @brief Inspector panel for character controller editing and debugging
 * 
 * Features:
 * - Movement speed and jump force configuration
 * - Character stats display (health, stamina, armor)
 * - Current state visualization
 * - Viewport debug overlays
 * - Locomotion animation blending
 * - Input buffer status display
 */
class CharacterControllerPanel {
public:
    CharacterControllerPanel();
    ~CharacterControllerPanel();
    
    /**
     * Render inspector panel for character controller
     * @param controller Pointer to character controller to edit (optional)
     */
    void Render(engine::character::CharacterController* controller);
    
    /**
     * Render viewport debug overlays
     * @param controller Pointer to character controller
     * @param viewport_width Width of viewport
     * @param viewport_height Height of viewport
     */
    void RenderViewportDebug(engine::character::CharacterController* controller, 
                            int viewport_width, int viewport_height);
    
    /**
     * Get current editor state
     */
    const CharacterControllerState& GetState() const { return state_; }
    
    /**
     * Apply editor state to character controller
     * @param controller Character controller to apply changes to
     */
    void ApplyState(engine::character::CharacterController* controller);
    
    /**
     * Debug tools: Edit character stats directly
     */
    void RenderStatsDebugEditor(engine::character::CharacterStats* stats);
    
    /**
     * Debug tools: State reset and clear
     */
    void RenderDebugControls(engine::character::CharacterController* controller);

private:
    CharacterControllerState state_;
    
    // Helper methods
    void RenderMovementProperties();
    void RenderCharacterStats(engine::character::CharacterStats* stats);
    void RenderCurrentState(engine::character::CharacterController* controller);
    void RenderInputBufferStatus(engine::character::CharacterController* controller);
    void RenderLocomotionBlending(engine::character::CharacterController* controller);
    void RenderDebugOptions();
};

} // namespace schizo::editor
