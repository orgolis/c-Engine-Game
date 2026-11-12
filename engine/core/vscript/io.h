#pragma once

// ============================================================================
// vscript/io.h -- a Script as text.
// ============================================================================
//
// One record per line, fields separated by tabs, so a saved script diffs line
// by line in version control. Tabs, newlines and backslashes inside a field
// are escaped. Pins are written by name.
//
//   VSCRIPT 2
//   name    <script name>
//   var     <name> <type> <exposed 0|1> <default> <category> <tooltip>
//   graph   <event|function> <name> <pure 0|1> <next id>
//   input   <name> <type> <default>          (function signature)
//   output  <name> <type> <default>
//   local   <name> <type> <default>
//   node    <id> <def> <x> <y> <name> <targ> <extra> <comment>
//   case    <node id> <label>
//   param   <node id> <name> <type> <default>
//   pin     <node id> <pin> <type> <literal>
//   link    <from id> <from pin> <to id> <to pin>
//   end

#include "vscript/graph.h"

#include <string>
#include <vector>
#include <string_view>

namespace schizo::vs {

std::string to_text(const Script& script);
/// Returns false and describes the problem in `error` when the text is not a
/// script this version can read.
bool from_text(std::string_view text, Script& out, std::string* error = nullptr);

/// Copy and paste. The selected nodes and the wires between them, in the file
/// format: text, so a selection can go to another graph, another script,
/// another editor, or a message to someone.
std::string selection_text(const Graph& g, const std::vector<int>& node_ids);
/// Add a copied selection to `g` under new ids, its top-left corner at (x, y).
/// Returns the new nodes' ids; empty when `text` is not a selection (any other
/// clipboard text, or a whole script). A function's entry node is not pasted:
/// a function has exactly one.
std::vector<int> paste_selection(Graph& g, std::string_view text, float x, float y);

}  // namespace schizo::vs
