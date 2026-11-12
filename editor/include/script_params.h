#pragma once

// ============================================================
// Script public-parameter declarations (Stage 12 — script fields)
// ============================================================
//
// The Inspector exposes editable "public fields" for a script WITHOUT executing
// it, by scanning the source file for declaration comments of the form:
//
//     @param <name> <type> <default> [label...]
//
// in a comment (any of `#`, `//`, `--` prefixes — so the same syntax works in
// Python, C++ and C#). Examples:
//
//     # @param speed  float 90.0 Rotation Speed
//     // @param count  int   3
//     // @param glow   bool  true
//     # @param target string Player Target Name
//
// `type` is one of: float, int, bool, string. The authored values live on the
// entity's ScriptComponent and are read back at runtime via engine.get_param_*.
//
// A logic graph (".lgraph") declares nothing in comments: its fields are the
// variables marked "show in Inspector". Those add the types vec2, vec3, vec4,
// color (stored as "1,2,3") and entity (stored as the object's NAME), and the
// logic host puts the authored values into the variables when the script starts.

#include <string>
#include <vector>

namespace schizo::editor {

struct ScriptParam {
    std::string name;
    std::string type;   // "float" | "int" | "bool" | "string"; a logic graph also
                        // "vec2" | "vec3" | "vec4" | "color" | "entity" | "enum"
    std::string def;    // declared default, string form
    std::string label;  // optional friendly label (defaults to name in the UI)
    std::vector<std::string> options;   // type "enum": the choices; the value is the index of one
};

// Scan `path` for @param declarations. Returns [] if the file is missing or has
// none. Duplicate names keep the first declaration. Malformed lines are skipped.
std::vector<ScriptParam> scan_script_params(const std::string& path);

}  // namespace schizo::editor
