#include "animator.h"
#include <algorithm>
#include <cstdint>

namespace schizo::renderer {

// ============================================================================
// Animator
// ============================================================================

Animator::Animator(std::shared_ptr<Skeleton> skeleton)
    : skeleton_(skeleton) {
    if (skeleton_) {
        // Store initial root position for root motion calculation
        if (const Bone* root = skeleton_->GetBone(0)) {
            last_root_position_ = root->world_matrix[3];
        }
    }
}

void Animator::AddAnimation(const std::string& name, std::shared_ptr<AnimationClip> clip) {
    animations_[name] = clip;
}

AnimationClip* Animator::GetAnimation(const std::string& name) {
    auto it = animations_.find(name);
    if (it != animations_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void Animator::Play(const std::string& animation_name, bool loop) {
    auto it = animations_.find(animation_name);
    if (it == animations_.end()) {
        return;  // Animation not found
    }
    
    current_animation_ = animation_name;
    current_time_ = 0.0f;
    is_looping_ = loop;
    state_ = AnimationState::Playing;
    blend_factor_ = 1.0f;  // No blending
}

void Animator::PlayBlended(const std::string& from, const std::string& to, float blend_time) {
    if (animations_.find(from) == animations_.end() ||
        animations_.find(to) == animations_.end()) {
        return;  // One or both animations not found
    }
    
    current_animation_ = from;
    next_animation_ = to;
    current_time_ = 0.0f;
    state_ = AnimationState::Blending;
    blend_factor_ = 0.0f;
    blend_duration_ = blend_time;
    blend_time_ = 0.0f;
}

void Animator::Stop() {
    state_ = AnimationState::Stopped;
    current_time_ = 0.0f;
    current_animation_.clear();
}

void Animator::Pause() {
    if (state_ == AnimationState::Playing) {
        state_ = AnimationState::Paused;
    }
}

void Animator::Resume() {
    if (state_ == AnimationState::Paused) {
        state_ = AnimationState::Playing;
    }
}

void Animator::Update(float delta_time) {
    if (!skeleton_) return;
    
    if (state_ == AnimationState::Stopped || state_ == AnimationState::Paused) {
        root_motion_delta_ = glm::vec3(0);
        return;
    }
    
    // Update current time
    current_time_ += delta_time * playback_speed_;
    
    if (state_ == AnimationState::Playing) {
        // Sample current animation
        AnimationClip* clip = GetAnimation(current_animation_);
        if (clip) {
            clip->Sample(*skeleton_, current_time_, is_looping_);
        }
    } else if (state_ == AnimationState::Blending) {
        // Update blend factor
        blend_time_ += delta_time;
        blend_factor_ = std::min(blend_time_ / blend_duration_, 1.0f);
        
        // Sample both animations
        AnimationClip* from_clip = GetAnimation(current_animation_);
        AnimationClip* to_clip = GetAnimation(next_animation_);
        
        if (from_clip && to_clip) {
            // Sample both
            Skeleton temp_skeleton = *skeleton_;
            from_clip->Sample(temp_skeleton, current_time_, is_looping_);
            glm::mat4 from_root = skeleton_->GetBone(0)->world_matrix;
            
            to_clip->Sample(*skeleton_, current_time_, is_looping_);
            glm::mat4 to_root = skeleton_->GetBone(0)->world_matrix;
            
            // Blend all bones
            for (uint16_t i = 0; i < skeleton_->GetBoneCount(); ++i) {
                Bone* bone = skeleton_->GetBone(i);
                if (bone) {
                    // Simple matrix blend (lerp positions, slerp rotations)
                    // For now, just use to_clip gradually
                    // A more sophisticated approach would interpolate bone transforms
                }
            }
        }
        
        // Check if blending is complete
        if (blend_factor_ >= 1.0f) {
            current_animation_ = next_animation_;
            state_ = AnimationState::Playing;
            current_time_ = 0.0f;
            blend_factor_ = 1.0f;
        }
    }
    
    UpdateRootMotion();
}

void Animator::UpdateRootMotion() {
    root_motion_delta_ = glm::vec3(0);
    
    if (!root_motion_enabled_ || !skeleton_) {
        return;
    }
    
    // Get root bone (bone 0)
    const Bone* root = skeleton_->GetBone(0);
    if (!root) {
        return;
    }
    
    glm::vec3 current_root_pos = glm::vec3(root->world_matrix[3]);
    root_motion_delta_ = current_root_pos - last_root_position_;
    last_root_position_ = current_root_pos;
}

} // namespace schizo::renderer
