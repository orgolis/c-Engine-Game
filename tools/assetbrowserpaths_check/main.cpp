// ============================================================================
// assetbrowserpaths_check — the asset browser's breadcrumbs must come out the
// same as they did when they were computed with fs::relative, every frame.
//
// fs::relative canonicalises both paths through the file system, and the
// panel called it on every frame -- ~0.65 ms a frame in every scene, for a row
// of folder names, with the panel's tab not even visible. compute_breadcrumbs
// does the same arithmetic lexically, once per folder change. Lexical and
// canonical path arithmetic differ exactly where a path has "." or "..", or
// where the folder sits right at a root, so those are the cases asserted --
// against the old algorithm, on real directories.
// ============================================================================
#include "asset_browser_paths.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using schizo::editor::Breadcrumbs;
using schizo::editor::compute_breadcrumbs;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// The panel's algorithm before: same root rule, segments from fs::relative.
Breadcrumbs old_algorithm(const fs::path& current, const std::vector<fs::path>& roots) {
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
    std::error_code ec;
    const fs::path rel = fs::relative(current, roots[b.root], ec);
    fs::path walk = roots[b.root];
    if (!ec && rel != fs::path(".")) {
        int i = 0;
        for (const auto& seg : rel) {
            walk /= seg;
            b.ids.push_back(seg.string() + "##bc" + std::to_string(i++));
            b.paths.push_back(walk);
        }
    }
    return b;
}

bool same(const Breadcrumbs& a, const Breadcrumbs& b) {
    if (a.root != b.root || a.ids != b.ids || a.paths.size() != b.paths.size()) return false;
    for (size_t i = 0; i < a.paths.size(); ++i)
        if (a.paths[i].lexically_normal() != b.paths[i].lexically_normal()) return false;
    return true;
}

}  // namespace

int main() {
    std::printf("=== assetbrowserpaths_check ===\n\n");

    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec) / "gws_assetbrowserpaths_check";
    fs::remove_all(base, ec);
    const fs::path project = base / "Project";
    const fs::path assets  = project / "assets";
    fs::create_directories(assets / "textures" / "rock", ec);
    fs::create_directories(assets / "meshes", ec);
    const std::vector<fs::path> roots = {project, assets};

    std::printf("The same breadcrumbs as fs::relative gave:\n");
    struct Case { fs::path cur; const char* what; };
    const Case cases[] = {
        {assets / "textures" / "rock",                  "a folder two levels under the deepest root"},
        {assets,                                        "the root itself (no segments)"},
        {project,                                       "the outer root itself"},
        {assets / "textures" / "." / "rock",            "a path with a '.' segment"},
        {assets / "meshes" / ".." / "textures" / "rock", "a path with a '..' segment"},
        {base,                                          "a folder no root contains"},
    };
    for (const auto& c : cases) {
        const Breadcrumbs got  = compute_breadcrumbs(c.cur, roots);
        const Breadcrumbs want = old_algorithm(c.cur, roots);
        check(same(got, want), c.what);
    }

    std::printf("\nAnd the shape is right, not merely equal:\n");
    {
        const Breadcrumbs b = compute_breadcrumbs(assets / "textures" / "rock", roots);
        check(b.root == 1, "the deepest containing root wins (assets, not Project)");
        check(b.ids.size() == 2 && b.ids[0] == "textures##bc0" && b.ids[1] == "rock##bc1",
              "two segments, labelled and uniquely id'd");
        check(b.paths.size() == 2 && fs::equivalent(b.paths[1], assets / "textures" / "rock", ec),
              "the last segment navigates to the folder itself");
    }

    fs::remove_all(base, ec);
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL assetbrowserpaths_check — the asset browser's breadcrumbs changed.\n");
    return g_fail ? 1 : 0;
}
