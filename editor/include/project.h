#pragma once
// ============================================================================
// Project model for the editor launcher.
//
// A *project* is a folder containing a `project.schizo` manifest, a `scenes/`
// folder and an `assets/` folder. The manifest records the project name, the
// default scene, and which modular features (see project_features.h) are on.
// ============================================================================
#include "project_features.h"
#include <string>
#include <vector>

namespace schizo::project {

inline constexpr const char* kManifestFilename = "project.schizo";

// ----------------------------------------------------------------------------
// Manifest: the on-disk description of a project (a simple key = value file).
// ----------------------------------------------------------------------------
struct ProjectManifest {
    std::string name;
    std::string engine_version = "0.1.0";
    std::string default_scene  = "scenes/main.scene";  // relative to project_dir
    FeatureSet  features       = FeatureSet::defaults();
    // Esc in Play: "release" frees the mouse for the editor (the default);
    // "game" leaves Esc to the game (a pause menu). Shift+F1 always frees it.
    // Not set in the file and the game's logic answers Esc itself (a pause menu
    // of its own: `escape_from` names that graph): "game" -- never both.
    std::string escape         = "release";
    bool        escape_set     = false;   // the file says (written back only then)
    std::string escape_from;              // the logic graph that took Esc, when the file does not say

    // Every other line of the file (a comment, another tool's key), written back as it was.
    std::vector<std::string> other_lines;

    // Absolute path to the project folder. Filled by load(); not serialized.
    std::string project_dir;

    // Absolute path to `<project_dir>/<default_scene>`.
    std::string default_scene_path() const;

    static bool save(const std::string& manifest_path, const ProjectManifest& m);
    static bool load(const std::string& manifest_path, ProjectManifest& out);
};

// Create a new project on disk: makes `<parent_dir>/<name>/` with the manifest,
// a `scenes/` folder and an `assets/` folder. Returns true on success and fills
// `out_manifest_path` with the full path to the written project.schizo.
bool create_project(const std::string& parent_dir,
                    const std::string& name,
                    const FeatureSet& features,
                    std::string& out_manifest_path,
                    const std::string& engine_version = "");

// ----------------------------------------------------------------------------
// Recent-projects registry, persisted in the user config dir.
// ----------------------------------------------------------------------------
struct RecentProject {
    std::string name;
    std::string manifest_path;  // full path to a project.schizo
};

class ProjectsRegistry {
public:
    void load();
    void save() const;

    void add(const RecentProject& p);              // add or promote to front (dedup by path)
    void remove(const std::string& manifest_path);

    const std::vector<RecentProject>& items() const { return items_; }

    // `%APPDATA%/GameWorldshaper/recent_projects.txt` (Windows) or a home fallback.
    static std::string config_path();

private:
    std::vector<RecentProject> items_;
};

// A sensible default parent folder for new projects
// (`%USERPROFILE%/GameWorldshaper Projects`, created on demand).
std::string default_projects_dir();

} // namespace schizo::project
