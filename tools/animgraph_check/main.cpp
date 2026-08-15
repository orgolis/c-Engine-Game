// ============================================================================
// animgraph_check — the state machine document stays sound while it is edited.
//
// A state machine fails quietly. A transition left pointing at a deleted state
// never fires; an unreachable state is authored work that simply never plays;
// a transition with no conditions fires the instant it is entered. None of
// those throw, none of them log, and the symptom is always "the character does
// not do the thing" — which points at the animation, not at the graph.
//
// So these assert what survives editing, and what the editor should be telling
// the user about before they go looking in the wrong place.
// ============================================================================
#include "anim_graph.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace schizo::editor;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool has_problem_containing(const AnimGraph& g, const std::string& needle) {
    for (const auto& p : g.validate())
        if (p.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

int main() {
    std::printf("=== animgraph_check ===\n\n");

    // ---- 1. a new graph is usable immediately --------------------------------
    // An empty canvas with no states has no obvious first move: there is
    // nothing to drag a transition from.
    {
        AnimGraph g;
        check(g.states().size() == 1, "a new graph starts with one state");
        check(g.state(g.entry_state()) != nullptr, "and that state is the entry point");
    }

    // ---- 2. deleting a state takes its transitions with it -------------------
    // A dangling transition is worse than a missing one: it looks authored and
    // silently never fires.
    {
        AnimGraph g;
        const uint32_t idle = g.entry_state();
        const uint32_t walk = g.add_state("Walk");
        const uint32_t run  = g.add_state("Run");
        g.add_transition(idle, walk);
        g.add_transition(walk, run);
        check(g.transitions().size() == 2, "two transitions authored");

        g.remove_state(walk);
        check(g.states().size() == 2, "deleting a state removes it");
        check(g.transitions().empty(),
              "and every transition touching it, leaving none dangling");
    }

    // ---- 3. the ENTRY state can never dangle ---------------------------------
    // If it did, nothing would play and the machine would look simply dead.
    {
        AnimGraph g;
        const uint32_t idle = g.entry_state();
        g.add_state("Walk");
        g.remove_state(idle);
        check(g.state(g.entry_state()) != nullptr,
              "deleting the entry state promotes another rather than leaving none");
    }

    // ---- 4. self-transitions are refused --------------------------------------
    // One either never fires or fires forever, and neither is what was drawn.
    {
        AnimGraph g;
        const uint32_t s = g.entry_state();
        check(!g.add_transition(s, s), "a state cannot transition to itself");
    }

    // ---- 5. transitions to nowhere are refused --------------------------------
    {
        AnimGraph g;
        const uint32_t s = g.entry_state();
        check(!g.add_transition(s, 9999u), "a transition to a non-existent state is refused");
        check(!g.add_transition(9999u, s), "and so is one from a non-existent state");
        check(!g.add_transition(s, kAnyState),
              "and one INTO Any State, which is a source and not a destination");
        check(g.add_transition(kAnyState, s),
              "but Any State -> a real state is exactly what it is for");
    }

    // ---- 6. UNREACHABLE STATES are found --------------------------------------
    // Authored work that will never play, with no runtime hint that it exists.
    {
        AnimGraph g;
        const uint32_t idle = g.entry_state();
        const uint32_t walk = g.add_state("Walk");
        const uint32_t ghost = g.add_state("Ghost");     // nothing points at it
        g.add_transition(idle, walk);

        const auto un = g.unreachable_states();
        check(un.size() == 1 && un[0] == ghost, "a state nothing points at is reported");
        check(has_problem_containing(g, "Ghost"), "and named in validate()");

        g.add_transition(walk, ghost);
        check(g.unreachable_states().empty(), "connecting it clears the report");
    }

    // ---- 7. Any-State transitions do NOT mask unreachability -------------------
    // They can reach anything, so counting them would call every state
    // reachable and the check would report nothing, ever -- a validator that
    // never complains is one nobody reads.
    {
        AnimGraph g;
        const uint32_t idle  = g.entry_state();
        const uint32_t death = g.add_state("Death");
        const uint32_t orphan = g.add_state("Orphan");
        g.add_transition(kAnyState, death);              // reachable from anywhere

        const auto un = g.unreachable_states();
        check(std::find(un.begin(), un.end(), orphan) != un.end(),
              "an orphan is still reported despite an Any-State transition existing");
        check(std::find(un.begin(), un.end(), death) != un.end(),
              "and Any-State targets are not counted as reachable — otherwise the "
              "check would never fire at all");
        (void)idle;
    }

    // ---- 8. a transition with no conditions is flagged --------------------------
    // It fires the instant the state is entered, so the state it leaves is
    // never actually seen. That looks like the animation not playing.
    {
        AnimGraph g;
        const uint32_t idle = g.entry_state();
        const uint32_t walk = g.add_state("Walk");
        g.add_transition(idle, walk);
        check(has_problem_containing(g, "no conditions"),
              "an unconditional transition is reported as firing immediately");

        g.transitions()[0].has_exit_time = true;
        check(!has_problem_containing(g, "no conditions"),
              "unless it has an exit time, which is a legitimate way to chain states");
    }

    // ---- 9. a sound graph reports nothing ---------------------------------------
    // A validator that always complains is one people learn to ignore.
    {
        AnimGraph g;
        const uint32_t idle = g.entry_state();
        const uint32_t walk = g.add_state("Walk");
        g.add_transition(idle, walk);
        g.transitions().back().conditions.push_back({"speed", AnimGraphCondOp::Greater, 0.1f});
        g.add_transition(walk, idle);
        g.transitions().back().conditions.push_back({"speed", AnimGraphCondOp::LessEqual, 0.1f});
        check(g.validate().empty(), "a complete idle/walk machine reports no problems");
    }

    // ---- 10. threshold-less operators are identified -----------------------------
    // Showing a threshold box beside "is true" invites setting a number that
    // does nothing.
    {
        check(cond_uses_threshold(AnimGraphCondOp::Greater), "'>' takes a threshold");
        check(!cond_uses_threshold(AnimGraphCondOp::True), "'is true' does not");
        check(!cond_uses_threshold(AnimGraphCondOp::TriggerSet), "nor does a trigger");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL animgraph_check\n");
    return g_fail ? 1 : 0;
}
