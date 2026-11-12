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
#include "vscript/registry.h"

namespace schizo::editor {

struct LogicApiSlot {
    const ScriptApi* api = nullptr;
};

/// Registers one node per ScriptApi entry. `slot` must outlive the registry.
void register_engine_logic_nodes(schizo::vs::Registry& reg, LogicApiSlot* slot);

}  // namespace schizo::editor
