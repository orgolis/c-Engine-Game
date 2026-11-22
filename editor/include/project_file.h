#pragma once
// ============================================================================
// project_file.h -- the parts of reading a project file (.schizo) that must
// not lose anything, and who answers Esc when the file does not say:
//
//   - read_manifest_lines: the keys the engine reads, and every other line (a
//     comment, a key of another tool or a later engine) kept to be written back
//     as it was. Writers that kept only the keys they knew erased the others: the
//     Hub dropped `escape = game` each time it changed a project's engine version,
//     and the game's pause menu and the engine's then both answered Esc.
//   - logic_answering_escape: the game logic graph (`*.lgraph`) of a project that
//     handles the Escape key itself (its own pause menu) -- a project that does
//     not set `escape` but has one gives Esc to the game.
//
// Pure (std only): checked by tools/projectfile_check.
// ============================================================================
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace schizo::editor {

struct ManifestLines {
    std::vector<std::pair<std::string, std::string>> keys;   // the known keys, in order: key, value
    std::vector<std::string> other;                           // every other line, as it was
};

inline std::string manifest_trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

/// Splits a manifest: `known` keys go to `keys`; comments (but the header the
/// writer puts back), keys not known and lines without `=` go to `other`.
inline ManifestLines read_manifest_lines(std::istream& in, const std::vector<std::string>& known) {
    ManifestLines out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = manifest_trim(line);
        if (t.empty() || t == "# GameWorldshaper project file") continue;
        const size_t eq = t.find('=');
        if (t[0] == '#' || eq == std::string::npos) { out.other.push_back(line); continue; }
        const std::string key = manifest_trim(t.substr(0, eq)), val = manifest_trim(t.substr(eq + 1));
        if (std::find(known.begin(), known.end(), key) != known.end()) out.keys.emplace_back(key, val);
        else out.other.push_back(line);
    }
    return out;
}

/// Whether a logic graph handles the Escape key (an event.key_pressed node for it).
inline bool lgraph_answers_escape(std::istream& in) {
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("node", 0) != 0 || line.find("event.key_pressed") == std::string::npos) continue;
        std::stringstream ts(line);
        std::string tok;
        bool key_event = false, escape = false;
        while (std::getline(ts, tok, '\t')) {
            tok = manifest_trim(tok);
            if (tok == "event.key_pressed") key_event = true;
            else if (tok == "Escape" || tok == "Esc") escape = true;
        }
        if (key_event && escape) return true;
    }
    return false;
}

/// The first of a project's logic graphs (project-relative, '/') that handles
/// Escape, or "" -- not looking in sources, saves, caches or builds.
inline std::string logic_answering_escape(const std::string& project_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path root(project_dir);
    static const char* skip[] = {"art_source", "saves", "cooked", "script_cache", "builds", "build", ".git"};
    std::vector<fs::path> found;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path& p = it->path();
        if (it->is_directory(ec)) {
            const std::string name = p.filename().string();
            if (std::find_if(std::begin(skip), std::end(skip), [&](const char* s) { return name == s; }) != std::end(skip))
                it.disable_recursion_pending();
            continue;
        }
        if (p.extension() == ".lgraph") found.push_back(p);
    }
    std::sort(found.begin(), found.end());
    for (const auto& p : found) {
        std::ifstream f(p);
        if (f && lgraph_answers_escape(f)) return fs::relative(p, root, ec).generic_string();
    }
    return "";
}

}  // namespace schizo::editor
