#pragma once
// ============================================================================
// logic_script_panel -- the graph editor for ".lgraph" logic scripts.
//
// Edits one script file at a time: its variables, its event graphs and
// functions, and the nodes and wires in them. It compiles the script after
// every change and marks the nodes the compiler complains about, so a graph
// that will not run says so while it is being built, not in Play mode.
// ============================================================================

#include <string>

namespace schizo::editor {

/// Draw the panel. `open` is cleared when the user closes it.
void draw_logic_script_panel(bool* open);
/// Load `path` into the panel (creating an empty script if the file does not
/// exist yet) and bring the panel up.
void open_logic_script(const std::string& path, bool* open_flag);

}  // namespace schizo::editor
