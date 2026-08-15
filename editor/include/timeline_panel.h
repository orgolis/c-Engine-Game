#pragma once
// ============================================================================
// timeline_panel — the sequencer's surface (4.4). Model lives in sequence.h.
// ============================================================================

#include <cstdint>
#include <memory>

namespace schizo::scene { class Scene; }

namespace schizo::editor {

class Sequence;

/// Transport state, kept by the caller so it survives the panel being closed.
struct TimelineState {
    float time      = 0.0f;
    bool  playing   = false;
    bool  loop      = true;
    /// Set for one frame when the playhead is dragged. Values are applied while
    /// playing OR scrubbing and at no other time -- a timeline that wrote
    /// transforms every frame would fight the gizmo, and whoever touched the
    /// mouse last would win.
    bool  scrubbing = false;
};

void draw_timeline_panel(bool& open,
                         Sequence& seq,
                         TimelineState& st,
                         const std::shared_ptr<schizo::scene::Scene>& scene,
                         uint32_t selected_entity_id,
                         float delta_time);

}  // namespace schizo::editor
