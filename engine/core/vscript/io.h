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
#include <string_view>

namespace schizo::vs {

std::string to_text(const Script& script);
/// Returns false and describes the problem in `error` when the text is not a
/// script this version can read.
bool from_text(std::string_view text, Script& out, std::string* error = nullptr);

}  // namespace schizo::vs
