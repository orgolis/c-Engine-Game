#include "edit_coalescer.h"

#include <cstdint>

namespace schizo::editor {

std::optional<CoalescedEdit> EditCoalescer::update(uint32_t entity_id,
                                                   const std::string& component,
                                                   const std::vector<uint8_t>& value,
                                                   bool changed,
                                                   bool interacting) {
    // Switching target mid-gesture: commit what the old one accumulated rather
    // than dropping it. The user really did change that value, and losing it
    // silently is the failure this class exists to avoid.
    if (active_ && (entity_id_ != entity_id || component_ != component)) {
        CoalescedEdit done{entity_id_, component_, std::move(before_), value};
        active_ = false;
        // Only a real difference is worth an undo entry.
        if (done.before != done.after) return done;
        return std::nullopt;
    }

    if (!active_) {
        if (!changed) return std::nullopt;      // nothing happening
        // A change arrived. `value` is the state AFTER this frame's edit, so the
        // pre-edit state has to have been captured earlier — the caller passes
        // the value it read before running the widgets (see update_before()).
        active_    = true;
        entity_id_ = entity_id;
        component_ = component;
        before_    = pre_;
        // A click that changes something without being held (a checkbox, a
        // combo) reports changed and non-interacting in the same frame; commit
        // it now rather than waiting for a gesture that already ended.
        if (!interacting) {
            CoalescedEdit done{entity_id_, component_, std::move(before_), value};
            active_ = false;
            if (done.before != done.after) return done;
            return std::nullopt;
        }
        return std::nullopt;
    }

    // Gesture in progress. Commit when the interaction ends.
    if (!interacting) {
        CoalescedEdit done{entity_id_, component_, std::move(before_), value};
        active_ = false;
        // Dragging back to where it started is not an edit. Recording it would
        // put a no-op on the undo stack, which reads as a broken Ctrl+Z.
        if (done.before != done.after) return done;
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<CoalescedEdit> EditCoalescer::flush(const std::vector<uint8_t>& final_value) {
    if (!active_) return std::nullopt;
    CoalescedEdit done{entity_id_, component_, std::move(before_), final_value};
    active_ = false;
    if (done.before != done.after) return done;
    return std::nullopt;
}

}  // namespace schizo::editor
