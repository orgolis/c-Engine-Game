#pragma once

// ============================================================================
// Locomotion-driven clip selection — pick idle vs move from actual motion.
//
// This is what joins two systems that know nothing about each other: an NPC
// agent (3.5) moves an entity's transform along a navmesh path, and a skinned
// character (3.8) reads that transform and animates accordingly. Neither has to
// call the other, and the same rule drives the player, an agent, or anything
// else that moves — including something dragged by the gizmo.
//
// KEPT SEPARATE AND ImGui/GPU-FREE for the same reason EditCoalescer is: the
// hard part is a RULE, not a mechanism, and its failure mode is visual
// flicker. Speed is derived from a position delta, so it is noisy by nature;
// a naive `speed > 0` test makes a standing character twitch between idle and
// walk every frame from floating-point jitter alone. Hysteresis fixes that, and
// hysteresis is exactly the kind of thing that looks right in code and is wrong
// in motion. As a pure function of (previous position, current position, dt) it
// is a handful of headless assertions instead.
// ============================================================================

#include <glm/glm.hpp>

#include <cmath>

namespace schizo::editor {

/// Per-entity locomotion state. Holds the previous position and the clip
/// currently chosen, because the choice depends on both speed AND what was
/// already playing (that is what hysteresis means).
struct LocomotionState {
    glm::vec3 last_pos{0.0f};
    bool      has_last = false;
    bool      moving   = false;   // the current decision, not the raw test
    float     speed    = 0.0f;    // last computed speed, for UI / debugging
};

/// Update `st` from a new world position and return true if the MOVE clip
/// should play.
///
/// Hysteresis: crossing `threshold` starts movement, but stopping requires
/// dropping below 60% of it. A single threshold would flicker for any character
/// hovering near it — which is most of them, since arriving at a waypoint
/// decelerates through exactly that speed.
inline bool locomotion_wants_move(LocomotionState& st,
                                  const glm::vec3& pos,
                                  float dt,
                                  float threshold) {
    if (dt <= 0.0f) return st.moving;          // no time passed: no new information
    if (!st.has_last) {                        // first frame: no delta to measure
        st.last_pos = pos;
        st.has_last = true;
        st.speed    = 0.0f;
        st.moving   = false;
        return false;
    }

    // Horizontal speed only. Vertical motion is gravity, terrain following or a
    // rebase — none of which mean "this character is walking".
    glm::vec3 d = pos - st.last_pos;
    d.y = 0.0f;
    st.speed   = glm::length(d) / dt;
    st.last_pos = pos;

    const float start = threshold;
    const float stop  = threshold * 0.6f;
    if (st.moving) st.moving = st.speed > stop;
    else           st.moving = st.speed > start;
    return st.moving;
}

/// A floating-origin rebase teleports the entity, which would otherwise read as
/// an enormous one-frame speed and snap every character into its run cycle.
/// Shift the remembered position by the same amount so the delta stays honest.
inline void locomotion_apply_origin_shift(LocomotionState& st, const glm::vec3& shift) {
    if (st.has_last) st.last_pos += shift;
}

}  // namespace schizo::editor
