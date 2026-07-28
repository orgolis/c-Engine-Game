#pragma once
// ============================================================================
// Project content sandbox.
//
// The editor resolves content (scenes, assets, scripts, cooked bundles) RELATIVE
// TO THE WORKING DIRECTORY. To sandbox a project we simply make the project
// folder the working directory when it opens — the asset browser, scene save/
// load, script paths and drag payloads then all scope to that project with no
// per-call-site changes.
//
// Engine "built-in" content (default skybox, default meshes, cooked defaults)
// lives with the engine, not the project. `init_base_dir()` locks that engine
// content dir once at startup (self-healing so the editor works no matter which
// directory it was launched from), and `engine_asset()` falls back to it.
// ============================================================================
#include <filesystem>
#include <string>

namespace schizo::editor {

// Call ONCE at startup, before any content is loaded. Ensures the CWD is a
// directory that actually contains the engine's `assets/` (probing up from the
// launch dir and the executable's dir), and remembers it as the base dir.
void init_base_dir();

// The engine content base (where bundled `assets/` live). Absolute.
const std::filesystem::path& base_dir();

// Open a project: make `project_dir` the working directory (so all content is
// project-scoped) and ensure the standard subfolders exist. Returns false if
// the directory can't be entered.
bool set_project_root(const std::filesystem::path& project_dir);

// The active project root (== CWD after set_project_root; base_dir if none open).
const std::filesystem::path& project_root();
bool has_project();

// Resolve a built-in/default resource: use the project's copy if it exists,
// otherwise the engine base dir (absolute). For content that should fall back
// to the engine when a project doesn't provide it.
std::string engine_asset(const std::string& relative);

} // namespace schizo::editor
