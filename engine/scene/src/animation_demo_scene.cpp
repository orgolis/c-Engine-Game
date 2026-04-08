#include "animation_demo_scene.h"
#include "../../renderer/include/skeleton.h"
#include "../../renderer/include/animator.h"
#include "../../renderer/include/animation.h"

namespace schizo::demo {

using namespace schizo::renderer;

AnimationDemoScene::AnimationDemoScene()
    : simulation_time_(0.0f)
{
}

AnimationDemoScene::~AnimationDemoScene()
{
    Cleanup();
}

void AnimationDemoScene::Initialize()
{
    // Create skeleton hierarchy for a simple humanoid character
    CreateCharacterSkeleton();
    
    // Create animation clips for different motions
    CreateAnimationClips();
    
    // Set up animator state machine
    SetupAnimator();
}

void AnimationDemoScene::Update(float delta_time)
{
    simulation_time_ += delta_time;
    
    if (animator_) {
        animator_->Update(delta_time);
    }
}

void AnimationDemoScene::Cleanup()
{
    animator_ = nullptr;
    skeleton_ = nullptr;
    idle_clip_ = nullptr;
    walk_clip_ = nullptr;
    run_clip_ = nullptr;
    jump_clip_ = nullptr;
}

void AnimationDemoScene::CreateCharacterSkeleton()
{
    skeleton_ = std::make_shared<Skeleton>();
    
    // Root/Hips
    uint16_t root_id = skeleton_->AddBone("Root", -1);
    
    // Spine chain
    uint16_t spine_id = skeleton_->AddBone("Spine", root_id);
    uint16_t chest_id = skeleton_->AddBone("Chest", spine_id);
    uint16_t neck_id = skeleton_->AddBone("Neck", chest_id);
    uint16_t head_id = skeleton_->AddBone("Head", neck_id);
    
    // Left arm chain
    uint16_t left_shoulder_id = skeleton_->AddBone("LeftShoulder", chest_id);
    uint16_t left_arm_id = skeleton_->AddBone("LeftArm", left_shoulder_id);
    uint16_t left_forearm_id = skeleton_->AddBone("LeftForearm", left_arm_id);
    uint16_t left_hand_id = skeleton_->AddBone("LeftHand", left_forearm_id);
    
    // Right arm chain
    uint16_t right_shoulder_id = skeleton_->AddBone("RightShoulder", chest_id);
    uint16_t right_arm_id = skeleton_->AddBone("RightArm", right_shoulder_id);
    uint16_t right_forearm_id = skeleton_->AddBone("RightForearm", right_arm_id);
    uint16_t right_hand_id = skeleton_->AddBone("RightHand", right_forearm_id);
    
    // Left leg chain
    uint16_t left_hip_id = skeleton_->AddBone("LeftHip", root_id);
    uint16_t left_thigh_id = skeleton_->AddBone("LeftThigh", left_hip_id);
    uint16_t left_shin_id = skeleton_->AddBone("LeftShin", left_thigh_id);
    uint16_t left_foot_id = skeleton_->AddBone("LeftFoot", left_shin_id);
    
    // Right leg chain
    uint16_t right_hip_id = skeleton_->AddBone("RightHip", root_id);
    uint16_t right_thigh_id = skeleton_->AddBone("RightThigh", right_hip_id);
    uint16_t right_shin_id = skeleton_->AddBone("RightShin", right_thigh_id);
    uint16_t right_foot_id = skeleton_->AddBone("RightFoot", right_shin_id);
}

void AnimationDemoScene::CreateAnimationClips()
{
    // Create empty animation clips for demonstration
    // In a real scenario, these would be loaded from files or created programmatically
    
    idle_clip_ = std::make_shared<AnimationClip>();
    walk_clip_ = std::make_shared<AnimationClip>();
    run_clip_ = std::make_shared<AnimationClip>();
    jump_clip_ = std::make_shared<AnimationClip>();
}

void AnimationDemoScene::SetupAnimator()
{
    if (!skeleton_) {
        return;
    }
    
    animator_ = std::make_shared<Animator>(skeleton_);
    
    // Add animations to animator
    if (idle_clip_) animator_->AddAnimation("Idle", idle_clip_);
    if (walk_clip_) animator_->AddAnimation("Walk", walk_clip_);
    if (run_clip_) animator_->AddAnimation("Run", run_clip_);
    if (jump_clip_) animator_->AddAnimation("Jump", jump_clip_);
    
    // Start with idle animation
    animator_->Play("Idle", true);  // Loop = true
}

void AnimationDemoScene::PlayAnimation(const std::string& animation_name, bool loop)
{
    if (animator_) {
        animator_->Play(animation_name, loop);
    }
}

void AnimationDemoScene::BlendAnimation(const std::string& from, const std::string& to, float duration)
{
    if (animator_) {
        animator_->PlayBlended(from, to, duration);
    }
}

} // namespace schizo::demo
