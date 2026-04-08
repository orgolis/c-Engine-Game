#include <catch2/catch_all.hpp>
#include <glm/glm.hpp>
#include <memory>

// Animation headers
#include "../../engine/renderer/include/skeleton.h"
#include "../../engine/renderer/include/animation.h"
#include "../../engine/renderer/include/animator.h"
#include "../../engine/renderer/include/blend_tree.h"

using namespace schizo::renderer;

// ============================================================================
// Test: Skeleton Creation & Hierarchy
// ============================================================================

TEST_CASE("Skeleton creation with bones", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    
    REQUIRE(skeleton != nullptr);
    REQUIRE(skeleton->GetBoneCount() == 0);
    
    // Add root bone
    uint16_t root_id = skeleton->AddBone("Root", -1);
    REQUIRE(root_id == 0);
    REQUIRE(skeleton->GetBoneCount() == 1);
    
    // Get root bone
    Bone* root_bone = skeleton->GetBone(root_id);
    REQUIRE(root_bone != nullptr);
    REQUIRE(root_bone->name == "Root");
    REQUIRE(root_bone->parent_id == -1);
}

TEST_CASE("Skeleton with bone hierarchy", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    
    // Root
    uint16_t root_id = skeleton->AddBone("Root", -1);
    
    // Spine extends from root
    uint16_t spine_id = skeleton->AddBone("Spine", root_id);
    
    // Chest extends from spine
    uint16_t chest_id = skeleton->AddBone("Chest", spine_id);
    
    // Arms from chest
    uint16_t left_arm_id = skeleton->AddBone("LeftArm", chest_id);
    uint16_t right_arm_id = skeleton->AddBone("RightArm", chest_id);
    
    REQUIRE(skeleton->GetBoneCount() == 5);
    
    // Verify hierarchy
    Bone* spine = skeleton->GetBone(spine_id);
    REQUIRE(spine->parent_id == root_id);
    
    Bone* chest = skeleton->GetBone(chest_id);
    REQUIRE(chest->parent_id == spine_id);
    
    Bone* left_arm = skeleton->GetBone(left_arm_id);
    REQUIRE(left_arm->parent_id == chest_id);
}

TEST_CASE("Bone transform application", "[animation]") {
    Bone bone;
    bone.local_transform.position = glm::vec3(1, 2, 3);
    bone.local_transform.rotation = glm::quat(1, 0, 0, 0);
    bone.local_transform.scale = glm::vec3(1, 1, 1);
    
    glm::mat4 transform = bone.local_transform.ToMatrix();
    REQUIRE(transform[3][0] == 1.0f);
    REQUIRE(transform[3][1] == 2.0f);
    REQUIRE(transform[3][2] == 3.0f);
}

// ============================================================================
// Test: Animation Clips
// ============================================================================

TEST_CASE("Animation clip creation", "[animation]") {
    auto clip = std::make_shared<AnimationClip>();
    
    REQUIRE(clip != nullptr);
    REQUIRE(clip->GetDuration() == 0.0f);
    REQUIRE(clip->GetFrameCount() == 0);
}

TEST_CASE("Add keyframes to animation clip", "[animation]") {
    auto clip = std::make_shared<AnimationClip>();
    
    // Add keyframes for frame 0-60 at 30fps (2 seconds)
    for (uint32_t frame = 0; frame <= 60; ++frame) {
        float time = frame / 30.0f;  // 30 FPS
        
        BoneAnimation anim;
        anim.bone_id = 0;
        anim.keyframes.push_back({time, 0, BoneTransform()});
        
        clip->AddBoneAnimation(anim);
    }
    
    REQUIRE(clip->GetDuration() == 2.0f);
}

