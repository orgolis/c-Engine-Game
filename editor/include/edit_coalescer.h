#pragma once

// ============================================================================
// EditCoalescer — turn a drag into ONE undo entry.
//
// THE PROBLEM. An inspector field is edited by dragging: ImGui reports a change
// on every frame the mouse moves. Pushing an undo command per report gives you
// forty entries for one drag, and Ctrl+Z then walks back through the drag one
// pixel at a time. That is worse than no undo — the plan's own line on this is
// "partial undo teaches people not to rely on it", and unusable undo teaches
// the same lesson faster.
//
// So an edit has to be COALESCED: capture the value when the interaction
// starts, and commit a single before/after pair when it ends.
//
// WHY THIS IS A SEPARATE, ImGui-FREE CLASS. The rule "one drag, one entry" is
// exactly the part that is easy to get subtly wrong — dragging back to the
// starting value, clicking without moving, switching fields mid-gesture, the
// inspector closing while the mouse is down. None of that can be tested by
// looking at it; you would have to drag things and count Ctrl+Z presses. Kept
// as a pure state machine over two booleans and a byte blob, every case is a
// headless assertion (tools/editcoalesce_check).
//
// The caller feeds it what ImGui reports; it decides when an undo entry exists.
// ============================================================================

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace schizo::editor {

// A completed edit, ready to become one undo command.
struct CoalescedEdit {
    uint32_t             entity_id = 0;
    std::string          component;    // authorable component name, or "Transform"
    std::vector<uint8_t> before;
    std::vector<uint8_t> after;
};

class EditCoalescer {
public:
    // Call BEFORE this frame's widgets run, with the value as it currently
    // stands. ImGui edits in place, so by the time `changed` is known the
    // original is already gone — this is the only moment the pre-edit state
    // exists. Cheap: it is only kept until a gesture actually starts.
    void observe(std::vector<uint8_t> value) { pre_ = std::move(value); }

    // Call once per frame for the value being inspected.
    //
    //   entity_id / component : what is being edited
    //   value                 : its serialized state THIS frame
    //   changed               : a widget reported a change this frame
    //   interacting           : ImGui::IsAnyItemActive() — the mouse is down in
    //                           a widget, i.e. the gesture is still going
    //
    // Returns a CoalescedEdit exactly once per completed gesture, and only when
    // the value actually differs from where it started.
    std::optional<CoalescedEdit> update(uint32_t entity_id,
                                        const std::string& component,
                                        const std::vector<uint8_t>& value,
                                        bool changed,
                                        bool interacting);

    // The inspector stopped drawing this value (selection changed, panel
    // closed, component removed). Commits whatever was in progress, because the
    // edit really did happen — dropping it would silently lose work.
    std::optional<CoalescedEdit> flush(const std::vector<uint8_t>& final_value);

    // Abandon without committing. For cases where the edit must not be kept,
    // such as the component being deleted out from under it.
    void cancel() { active_ = false; }

    bool in_progress() const { return active_; }

private:
    bool                 active_ = false;
    uint32_t             entity_id_ = 0;
    std::string          component_;
    std::vector<uint8_t> before_;   // state when the gesture began
    std::vector<uint8_t> pre_;      // state before THIS frame's widgets ran
};

}  // namespace schizo::editor
