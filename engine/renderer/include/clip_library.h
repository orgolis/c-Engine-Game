#pragma once
// ============================================================================
// clip_library.h -- clips made on one model used by another with the same rig,
// and a clip's root motion (0.9.1).
//
// A clip library is a model file whose clips are wanted, not its mesh: the
// bones are matched by NAME, so clips made for one character play on every
// character built on that rig (the host's convention: `<model>@<name>.glb`
// beside a model adds its clips). Root motion: how far a clip moves its root
// bone over a stretch of its time -- the character is moved by that, and the
// pose keeps the root where the clip started (in place).
//
// Pure (no GPU): checked by tools/skinned_import_check.
// ============================================================================
#include "animation.h"
#include "skeleton.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace schizo::renderer {

/// A clip of `from`'s skeleton on `to`'s: each bone's channels onto the bone of
/// the same name (bones `to` lacks are left out). Null when no bone matched.
inline std::shared_ptr<AnimationClip> remap_clip(const AnimationClip& c, const Skeleton& from, const Skeleton& to, int* matched = nullptr) {
    auto out = std::make_shared<AnimationClip>(c.GetName(), c.GetFPS());
    int n = 0;
    for (const auto& ba : c.GetBoneAnimations()) {
        const Bone* fb = from.GetBone(ba->GetBoneId());
        const Bone* tb = fb ? to.GetBone(fb->name) : nullptr;
        if (!tb) continue;
        BoneAnimation* nb = out->AddBoneAnimation(tb->id);
        if (ba->HasChannels()) nb->SetChannels(ba->PositionChannel(), ba->RotationChannel(), ba->ScaleChannel());
        else nb->SetKeyframes(ba->GetKeyframes());
        ++n;
    }
    if (matched) *matched = n;
    if (n == 0) return nullptr;
    out->UpdateDuration();
    out->SetDuration(std::max(out->GetDuration(), c.GetDuration()));
    out->SetLooping(c.IsLooping());
    return out;
}

/// `lib`'s clips (made on `lib_skel`) added to `clips` (made on `model`): one of
/// a name already there replaces it. Returns how many were added or replaced.
inline int merge_clips(std::vector<std::shared_ptr<AnimationClip>>& clips, const Skeleton& model, const Skeleton& lib_skel,
                       const std::vector<std::shared_ptr<AnimationClip>>& lib) {
    int added = 0;
    for (const auto& c : lib) {
        if (!c) continue;
        auto r = remap_clip(*c, lib_skel, model);
        if (!r) continue;
        auto it = std::find_if(clips.begin(), clips.end(), [&](const auto& x) { return x && x->GetName() == r->GetName(); });
        if (it != clips.end()) *it = r;
        else clips.push_back(r);
        ++added;
    }
    return added;
}

/// Where `bone`'s local position is in the clip at `t` (its bind / rest when the
/// clip leaves it).
inline glm::vec3 bone_position_at(const AnimationClip& c, uint16_t bone, float t, const glm::vec3& rest = glm::vec3(0.0f)) {
    for (const auto& ba : c.GetBoneAnimations())
        if (ba->GetBoneId() == bone) return ba->Sample(t).position;
    return rest;
}

/// Root motion: how far `bone` moved (local position) from `was` to `now` in the
/// clip. A loop wrapping round adds the rest of the lap and the start of the next;
/// a once clip is held at its ends.
inline glm::vec3 root_travel(const AnimationClip& c, uint16_t bone, float was, float now, bool loop) {
    const float d = c.GetDuration();
    if (d <= 0.0f || now == was) return glm::vec3(0.0f);
    if (!loop) {
        was = std::clamp(was, 0.0f, d);
        now = std::clamp(now, 0.0f, d);
        return bone_position_at(c, bone, now) - bone_position_at(c, bone, was);
    }
    const glm::vec3 lap = bone_position_at(c, bone, d) - bone_position_at(c, bone, 0.0f);
    const float w = std::fmod(std::max(0.0f, was), d), n = std::fmod(std::max(0.0f, now), d);
    const float laps = std::floor(std::max(0.0f, now) / d) - std::floor(std::max(0.0f, was) / d);
    return bone_position_at(c, bone, n) - bone_position_at(c, bone, w) + lap * laps;
}

}  // namespace schizo::renderer
