#include "script_params.h"

#include "vscript/graph.h"
#include "vscript/io.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
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

bool ends_with(const std::string& s, const char* tail) {
    const size_t n = std::char_traits<char>::length(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

/// A logic graph's fields are the variables marked "show in Inspector". The
/// file is parsed with the format's own reader; the Inspector asks every frame
/// while the object is selected, so the answer is kept until the file changes.
std::vector<ScriptParam> scan_logic_graph(const std::string& path) {
    namespace fs = std::filesystem;
    struct Cached {
        fs::file_time_type       stamp;
        uintmax_t                size = 0;
        std::vector<ScriptParam> params;
    };
    static std::unordered_map<std::string, Cached> cache;
    std::error_code ec;
    const fs::file_time_type stamp = fs::last_write_time(path, ec);
    if (ec) return {};
    const uintmax_t size = fs::file_size(path, ec);
    if (ec) return {};
    const auto hit = cache.find(path);
    if (hit != cache.end() && hit->second.stamp == stamp && hit->second.size == size) return hit->second.params;

    std::vector<ScriptParam> out;
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    schizo::vs::Script script;
    if (f && schizo::vs::from_text(ss.str(), script)) {
        for (const schizo::vs::Variable& v : script.variables) {
            if (!v.exposed || v.type.array) continue;
            const char* type = nullptr;
            switch (v.type.kind) {
                case schizo::vs::Kind::Bool: type = "bool"; break;
                case schizo::vs::Kind::Int: type = "int"; break;
                case schizo::vs::Kind::Float: type = "float"; break;
                case schizo::vs::Kind::String: type = "string"; break;
                case schizo::vs::Kind::Vec2: type = "vec2"; break;
                case schizo::vs::Kind::Vec3: type = "vec3"; break;
                case schizo::vs::Kind::Vec4: type = "vec4"; break;
                case schizo::vs::Kind::Color: type = "color"; break;
                case schizo::vs::Kind::Entity: type = "entity"; break;   // kept by name
                default: break;
            }
            if (!type) continue;
            ScriptParam p;
            p.name = v.name;
            p.type = type;
            if (v.type.kind != schizo::vs::Kind::Entity) p.def = schizo::vs::literal_text(v.def);
            out.push_back(std::move(p));
        }
    }
    if (cache.size() > 256) cache.clear();
    cache[path] = Cached{stamp, size, out};
    return out;
}
}  // namespace

std::vector<ScriptParam> scan_script_params(const std::string& path) {
    std::vector<ScriptParam> out;
    if (path.empty()) return out;
    if (ends_with(path, ".lgraph")) return scan_logic_graph(path);
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
