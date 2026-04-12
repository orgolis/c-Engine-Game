#pragma once

#include <memory>
#include <cstdint>

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
    schizo::scene::Entity* GetPlayerEntity() const { return player_entity_; }
    
    /**
     * Get the character controller for the player
     */
    engine::character::CharacterController* GetPlayerController() const 
    { return player_controller_; }
    
    /**
     * Get total playback time
     */
    float GetPlaybackTime() const { return playback_time_; }

private:
    std::shared_ptr<schizo::scene::Scene> scene_;
    schizo::scene::Entity* player_entity_ = nullptr;
    engine::character::CharacterController* player_controller_ = nullptr;
    
    bool is_playing_ = false;
    bool is_paused_ = false;
    float playback_time_ = 0.0f;
    
    // Helper methods
    bool FindAndSetupPlayer();
    bool AttachCharacterController();
    void UpdateCamera();
    void UpdateCharacterMovement(float delta_time);
};

} // namespace schizo::editor
