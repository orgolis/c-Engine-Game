#pragma once
// ============================================================================
// link -- several script files become the one script the compiler is given.
//
// A script can name a PARENT. It then has everything the parent has (types,
// variables, dispatchers, functions, events), and where it defines a function
// or an event of the same name, its own is the one that runs: the parent's is
// kept under "Parent::<name>" so the child can still call it. A call inside
// the parent to a function the child replaced runs the child's.
//
// A script can also IMPORT libraries: scripts whose functions it may call as
// if they were its own. Only functions come along, not variables or events.
//
// The compiler knows nothing of this. link() hands it one flat script, with
// the script's own graphs first and in file order, so a graph index in the
// file is the same graph index in the compiled program, and reports for every
// graph which file it came from (the debugger needs that: a breakpoint is
// kept by file).
//
// Types are told apart by their position in a script's list. The script's own
// come first and keep their numbers; a parent's or a library's are added
// after them, and everything that came from that file is renumbered to match.
// ============================================================================

#include "graph.h"
#include "registry.h"

#include <functional>
#include <string>
#include <vector>

namespace schizo::vs {

/// Reads the script a path names. False when there is none to read.
using ScriptLoader = std::function<bool(const std::string& path, Script& out)>;

/// Where a graph of a linked script came from.
struct GraphOrigin {
    std::string file;       // empty: the script itself; else the parent or library it was taken from
    int         graph = 0;  // its index in that file
};

struct Linked {
    Script                   script;    // what the compiler is given
    std::vector<GraphOrigin> origins;   // one per graph of `script`
    std::vector<std::string> files;     // every other file that went into it
};

/// The name a parent's function or event goes by once the child has its own.
std::string parent_name(const std::string& name);

/// The name an event node is fired by: a host event's id, a named event's id
/// and name ("event.key_pressed:Space"), a custom event's name. Empty when
/// the node is not an event.
std::string event_key(const Registry& reg, const Node& node);

/// Call `fn` on every Type a script mentions: variables, members, parameters,
/// node types, and the literals on pins, into arrays and structs.
void for_each_type(Script& s, const std::function<void(Type&)>& fn);

/// Link `root` with its parent chain and its libraries. With neither it is a
/// copy. False, with `error`, when a file cannot be read or the files name
/// each other in a circle.
bool link(const Registry& reg, const Script& root, const ScriptLoader& load, Linked& out, std::string* error = nullptr);

}  // namespace schizo::vs
