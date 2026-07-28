#include "project.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace schizo::project {

// ============================================================================
// Feature registry
// ============================================================================
const std::vector<FeatureInfo>& feature_table() {
    static const std::vector<FeatureInfo> table = {
        // id                      key               name              desc                                                    default depends_on
        { Feature::Physics,        "Physics",        "Physics",        "Rigid bodies + character controller (Jolt).",          true,   Feature::Count },
        { Feature::Animation,      "Animation",      "Animation",      "Skeletal animation, IK, state machines.",              true,   Feature::Count },
        { Feature::Audio,          "Audio",          "Audio",          "Spatial mixer, occlusion, listeners.",                 true,   Feature::Count },
        { Feature::Networking,     "Networking",     "Multiplayer",    "Networked play: replication + prediction.",            false,  Feature::Count },
        { Feature::AI,             "AI",             "AI",             "Navmesh pathfinding + behaviour trees.",               false,  Feature::Count },
        { Feature::Terrain,        "Terrain",        "Terrain & Water","Terrain sculpting, splat paint, water.",               true,   Feature::Count },
        { Feature::WorldStreaming, "WorldStreaming", "World Streaming","Large-world streaming + floating origin.",             false,  Feature::Count },
        { Feature::VFX,            "VFX",            "VFX",            "Particle and billboard effects.",                      true,   Feature::Count },
        { Feature::Combat,         "Combat",         "Combat",         "Frame-data combat: hitboxes, i-frames.",               true,   Feature::Physics },
        { Feature::Scripting,      "Scripting",      "Scripting",      "Python / C++ / C# scripts with hot reload.",           false,  Feature::Count },
        { Feature::GameUI,         "GameUI",         "Game UI",        "Runtime HUD and menus.",                               true,   Feature::Count },
    };
    return table;
}

const FeatureInfo* feature_by_key(const std::string& key) {
    for (const auto& fi : feature_table())
        if (key == fi.key) return &fi;
    return nullptr;
}

const FeatureInfo& feature_info(Feature f) {
    for (const auto& fi : feature_table())
        if (fi.id == f) return fi;
    return feature_table().front();  // unreachable for valid f
}

void FeatureSet::resolve_dependencies() {
    // Single pass is enough for the current one-level dependency chains, but
    // loop until stable in case a dependency itself has a dependency.
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& fi : feature_table()) {
            if (has(fi.id) && fi.depends_on != Feature::Count && !has(fi.depends_on)) {
                enable(fi.depends_on);
                changed = true;
            }
        }
    }
}

FeatureSet FeatureSet::defaults() {
    FeatureSet s;
    for (const auto& fi : feature_table())
        if (fi.default_on) s.enable(fi.id);
    s.resolve_dependencies();
    return s;
}

FeatureSet FeatureSet::all() {
    FeatureSet s;
    for (const auto& fi : feature_table()) s.enable(fi.id);
    return s;
}

// ============================================================================
// Small text helpers
// ============================================================================
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ============================================================================
// ProjectManifest
// ============================================================================
std::string ProjectManifest::default_scene_path() const {
    if (project_dir.empty()) return default_scene;
    return (fs::path(project_dir) / default_scene).string();
}

bool ProjectManifest::save(const std::string& manifest_path, const ProjectManifest& m) {
    std::ofstream out(manifest_path, std::ios::trunc);
    if (!out) {
        spdlog::error("[project] cannot write manifest: {}", manifest_path);
        return false;
    }
    out << "# GameWorldshaper project file\n";
    out << "name = "           << m.name           << "\n";
    out << "engine_version = " << m.engine_version << "\n";
    out << "default_scene = "  << m.default_scene  << "\n";

    std::string csv;
    for (const auto& fi : feature_table()) {
        if (m.features.has(fi.id)) {
            if (!csv.empty()) csv += ",";
            csv += fi.key;
        }
    }
    out << "features = " << csv << "\n";
    return true;
}

