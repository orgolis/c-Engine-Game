#pragma once

// Visual node-canvas editor for the scene logic graph (events -> actions). Draws
// the bridge's LogicGraph on a pannable canvas: nodes with param fields, pins, and
// bezier wires; right-click to add nodes, click pins to connect, right-click a node
// to delete. Lives in its own TU so main.cpp needs no EnTT/ECS include.

namespace schizo::editor {

class EcsSceneBridge;

// Render the logic-graph editor window. `open` drives the close button.
void draw_logic_graph_panel(EcsSceneBridge& bridge, bool* open);

}  // namespace schizo::editor