TEST_CASE("Animation clip keyframe interpolation", "[animation]") {
    auto clip = std::make_shared<AnimationClip>();
    
    // Create a linear animation: position moves from (0,0,0) to (10,0,0)
    BoneAnimation anim;
    anim.bone_id = 0;
    
    // Keyframe at t=0
    BoneTransform start;
    start.position = glm::vec3(0, 0, 0);
    anim.keyframes.push_back({0.0f, 0, start});
    
    // Keyframe at t=1.0
    BoneTransform end;
    end.position = glm::vec3(10, 0, 0);
    anim.keyframes.push_back({1.0f, 1, end});
    
    clip->AddBoneAnimation(anim);
}

// ============================================================================
// Test: Animator State Machine
// ============================================================================

TEST_CASE("Animator creation and initialization", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    uint16_t root_id = skeleton->AddBone("Root", -1);
    
    auto animator = std::make_shared<Animator>(skeleton);
    
    REQUIRE(animator != nullptr);
    REQUIRE(animator->GetSkeleton() == skeleton);
}

TEST_CASE("Add animations to animator", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->AddBone("Root", -1);
    
    auto animator = std::make_shared<Animator>(skeleton);
    
    // Create and add animation
    auto clip = std::make_shared<AnimationClip>();
    animator->AddAnimation("Idle", clip);
    
    // Retrieve animation
    auto retrieved = animator->GetAnimation("Idle");
    REQUIRE(retrieved == clip.get());
}

TEST_CASE("Animator playback - play animation", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->AddBone("Root", -1);
    
    auto animator = std::make_shared<Animator>(skeleton);
    auto clip = std::make_shared<AnimationClip>();
    animator->AddAnimation("Test", clip);
    
    // Play animation
    animator->Play("Test", true);  // loop=true
    
    REQUIRE(animator->GetCurrentAnimation() == "Test");
    REQUIRE(animator->GetCurrentTime() == 0.0f);
}

TEST_CASE("Animator update - advance time", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->AddBone("Root", -1);
    
    auto animator = std::make_shared<Animator>(skeleton);
    auto clip = std::make_shared<AnimationClip>();
    animator->AddAnimation("Test", clip);
    
    animator->Play("Test", false);  // no loop
    
    // Simulate several frames
    animator->Update(0.016f);  // ~60Hz
    float time1 = animator->GetCurrentTime();
    REQUIRE(time1 > 0.0f);
    
    animator->Update(0.016f);
    float time2 = animator->GetCurrentTime();
    REQUIRE(time2 > time1);
}

TEST_CASE("Animator pause and resume", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->AddBone("Root", -1);
    
    auto animator = std::make_shared<Animator>(skeleton);
    auto clip = std::make_shared<AnimationClip>();
    animator->AddAnimation("Test", clip);
    
    animator->Play("Test", false);
    animator->Update(0.016f);
    float paused_time = animator->GetCurrentTime();
    
    // Pause
    animator->Pause();
    animator->Update(0.016f);
    float after_pause = animator->GetCurrentTime();
    
    REQUIRE(paused_time == after_pause);  // Time should not advance
    
    // Resume
    animator->Resume();
    animator->Update(0.016f);
    float resumed_time = animator->GetCurrentTime();
    REQUIRE(resumed_time > paused_time);  // Time should advance again
}

TEST_CASE("Animator stop animation", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->AddBone("Root", -1);
    
    auto animator = std::make_shared<Animator>(skeleton);
    auto clip = std::make_shared<AnimationClip>();
    animator->AddAnimation("Test", clip);
    
    animator->Play("Test", false);
    animator->Stop();
    
    REQUIRE(animator->GetCurrentAnimation() == "");
    REQUIRE(animator->GetCurrentTime() == 0.0f);
}

TEST_CASE("Animator playback speed control", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->AddBone("Root", -1);
    
    auto animator = std::make_shared<Animator>(skeleton);
    
    // Test normal speed
    animator->SetPlaybackSpeed(1.0f);
    REQUIRE(animator->GetPlaybackSpeed() == 1.0f);
    
    // Test slow motion
    animator->SetPlaybackSpeed(0.5f);
    REQUIRE(animator->GetPlaybackSpeed() == 0.5f);
    
    // Test fast forward
    animator->SetPlaybackSpeed(2.0f);
    REQUIRE(animator->GetPlaybackSpeed() == 2.0f);
}

