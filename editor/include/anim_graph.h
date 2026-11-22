#pragma once
// ============================================================================
// anim_graph — an editable animation state machine (4.6).
//
// The runtime AnimationStateMachine has existed for a while and is authored in
// C++: states are added by calling AddState, transitions by calling
// AddTransition. That makes "the character should blend to a limp below 30%
// health" a code change, which is the same complaint 4.1 answered for shading.
//
// This is the DOCUMENT: plain data describing states and transitions, editable,
// serialisable, and convertible into the runtime object. Kept separate from the
// runtime type on purpose -- the runtime one owns a skeleton, holds live
// playback state and is mid-blend at any given moment, none of which an editor
// should be poking at while the user drags a node.
//
// Free of ImGui and of the renderer, so the rules that matter can be tested:
// which transitions are reachable, what happens when a state is deleted, and
// whether the machine has a valid entry point at all.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::editor {

/// Mirrors AnimCondOp in the runtime state machine. Duplicated deliberately:
/// this header must not drag the renderer in, and the two are converted at the
/// one place that builds a runtime machine.
enum class AnimGraphCondOp : uint8_t {
    Greater, GreaterEqual, Less, LessEqual, Equals, NotEquals, True, False, TriggerSet
};

inline const char* cond_op_name(AnimGraphCondOp op) {
    switch (op) {
        case AnimGraphCondOp::Greater:      return ">";
        case AnimGraphCondOp::GreaterEqual: return ">=";
        case AnimGraphCondOp::Less:         return "<";
        case AnimGraphCondOp::LessEqual:    return "<=";
        case AnimGraphCondOp::Equals:       return "==";
        case AnimGraphCondOp::NotEquals:    return "!=";
        case AnimGraphCondOp::True:         return "is true";
        case AnimGraphCondOp::False:        return "is false";
        case AnimGraphCondOp::TriggerSet:   return "triggered";
    }
    return "?";
}

/// True for operators that compare against a threshold. The others read a bool
/// or a trigger, and showing a threshold box beside "is true" invites people to
/// set a number that does nothing.
inline bool cond_uses_threshold(AnimGraphCondOp op) {
    return op != AnimGraphCondOp::True && op != AnimGraphCondOp::False &&
           op != AnimGraphCondOp::TriggerSet;
}

struct AnimGraphCondition {
    std::string     param;
    AnimGraphCondOp op        = AnimGraphCondOp::Greater;
    float           threshold = 0.0f;
};

/// What a state plays: one clip, a blend space of clips over one or two
/// parameters, or nothing (an upper layer letting the one below show).
enum class AnimGraphStateKind : uint8_t { Clip, Blend1D, Blend2D, None };

inline const char* state_kind_name(AnimGraphStateKind k) {
    switch (k) {
        case AnimGraphStateKind::Clip:    return "clip";
        case AnimGraphStateKind::Blend1D: return "blend1d";
        case AnimGraphStateKind::Blend2D: return "blend2d";
        case AnimGraphStateKind::None:    return "none";
    }
    return "clip";
}

/// A clip in a blend space, where it sits; `measured`: at the velocity the clip
/// was measured to move at (a locomotion clip), x and y only where it cannot be.
struct AnimGraphSample {
    std::string clip;
    float x = 0.0f, y = 0.0f;
    bool measured = false;
};

struct AnimGraphState {
    uint32_t    id   = 0;
    std::string name = "State";
    int         clip_index      = 0;
    bool        loop            = true;
    float       speed           = 1.0f;
    bool        apply_root_motion = false;
    float       x = 0.0f, y = 0.0f;      // canvas position
    // The runtime graph (anim_graph_runtime.h):
    AnimGraphStateKind kind = AnimGraphStateKind::Clip;
    std::string clip;                    // the clip by name (used over clip_index when set)
    std::vector<AnimGraphSample> samples;   // a blend space's
    std::string param_x, param_y;        // a blend space's parameters
    bool        radial = false;          // outside the space: clamped from the origin (locomotion)
    std::string rate_param;              // a float parameter multiplying the speed
    std::string sync;                    // sync group: in step with the other states of it
    int         layer = 0;
};

