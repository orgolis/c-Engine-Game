#pragma once

#include <glm/glm.hpp>
#include <string>
#include <map>
#include <memory>

namespace schizo::renderer {

// Forward declarations
class AnimationClip;
class Skeleton;

// ============================================================================
// Blend Tree for Locomotion Blending
// ============================================================================

/**
 * @class BlendTree
 * @brief 2D or 1D blend space for smooth locomotion animation transitions
 * 
 * Allows blending between multiple animations based on a blend parameter
 * (typically movement speed and direction for locomotion)
 */
class BlendTree {
public:
    enum class BlendMode {
        Linear1D,    // Single parameter blending (e.g., walk/run speed)
        Linear2D,    // Two-parameter blending (e.g., speed and direction)
        Additive     // Additive blending for overlays
    };
    
    struct BlendPoint {
        float parameter_x = 0.0f;
        float parameter_y = 0.0f;  // For 2D blending
        std::shared_ptr<AnimationClip> animation;
    };
    
    BlendTree(const std::string& name, BlendMode mode = BlendMode::Linear1D);
    virtual ~BlendTree() = default;
    
    // Setup
    const std::string& GetName() const { return name_; }
    BlendMode GetBlendMode() const { return mode_; }
    
    // Add blend points
    void AddBlendPoint(float param_x, std::shared_ptr<AnimationClip> animation);
    void AddBlendPoint(float param_x, float param_y, std::shared_ptr<AnimationClip> animation);
    
    // Get blended animation
    // Returns blended animation clips with their weights
    void GetBlendedAnimations(float param_x, float param_y,
                            std::vector<std::pair<AnimationClip*, float>>& out_animations) const;
    
private:
    std::string name_;
    BlendMode mode_;
    std::vector<BlendPoint> blend_points_;
    
    void GetBlended1D(float param_x,
                     std::vector<std::pair<AnimationClip*, float>>& out_animations) const;
    
    void GetBlended2D(float param_x, float param_y,
                     std::vector<std::pair<AnimationClip*, float>>& out_animations) const;
};

/**
 * @class LocomotionSystem
 * @brief High-level locomotion blending for character movement
 * 
 * Handles transitions between idle, walk, run, strafe, etc. based on
 * movement direction and speed
 */
class LocomotionSystem {
public:
    LocomotionSystem();
    
    // Setup animations
    void SetIdleAnimation(std::shared_ptr<AnimationClip> clip) { idle_ = clip; }
    void SetWalkAnimation(std::shared_ptr<AnimationClip> clip) { walk_ = clip; }
    void SetRunAnimation(std::shared_ptr<AnimationClip> clip) { run_ = clip; }
    void SetSprintAnimation(std::shared_ptr<AnimationClip> clip) { sprint_ = clip; }
    void SetCrouchAnimation(std::shared_ptr<AnimationClip> clip) { crouch_ = clip; }
    
    // Set movement parameters
    void SetMovementVector(glm::vec2 direction) { movement_direction_ = direction; }
    void SetMovementSpeed(float speed) { movement_speed_ = speed; }
    
    // Speed thresholds
    void SetWalkSpeedThreshold(float speed) { walk_threshold_ = speed; }
    void SetRunSpeedThreshold(float speed) { run_threshold_ = speed; }
    void SetSprintSpeedThreshold(float speed) { sprint_threshold_ = speed; }
    
    // Get current animation based on movement parameters
    AnimationClip* GetCurrentAnimation() const;
    
private:
    std::shared_ptr<AnimationClip> idle_;
    std::shared_ptr<AnimationClip> walk_;
    std::shared_ptr<AnimationClip> run_;
    std::shared_ptr<AnimationClip> sprint_;
    std::shared_ptr<AnimationClip> crouch_;
    
    glm::vec2 movement_direction_ = glm::vec2(0);
    float movement_speed_ = 0.0f;
    
    // Thresholds
    float walk_threshold_ = 0.5f;
    float run_threshold_ = 3.0f;
    float sprint_threshold_ = 5.5f;
};

} // namespace schizo::renderer
