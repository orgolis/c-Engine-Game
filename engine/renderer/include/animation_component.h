#pragma once

#include "animator.h"
#include "skeleton.h"
#include "animation.h"
#include "../scene/include/entity.h"
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>

namespace schizo::renderer {

/**
 * @class AnimationComponent
 * @brief Scene component for character animation
 * 
 * Integrates skeleton, animations, and animator into the ECS
 * Syncs with entity transform for root motion support
 */
class AnimationComponent : public schizo::scene::Component {
public:
    AnimationComponent();
    virtual ~AnimationComponent() = default;
    
    // Component lifecycle
    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float delta_time) override;
    void OnLateUpdate(float delta_time) override;
    void OnEnable() override;
    void OnDisable() override;
    
    // Skeleton access
    void SetSkeleton(std::shared_ptr<Skeleton> skeleton);
    std::shared_ptr<Skeleton> GetSkeleton() const { return animator_ ? animator_->GetSkeleton() : nullptr; }
    
    // Animator access
    std::shared_ptr<Animator> GetAnimator() { return animator_; }
    
    // Animation library
    void AddAnimation(const std::string& name, std::shared_ptr<AnimationClip> clip);
    AnimationClip* GetAnimation(const std::string& name);
    
    // Playback
    void Play(const std::string& animation_name, bool loop = true);
    void PlayBlended(const std::string& from, const std::string& to, float blend_duration);
    void Stop();
    void Pause();
    void Resume();
    
    // Animation state
    const std::string& GetCurrentAnimation() const;
    float GetCurrentTime() const;
    float GetPlaybackSpeed() const;
    void SetPlaybackSpeed(float speed);
    
    // Root motion
    void SetRootMotionEnabled(bool enabled);
    bool IsRootMotionEnabled() const;
    glm::vec3 GetRootMotionDelta() const;
    
    // Bone access for IK or other purposes
    Bone* GetBone(const std::string& name);
    Bone* GetBone(uint16_t id);
    
private:
    std::shared_ptr<Animator> animator_;
    
    // Root motion smoothing
    glm::vec3 accumulated_root_motion_ = glm::vec3(0);
};

} // namespace schizo::renderer
