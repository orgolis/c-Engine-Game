#include "animation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace schizo::renderer {

// ============================================================================
// BoneAnimation
// ============================================================================

void BoneAnimation::AddKeyframe(float time, const BoneTransform& transform) {
    Keyframe kf;
    kf.time = time;
    kf.transform = transform;
    
    // Insert in sorted order
    auto it = std::lower_bound(keyframes_.begin(), keyframes_.end(), kf,
                               [](const Keyframe& a, const Keyframe& b) {
                                   return a.time < b.time;
                               });
    keyframes_.insert(it, kf);
}

BoneTransform BoneAnimation::Sample(float time) const {
    if (keyframes_.empty()) {
        return BoneTransform();
    }
    
    if (keyframes_.size() == 1) {
        return keyframes_[0].transform;
    }
    
    // Clamp to duration
    float duration = GetDuration();
    if (time > duration) {
        time = duration;
    }
    if (time < 0) {
        time = 0;
    }
    
    // Find surrounding keyframes
    int idx = FindKeyframeIndex(time);
    
    if (idx < 0) {
        // Before first keyframe
        return keyframes_[0].transform;
    }
    
    if (idx >= static_cast<int>(keyframes_.size() - 1)) {
        // After last keyframe
        return keyframes_.back().transform;
    }
    
    // Interpolate between keyframes[idx] and keyframes[idx+1]
    const Keyframe& kf0 = keyframes_[idx];
    const Keyframe& kf1 = keyframes_[idx + 1];
    
    float time_diff = kf1.time - kf0.time;
    if (time_diff < 0.001f) {
        return kf0.transform;
    }
    
    float t = (time - kf0.time) / time_diff;
    t = std::clamp(t, 0.0f, 1.0f);
    
    // Interpolate transform
    return BoneTransform::Slerp(kf0.transform, kf1.transform, t);
}

float BoneAnimation::GetDuration() const {
    if (keyframes_.empty()) {
        return 0.0f;
    }
    return keyframes_.back().time;
}

int BoneAnimation::FindKeyframeIndex(float time) const {
    // Binary search for the keyframe just before or at time
    int left = 0;
    int right = keyframes_.size() - 1;
    
    if (time < keyframes_[0].time) return -1;
    if (time >= keyframes_.back().time) return right - 1;
    
    while (left < right) {
        int mid = (left + right + 1) / 2;
        if (keyframes_[mid].time <= time) {
            left = mid;
        } else {
            right = mid - 1;
        }
    }
    
    return left;
}

// ============================================================================
// AnimationClip
// ============================================================================

AnimationClip::AnimationClip(const std::string& name, float fps)
    : name_(name), fps_(fps) {
}

BoneAnimation* AnimationClip::AddBoneAnimation(uint16_t bone_id) {
    auto anim = std::make_unique<BoneAnimation>(bone_id);
    BoneAnimation* anim_ptr = anim.get();
    bone_animations_.push_back(std::move(anim));
    bone_animation_map_[bone_id] = anim_ptr;
    return anim_ptr;
}

BoneAnimation* AnimationClip::GetBoneAnimation(uint16_t bone_id) {
    auto it = bone_animation_map_.find(bone_id);
    if (it != bone_animation_map_.end()) {
        return it->second;
    }
    return nullptr;
}

void AnimationClip::Sample(Skeleton& skeleton, float time, bool loop) const {
    if (duration_ <= 0.0f) return;
    
    // Apply looping and playback speed
    float adjusted_time = time * playback_speed_;
    if (loop && duration_ > 0.0f) {
        adjusted_time = std::fmod(adjusted_time, duration_);
        if (adjusted_time < 0) {
            adjusted_time += duration_;
        }
    }
    
    // Sample all bone animations
    for (const auto& bone_anim : bone_animations_) {
        BoneTransform transform = bone_anim->Sample(adjusted_time);
        skeleton.SetBoneLocalTransform(bone_anim->GetBoneId(), transform);
    }
    
    // Update skeleton world transforms
    skeleton.UpdateWorldTransforms();
}

void AnimationClip::UpdateDuration() {
    duration_ = 0.0f;
    for (const auto& bone_anim : bone_animations_) {
        duration_ = std::max(duration_, bone_anim->GetDuration());
    }
}

} // namespace schizo::renderer
