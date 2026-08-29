#pragma once
// ============================================================================
// anim_graph_panel — the animation state machine on the generic canvas (4.6).
//
// The second consumer of NodeCanvas, and the reason it exists: a state machine
// is about as unlike a material graph as this editor gets, so needing to change
// the canvas for it would have meant "reusable" was wrong. The canvas is
// unchanged by this panel.
// ============================================================================

#include "document_bar.h"

namespace schizo::editor {

class AnimGraph;
class NodeCanvas;

/// `out_modified` is SET when the graph changes, never cleared.
void draw_anim_graph_panel(bool& open, AnimGraph& graph, NodeCanvas& canvas,
                           bool& out_modified, DocumentFile* doc = nullptr);

}  // namespace schizo::editor
