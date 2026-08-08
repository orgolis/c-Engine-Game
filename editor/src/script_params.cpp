#include "script_params.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace schizo::editor {

namespace {
bool is_valid_type(const std::string& t) {
    return t == "float" || t == "int" || t == "bool" || t == "string";
}
}  // namespace

std::vector<ScriptParam> scan_script_params(const std::string& path) {
    std::vector<ScriptParam> out;
    if (path.empty()) return out;
    std::ifstream f(path);
    if (!f) return out;

    std::unordered_set<std::string> seen;
    std::string line;
    while (std::getline(f, line)) {
        const auto at = line.find("@param");
        if (at == std::string::npos) continue;

        // Tokenise everything after "@param". Whitespace-separated:
        //   <name> <type> <default> [label words...]
        std::istringstream ss(line.substr(at + 6));
        ScriptParam p;
        if (!(ss >> p.name >> p.type >> p.def)) continue;   // need at least 3 tokens
        if (!is_valid_type(p.type)) continue;
        if (!seen.insert(p.name).second) continue;          // first declaration wins

        std::string rest;
        std::getline(ss, rest);
        // Trim leading/trailing whitespace off the label.
        const auto b = rest.find_first_not_of(" \t\r\n");
        const auto e = rest.find_last_not_of(" \t\r\n");
        if (b != std::string::npos) p.label = rest.substr(b, e - b + 1);

        out.push_back(std::move(p));
    }
    return out;
}

}  // namespace schizo::editor
