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
/// Bring `path` up in a tab of the panel: the tab that already has it, or a
/// new one (an empty script if the file does not exist yet).
void open_logic_script(const std::string& path, bool* open_flag);
/// The text of a new, empty logic script (Begin Play and Tick, nothing wired),
/// for whoever creates the file: the asset browser, the Inspector.
std::string new_logic_script_text(const std::string& name);
/// Whether any open script has changes that were not saved.
bool logic_scripts_unsaved();

}  // namespace schizo::editor
