#pragma once
// ============================================================================
// logic_nodes.h -- the engine's nodes for logic graphs.
//
// One list, used twice: the ".lgraph" script backend runs these nodes, and the
// graph editor shows them in its palette. The nodes reach the engine through a
// ScriptApi table that the caller points `api` at before running anything; the
// editor leaves it null, because it only needs to know what pins a node has.
// ============================================================================

#include "script_api.h"
#include "vscript/graph.h"
#include "vscript/registry.h"

#include <string>

namespace schizo::editor {

struct LogicApiSlot {
    const ScriptApi* api = nullptr;
};

/// Registers one node per ScriptApi entry. `slot` must outlive the registry.
void register_engine_logic_nodes(schizo::vs::Registry& reg, LogicApiSlot* slot);

/// Where the script file that `from` names as its parent or library is: the
/// path as written (project-relative, like a Script component's), or else
/// beside `from`. Empty when neither exists.
std::string resolve_logic_script(const std::string& named, const std::string& from);
/// Read a logic script file. False, with `error`, when it cannot be read.
bool read_logic_script(const std::string& path, schizo::vs::Script& out, std::string* error = nullptr);
/// A script file by one spelling, however the path was written: what the
/// debugger and the editor compare files by.
std::string logic_script_key(const std::string& path);

}  // namespace schizo::editor
