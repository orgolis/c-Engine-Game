#pragma once

// ============================================================================
// SkinnedMeshComponent — the gap between a working animation stack and a
// visible animated character.
//
// Everything under it already existed and was verified: rigged-glTF import, a
// Skeleton, AnimationClip sampling, GPU compute skinning, an animation state
// machine, IK. What did not exist was a way to say "THIS entity is that rigged
// character" — so the editor drove exactly ONE actor, held in a global
// unique_ptr in main(), positioned by its own world_pos field rather than by a
// Transform. You could not place two, and you could not place one where you
// wanted it through the normal editing flow.
//
// This is deliberately DATA ONLY. GPU resources (the skinned mesh, its material,
// the skinning dispatch) are owned editor-side and keyed by entity, for the same
// reason MeshComponent stores a path rather than a Mesh: a scene must be
// loadable, savable and diffable without a Vulkan device in the process.
// ============================================================================

#include <string>

namespace schizo::scene {

struct SkinnedMeshComponent {
    /// Rigged .gltf/.glb. Empty means "this entity is not a skinned character",
    /// which is the check the render path uses.
    std::string gltf_path;

    /// Which animation clip to play, by index into the file's clip list. Kept as
    /// an index rather than a name because that is what the loader exposes;
    /// resolving names is a later refinement, not a blocker.
    int  clip_index = 0;

    /// Paused actors still render — they hold their current pose rather than
    /// snapping to bind pose, which is what you want when scrubbing or posing.
    bool playing = true;

    /// Playback rate. Negative plays backwards; the sampler wraps either way.
    float speed = 1.0f;

    // ---- locomotion-driven clip selection ---------------------------------
    // When enabled, the clip is chosen from how fast the entity is ACTUALLY
    // moving, rather than being set by hand. That is what connects a walking
    // NPC agent to a walking animation without either system knowing about the
    // other: the agent moves the transform, and this reads the transform.
    //
    // Off by default, because it silently overrides clip_index and a component
    // that ignores the value you typed into it is worse than one that does
    // nothing.
    bool  drive_from_motion = false;
    int   idle_clip = 0;
    int   move_clip = 1;
    /// Metres/second above which `move_clip` is used. Not zero: floating-point
    /// jitter in a transform would otherwise flicker between clips every frame.
    float move_threshold = 0.15f;

    SkinnedMeshComponent() = default;
    explicit SkinnedMeshComponent(std::string path) : gltf_path(std::move(path)) {}

    bool active() const { return !gltf_path.empty(); }
};

}  // namespace schizo::scene
