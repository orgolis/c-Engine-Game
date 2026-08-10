#pragma once

// Visual node-canvas editor for the scene logic graph (events -> actions). Draws
// the bridge's LogicGraph on a pannable canvas: nodes with param fields, pins, and
// bezier wires; right-click to add nodes, click pins to connect, right-click a node
// to delete. Lives in its own TU so main.cpp needs no EnTT/ECS include.

#include "edit_coalescer.h"

#include <functional>

namespace schizo::editor {

class EcsSceneBridge;

// Render the logic-graph editor window. `open` drives the close button.
// Replace the scene's logic graph from its text form. Lives in this TU because
// main.cpp deliberately includes no EnTT/ECS headers -- the same boundary that
// keeps component_inspector.cpp separate. Used by the undo path.
void apply_logic_graph_text(EcsSceneBridge& bridge, const std::string& text);

// `coalescer` and `on_edit` are optional. When supplied, graph edits are
// coalesced into ONE undo entry per gesture -- dragging a node moves it every
// frame, so without this a single drag would fill the undo stack. The graph is
// snapshotted as text (logic_to_text), which is small enough to store whole,
// unlike a heightmap.
void draw_logic_graph_panel(EcsSceneBridge& bridge, bool* open,
                            EditCoalescer* coalescer = nullptr,
                            const std::function<void(const CoalescedEdit&)>& on_edit = {});

}  // namespace schizo::editor
