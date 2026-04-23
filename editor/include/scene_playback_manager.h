#pragma once

#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

namespace engine::character {
    class CharacterController;
}

namespace schizo::scene {
    class Entity;
    class Scene;
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

private:
    std::shared_ptr<schizo::scene::Scene> scene_;
    std::shared_ptr<schizo::scene::Entity> player_entity_;
    std::shared_ptr<schizo::scene::Entity> playback_camera_;  // Camera being used during playback
    std::shared_ptr<engine::character::CharacterController> player_controller_;
    
    bool is_playing_ = false;
    bool is_paused_ = false;
    float playback_time_ = 0.0f;
    
    // Physics state
    glm::vec3 player_velocity_ = glm::vec3(0.0f);  // Current velocity for gravity
    bool is_on_ground_ = false;
    
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
    void UpdateCharacterMovement(float delta_time);
    void ApplyGravity(float delta_time);
    void ApplyVelocityToPosition(float delta_time);
};

} // namespace schizo::editor
