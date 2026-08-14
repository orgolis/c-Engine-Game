#pragma once
// ============================================================================
// material_graph_panel — the material graph (4.1) drawn on the generic canvas.
//
// The seam between the two halves: NodeCanvas knows nothing about materials,
// MaterialGraph knows nothing about drawing, and everything material-specific
// lives in the .cpp. That division is what the old logic-graph panel lacked,
// and why none of it could be reused here.
// ============================================================================

#include "material_graph.h"

#include <string>

namespace schizo::editor {

class NodeCanvas;

/// Draw the panel. `out_glsl` receives the generated source when the graph is
/// valid; `out_dirty` is SET (never cleared) when the graph changed, so the
/// caller decides when a recompile is worth doing rather than the panel
/// recompiling on every keystroke.
void draw_material_graph_panel(bool& open,
                               gws::shadergraph::MaterialGraph& graph,
                               NodeCanvas& canvas,
                               std::string& out_glsl,
                               bool& out_dirty);

}  // namespace schizo::editor
