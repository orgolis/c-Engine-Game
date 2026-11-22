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

#include <glm/glm.hpp>

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

    // ---- driven by gameplay: a character in Play ---------------------------
    // The clip follows what the character is doing: standing, walking or
    // running (by how fast it moves), crouched, aiming, firing, reloading, hit,
    // dead. `clips` names a clip for each, "state=clip name" separated by
    // commas: idle, walk, run, crouch, crouch_walk, aim, fire, reload, hit,
    // death, draw. A name matches the start or any part of a clip's name
    // ("Walk" finds "01_Walk"); a state without a clip is skipped. Clips blend
    // into each other; fire, reload, hit and death play once (a death stays down).
    bool        drive_from_gameplay = false;
    std::string clips;
    float       run_speed = 3.2f;   // m/s: above it the run clip, below it the walk
    /// A bone: aim, fire, reload and hit play on it and everything under it
    /// while the legs walk or run. Empty: those take the whole body.
    std::string upper_body;
    /// An .animgraph (project path) the character runs in Play: its states,
    /// blend spaces, layers and slots. Empty: the built-in humanoid graph made
    /// from `clips` (docs/systems/CHARACTERS.md).
    std::string graph;

    // ---- held in the parent's hand -----------------------------------------
    // A bone of the PARENT's skinned character (a name, matched as a prefix):
    // this entity's model is drawn there, turned by `attach_rot` (degrees,
    // x then y then z) and moved by `attach_pos` in the bone's space. A gun in
    // a hand; with drive_from_gameplay it fires and reloads with its holder.
    std::string attach_bone;
    glm::vec3   attach_pos{0.0f};
    glm::vec3   attach_rot{0.0f};

    SkinnedMeshComponent() = default;
    explicit SkinnedMeshComponent(std::string path) : gltf_path(std::move(path)) {}

    bool active() const { return !gltf_path.empty(); }
};

}  // namespace schizo::scene
