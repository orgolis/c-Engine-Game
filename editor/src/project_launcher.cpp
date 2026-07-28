#include "project_launcher.h"

#include <imgui.h>

#include <cstring>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace schizo::project {

// ----------------------------------------------------------------------------
// Native "pick a folder" dialog (Windows). Returns "" if cancelled/unsupported.
// ----------------------------------------------------------------------------
static std::string browse_folder(const char* title) {
#ifdef _WIN32
    char path[MAX_PATH] = {0};
    BROWSEINFOA bi{};
    bi.lpszTitle = title;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        SHGetPathFromIDListA(pidl, path);
        CoTaskMemFree(pidl);
        return std::string(path);
    }
    return {};
#else
    (void)title;
    return {};
#endif
}

// ----------------------------------------------------------------------------
// Feature checkboxes with dependency handling. A feature required by an enabled
// dependent is shown checked + disabled (can't be turned off while needed).
// ----------------------------------------------------------------------------
static bool draw_feature_checkboxes(FeatureSet& features) {
    bool changed = false;
    for (const auto& fi : feature_table()) {
        // Is this feature forced on because an enabled feature depends on it?
        const char* forcer = nullptr;
        for (const auto& g : feature_table()) {
            if (g.depends_on == fi.id && features.has(g.id)) { forcer = g.name; break; }
        }
        bool on = features.has(fi.id);

        ImGui::PushID(static_cast<int>(fi.id));
        if (forcer) {
            bool forced_on = true;
            ImGui::BeginDisabled();
            ImGui::Checkbox(fi.name, &forced_on);
            ImGui::EndDisabled();
        } else {
            if (ImGui::Checkbox(fi.name, &on)) {
                features.set(fi.id, on);
                if (on) features.resolve_dependencies();
                changed = true;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            std::string tip = fi.desc;
            if (fi.depends_on != Feature::Count) {
                tip += "\nRequires: ";
                tip += feature_info(fi.depends_on).name;
            }
            if (forcer) {
                tip += "\nRequired by: ";
                tip += forcer;
            }
            ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::PopID();
    }
    return changed;
}

// Load a manifest, register it as recent, and persist the registry.
static bool open_manifest(const std::string& manifest_path,
                          ProjectsRegistry& registry,
                          ProjectManifest& out,
                          std::string& error) {
    if (!fs::exists(manifest_path)) {
        error = "No " + std::string(kManifestFilename) + " found there.";
        return false;
    }
    if (!ProjectManifest::load(manifest_path, out)) {
        error = "Failed to read the project manifest.";
        return false;
    }
    RecentProject rp;
    rp.name          = out.name;
    rp.manifest_path = manifest_path;
    registry.add(rp);
    registry.save();
    return true;
}

// ----------------------------------------------------------------------------
// The launcher
// ----------------------------------------------------------------------------
bool draw_launcher(ProjectsRegistry& registry, ProjectManifest& out, bool& quit) {
    quit = false;
    bool chosen = false;

    static bool        initialized = false;
    static char        new_name[128]  = "MyGame";
    static char        new_loc[512]   = "";
    static FeatureSet  new_features   = FeatureSet::defaults();
    static char        open_path[512] = "";
    static std::string error_msg;
    static int         selected_recent = -1;

    if (!initialized) {
        std::string d = default_projects_dir();
        std::snprintf(new_loc, sizeof(new_loc), "%s", d.c_str());
        initialized = true;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##launcher", nullptr, flags);

    // ---- Header ----
    ImGui::SetWindowFontScale(1.7f);
    ImGui::TextUnformatted("GameWorldshaper");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Project Launcher");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    if (ImGui::BeginTabBar("launcher_tabs")) {

        // ================= Recent =================
        if (ImGui::BeginTabItem("Recent Projects")) {
            error_msg.clear();
            if (registry.items().empty()) {
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::TextDisabled("No recent projects. Create one in the \"New Project\" tab.");
            } else {
                ImGui::TextDisabled("Double-click a project to open it.");
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::BeginChild("recent_list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.4f), true);
                const auto& items = registry.items();
                for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                    const auto& it = items[i];
                    bool exists = fs::exists(it.manifest_path);
                    ImGui::PushID(i);
                    std::string label = it.name.empty() ? it.manifest_path : it.name;
                    if (!exists) label += "   (missing)";
                    if (ImGui::Selectable(label.c_str(), selected_recent == i,
                                          ImGuiSelectableFlags_AllowDoubleClick)) {
                        selected_recent = i;
                        if (ImGui::IsMouseDoubleClicked(0) && exists) {
                            if (open_manifest(it.manifest_path, registry, out, error_msg))
                                chosen = true;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("  %s", it.manifest_path.c_str());
                    ImGui::PopID();
                }
                ImGui::EndChild();

                bool can_open = selected_recent >= 0 &&
                                selected_recent < static_cast<int>(items.size()) &&
                                fs::exists(items[selected_recent].manifest_path);
                ImGui::BeginDisabled(!can_open);
                if (ImGui::Button("Open", ImVec2(120, 0)) && can_open) {
                    if (open_manifest(items[selected_recent].manifest_path, registry, out, error_msg))
                        chosen = true;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Remove from list", ImVec2(150, 0)) &&
                    selected_recent >= 0 && selected_recent < static_cast<int>(items.size())) {
                    registry.remove(items[selected_recent].manifest_path);
                    registry.save();
                    selected_recent = -1;
                }
            }
            ImGui::EndTabItem();
        }

        // ================= New =================
        if (ImGui::BeginTabItem("New Project")) {
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(360);
            ImGui::InputText("##name", new_name, sizeof(new_name));

            ImGui::TextUnformatted("Location");
            ImGui::SetNextItemWidth(360);
            ImGui::InputText("##loc", new_loc, sizeof(new_loc));
            ImGui::SameLine();
            if (ImGui::Button("Browse...##loc")) {
                std::string picked = browse_folder("Choose where to create the project");
                if (!picked.empty()) std::snprintf(new_loc, sizeof(new_loc), "%s", picked.c_str());
            }

            ImGui::Dummy(ImVec2(0, 6));
            ImGui::TextUnformatted("Features");
            ImGui::TextDisabled("Core systems (Rendering, ECS, Assets, Editor) are always included.");
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::BeginChild("new_features", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.8f), true);
            draw_feature_checkboxes(new_features);
            ImGui::EndChild();

            ImGui::Dummy(ImVec2(0, 4));
            const bool can_create = std::strlen(new_name) > 0 && std::strlen(new_loc) > 0;
            ImGui::BeginDisabled(!can_create);
            if (ImGui::Button("Create Project", ImVec2(160, 0)) && can_create) {
                std::string manifest_path;
                if (create_project(new_loc, new_name, new_features, manifest_path)) {
                    if (open_manifest(manifest_path, registry, out, error_msg))
                        chosen = true;
                } else {
                    error_msg = "Could not create the project (name taken or path not writable?).";
                }
            }
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }

        // ================= Open =================
        if (ImGui::BeginTabItem("Open Project")) {
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextUnformatted("Project folder (or a project.schizo path)");
            ImGui::SetNextItemWidth(360);
            ImGui::InputText("##openpath", open_path, sizeof(open_path));
            ImGui::SameLine();
            if (ImGui::Button("Browse...##open")) {
                std::string picked = browse_folder("Choose a project folder");
                if (!picked.empty()) std::snprintf(open_path, sizeof(open_path), "%s", picked.c_str());
            }
            ImGui::Dummy(ImVec2(0, 6));
            if (ImGui::Button("Open", ImVec2(120, 0)) && std::strlen(open_path) > 0) {
                fs::path p(open_path);
                std::string manifest_path =
                    (p.filename() == kManifestFilename) ? p.string()
                                                        : (p / kManifestFilename).string();
                if (open_manifest(manifest_path, registry, out, error_msg))
                    chosen = true;
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // ---- Error line + Quit ----
    if (!error_msg.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", error_msg.c_str());
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() - 8.0f);
    if (ImGui::Button("Quit", ImVec2(100, 0))) quit = true;

    ImGui::End();
    return chosen;
}

// ----------------------------------------------------------------------------
// In-editor Project Settings > Features (add / remove features later).
// ----------------------------------------------------------------------------
bool draw_feature_settings(FeatureSet& features, bool* p_open) {
    bool changed = false;
    ImGui::SetNextWindowSize(ImVec2(420, 460), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Project Settings — Features", p_open)) {
        ImGui::TextWrapped("Toggle which engine systems this project uses. "
                           "Core systems (Rendering, ECS, Assets, Editor) are always on.");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));
        changed = draw_feature_checkboxes(features);
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::Separator();
        ImGui::TextDisabled("Panel visibility updates immediately. Systems that need engine\n"
                            "initialization fully apply after reopening the project.");
    }
    ImGui::End();
    return changed;
}

} // namespace schizo::project
