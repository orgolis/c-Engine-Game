// ============================================================================
// anim_graph — see anim_graph.h.
// ============================================================================
#include "anim_graph.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>

namespace schizo::editor {

AnimGraph::AnimGraph() {
    // A machine with no states cannot be edited into one usefully -- there is
    // nothing to drag a transition from. Starting with an entry state means the
    // canvas is never an empty void with no obvious first move.
    entry_ = add_state("Idle", 80.0f, 120.0f);
}

uint32_t AnimGraph::add_state(const std::string& name, float x, float y) {
    AnimGraphState s;
    s.id   = next_id_++;
    s.name = name;
    s.x    = x;
    s.y    = y;
    states_.push_back(s);
    return s.id;
}

void AnimGraph::remove_state(uint32_t id) {
    states_.erase(std::remove_if(states_.begin(), states_.end(),
                                 [id](const AnimGraphState& s) { return s.id == id; }),
                  states_.end());

    // Transitions referencing it go too. A dangling transition is worse than a
    // missing one: it looks authored and silently never fires.
    transitions_.erase(std::remove_if(transitions_.begin(), transitions_.end(),
                                      [id](const AnimGraphTransition& t) {
                                          return t.from == id || t.to == id;
                                      }),
                       transitions_.end());

    // The entry state cannot dangle either -- promote the first survivor.
    if (entry_ == id) entry_ = states_.empty() ? 0 : states_.front().id;
}

AnimGraphState* AnimGraph::state(uint32_t id) {
    for (AnimGraphState& s : states_) if (s.id == id) return &s;
    return nullptr;
}
const AnimGraphState* AnimGraph::state(uint32_t id) const {
    for (const AnimGraphState& s : states_) if (s.id == id) return &s;
    return nullptr;
}

bool AnimGraph::add_transition(uint32_t from, uint32_t to) {
    if (from == to) return false;                       // never fires, or never stops
    if (from != kAnyState && !state(from)) return false;
    if (!state(to)) return false;
    // Any State -> X is legitimate; X -> Any State is not, because Any State is
    // a source, not a destination.
    if (to == kAnyState) return false;

    AnimGraphTransition t;
    t.from = from;
    t.to   = to;
    transitions_.push_back(t);
    return true;
}

void AnimGraph::remove_transition(size_t index) {
    if (index < transitions_.size())
        transitions_.erase(transitions_.begin() + static_cast<long>(index));
}

void AnimGraph::set_entry_state(uint32_t id) {
    if (state(id)) entry_ = id;
}

std::vector<uint32_t> AnimGraph::unreachable_states() const {
    std::vector<uint32_t> out;
    if (states_.empty()) return out;

    // Breadth-first from the entry. Any-State transitions are DELIBERATELY
    // ignored: they can reach anything, so counting them would call every state
    // reachable and the check would report nothing, ever.
    std::unordered_set<uint32_t> seen;
    std::vector<uint32_t> frontier;
    if (state(entry_)) { seen.insert(entry_); frontier.push_back(entry_); }

    while (!frontier.empty()) {
        const uint32_t cur = frontier.back();
        frontier.pop_back();
        for (const AnimGraphTransition& t : transitions_) {
            if (t.from != cur) continue;
            if (seen.insert(t.to).second) frontier.push_back(t.to);
        }
    }

    for (const AnimGraphState& s : states_)
        if (!seen.count(s.id)) out.push_back(s.id);
    return out;
}

std::vector<std::string> AnimGraph::validate() const {
    std::vector<std::string> problems;

    if (states_.empty()) {
        problems.push_back("the graph has no states");
        return problems;
    }
    if (!state(entry_))
        problems.push_back("no entry state is set — nothing would play");

    for (const AnimGraphTransition& t : transitions_) {
        if (t.from != kAnyState && !state(t.from))
            problems.push_back("a transition starts from a state that no longer exists");
        if (!state(t.to))
            problems.push_back("a transition points at a state that no longer exists");
        if (t.conditions.empty() && !t.has_exit_time)
            problems.push_back("a transition out of '" +
                               std::string(state(t.from) ? state(t.from)->name : "Any State") +
                               "' has no conditions and no exit time, so it fires immediately");
    }

    for (uint32_t id : unreachable_states()) {
        const AnimGraphState* s = state(id);
        problems.push_back("'" + (s ? s->name : std::to_string(id)) +
                           "' cannot be reached from the entry state");
    }
    return problems;
}

}  // namespace schizo::editor
