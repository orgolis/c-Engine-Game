#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace schizo::renderer {
    class Skeleton;
    class Animator;
    class AnimationClip;
    class SkeletalMeshComponent;
}

namespace schizo::scene {
    class Entity;
}

namespace schizo::demo {

/**
 * @class AnimationDemoScene
 * @brief Demonstrates skeletal animation system capabilities
 * 
 * Features demonstrated:
 * - Skeleton hierarchy creation
 * - Animation clip playback
 * - Animation blending
 * - Root motion support
 * - Character setup with animations
 */
class AnimationDemoScene {
public:
    AnimationDemoScene();
    ~AnimationDemoScene();
    
    // Scene lifecycle
    void Initialize();
    void Update(float delta_time);
    void Cleanup();
    
    // Scene components
    void CreateCharacterSkeleton();
    void CreateAnimationClips();
    void SetupAnimator();
    void PlayAnimation(const std::string& animation_name, bool loop = true);
    void BlendAnimation(const std::string& from, const std::string& to, float duration);
    
    // Query scene state
    std::shared_ptr<schizo::renderer::Animator> GetAnimator() const { return animator_; }
    std::shared_ptr<schizo::renderer::Skeleton> GetSkeleton() const { return skeleton_; }
    
    float GetSimulationTime() const { return simulation_time_; }
    
private:
    std::shared_ptr<schizo::renderer::Skeleton> skeleton_;
    std::shared_ptr<schizo::renderer::Animator> animator_;
    
    // Animation clips
    std::shared_ptr<schizo::renderer::AnimationClip> idle_clip_;
    std::shared_ptr<schizo::renderer::AnimationClip> walk_clip_;
    std::shared_ptr<schizo::renderer::AnimationClip> run_clip_;
    std::shared_ptr<schizo::renderer::AnimationClip> jump_clip_;
    
    float simulation_time_ = 0.0f;
    float animation_blend_speed_ = 0.5f;  // Seconds for blend transition
    
    // Helper methods
    void CreateIdleAnimation();
    void CreateWalkAnimation();
    void CreateRunAnimation();
    void CreateJumpAnimation();
};

} // namespace schizo::demo
