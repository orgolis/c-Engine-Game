#pragma once
// Breadcrumbs for the asset browser: which pinned root contains a folder, and
// the clickable segments from that root down to it.
//
// Pure path arithmetic, split out of the panel so it can be checked without
// ImGui (assetbrowserpaths_check). It used to be computed every frame with
// fs::relative(), which canonicalises both paths through the file system --
// syscalls on every frame to draw a row of folder names, ~0.65 ms a frame in
// every scene, with the panel's tab not even visible. The panel now computes
// this once per folder change, and lexically.

#include <filesystem>
#include <string>
#include <vector>

namespace schizo::editor {

struct Breadcrumbs {
    int root = -1;                               // index into roots, -1 = none contains it
    std::vector<std::string>           ids;      // "<segment>##bc<i>" (ImGui ids)
    std::vector<std::filesystem::path> paths;    // where each segment navigates
};

/// The deepest root whose path is a string prefix of `current` (the panel's
/// long-standing rule), and the segments from it to `current`. Both sides are
/// normalised lexically, which removes "." and ".." exactly as fs::relative
/// did -- without asking the file system.
inline Breadcrumbs compute_breadcrumbs(const std::filesystem::path& current,
                                       const std::vector<std::filesystem::path>& roots) {
    namespace fs = std::filesystem;
    Breadcrumbs b;
    const std::string cur = current.generic_string();
    size_t best = 0;
    for (size_t r = 0; r < roots.size(); ++r) {
        const std::string rp = roots[r].generic_string();
        if (cur.rfind(rp, 0) == 0 && (b.root < 0 || rp.size() > best)) {
            b.root = static_cast<int>(r);
            best = rp.size();
        }
    }
    if (b.root < 0) return b;
    const fs::path& base = roots[b.root];
    const fs::path rel = current.lexically_normal().lexically_relative(base.lexically_normal());
    fs::path walk = base;
    if (!rel.empty() && rel != fs::path(".")) {
        int i = 0;
        for (const auto& seg : rel) {
            walk /= seg;
            b.ids.push_back(seg.string() + "##bc" + std::to_string(i++));
            b.paths.push_back(walk);
        }
    }
    return b;
}

}  // namespace schizo::editor