/// A parameter gameplay sets: 0 float, 1 bool, 2 trigger.
struct AnimGraphParam {
    std::string name;
    int   type = 0;
    float init = 0.0f;
};
/// A layer above the first poses the bones under its mask bone (none: all).
struct AnimGraphLayer {
    std::string name;
    std::string mask_bone;
    uint32_t    entry = 0;               // its entry state (layer 0's is the graph's)
};
/// A slot montages play on (a shot on "upper"), on the bones under its mask bone.
struct AnimGraphSlot {
    std::string name;
    std::string mask_bone;
};

struct AnimGraphTransition {
    uint32_t from = 0;                   // state id; kAnyState for "Any State"
    uint32_t to   = 0;
    std::vector<AnimGraphCondition> conditions;   // ALL must hold
    float blend_duration = 0.2f;
    bool  has_exit_time  = false;
    float exit_time      = 0.9f;
};

/// The pseudo-state every "interrupt from anywhere" transition comes from --
/// hit reactions, deaths, teleports. Without it every such transition has to be
/// duplicated from every state, and the one somebody forgets is the bug.
inline constexpr uint32_t kAnyState = 0xFFFFFFFFu;

class AnimGraph {
public:
    AnimGraph();

    /// Empty the document completely -- no states, no transitions, ids restarted.
    /// Exists for LOADING: the constructor seeds an "Idle" state so a fresh
    /// canvas is never an empty void, and a loader that did not clear would
    /// leave that seed sitting alongside the file's own states, gaining one
    /// orphan every time a graph was opened.
    void clear();

    uint32_t add_state(const std::string& name, float x = 0.0f, float y = 0.0f);
    void     remove_state(uint32_t id);
    AnimGraphState*       state(uint32_t id);
    const AnimGraphState* state(uint32_t id) const;

    /// Add a transition. Refused when either end does not exist, or when it
    /// would leave a state pointing at itself -- a self-transition either never
    /// fires or fires forever, and neither is what anyone drew.
    bool add_transition(uint32_t from, uint32_t to);
    void remove_transition(size_t index);

    const std::vector<AnimGraphState>&      states()      const { return states_; }
    std::vector<AnimGraphState>&            states()            { return states_; }
    const std::vector<AnimGraphTransition>& transitions() const { return transitions_; }
    std::vector<AnimGraphTransition>&       transitions()       { return transitions_; }

    uint32_t entry_state() const { return entry_; }
    void set_entry_state(uint32_t id);

    std::vector<AnimGraphParam>& params() { return params_; }
    const std::vector<AnimGraphParam>& params() const { return params_; }
    /// Layers above the first (index 1 up is layers_[index - 1]).
    std::vector<AnimGraphLayer>& layers() { return layers_; }
    const std::vector<AnimGraphLayer>& layers() const { return layers_; }
    std::vector<AnimGraphSlot>& slots() { return slots_; }
    const std::vector<AnimGraphSlot>& slots() const { return slots_; }
    /// A layer's entry state (0: the graph's entry).
    uint32_t layer_entry(int layer) const {
        if (layer <= 0) return entry_;
        return static_cast<size_t>(layer - 1) < layers_.size() ? layers_[static_cast<size_t>(layer - 1)].entry : 0;
    }

    /// States that cannot be reached from the entry state, ignoring Any-State
    /// transitions.
    ///
    /// Worth surfacing rather than leaving to be discovered: an unreachable
    /// state is authored work that will never play, and the machine gives no
    /// hint of it at runtime -- the character simply never does the thing.
    std::vector<uint32_t> unreachable_states() const;

    /// Human-readable problems: no entry, dangling transitions, states with no
    /// way out. Empty means the graph is sound.
    std::vector<std::string> validate() const;

private:
    std::vector<AnimGraphState>      states_;
    std::vector<AnimGraphTransition> transitions_;
    std::vector<AnimGraphParam>      params_;
    std::vector<AnimGraphLayer>      layers_;
    std::vector<AnimGraphSlot>       slots_;
    uint32_t next_id_ = 1;
    uint32_t entry_   = 0;
};

}  // namespace schizo::editor