bool ProjectManifest::load(const std::string& manifest_path, ProjectManifest& out) {
    std::ifstream in(manifest_path);
    if (!in) {
        spdlog::error("[project] cannot read manifest: {}", manifest_path);
        return false;
    }
    out = ProjectManifest{};
    out.features = FeatureSet::none();  // start empty; manifest is authoritative
    bool saw_features = false;

    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));

        if (key == "name")                out.name           = val;
        else if (key == "engine_version") out.engine_version = val;
        else if (key == "default_scene")  out.default_scene  = val;
        else if (key == "features") {
            saw_features = true;
            std::stringstream ss(val);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                tok = trim(tok);
                if (tok.empty()) continue;
                if (const FeatureInfo* fi = feature_by_key(tok))
                    out.features.enable(fi->id);
                else
                    spdlog::warn("[project] unknown feature in manifest: {}", tok);
            }
        }
    }

    if (!saw_features) out.features = FeatureSet::defaults();
    out.features.resolve_dependencies();

    out.project_dir = fs::path(manifest_path).parent_path().string();
    if (out.name.empty())
        out.name = fs::path(out.project_dir).filename().string();
    return true;
}

// ============================================================================
// create_project
// ============================================================================
bool create_project(const std::string& parent_dir,
                    const std::string& name,
                    const FeatureSet& features,
                    std::string& out_manifest_path,
                    const std::string& engine_version) {
    if (trim(name).empty()) {
        spdlog::error("[project] cannot create project with empty name");
        return false;
    }
    std::error_code ec;
    fs::path root = fs::path(parent_dir) / name;
    if (fs::exists(root) && !fs::is_empty(root, ec)) {
        spdlog::error("[project] target folder exists and is not empty: {}", root.string());
        return false;
    }
    fs::create_directories(root / "scenes", ec);
    fs::create_directories(root / "assets", ec);
    if (ec) {
        spdlog::error("[project] failed to create project folders: {}", ec.message());
        return false;
    }

    ProjectManifest m;
    m.name          = name;
    m.default_scene = "scenes/main.scene";
    m.features      = features;
    m.features.resolve_dependencies();
    m.project_dir   = root.string();
    if (!engine_version.empty()) m.engine_version = engine_version;

    std::string manifest_path = (root / kManifestFilename).string();
    if (!ProjectManifest::save(manifest_path, m)) return false;

    out_manifest_path = manifest_path;
    spdlog::info("[project] created project '{}' at {}", name, root.string());
    return true;
}

// ============================================================================
// User config paths
// ============================================================================
static fs::path config_dir() {
    if (const char* appdata = std::getenv("APPDATA"))
        return fs::path(appdata) / "GameWorldshaper";
    if (const char* home = std::getenv("USERPROFILE"))
        return fs::path(home) / ".gameworldshaper";
    return fs::path(".") / ".gameworldshaper";
}

std::string default_projects_dir() {
    fs::path base;
    if (const char* home = std::getenv("USERPROFILE"))
        base = fs::path(home) / "GameWorldshaper Projects";
    else
        base = fs::path(".") / "GameWorldshaper Projects";
    std::error_code ec;
    fs::create_directories(base, ec);
    return base.string();
}

// ============================================================================
// ProjectsRegistry
// ============================================================================
std::string ProjectsRegistry::config_path() {
    return (config_dir() / "recent_projects.txt").string();
}

void ProjectsRegistry::load() {
    items_.clear();
    std::ifstream in(config_path());
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t tab = t.find('\t');
        RecentProject p;
        if (tab == std::string::npos) {
            p.manifest_path = t;
            p.name = fs::path(fs::path(t).parent_path()).filename().string();
        } else {
            p.name          = trim(t.substr(0, tab));
            p.manifest_path = trim(t.substr(tab + 1));
        }
        if (!p.manifest_path.empty()) items_.push_back(std::move(p));
    }
}

void ProjectsRegistry::save() const {
    std::error_code ec;
    fs::create_directories(config_dir(), ec);
    std::ofstream out(config_path(), std::ios::trunc);
    if (!out) {
        spdlog::warn("[project] cannot write recent-projects file: {}", config_path());
        return;
    }
    out << "# GameWorldshaper recent projects (name<TAB>path)\n";
    for (const auto& p : items_)
        out << p.name << "\t" << p.manifest_path << "\n";
}

void ProjectsRegistry::add(const RecentProject& p) {
    remove(p.manifest_path);
    items_.insert(items_.begin(), p);
    if (items_.size() > 20) items_.resize(20);
}

void ProjectsRegistry::remove(const std::string& manifest_path) {
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [&](const RecentProject& r) {
                                    return r.manifest_path == manifest_path;
                                }),
                 items_.end());
}

} // namespace schizo::project