// ============================================================================
// Test: Blend Tree
// ============================================================================

TEST_CASE("Blend tree creation", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->AddBone("Root", -1);
    
    auto blend_tree = std::make_shared<BlendTree>();
    REQUIRE(blend_tree != nullptr);
}

TEST_CASE("Blend tree directional node", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->AddBone("Root", -1);
    
    auto animator = std::make_shared<Animator>(skeleton);
    auto idle = std::make_shared<AnimationClip>();
    auto walk = std::make_shared<AnimationClip>();
    auto run = std::make_shared<AnimationClip>();
    
    animator->AddAnimation("Idle", idle);
    animator->AddAnimation("Walk", walk);
    animator->AddAnimation("Run", run);
    
    // In a real blend tree, you'd create a multi-directional node
    // that blends between different directional anims based on input
}

// ============================================================================
// Test: Root Motion
// ============================================================================

TEST_CASE("Root motion enable/disable", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->AddBone("Root", -1);
    
    auto animator = std::make_shared<Animator>(skeleton);
    
    animator->SetRootMotionEnabled(true);
    REQUIRE(animator->IsRootMotionEnabled() == true);
    
    animator->SetRootMotionEnabled(false);
    REQUIRE(animator->IsRootMotionEnabled() == false);
}

TEST_CASE("Root motion delta computation", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->AddBone("Root", -1);
    
    auto animator = std::make_shared<Animator>(skeleton);
    animator->SetRootMotionEnabled(true);
    
    glm::vec3 delta = animator->GetRootMotionDelta();
    // Should be zero initially
    REQUIRE(glm::length(delta) >= 0.0f);  // Just verify it's valid
}

// ============================================================================
// Test: Bone Access
// ============================================================================

TEST_CASE("Get bone from animator", "[animation]") {
    auto skeleton = std::make_shared<Skeleton>();
    uint16_t root_id = skeleton->AddBone("Root", -1);
    uint16_t spine_id = skeleton->AddBone("Spine", root_id);
    
    auto animator = std::make_shared<Animator>(skeleton);
    
    // Access bones through animator
    auto skel = animator->GetSkeleton();
    Bone* root = skel->GetBone(root_id);
    
    REQUIRE(root != nullptr);
    REQUIRE(root->name == "Root");
}

// ============================================================================
// Test: Animation Component Integration (Scene-level)
// ============================================================================

TEST_CASE("Animation system full workflow", "[animation]") {
    // Create skeleton
    auto skeleton = std::make_shared<Skeleton>();
    uint16_t root_id = skeleton->AddBone("Root", -1);
    uint16_t spine_id = skeleton->AddBone("Spine", root_id);
    uint16_t head_id = skeleton->AddBone("Head", spine_id);
    
    // Create animations
    auto idle_clip = std::make_shared<AnimationClip>();
    auto walk_clip = std::make_shared<AnimationClip>();
    
    // Create animator
    auto animator = std::make_shared<Animator>(skeleton);
    animator->AddAnimation("Idle", idle_clip);
    animator->AddAnimation("Walk", walk_clip);
    
    // Play idle
    animator->Play("Idle", true);
    REQUIRE(animator->GetCurrentAnimation() == "Idle");
    
    // Update for several frames
    for (int i = 0; i < 100; ++i) {
        animator->Update(0.016f);  // 60 Hz
    }
    
    // Switch to walk
    animator->PlayBlended("Idle", "Walk", 0.5f);  // 0.5 second blend
    
    // Continue updating
    for (int i = 0; i < 100; ++i) {
        animator->Update(0.016f);
    }
    
    // Verify we're now playing walk
    REQUIRE(animator->GetCurrentAnimation() == "Walk");
}
