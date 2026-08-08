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

#include <string>
#include <vector>

namespace schizo::editor {

struct ScriptParam {
    std::string name;
    std::string type;   // "float" | "int" | "bool" | "string"
    std::string def;    // declared default, string form
    std::string label;  // optional friendly label (defaults to name in the UI)
};

// Scan `path` for @param declarations. Returns [] if the file is missing or has
// none. Duplicate names keep the first declaration. Malformed lines are skipped.
std::vector<ScriptParam> scan_script_params(const std::string& path);

}  // namespace schizo::editor
