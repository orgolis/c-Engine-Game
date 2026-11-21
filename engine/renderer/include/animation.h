#pragma once

#include "skeleton.h"
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

namespace schizo::renderer {

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
/// One channel of a bone's motion (position, rotation or scale) sampled at a
/// uniform rate: sampling is an index, not a search. One value: constant (a
/// channel that never moves costs one value). `step` holds each value until the
/// next (glTF STEP). The importer resamples every glTF sampler -- STEP, LINEAR
/// and CUBICSPLINE, each channel on its own key times -- exactly onto this.
template <typename T>
struct UniformTrack {
    std::vector<T> values;
    float rate = 30.0f;   // samples per second
    bool  step = false;
    bool  empty() const { return values.empty(); }
    float duration() const { return values.size() > 1 ? static_cast<float>(values.size() - 1) / rate : 0.0f; }
};

class BoneAnimation {
public:
    BoneAnimation(uint16_t bone_id) : bone_id_(bone_id) {}
    
    // Add keyframes
    void AddKeyframe(float time, const BoneTransform& transform);
    void SetKeyframes(const std::vector<Keyframe>& keyframes) { keyframes_ = keyframes; }
    
    // Sample animation at specific time
    BoneTransform Sample(float time) const;
    /// Uniform channels (clip format v2): when set, Sample reads them and not the
    /// keyframes. Each must have at least one value.
    void SetChannels(UniformTrack<glm::vec3> position, UniformTrack<glm::quat> rotation, UniformTrack<glm::vec3> scale);
    bool HasChannels() const { return !rotation_.empty(); }
    const UniformTrack<glm::vec3>& PositionChannel() const { return position_; }
    const UniformTrack<glm::quat>& RotationChannel() const { return rotation_; }
    const UniformTrack<glm::vec3>& ScaleChannel() const { return scale_; }
    /// Bytes of motion data (for the importer's report).
    size_t DataBytes() const;
    
    // Get keyframe data
    const std::vector<Keyframe>& GetKeyframes() const { return keyframes_; }
    uint16_t GetBoneId() const { return bone_id_; }
    float GetDuration() const;
    bool IsEmpty() const { return keyframes_.empty() && rotation_.empty(); }
    
private:
    uint16_t bone_id_;
    std::vector<Keyframe> keyframes_;  // Should be sorted by time
    UniformTrack<glm::vec3> position_, scale_;
    UniformTrack<glm::quat> rotation_;
    float channel_duration_ = 0.0f;
    
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
    /// Sample into a pose (local transforms by bone id) without touching a
    /// skeleton: bones this clip does not animate keep what `pose` holds.
    void SampleLocal(float time, bool loop, std::vector<BoneTransform>& pose) const;
    
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
    void SetDuration(float seconds) { duration_ = seconds; }
    
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
