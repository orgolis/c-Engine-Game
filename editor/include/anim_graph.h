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

struct AnimGraphState {
    uint32_t    id   = 0;
    std::string name = "State";
    int         clip_index      = 0;
    bool        loop            = true;
    float       speed           = 1.0f;
    bool        apply_root_motion = false;
    float       x = 0.0f, y = 0.0f;      // canvas position
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
    uint32_t next_id_ = 1;
    uint32_t entry_   = 0;
};

}  // namespace schizo::editor
