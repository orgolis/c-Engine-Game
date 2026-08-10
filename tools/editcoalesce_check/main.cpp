// ============================================================================
// editcoalesce_check — one drag must be one undo entry.
//
// This is the rule that decides whether field-edit undo is usable or actively
// annoying. ImGui reports a change on EVERY frame the mouse moves, so the naive
// wiring gives forty undo entries for one drag and Ctrl+Z walks back through it
// a pixel at a time. Nobody would use that twice.
//
// The awkward cases are not the happy path, they are the gestures people
// actually make without thinking: dragging back to where you started, clicking
// a checkbox (change and release in one frame), moving to another field without
// letting go, closing the panel mid-drag. Each has an obviously-correct answer
// and no obvious implementation, and none of them can be checked by looking at
// the code — you would have to drag things and count Ctrl+Z presses.
//
// Which is the entire reason the logic is an ImGui-free state machine: every
// one of those gestures is a few lines here instead of a manual test nobody
// repeats.
// ============================================================================
#include "edit_coalescer.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace schizo::editor;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// Stand-in for a serialized component: one byte is enough to model "the value".
std::vector<uint8_t> v(uint8_t x) { return {x}; }

}  // namespace

int main() {
    std::printf("=== editcoalesce_check ===\n\n");

    // ---- 1. a drag across many frames produces exactly ONE entry -----------
    {
        EditCoalescer c;
        int entries = 0;
        CoalescedEdit last{};

        uint8_t value = 10;
        c.observe(v(value));
        // frame 1: mouse goes down and the value starts moving
        for (int frame = 0; frame < 20; ++frame) {
            const uint8_t before_frame = value;
            value = static_cast<uint8_t>(value + 1);          // ImGui edited in place
            if (auto e = c.update(1, "Health", v(value), true, /*interacting=*/true)) {
                ++entries; last = *e;
            }
            (void)before_frame;
            c.observe(v(value));
        }
        check(entries == 0, "nothing is committed while the drag is in progress");

        // mouse released: the value no longer changes and nothing is active
        if (auto e = c.update(1, "Health", v(value), false, /*interacting=*/false)) {
            ++entries; last = *e;
        }
        check(entries == 1, "releasing commits exactly ONE entry for the whole drag");
        check(last.before == v(10), "it records where the drag STARTED, not the last frame");
        check(last.after == v(30), "and where it ended");
        check(last.component == "Health" && last.entity_id == 1, "with the right target");
    }

    // ---- 2. dragging back to the starting value is not an edit -------------
    // Recording it would put a no-op on the undo stack, which reads as Ctrl+Z
    // being broken.
    {
        EditCoalescer c;
        int entries = 0;
        c.observe(v(5));
        c.update(1, "Health", v(9), true, true);      // drag away
        c.observe(v(9));
        c.update(1, "Health", v(5), true, true);      // drag back
        c.observe(v(5));
        if (c.update(1, "Health", v(5), false, false)) ++entries;
        check(entries == 0, "a drag that ends where it began records nothing");
        check(!c.in_progress(), "and the gesture is closed out either way");
    }

    // ---- 3. a click that changes and releases in one frame ------------------
    // Checkboxes and combos do this: changed and not-interacting together.
    // Waiting for a gesture that already ended would drop the edit entirely.
    {
        EditCoalescer c;
        c.observe(v(0));
        auto e = c.update(1, "Health", v(1), true, /*interacting=*/false);
        check(e.has_value(), "a single-frame click commits immediately");
        check(e && e->before == v(0) && e->after == v(1), "with the correct before/after");
    }

    // ---- 4. no change means no entry ---------------------------------------
    // The inspector redraws every frame; merely looking at a value must not
    // fill the undo stack.
    {
        EditCoalescer c;
        int entries = 0;
        for (int frame = 0; frame < 100; ++frame) {
            c.observe(v(7));
            if (c.update(1, "Health", v(7), false, false)) ++entries;
        }
        check(entries == 0, "100 frames of drawing an untouched value record nothing");
    }

    // ---- 5. moving to another field mid-gesture ----------------------------
    // The first edit really happened; dropping it would silently lose work.
    {
        EditCoalescer c;
        c.observe(v(1));
        c.update(1, "Health", v(2), true, true);          // start editing Health
        c.observe(v(2));
        auto e = c.update(1, "Ability State", v(2), true, true);   // jump to another component
        check(e.has_value(), "switching component mid-gesture commits the first edit");
        check(e && e->component == "Health", "attributed to the component that was edited");
        check(e && e->before == v(1), "with the value it had before that edit");
    }

    // ---- 6. switching ENTITY mid-gesture ------------------------------------
    {
        EditCoalescer c;
        c.observe(v(1));
        c.update(7, "Health", v(4), true, true);
        c.observe(v(4));
        auto e = c.update(8, "Health", v(4), true, true);
        check(e.has_value(), "switching entity mid-gesture commits the first edit");
        check(e && e->entity_id == 7, "attributed to the entity that was edited");
    }

    // ---- 7. the panel closing mid-drag --------------------------------------
    // Selection changes, the inspector stops drawing. The edit happened.
    {
        EditCoalescer c;
        c.observe(v(3));
        c.update(1, "Health", v(8), true, true);
        auto e = c.flush(v(8));
        check(e.has_value(), "flush commits an edit interrupted by the panel closing");
        check(e && e->before == v(3) && e->after == v(8), "with the full range of the gesture");
        check(!c.in_progress(), "and clears the in-progress state");
        check(!c.flush(v(8)).has_value(), "a second flush commits nothing");
    }

    // ---- 8. flush with nothing in progress ----------------------------------
    {
        EditCoalescer c;
        check(!c.flush(v(1)).has_value(), "flushing an idle coalescer is harmless");
    }

    // ---- 9. cancel discards --------------------------------------------------
    // For a component being deleted out from under the edit: keeping it would
    // resurrect a component the user just removed.
    {
        EditCoalescer c;
        c.observe(v(1));
        c.update(1, "Health", v(2), true, true);
        check(c.in_progress(), "gesture is open");
        c.cancel();
        check(!c.in_progress(), "cancel closes it");
        check(!c.flush(v(2)).has_value(), "and nothing is committed afterwards");
    }

    // ---- 10. back-to-back drags stay separate --------------------------------
    {
        EditCoalescer c;
        int entries = 0;
        std::vector<CoalescedEdit> got;

        c.observe(v(0));
        c.update(1, "Health", v(3), true, true);
        c.observe(v(3));
        if (auto e = c.update(1, "Health", v(3), false, false)) { got.push_back(*e); ++entries; }

        c.observe(v(3));
        c.update(1, "Health", v(9), true, true);
        c.observe(v(9));
        if (auto e = c.update(1, "Health", v(9), false, false)) { got.push_back(*e); ++entries; }

        check(entries == 2, "two separate drags produce two entries");
        check(got.size() == 2 && got[0].before == v(0) && got[0].after == v(3),
              "the first covers its own range");
        check(got.size() == 2 && got[1].before == v(3) && got[1].after == v(9),
              "and the second starts where the first ended");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL editcoalesce_check\n");
    return g_fail ? 1 : 0;
}
