#include "animation_component.h"
#include "../scene/include/entity.h"
#include "skeleton.h"
#include "animator.h"
#include "animation.h"

namespace schizo::renderer {

AnimationComponent::AnimationComponent() = default;

void AnimationComponent::OnAttach() {
    if (!animator_) {
        auto skeleton = std::make_shared<Skeleton>();
        animator_ = std::make_shared<Animator>(skeleton);
    }
}

void AnimationComponent::OnDetach() {
    // Cleanup if needed
}

void AnimationComponent::OnUpdate(float delta_time) {
    // Nothing needed before update typically
}

void AnimationComponent::OnLateUpdate(float delta_time) {
    if (!animator_) return;
    
    // Update animator
    animator_->Update(delta_time);
    
    // Handle root motion if enabled
    if (animator_->IsRootMotionEnabled() && GetEntity()) {
        glm::vec3 root_motion = animator_->GetRootMotionDelta();
        
        // Apply root motion to entity transform
        if (glm::length(root_motion) > 0.0001f) {
            auto transform = GetEntity()->GetTransform();
            if (transform) {
                glm::vec3 current_pos = transform->GetWorldPosition();
                transform->SetWorldPosition(current_pos + root_motion);
            }
        }
    }
}

void AnimationComponent::OnEnable() {
    if (animator_) {
        animator_->Resume();
    }
}

void AnimationComponent::OnDisable() {
    if (animator_) {
        animator_->Pause();
    }
}

void AnimationComponent::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
    if (!animator_) {
        animator_ = std::make_shared<Animator>(skeleton);
    } else {
        // Would need to implement skeleton reassignment
    }
}

void AnimationComponent::AddAnimation(const std::string& name, std::shared_ptr<AnimationClip> clip) {
    if (animator_) {
        animator_->AddAnimation(name, clip);
    }
}

AnimationClip* AnimationComponent::GetAnimation(const std::string& name) {
    if (animator_) {
        return animator_->GetAnimation(name);
    }
    return nullptr;
}

void AnimationComponent::Play(const std::string& animation_name, bool loop) {
    if (animator_) {
        animator_->Play(animation_name, loop);
    }
}

void AnimationComponent::PlayBlended(const std::string& from, const std::string& to, float blend_duration) {
    if (animator_) {
        animator_->PlayBlended(from, to, blend_duration);
    }
}

void AnimationComponent::Stop() {
    if (animator_) {
        animator_->Stop();
    }
}

void AnimationComponent::Pause() {
    if (animator_) {
        animator_->Pause();
    }
}

void AnimationComponent::Resume() {
    if (animator_) {
        animator_->Resume();
    }
}

const std::string& AnimationComponent::GetCurrentAnimation() const {
    static std::string empty;
    if (animator_) {
        return animator_->GetCurrentAnimation();
    }
    return empty;
}

float AnimationComponent::GetCurrentTime() const {
    if (animator_) {
        return animator_->GetCurrentTime();
    }
    return 0.0f;
}

float AnimationComponent::GetPlaybackSpeed() const {
    if (animator_) {
        return animator_->GetPlaybackSpeed();
    }
    return 1.0f;
}

void AnimationComponent::SetPlaybackSpeed(float speed) {
    if (animator_) {
        animator_->SetPlaybackSpeed(speed);
    }
}

void AnimationComponent::SetRootMotionEnabled(bool enabled) {
    if (animator_) {
        animator_->SetRootMotionEnabled(enabled);
    }
}

bool AnimationComponent::IsRootMotionEnabled() const {
    if (animator_) {
        return animator_->IsRootMotionEnabled();
    }
    return false;
}

glm::vec3 AnimationComponent::GetRootMotionDelta() const {
    if (animator_) {
        return animator_->GetRootMotionDelta();
    }
    return glm::vec3(0);
}

Bone* AnimationComponent::GetBone(const std::string& name) {
    if (animator_) {
        auto skeleton = animator_->GetSkeleton();
        if (skeleton) {
            return skeleton->GetBone(name);
        }
    }
    return nullptr;
}

Bone* AnimationComponent::GetBone(uint16_t id) {
    if (animator_) {
        auto skeleton = animator_->GetSkeleton();
        if (skeleton) {
            return skeleton->GetBone(id);
        }
    }
    return nullptr;
}

} // namespace schizo::renderer
