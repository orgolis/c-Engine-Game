#pragma once

#include <memory>
#include <cstdint>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::character {
    class CharacterController;
}

namespace schizo::scene {
    class Entity;
    class Scene;
}

namespace schizo::physics {
    class PhysicsWorld;   // Jolt-backed (Stage 4)
}

namespace schizo::editor {

/**
 * @class ScenePlaybackManager
 * @brief Manages scene playback with character controller integration
 * 
 * Features:
 * - Play/pause/stop scene execution
 * - Player entity detection and setup
 * - Character controller attachment
 * - Camera following player
 * - Game loop time management
 */
class ScenePlaybackManager {
public:
    ScenePlaybackManager();
    ~ScenePlaybackManager();
    
    /**
     * Start playing the scene
     * @param scene Scene to play
     * @return true if playback started successfully
     */
    bool StartPlayback(std::shared_ptr<schizo::scene::Scene> scene);
    
    /**
     * Stop playing the scene
     */
    void StopPlayback();
    
    /**
     * Pause/resume playback
     */
    void SetPaused(bool paused) { is_paused_ = paused; }
    
    /**
     * Update scene during playback (called each frame)
     * @param delta_time Time elapsed since last update
     */
    void Update(float delta_time);
    
    /**
     * Check if scene is currently playing
     */
    bool IsPlaying() const { return is_playing_; }
    
    /**
     * Check if scene is paused
     */
    bool IsPaused() const { return is_paused_; }
    
    /**
     * Get the player entity (if any)
     */
    schizo::scene::Entity* GetPlayerEntity() const { return player_entity_.get(); }
    
    /**
     * Get the character controller for the player
     */
    engine::character::CharacterController* GetPlayerController() const 
    { return player_controller_.get(); }
    
    /**
     * Get total playback time
     */
    float GetPlaybackTime() const { return playback_time_; }
    
    /**
     * Get the active camera for playback (usually player's camera)
     */
    schizo::scene::Entity* GetPlaybackCamera() const { return playback_camera_.get(); }

    /**
     * Camera-view selection. The first-person child is the default; these
     * helpers swap the active playback camera to the player's other child.
     */
    void SwitchToFirstPerson();
    void SwitchToThirdPerson();
    void ToggleCameraView();

    /**
     * Cursor capture state. While captured, main.cpp hides the OS cursor
     * (GLFW_CURSOR_DISABLED) and tells ImGui to ignore mouse input so the
     * player cannot hover/click editor panels. The game can release the
     * cursor when an in-game GUI opens by calling SetCursorCaptured(false).
     */
    bool IsCursorCaptured() const { return is_cursor_captured_; }
    void SetCursorCaptured(bool captured) { is_cursor_captured_ = captured; }
    void ToggleCursorCaptured() { is_cursor_captured_ = !is_cursor_captured_; }

    /**
     * Mouse delta from the editor host. While the cursor is captured, ImGui
     * is told to ignore the mouse, so the host (main.cpp) reads the raw
     * GLFW position delta and feeds it here for the player look code.
     */
    void OnMouseDelta(float dx, float dy) { mouse_delta_x_ += dx; mouse_delta_y_ += dy; }

private:
    std::shared_ptr<schizo::scene::Scene> scene_;
    std::shared_ptr<schizo::scene::Entity> player_entity_;
    std::shared_ptr<schizo::scene::Entity> playback_camera_;  // Camera being used during playback
    std::shared_ptr<engine::character::CharacterController> player_controller_;
    
    bool is_playing_ = false;
    bool is_paused_ = false;
    bool is_cursor_captured_ = false;  // True while the host should hide+lock the OS cursor
    float playback_time_ = 0.0f;

    // Mouse delta accumulated by the host between Update() calls (raw GLFW)
    float mouse_delta_x_ = 0.0f;
    float mouse_delta_y_ = 0.0f;

    // External ground check fed into the CharacterController, since the
    // controller's GroundDetector raycast is a stub. Hard-coded floor for now.
    static constexpr float GROUND_LEVEL = 0.0f;
    bool is_on_ground_ = false;

    // Snapshot taken on StartPlayback and replayed on StopPlayback so play
    // testing does not mutate the saved scene. Keyed by Entity::GetId().
    // Only transform state is captured for now — components added/removed
    // during play are NOT undone.
    struct EntitySnapshot {
        glm::vec3 local_position{0.0f};
        glm::quat local_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 local_scale{1.0f};
    };
    std::unordered_map<uint32_t, EntitySnapshot> entity_snapshots_;

    // Stage-4 physics: a Jolt PhysicsWorld is rebuilt fresh on every
    // StartPlayback from the scene's ColliderComponents. `entity_bodies_` maps
    // entity id -> Jolt BodyId; `dynamic_entities_` is the subset whose
    // simulated transform is written back each step. The player is a Jolt
    // CharacterVirtual (`player_char_id_`), not a rigid body.
    std::unique_ptr<schizo::physics::PhysicsWorld> physics_world_;
    std::unordered_map<uint32_t, uint32_t> entity_bodies_;   // entity id -> BodyId
    std::vector<uint32_t>                  dynamic_entities_; // entity ids w/ dynamic body
    uint32_t                               player_char_id_ = 0xFFFFFFFFu;
    
    // Camera state
    glm::vec3 camera_position_ = glm::vec3(0.0f);  // Current camera world position
    glm::vec3 camera_target_position_ = glm::vec3(0.0f);  // Target camera position for smooth following
    float camera_distance_ = 3.0f;  // Distance behind player
    float camera_height_ = 1.5f;  // Height above player
    float camera_smoothing_ = 0.15f;  // Smoothing factor for camera follow (0.0-1.0)
    bool should_hide_cursor_ = true;  // Whether to hide mouse cursor during playback
    
    // Helper methods
    bool FindAndSetupPlayer();
    bool AttachCharacterController();
    void SetupPlaybackCamera();
    void UpdateCamera();
    void UpdateMouseLook();
    void DriveCharacterController(float delta_time);
    void CaptureSceneSnapshot();
    void RestoreSceneSnapshot();
    void BuildPhysicsWorld();
    void TearDownPhysicsWorld();
    void StepPhysics(float delta_time);
};

} // namespace schizo::editor
