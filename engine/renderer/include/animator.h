#pragma once

#include "skeleton.h"
#include "animation.h"
#include <map>
#include <memory>
#include <string>

namespace schizo::renderer {

// ============================================================================
// Animation State Machine
// ============================================================================

/**
 * @class Animator
 * @brief Manages animation playback, blending, and state machine
 * 
 * Features:
 * - Multiple animation states
 * - Smooth blending between animations
 * - Animation speed control
 * - Loop/non-loop support
 */
class Animator {
public:
    enum class AnimationState {
        Stopped,
        Playing,
        Paused,
        Blending
    };
    
    Animator(std::shared_ptr<Skeleton> skeleton);
    virtual ~Animator() = default;
    
    // Skeleton access
    std::shared_ptr<Skeleton> GetSkeleton() { return skeleton_; }
    
    // Register animations
    void AddAnimation(const std::string& name, std::shared_ptr<AnimationClip> clip);
    AnimationClip* GetAnimation(const std::string& name);
    
    // Playback control
    void Play(const std::string& animation_name, bool loop = true);
    void PlayBlended(const std::string& from, const std::string& to, float blend_time);
    void Stop();
    void Pause();
    void Resume();
    
    // Update (call once per frame)
    void Update(float delta_time);
    
    // Playback information
    AnimationState GetState() const { return state_; }
    const std::string& GetCurrentAnimation() const { return current_animation_; }
    float GetCurrentTime() const { return current_time_; }
    float GetPlaybackSpeed() const { return playback_speed_; }
    void SetPlaybackSpeed(float speed) { playback_speed_ = speed; }
    
    // Blending
    float GetBlendFactor() const { return blend_factor_; }
    bool IsBlending() const { return state_ == AnimationState::Blending; }
    
    // Root motion (animation-driven position)
    glm::vec3 GetRootMotionDelta() const { return root_motion_delta_; }
    void SetRootMotionEnabled(bool enabled) { root_motion_enabled_ = enabled; }
    bool IsRootMotionEnabled() const { return root_motion_enabled_; }
    
private:
    std::shared_ptr<Skeleton> skeleton_;
    std::map<std::string, std::shared_ptr<AnimationClip>> animations_;
    
    // Current playback state
    AnimationState state_ = AnimationState::Stopped;
    std::string current_animation_;
    std::string next_animation_;  // For blending
    float current_time_ = 0.0f;
    float playback_speed_ = 1.0f;
    bool is_looping_ = true;
    
    // Blending
    float blend_factor_ = 0.0f;  // 0 = from, 1 = to
    float blend_duration_ = 0.0f;
    float blend_time_ = 0.0f;
    
    // Root motion
    bool root_motion_enabled_ = false;
    glm::vec3 root_motion_delta_ = glm::vec3(0);
    glm::vec3 last_root_position_ = glm::vec3(0);
    
    void UpdateRootMotion();
};

} // namespace schizo::renderer
