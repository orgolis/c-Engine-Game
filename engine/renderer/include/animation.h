#pragma once

#include "skeleton.h"
#include <vector>
#include <string>
#include <memory>

namespace gws {

// ============================================================================
// Keyframe Animation
// ============================================================================

/**
 * @struct Keyframe
 * @brief A single keyframe for a bone
 */
struct Keyframe {
    float time = 0.0f;  // Time in seconds
    BoneTransform transform;
};

/**
 * @class BoneAnimation
 * @brief Animation track for a single bone
 */
class BoneAnimation {
public:
    BoneAnimation(uint16_t bone_id) : bone_id_(bone_id) {}
    
    // Add keyframes
    void AddKeyframe(float time, const BoneTransform& transform);
    void SetKeyframes(const std::vector<Keyframe>& keyframes) { keyframes_ = keyframes; }
    
    // Sample animation at specific time
    BoneTransform Sample(float time) const;
    
    // Get keyframe data
    const std::vector<Keyframe>& GetKeyframes() const { return keyframes_; }
    uint16_t GetBoneId() const { return bone_id_; }
    float GetDuration() const;
    bool IsEmpty() const { return keyframes_.empty(); }
    
private:
    uint16_t bone_id_;
    std::vector<Keyframe> keyframes_;  // Should be sorted by time
    
    // Find surrounding keyframes for interpolation
    int FindKeyframeIndex(float time) const;
};

/**
 * @class AnimationClip
 * @brief Complete animation for a skeleton (all bones over time)
 */
class AnimationClip {
public:
    AnimationClip(const std::string& name = "Unnamed", float fps = 30.0f);
    virtual ~AnimationClip() = default;
    
    // Metadata
    const std::string& GetName() const { return name_; }
    void SetName(const std::string& name) { name_ = name; }
    
    float GetFPS() const { return fps_; }
    float GetDuration() const { return duration_; }
    
    // Add animation tracks for bones
    BoneAnimation* AddBoneAnimation(uint16_t bone_id);
    BoneAnimation* GetBoneAnimation(uint16_t bone_id);
    
    // Sample the entire animation at a specific time
    void Sample(Skeleton& skeleton, float time, bool loop = true) const;
    
    // Get all animations
    const std::vector<std::unique_ptr<BoneAnimation>>& GetBoneAnimations() const {
        return bone_animations_;
    }
    
    // Animation properties
    bool IsLooping() const { return is_looping_; }
    void SetLooping(bool loop) { is_looping_ = loop; }
    
    // Adjust playback speed
    float GetPlaybackSpeed() const { return playback_speed_; }
    void SetPlaybackSpeed(float speed) { playback_speed_ = speed; }
    
    // Update duration based on bone animations
    void UpdateDuration();
    
private:
    std::string name_;
    float fps_;
    float duration_ = 0.0f;
    float playback_speed_ = 1.0f;
    bool is_looping_ = true;
    
    std::vector<std::unique_ptr<BoneAnimation>> bone_animations_;
    std::map<uint16_t, BoneAnimation*> bone_animation_map_;
};

} // namespace schizo::renderer
