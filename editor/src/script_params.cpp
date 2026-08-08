#include "script_params.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace schizo::editor {

namespace {
bool is_valid_type(const std::string& t) {
    return t == "float" || t == "int" || t == "bool" || t == "string";
}
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
bool is_identifier(const std::string& s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

// Recognise a Python module-level "public field": `name = <literal>` at column 0
// (not indented, not a comment), name not starting with '_'. Infers the type +
// string-form default from the literal. Rejects anything that isn't a plain
// int / float / bool / quoted-string literal (function calls, lists, dicts,
// expressions, comparisons...). Returns false when the line is not such a field.
bool parse_py_field(const std::string& raw, ScriptParam& out) {
    if (raw.empty() || raw[0] == ' ' || raw[0] == '\t' || raw[0] == '#') return false;
    const size_t eq = raw.find('=');
    if (eq == std::string::npos || eq == 0) return false;
    if (eq + 1 < raw.size() && raw[eq + 1] == '=') return false;   // ==
    const char pc = raw[eq - 1];
    if (pc == '=' || pc == '!' || pc == '<' || pc == '>' ||
        pc == '+' || pc == '-' || pc == '*' || pc == '/' || pc == '%') return false;

    const std::string name = trim(raw.substr(0, eq));
    std::string       val  = trim(raw.substr(eq + 1));
    if (!is_identifier(name) || name[0] == '_') return false;
    if (val.empty()) return false;

    // String literal (keep quotes-stripped value; ignore trailing comment).
    if (val[0] == '"' || val[0] == '\'') {
        const char q = val[0];
        const size_t end = val.find(q, 1);
        if (end == std::string::npos) return false;
        out.name = name; out.type = "string"; out.def = val.substr(1, end - 1);
        return true;
    }
    // Non-string: strip an inline comment, then classify.
    if (const size_t h = val.find('#'); h != std::string::npos) val = trim(val.substr(0, h));
    if (val.empty()) return false;
    if (val == "True" || val == "False") {
        out.name = name; out.type = "bool"; out.def = (val == "True") ? "1" : "0";
        return true;
    }
    bool has_dot = false, has_digit = false;
    for (size_t i = 0; i < val.size(); ++i) {
        const char c = val[i];
        if (c == '.' || c == 'e' || c == 'E') has_dot = true;
        else if (std::isdigit(static_cast<unsigned char>(c))) has_digit = true;
        else if (!((i == 0) && (c == '-' || c == '+'))) return false;   // not a number
    }
    if (!has_digit) return false;
    out.name = name; out.type = has_dot ? "float" : "int"; out.def = val;
    return true;
}
}  // namespace

std::vector<ScriptParam> scan_script_params(const std::string& path) {
    std::vector<ScriptParam> out;
    if (path.empty()) return out;
    std::ifstream f(path);
    if (!f) return out;

    const bool is_python = path.size() >= 3 && path.compare(path.size() - 3, 3, ".py") == 0;

    std::unordered_set<std::string> seen;
    std::vector<ScriptParam>        autovars;   // Python module-level fields (lower priority)

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // 1) Explicit `@param <name> <type> <default> [label...]` in a comment.
        if (const auto at = line.find("@param"); at != std::string::npos) {
            std::istringstream ss(line.substr(at + 6));
            ScriptParam p;
            if ((ss >> p.name >> p.type >> p.def) && is_valid_type(p.type)) {
                std::string rest;
                std::getline(ss, rest);
                p.label = trim(rest);
                if (seen.insert(p.name).second) out.push_back(std::move(p));
            }
            continue;
        }

        // 2) Python auto-discovery: any module-level literal assignment.
        if (is_python) {
            ScriptParam p;
            if (parse_py_field(line, p)) autovars.push_back(std::move(p));
        }
    }

    // Merge auto-discovered fields that weren't explicitly @param-declared.
    for (auto& a : autovars)
        if (seen.insert(a.name).second) out.push_back(std::move(a));
    return out;
}

}  // namespace schizo::editor
