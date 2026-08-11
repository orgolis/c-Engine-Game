#pragma once

// ============================================================================
// Pose — a full-skeleton snapshot of local BoneTransforms, plus the sampling
// and blending helpers the animation state machine needs. (Animation Stage 5.)
//
// A `Pose` is indexed by bone id (== index in Skeleton::GetAllBones()). The
// existing AnimationClip::Sample writes straight into a Skeleton, which can't be
// blended; sampling into a Pose lets us cross-fade two states / weight-blend a
// blend tree before committing to the skeleton.
// ============================================================================

#include "skeleton.h"
#include "animation.h"
#include "blend_tree.h"

#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <utility>
#include <vector>
#include <cstdint>

namespace schizo::renderer {

using Pose = std::vector<BoneTransform>;

/// Initialise a pose from the skeleton's current local transforms (the bind /
/// authored pose). Bones a clip doesn't animate keep these values.
inline void PoseFromSkeleton(const Skeleton& skel, Pose& out) {
    const auto& bones = skel.GetAllBones();
    out.resize(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) out[i] = bones[i].local_transform;
}

/// Commit a pose to the skeleton and recompute world matrices.
inline void ApplyPoseToSkeleton(Skeleton& skel, const Pose& pose) {
    const uint32_t n = skel.GetBoneCount();
    for (uint32_t i = 0; i < n && i < pose.size(); ++i)
        skel.SetBoneLocalTransform(static_cast<uint16_t>(i), pose[i]);
    skel.UpdateWorldTransforms();
}

/// Blend b into a by t (0 = a, 1 = b), component-wise. Slerp for rotation.
inline void BlendPoses(const Pose& a, const Pose& b, float t, Pose& out) {
    const size_t n = std::min(a.size(), b.size());
    out.resize(a.size());
    for (size_t i = 0; i < n; ++i) {
        out[i].position = glm::mix(a[i].position, b[i].position, t);
        out[i].scale    = glm::mix(a[i].scale,    b[i].scale,    t);
        out[i].rotation = glm::slerp(a[i].rotation, b[i].rotation, t);
    }
    for (size_t i = n; i < a.size(); ++i) out[i] = a[i];  // if a is longer
}

/// Sample one clip into `pose` (which must be pre-filled from the skeleton so
/// non-animated bones retain their bind values). `time` is looped/clamped.
inline void SampleClipToPose(const AnimationClip& clip, float time, bool loop, Pose& pose) {
    const float dur = clip.GetDuration();
    float t = time;
    if (dur > 0.0f) t = loop ? std::fmod(std::fmod(time, dur) + dur, dur)
                             : glm::clamp(time, 0.0f, dur);
    for (const auto& track : clip.GetBoneAnimations()) {
        if (!track || track->IsEmpty()) continue;
        const uint16_t id = track->GetBoneId();
        if (id < pose.size()) pose[id] = track->Sample(t);
    }
}

/// Sample a 1D/2D blend tree into `pose`. `pose` must be pre-filled from the
/// skeleton. Each contributing clip is sampled (all at the same normalized
/// phase so gaits stay foot-synced) and weight-blended.
inline void SampleBlendTreeToPose(const BlendTree& tree, float param_x, float param_y,
                                  float phase01, Pose& pose) {
    std::vector<std::pair<AnimationClip*, float>> blend;
    tree.GetBlendedAnimations(param_x, param_y, blend);
    if (blend.empty()) return;

    // Normalize weights.
    float wsum = 0.0f;
    for (auto& [clip, w] : blend) wsum += (clip ? w : 0.0f);
    if (wsum <= 1e-6f) return;

    const Pose base = pose;   // bind values for non-animated bones
    Pose accum;
    float running = 0.0f;
    for (auto& [clip, w] : blend) {
        if (!clip || w <= 1e-6f) continue;
        Pose p = base;
        const float dur = clip->GetDuration();
        SampleClipToPose(*clip, phase01 * (dur > 0 ? dur : 1.0f), clip->IsLooping(), p);
        const float nw = w / wsum;
        if (running <= 1e-6f) { accum = std::move(p); running = nw; }
        else {
            const float t = nw / (running + nw);
            BlendPoses(accum, p, t, accum);
            running += nw;
        }
    }
    if (running > 1e-6f) pose = std::move(accum);
}

} // namespace schizo::renderer
