#pragma once
// ============================================================================
// logic_debug -- what the graph editor can ask of the logic scripts that are
// running in Play mode: where they are paused, what ran, what is on a pin.
//
// The logic host (script_host_logic.cpp) implements it. The graph editor only
// sees this interface, so it holds no pointer into the VM and has no say in
// when a script instance dies.
//
// A script is named by its FILE, as the Script component names it. Breakpoints
// are kept per file, so they can be set before Play starts, apply to every
// object that runs the script, and survive a hot reload.
// ============================================================================

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace schizo::editor {

/// One running logic script: the object it is on and the file it came from.
struct LogicDebugTarget {
    uint32_t    entity = 0;
    std::string path;
};

enum class LogicStep : uint8_t { Continue, Into, Over, Out };

class LogicDebugger {
public:
    virtual ~LogicDebugger() = default;

    /// Every logic script that is running, ordered by entity id.
    virtual std::vector<LogicDebugTarget> targets() const = 0;

    /// Replace the breakpoints of one script file: (graph index, node id).
    virtual void set_breakpoints(const std::string& path, const std::vector<std::pair<int, int>>& graph_node) = 0;

    /// Ask for tracing (which node ran when). Call it every frame it is
    /// wanted: tracing switches itself off about a second after the last call.
    virtual void want_tracing() = 0;

    /// Whether a script is stopped at a breakpoint, and where.
    virtual bool paused(LogicDebugTarget* where = nullptr, int* graph = nullptr, int* node = nullptr) const = 0;
    /// Continue or step. Returns when the script finishes or stops again.
    virtual void resume(LogicStep how) = 0;

    /// Seconds since that node last ran on that object; negative if it has
    /// not run since tracing was switched on.
    virtual double seconds_since_run(uint32_t entity, int graph, int node) const = 0;
    /// The value on a node's data output (`slot` counts its data outputs from
    /// 0), as text. False when there is none to show: a pure node, which is
    /// computed where it is used, or a function that is not the paused one.
    virtual bool output_text(uint32_t entity, int graph, int node, int slot, std::string& text) const = 0;
    /// The current value of one of the script's variables on that object.
    virtual bool variable_text(uint32_t entity, const std::string& name, std::string& text) const = 0;
};

/// The debugger of the logic host that was created last (the editor makes
/// one); nullptr while there is none.
LogicDebugger* logic_debugger();

}  // namespace schizo::editor
