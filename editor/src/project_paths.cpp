#include "project_paths.h"

#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace schizo::editor {
namespace {
fs::path g_base_dir;      // engine content dir (has assets/)
fs::path g_project_root;  // active project (empty if none)

// Nearest ancestor of `start` (inclusive, up to `levels`) that contains assets/.
fs::path find_assets_root(const fs::path& start, int levels = 8) {
    std::error_code ec;
    fs::path p = start;
    for (int i = 0; i < levels && !p.empty(); ++i) {
        if (fs::exists(p / "assets", ec)) return p;
        if (p == p.parent_path()) break;
        p = p.parent_path();
    }
    return {};
}

fs::path executable_dir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0) return fs::path(std::string(buf, n)).parent_path();
#endif
    return {};
}
} // namespace

void init_base_dir() {
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);

    // Prefer a CWD (or ancestor) that holds assets/ — dev builds are launched
    // from the repo root, or from the Hub with the exe's dir as CWD.
    fs::path base = find_assets_root(cwd);
    if (base.empty()) base = find_assets_root(executable_dir());
    if (base.empty()) base = cwd;  // best effort

    if (base != cwd) {
        fs::current_path(base, ec);
        if (ec) { spdlog::warn("[paths] could not chdir to base '{}': {}", base.string(), ec.message()); base = cwd; }
    }
    g_base_dir = fs::current_path(ec);
    spdlog::info("[paths] engine content base: {}", g_base_dir.string());
}

const fs::path& base_dir() { return g_base_dir; }

bool set_project_root(const fs::path& project_dir) {
    std::error_code ec;
    if (project_dir.empty() || !fs::exists(project_dir, ec)) {
        spdlog::error("[paths] project dir does not exist: {}", project_dir.string());
        return false;
    }
    // Ensure the standard project layout so saves land in the right place.
    for (const char* sub : {"scenes", "assets", "assets/models", "assets/scripts",
                            "assets/textures", "cooked"}) {
        fs::create_directories(project_dir / sub, ec);
    }
    fs::current_path(project_dir, ec);
    if (ec) { spdlog::error("[paths] chdir to project failed: {}", ec.message()); return false; }
    g_project_root = fs::current_path(ec);
    spdlog::info("[paths] project sandbox active: {}", g_project_root.string());
    return true;
}

const fs::path& project_root() {
    return g_project_root.empty() ? g_base_dir : g_project_root;
}

bool has_project() { return !g_project_root.empty(); }

std::string engine_asset(const std::string& relative) {
    std::error_code ec;
    // Project's own copy wins (when a project is open, CWD == project root).
    if (fs::exists(fs::path(relative), ec)) return relative;
    fs::path from_base = g_base_dir / relative;
    if (fs::exists(from_base, ec)) return from_base.string();
    return relative;  // let the caller fail/normally
}

} // namespace schizo::editor
