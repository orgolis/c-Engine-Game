// ============================================================
// Asset Browser implementation — see asset_browser_panel.h
// ============================================================

#include "asset_browser_panel.h"

#include "scene.h"
#include "material_desc.h"
#include "texture_gen.h"     // New > Texture: a fresh project has no texture to apply   // New > Material writes a default .mat through the format's own writer

#include <imgui.h>
#include <imgui_internal.h>   // BeginDragDropTargetCustom: a pane-wide drop target
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <cstdint>
#endif

namespace schizo::editor {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

ImVec4 type_color(const char* type) {
    if (!std::strcmp(type, "Folder"))  return {1.00f, 0.85f, 0.45f, 1.0f};
    if (!std::strcmp(type, "Mesh"))    return {0.45f, 0.85f, 1.00f, 1.0f};
    if (!std::strcmp(type, "Texture")) return {0.50f, 0.95f, 0.55f, 1.0f};
    if (!std::strcmp(type, "Audio"))   return {1.00f, 0.65f, 0.30f, 1.0f};
    if (!std::strcmp(type, "Script"))  return {0.80f, 0.60f, 1.00f, 1.0f};
    if (!std::strcmp(type, "Scene"))   return {0.45f, 0.70f, 1.00f, 1.0f};
    if (!std::strcmp(type, "Material")) return {1.00f, 0.55f, 0.75f, 1.0f};
    if (!std::strcmp(type, "Shader"))  return {0.95f, 0.80f, 0.50f, 1.0f};
    if (!std::strcmp(type, "Cooked"))  return {0.70f, 0.72f, 0.78f, 1.0f};
    return {0.85f, 0.85f, 0.85f, 1.0f};
}

const char* payload_for(const char* type) {
    if (!std::strcmp(type, "Mesh"))    return AssetBrowserPanel::kPayloadMesh;
    if (!std::strcmp(type, "Texture")) return AssetBrowserPanel::kPayloadTexture;
    if (!std::strcmp(type, "Audio"))   return AssetBrowserPanel::kPayloadAudio;
    if (!std::strcmp(type, "Script"))  return AssetBrowserPanel::kPayloadScript;
    if (!std::strcmp(type, "Scene"))   return AssetBrowserPanel::kPayloadScene;
    if (!std::strcmp(type, "Material")) return AssetBrowserPanel::kPayloadMaterial;
    if (!std::strcmp(type, "Prefab"))  return AssetBrowserPanel::kPayloadPrefab;
    return AssetBrowserPanel::kPayloadOther;
}

std::string human_size(uint64_t b) {
    char buf[32];
    if (b >= 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f GB", b / (1024.0 * 1024 * 1024));
    else if (b >= 1024ull * 1024)   std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024));
    else if (b >= 1024ull)          std::snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
    else                            std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(b));
    return buf;
}

void os_open(const std::string& abs) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", abs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)abs;
#endif
}

void os_reveal(const std::string& abs) {
#ifdef _WIN32
    const std::string args = "/select,\"" + abs + "\"";
    ShellExecuteA(nullptr, "open", "explorer", args.c_str(), nullptr, SW_SHOWNORMAL);
#else
    (void)abs;
#endif
}

// Starter templates for the "New" menu (kept in sync with the script SDKs).
constexpr const char* kTemplatePy = R"(# New script — attach via Inspector > Add Component > Script.
import engine

def on_start(e):
    engine.log("script started on entity " + str(e))

def on_update(e, dt):
    pass
)";
constexpr const char* kTemplateCs = R"(using Schizo;

public static class Script {
    public static void OnStart(Api api, uint entity) { api.Log("C# script started"); }
    public static void OnUpdate(Api api, uint entity, float dt) { }
}
)";
constexpr const char* kTemplateCpp = R"(#include "schizo_script.h"

SCHIZO_SCRIPT void on_start(const SchizoScriptApi* api, unsigned entity) {
    api->log(api->ctx, "C++ script started");
}
SCHIZO_SCRIPT void on_update(const SchizoScriptApi* api, unsigned entity, float dt) {}
)";
constexpr const char* kTemplateItems = R"(# Item definitions (.items) — loaded into the catalog at startup / Tools > Reload Item Defs.
item my_item
  name My Item
  kind consumable          # misc|weapon|armor|consumable|material|currency
  max_stack 10
  weight 0.1
  on_use Health 25
end
)";

// Draw a small file-explorer-style icon (folder or document) at `tl`, size h x h,
// tinted by the entry's type color. No icon font needed — pure draw-list shapes.
void draw_entry_icon(ImDrawList* dl, ImVec2 tl, float h, const AssetEntry& e) {
    const float p = 1.0f;
    const ImVec2 a(tl.x + p, tl.y + p);
    const ImVec2 b(tl.x + h - p, tl.y + h - p);
    const ImU32 accent = ImGui::GetColorU32(type_color(e.type));
    if (e.is_dir) {
        const ImU32 fill = ImGui::GetColorU32(ImVec4(1.00f, 0.80f, 0.35f, 1.0f));
        const float w = b.x - a.x, hh = b.y - a.y;
        dl->AddRectFilled(ImVec2(a.x, a.y + hh * 0.15f), ImVec2(a.x + w * 0.5f, a.y + hh * 0.38f), fill, 1.5f);  // tab
        dl->AddRectFilled(ImVec2(a.x, a.y + hh * 0.30f), b, fill, 2.0f);                                        // body
    } else {
        const ImU32 page = ImGui::GetColorU32(ImVec4(0.90f, 0.90f, 0.93f, 1.0f));
        const ImU32 fold = ImGui::GetColorU32(ImVec4(0.62f, 0.63f, 0.68f, 1.0f));
        const float f = (b.x - a.x) * 0.36f;
        dl->AddRectFilled(a, b, page, 2.0f);                                                       // page body
        dl->AddTriangleFilled(ImVec2(b.x - f, a.y), ImVec2(b.x, a.y), ImVec2(b.x, a.y + f), fold); // folded corner
        dl->AddRectFilled(ImVec2(a.x + 1, b.y - (b.y - a.y) * 0.30f), ImVec2(b.x - 1, b.y - 2), accent, 1.0f);  // type bar
    }
}

}  // namespace

const char* AssetBrowserPanel::classify(const std::string& e) {
    if (e == ".obj" || e == ".gltf" || e == ".glb" || e == ".fbx" ||
        e == ".usd" || e == ".usda" || e == ".usdc" || e == ".usdz") return "Mesh";
    if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" ||
        e == ".tga" || e == ".hdr" || e == ".dds" || e == ".ktx")    return "Texture";
    if (e == ".wav" || e == ".mp3" || e == ".ogg" || e == ".flac")   return "Audio";
    if (e == ".py" || e == ".cpp" || e == ".cc" || e == ".cs")       return "Script";
    if (e == ".scene")                                               return "Scene";
    if (e == ".mat")                                                 return "Material";
    if (e == ".prefab")                                              return "Prefab";
    if (e == ".pak" || e == ".vt" || e == ".r32" || e == ".splat")   return "Cooked";
    if (e == ".frag" || e == ".vert" || e == ".comp" || e == ".spv" ||
        e == ".glsl")                                                return "Shader";
    if (e == ".h" || e == ".hpp")                                    return "Header";
    if (e == ".txt" || e == ".md" || e == ".json" || e == ".xml" ||
        e == ".ini" || e == ".cmake" || e == ".mtl")                 return "Text";
    return "File";
}

bool AssetBrowserPanel::search_skip_dir(const std::string& name) {
    return name == ".git" || name == ".vs" || name == "CMakeFiles" ||
           name == "third_party" || name == "script_cache" ||
           name.rfind("build", 0) == 0;   // build, build-msvc, build-editor...
}

std::string AssetBrowserPanel::runtime_relative(const fs::path& abs) const {
    std::error_code ec;
    fs::path rel = fs::relative(abs, cwd_, ec);
    if (ec || rel.empty()) return abs.generic_string();
    return rel.generic_string();
}

// ---------------------------------------------------------------------------
// lifecycle / navigation
// ---------------------------------------------------------------------------

AssetBrowserPanel::AssetBrowserPanel() {
    detect_roots();
    // Start where gameplay assets live at runtime.
    std::error_code ec;
    if (fs::exists(cwd_ / "assets", ec)) current_ = cwd_ / "assets";
    else                                 current_ = cwd_;
    dirty_ = true;
}

AssetBrowserPanel::~AssetBrowserPanel() = default;

void AssetBrowserPanel::Reroot() {
    detect_roots();
    std::error_code ec;
    current_ = fs::exists(cwd_ / "assets", ec) ? (cwd_ / "assets") : cwd_;
    selected_ = -1;
    search_buf_[0] = '\0';
    dirty_ = true;
}

void AssetBrowserPanel::detect_roots() {
    std::error_code ec;
    cwd_ = fs::current_path(ec);

    // Project root: the nearest ancestor that is a real project (has a
    // `project.schizo` manifest) — this is what the Hub launches us into. Fall
    // back to the dev-tree heuristic (CMakeLists.txt + assets/) so running from
    // the source repo still roots at the repo. Checking project.schizo FIRST is
    // what keeps the browser scoped to the PROJECT, not the engine/repo it runs in.
    project_root_ = cwd_;
    fs::path probe = cwd_;
    for (int i = 0; i < 8 && !probe.empty(); ++i) {
        const bool is_project = fs::exists(probe / "project.schizo", ec);
        const bool is_devtree = fs::exists(probe / "CMakeLists.txt", ec) && fs::exists(probe / "assets", ec);
        if (is_project || is_devtree) {
            project_root_ = probe;
            break;
        }
        if (probe == probe.parent_path()) break;
        probe = probe.parent_path();
    }

    roots_.clear();
    auto add = [&](const char* label, const fs::path& p) {
        std::error_code e2;
        if (fs::exists(p, e2) && fs::is_directory(p, e2)) roots_.push_back({label, p});
    };
    add("Runtime", cwd_);                              // what relative paths resolve against
    add("Assets",  project_root_ / "assets");          // source content
    add("Scripts", project_root_ / "assets" / "scripts");
    add("Scenes",  cwd_ / "scenes");
    add("Project", project_root_);                     // everything
}

void AssetBrowserPanel::navigate(const fs::path& dir) {
    current_ = dir;
    selected_ = -1;
    search_buf_[0] = '\0';
    dirty_ = true;
}

void AssetBrowserPanel::SetDirectory(const std::string& path) {
    std::error_code ec;
    fs::path p(path);
    if (p.is_relative()) p = cwd_ / p;
    if (fs::is_directory(p, ec)) navigate(p);
}

void AssetBrowserPanel::RefreshAssets() { dirty_ = true; }

// ---------------------------------------------------------------------------
// listing
// ---------------------------------------------------------------------------

void AssetBrowserPanel::list_current() {
    entries_.clear();
    selected_ = -1;
    std::error_code ec;

    const bool recursive = search_recursive_ && search_buf_[0] != '\0';
    const std::string needle = lower(search_buf_);

    auto push = [&](const fs::directory_entry& de, bool as_rel_name) {
        AssetEntry e;
        std::error_code e2;
        e.is_dir   = de.is_directory(e2);
        e.abs_path = de.path().string();
        e.rel_path = runtime_relative(de.path());
        e.name     = as_rel_name
                       ? fs::relative(de.path(), current_, e2).generic_string()
                       : de.path().filename().string();
        if (!e.is_dir) {
            e.ext  = lower(de.path().extension().string());
            e.type = classify(e.ext);
            e.size = de.file_size(e2);
        } else {
            e.type = "Folder";
        }
        entries_.push_back(std::move(e));
    };

    if (recursive) {
        // Capped subtree search, skipping build junk / vendored code.
        size_t hits = 0;
        fs::recursive_directory_iterator it(
            current_, fs::directory_options::skip_permission_denied, ec), end;
        while (!ec && it != end && hits < 500) {
            const fs::directory_entry de = *it;
            std::error_code e2;
            if (de.is_directory(e2) && search_skip_dir(de.path().filename().string())) {
                it.disable_recursion_pending();
                it.increment(ec);
                continue;
            }
            if (!de.is_directory(e2) &&
                lower(de.path().filename().string()).find(needle) != std::string::npos) {
                push(de, /*as_rel_name=*/true);
                ++hits;
            }
            it.increment(ec);
        }
    } else {
        for (fs::directory_iterator it(
                 current_, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            push(*it, false);
        }
        const int mode = sort_mode_;
        std::sort(entries_.begin(), entries_.end(),
                  [mode](const AssetEntry& a, const AssetEntry& b) {
                      if (a.is_dir != b.is_dir) return a.is_dir;   // folders always first
                      if (mode == 1) {                             // by type, then name
                          const int t = std::strcmp(a.type, b.type);
                          if (t != 0) return t < 0;
                      } else if (mode == 2 && !a.is_dir && !b.is_dir) {  // by size (largest first)
                          if (a.size != b.size) return a.size > b.size;
                      }
                      return lower(a.name) < lower(b.name);
                  });
    }
    dirty_ = false;
}

// ---------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------

void AssetBrowserPanel::Render(const std::shared_ptr<schizo::scene::Scene>& /*scene*/,
                               bool* open) {
    if (dirty_) list_current();

    // Docked window = child window internally; End() must run unconditionally.
    ImGui::Begin("Asset Browser##panel", open);
    {
        render_toolbar();
        render_breadcrumbs();
        ImGui::Separator();

        const float status_h = ImGui::GetFrameHeightWithSpacing();
        if (show_tree_) {
            if (ImGui::BeginTable("##ab_split", 2,
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                                  ImVec2(0, -status_h))) {
                ImGui::TableSetupColumn("tree", ImGuiTableColumnFlags_WidthFixed, 220.0f);
                ImGui::TableSetupColumn("files", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("##ab_tree", ImVec2(0, 0));
                render_tree();
                ImGui::EndChild();
                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("##ab_files", ImVec2(0, 0));
                render_entries();
                render_entity_drop_target();
                render_background_menu();
                ImGui::EndChild();
                ImGui::EndTable();
            }
        } else {
            ImGui::BeginChild("##ab_files", ImVec2(0, -status_h));
            render_entries();
            render_entity_drop_target();
            render_background_menu();
            ImGui::EndChild();
        }

        // Status bar.
        ImGui::Separator();
        if (selected_ >= 0 && selected_ < static_cast<int>(entries_.size())) {
            const AssetEntry& s = entries_[selected_];
            if (s.is_dir) {
                ImGui::Text("%zu items  |  %s", entries_.size(), s.rel_path.c_str());
            } else {
                ImGui::Text("%zu items  |  %s  (%s, %s)", entries_.size(), s.rel_path.c_str(),
                            s.type, human_size(s.size).c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy Path"))
                ImGui::SetClipboardText(s.rel_path.c_str());
        } else {
            ImGui::Text("%zu items", entries_.size());
        }

        // What is on the clipboard, and how it will land. Explorer shows this
        // by ghosting the cut icon; there is no icon here to ghost, so it is
        // said in words -- otherwise a cut made two folders ago is invisible.
        if (!clipboard_.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("|  %s: %s", clipboard_.cut ? "Cut" : "Copied",
                                clipboard_.items.front().filename().string().c_str());
        }
        if (!status_.empty() && ImGui::GetTime() < status_until_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "|  %s", status_.c_str());
        }

        handle_shortcuts();
        render_modals();
    }
    ImGui::End();
}

void AssetBrowserPanel::render_toolbar() {
    // Pinned roots.
    for (size_t i = 0; i < roots_.size(); ++i) {
        if (i) ImGui::SameLine();
        const bool here = current_ == roots_[i].path;
        if (here) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.45f, 0.70f, 1.0f));
        if (ImGui::SmallButton(roots_[i].label.c_str())) navigate(roots_[i].path);
        if (here) ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("New...")) ImGui::OpenPopup("##ab_newmenu");
    if (ImGui::BeginPopup("##ab_newmenu")) { render_new_menu(); ImGui::EndPopup(); }
    ImGui::SameLine();
    if (ImGui::SmallButton(grid_view_ ? "List" : "Tiles")) grid_view_ = !grid_view_;
    ImGui::SameLine();
    if (ImGui::SmallButton(show_tree_ ? "Hide Tree" : "Show Tree")) show_tree_ = !show_tree_;
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) dirty_ = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Import...")) {
#ifdef _WIN32
        char file_buf[MAX_PATH] = {0};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFile   = file_buf;
        ofn.nMaxFile    = sizeof(file_buf);
        ofn.lpstrFilter = "All Files (*.*)\0*.*\0"
                          "3D Models (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0"
                          "Textures (*.png;*.jpg;*.jpeg;*.tga;*.hdr)\0*.png;*.jpg;*.jpeg;*.tga;*.hdr\0"
                          "Audio (*.wav;*.mp3;*.flac;*.ogg)\0*.wav;*.mp3;*.flac;*.ogg\0";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn) && file_buf[0]) {
            const fs::path src(file_buf);
            const fs::path dst = current_ / src.filename();
            // Stream copy (fs::copy_file overwrite is unreliable on MinGW).
            std::ifstream in(src, std::ios::binary);
            std::ofstream out(dst, std::ios::binary | std::ios::trunc);
            if (in.is_open() && out.is_open()) {
                out << in.rdbuf();
                spdlog::info("[AssetBrowser] imported '{}' -> '{}'",
                             src.string(), dst.string());
                dirty_ = true;
            } else {
                spdlog::warn("[AssetBrowser] import failed for '{}'", src.string());
            }
        }
#endif
    }

    // Search row.
    ImGui::SetNextItemWidth(220);
    const bool entered = ImGui::InputTextWithHint(
        "##ab_search",
        search_recursive_ ? "search subtree (Enter)" : "filter this folder",
        search_buf_, sizeof search_buf_,
        search_recursive_ ? ImGuiInputTextFlags_EnterReturnsTrue : 0);
    if (search_recursive_ ? entered : ImGui::IsItemEdited()) dirty_ = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Recursive", &search_recursive_)) dirty_ = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    static const char* kFilters[] = {"All", "Meshes", "Textures", "Audio",
                                     "Scripts", "Scenes", "Materials", "Other"};
    ImGui::Combo("##ab_filter", &type_filter_, kFilters, IM_ARRAYSIZE(kFilters));

    // Sort mode (folders always stay first).
    ImGui::SameLine();
    ImGui::TextDisabled("Sort");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    static const char* kSorts[] = {"Name", "Type", "Size"};
    if (ImGui::Combo("##ab_sort", &sort_mode_, kSorts, IM_ARRAYSIZE(kSorts))) dirty_ = true;

    // Tile zoom (grid view only) — Unreal-style content-browser scaling.
    if (grid_view_) {
        ImGui::SameLine();
        ImGui::TextDisabled("Zoom");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::SliderFloat("##ab_zoom", &tile_scale_, 0.6f, 2.0f, "%.1fx");
    }
}

void AssetBrowserPanel::render_breadcrumbs() {
    // Deepest pinned root containing current_; show clickable segments from it.
    const Root* base = nullptr;
    const std::string cur = current_.generic_string();
    for (const auto& r : roots_) {
        const std::string rp = r.path.generic_string();
        if (cur.rfind(rp, 0) == 0 &&
            (!base || rp.size() > base->path.generic_string().size()))
            base = &r;
    }
    if (!base) { ImGui::TextDisabled("%s", cur.c_str()); return; }

    if (ImGui::SmallButton((base->label + "##bc_root").c_str())) navigate(base->path);
    std::error_code ec;
    const fs::path rel = fs::relative(current_, base->path, ec);
    fs::path walk = base->path;
    if (!ec && rel != fs::path(".")) {
        int i = 0;
        for (const auto& seg : rel) {
            walk /= seg;
            ImGui::SameLine(); ImGui::TextDisabled(">"); ImGui::SameLine();
            const std::string id = seg.string() + "##bc" + std::to_string(i++);
            if (ImGui::SmallButton(id.c_str())) { navigate(walk); break; }
        }
    }
}

void AssetBrowserPanel::render_tree() {
    for (const auto& r : roots_) {
        ImGui::PushID(r.label.c_str());
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (current_ == r.path) flags |= ImGuiTreeNodeFlags_Selected;
        if (r.label == "Assets") flags |= ImGuiTreeNodeFlags_DefaultOpen;
        const bool node_open = ImGui::TreeNodeEx(r.label.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) navigate(r.path);
        if (node_open) {
            render_tree_dir(r.path, 0);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void AssetBrowserPanel::render_tree_dir(const fs::path& dir, int depth) {
    if (depth > 24) return;
    std::error_code ec;
    std::vector<fs::path> subdirs;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_directory(e2)) subdirs.push_back(it->path());
    }
    std::sort(subdirs.begin(), subdirs.end(), [](const fs::path& a, const fs::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    for (const auto& sd : subdirs) {
        const std::string name = sd.filename().string();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (current_ == sd) flags |= ImGuiTreeNodeFlags_Selected;
        const bool node_open = ImGui::TreeNodeEx(name.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) navigate(sd);
        if (node_open) {
            render_tree_dir(sd, depth + 1);   // lazy: only when expanded
            ImGui::TreePop();
        }
    }
}

void AssetBrowserPanel::render_entries() {
    const std::string needle = lower(search_buf_);
    const bool local_filter = !search_recursive_ && !needle.empty();

    auto passes_type = [&](const AssetEntry& e) {
        if (type_filter_ == 0 || e.is_dir) return true;
        switch (type_filter_) {
            case 1: return 0 == std::strcmp(e.type, "Mesh");
            case 2: return 0 == std::strcmp(e.type, "Texture");
            case 3: return 0 == std::strcmp(e.type, "Audio");
            case 4: return 0 == std::strcmp(e.type, "Script");
            case 5: return 0 == std::strcmp(e.type, "Scene");
            case 6: return 0 == std::strcmp(e.type, "Material");
            default:
                return std::strcmp(e.type, "Mesh") && std::strcmp(e.type, "Texture") &&
                       std::strcmp(e.type, "Audio") && std::strcmp(e.type, "Script") &&
                       std::strcmp(e.type, "Scene") && std::strcmp(e.type, "Material");
        }
    };

    // Background context menu (empty space): folder-level actions.
    if (ImGui::BeginPopupContextWindow("##ab_bg", ImGuiPopupFlags_MouseButtonRight |
                                                  ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu("New")) { render_new_menu(); ImGui::EndMenu(); }
        if (ImGui::MenuItem("Reveal in Explorer")) os_reveal(current_.string());
        if (ImGui::MenuItem("Refresh")) dirty_ = true;
        ImGui::EndPopup();
    }

    // ---- Tiles (thumbnail grid) view — the Unreal-style content browser look ----
    if (grid_view_) {
        const float tile_w = 92.0f * tile_scale_;
        const float icon   = 46.0f * tile_scale_;
        const float label_h = ImGui::GetTextLineHeight() * 2.0f + 4.0f;
        const float cell_h = 6.0f + icon + 8.0f + label_h;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int per_row = std::max(1, static_cast<int>(avail / (tile_w + 8.0f)));

        ImGui::BeginChild("##ab_grid", ImVec2(0, 0));
        int col = 0;
        bool navigated = false;
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            const AssetEntry& e = entries_[i];
            if (local_filter && lower(e.name).find(needle) == std::string::npos) continue;
            if (!passes_type(e)) continue;

            ImGui::PushID(i);
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::Selectable("##tile", selected_ == i,
                                                   ImGuiSelectableFlags_AllowDoubleClick, ImVec2(tile_w, cell_h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            draw_entry_icon(dl, ImVec2(cur.x + (tile_w - icon) * 0.5f, cur.y + 6.0f), icon, e);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(cur.x + 3.0f, cur.y + 6.0f + icon + 6.0f),
                        ImGui::GetColorU32(type_color(e.type)), e.name.c_str(), nullptr, tile_w - 6.0f);

            if (clicked) {
                if (selected_ != i && on_select_asset) on_select_asset(e);
                selected_ = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (e.is_dir) { navigate(e.abs_path); navigated = true; ImGui::PopID(); break; }
                    else if (0 == std::strcmp(e.type, "Scene") && on_open_scene) on_open_scene(e.rel_path);
                    else os_open(e.abs_path);
                }
            }
            if (!e.is_dir && ImGui::BeginDragDropSource()) {
                const std::string& data = (0 == std::strcmp(e.type, "Mesh")) ? e.abs_path : e.rel_path;
                ImGui::SetDragDropPayload(payload_for(e.type), data.c_str(), data.size() + 1);
                ImGui::Text("%s  (%s)", e.name.c_str(), e.type);
                ImGui::EndDragDropSource();
            }
            render_context_menu(e);
            ImGui::PopID();

            if (++col < per_row) ImGui::SameLine(0.0f, 8.0f); else col = 0;
        }
        (void)navigated;
        ImGui::EndChild();
        return;
    }

    if (ImGui::BeginTable("##ab_list", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            const AssetEntry& e = entries_[i];
            if (local_filter && lower(e.name).find(needle) == std::string::npos) continue;
            if (!passes_type(e)) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);

            // A hidden full-row Selectable handles hit-testing; the icon + name are
            // drawn on top so every file gets an explorer-style type icon.
            const ImVec2  icon_tl = ImGui::GetCursorScreenPos();
            const float   icon_h  = ImGui::GetTextLineHeight();
            const bool clicked = ImGui::Selectable(
                "##row", selected_ == i,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                ImVec2(0, icon_h));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            draw_entry_icon(dl, icon_tl, icon_h, e);
            dl->AddText(ImVec2(icon_tl.x + icon_h + 5.0f, icon_tl.y),
                        ImGui::GetColorU32(type_color(e.type)), e.name.c_str());
            if (clicked) {
                if (selected_ != i && on_select_asset) on_select_asset(e);
                selected_ = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (e.is_dir) {
                        navigate(e.abs_path);
                        ImGui::PopID();
                        break;   // entries_ changed — stop iterating
                    } else if (0 == std::strcmp(e.type, "Scene") && on_open_scene) {
                        on_open_scene(e.rel_path);
                    } else {
                        os_open(e.abs_path);   // scripts/textures open in default app
                    }
                }
            }

            // Drag source (files only): typed payload carrying the path string.
            // Meshes carry the ABSOLUTE path so the drop can always locate the
            // source file and import it into the project (a project-relative path
            // can fail to resolve depending on the launch CWD). Other asset types
            // keep the runtime-relative path.
            if (!e.is_dir && ImGui::BeginDragDropSource()) {
                const std::string& data =
                    (0 == std::strcmp(e.type, "Mesh")) ? e.abs_path : e.rel_path;
                ImGui::SetDragDropPayload(payload_for(e.type), data.c_str(), data.size() + 1);
                ImGui::Text("%s  (%s)", e.name.c_str(), e.type);
                ImGui::EndDragDropSource();
            }

            render_context_menu(e);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", e.type);
            ImGui::TableSetColumnIndex(2);
            if (!e.is_dir) ImGui::TextDisabled("%s", human_size(e.size).c_str());

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void AssetBrowserPanel::OsOpen(const std::string& abs_path)   { os_open(abs_path); }
void AssetBrowserPanel::OsReveal(const std::string& abs_path) { os_reveal(abs_path); }

const AssetEntry* AssetBrowserPanel::selected_entry() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size())) return nullptr;
    return &entries_[static_cast<size_t>(selected_)];
}

void AssetBrowserPanel::report(assetops::Error err, const std::string& ok_message) {
    dirty_ = true;
    if (err != assetops::Error::None) {
        // A refusal gets a modal rather than a log line. These are all cases
        // where the user asked for something and nothing happened; a silent
        // no-op reads as a broken editor.
        op_error_ = assetops::message(err);
        spdlog::warn("[AssetBrowser] {}", op_error_);
        return;
    }
    status_       = ok_message;
    status_until_ = ImGui::GetTime() + 4.0;
    spdlog::info("[AssetBrowser] {}", ok_message);
}

void AssetBrowserPanel::begin_rename(const AssetEntry& e) {
    pending_rename_ = e.abs_path;
    std::snprintf(rename_buf_, sizeof rename_buf_, "%s", e.name.c_str());
    want_rename_ = true;
}

void AssetBrowserPanel::begin_properties(const AssetEntry& e) {
    pending_props_ = e.abs_path;
    // Inspected ONCE here, not per frame: a folder's size means walking its
    // whole subtree, which a modal redrawn at 60 Hz would do 60 times a second.
    props_       = assetops::inspect(e.abs_path, e.type);
    want_props_  = true;
}

// The edit verbs, shared by the item menu and the empty-space menu so the two
// can never drift apart. `e` is null when the user right-clicked empty space,
// where only Paste applies.
void AssetBrowserPanel::render_edit_items(const AssetEntry* e) {
    const bool has_item = (e != nullptr);

    if (ImGui::MenuItem("Cut", "Ctrl+X", false, has_item))
        clipboard_.cut_one(e->abs_path);
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, has_item))
        clipboard_.copy_one(e->abs_path);

    // Paste targets the folder being SHOWN, or the folder under the cursor if
    // that is what was right-clicked -- which is what Explorer does.
    const bool into_dir = has_item && e->is_dir;
    const fs::path paste_dst = into_dir ? fs::path(e->abs_path) : current_;
    const std::string paste_label =
        into_dir ? std::string("Paste into \"") + e->name + "\"" : std::string("Paste");
    if (ImGui::MenuItem(paste_label.c_str(), "Ctrl+V", false, !clipboard_.empty())) {
        std::vector<fs::path> made;
        const assetops::Error err = assetops::paste(clipboard_, paste_dst, &made);
        report(err, made.empty() ? "Pasted." : "Pasted " + made[0].filename().string());
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Rename", "F2", false, has_item)) begin_rename(*e);
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has_item)) {
        fs::path out;
        const assetops::Error err = assetops::duplicate(e->abs_path, &out);
        report(err, out.empty() ? "Duplicated." : "Created " + out.filename().string());
    }
    if (ImGui::MenuItem("Delete...", "Del", false, has_item)) pending_delete_ = e->abs_path;
}

void AssetBrowserPanel::render_context_menu(const AssetEntry& e) {
    if (ImGui::BeginPopupContextItem("##ab_item")) {
        ImGui::TextDisabled("%s", e.name.c_str());
        ImGui::Separator();
        if (!e.is_dir && ImGui::MenuItem("Open", "Enter")) os_open(e.abs_path);
        if (e.is_dir && ImGui::MenuItem("Open Folder", "Enter")) navigate(e.abs_path);
        ImGui::Separator();
        render_edit_items(&e);
        ImGui::Separator();
        if (ImGui::MenuItem("Reveal in Explorer")) os_reveal(e.abs_path);
        if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(e.rel_path.c_str());
        if (ImGui::MenuItem("Copy Absolute Path")) ImGui::SetClipboardText(e.abs_path.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Properties", "Alt+Enter")) begin_properties(e);
        ImGui::EndPopup();
    }
}

// Right-click on empty space. Explorer offers Paste and New here; without it,
// a copied file has nowhere to be pasted except onto another file.
// Dropping a scene entity here saves it as a prefab in the folder being shown.
// The target covers the WHOLE file pane rather than a single row, because the
// gesture is "put this in this folder", not "put this on that file".
void AssetBrowserPanel::render_entity_drop_target() {
    if (!ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect,
                                          ImGui::GetID("##ab_entity_drop")))
        return;
    if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ENTITY_NODE")) {
        if (pl->DataSize == sizeof(uint32_t) && on_drop_entity) {
            uint32_t id = 0;
            std::memcpy(&id, pl->Data, sizeof id);
            const std::string written = on_drop_entity(id, current_.string());
            if (written.empty()) report(assetops::Error::Filesystem, "");
            else                 report(assetops::Error::None, "Saved " + written);
        }
    }
    ImGui::EndDragDropTarget();
}

void AssetBrowserPanel::render_background_menu() {
    if (ImGui::BeginPopupContextWindow("##ab_bg",
                                       ImGuiPopupFlags_MouseButtonRight |
                                       ImGuiPopupFlags_NoOpenOverItems)) {
        ImGui::TextDisabled("%s", current_.filename().string().c_str());
        ImGui::Separator();
        render_edit_items(nullptr);
        ImGui::Separator();
        if (ImGui::BeginMenu("New")) { render_new_menu(); ImGui::EndMenu(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Reveal Folder in Explorer")) os_reveal(current_.string());
        if (ImGui::MenuItem("Refresh")) dirty_ = true;
        ImGui::EndPopup();
    }
}

// Keyboard verbs. Gated on the panel being focused, or Ctrl+C in the viewport
// would copy a file instead of an entity.
void AssetBrowserPanel::handle_shortcuts() {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    // A modal is up, or a text field has the keyboard: F2 inside the rename box
    // must type an 'F2', not restart the rename.
    if (ImGui::GetIO().WantTextInput) return;
    if (!pending_rename_.empty() || !pending_delete_.empty() || !op_error_.empty()) return;

    const bool ctrl = ImGui::GetIO().KeyCtrl;
    const AssetEntry* e = selected_entry();

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !clipboard_.empty()) {
        std::vector<fs::path> made;
        const assetops::Error err = assetops::paste(clipboard_, current_, &made);
        report(err, made.empty() ? "Pasted." : "Pasted " + made[0].filename().string());
        return;
    }
    if (!e) return;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) clipboard_.copy_one(e->abs_path);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) clipboard_.cut_one(e->abs_path);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        fs::path out;
        const assetops::Error err = assetops::duplicate(e->abs_path, &out);
        report(err, out.empty() ? "Duplicated." : "Created " + out.filename().string());
    }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F2, false))     begin_rename(*e);
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) pending_delete_ = e->abs_path;
    if (ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) begin_properties(*e);
}

void AssetBrowserPanel::render_new_menu() {
    if (ImGui::MenuItem("Folder...")) { want_new_folder_ = true; new_folder_buf_[0] = '\0'; }
    ImGui::Separator();
    auto file = [&](const char* label, const char* defname, const char* tmpl) {
        if (ImGui::MenuItem(label)) {
            std::snprintf(new_file_buf_, sizeof new_file_buf_, "%s", defname);
            new_file_template_ = tmpl;
            want_new_file_ = true;
        }
    };
    // A material was the one asset with NO way to create it. Every control for
    // editing one existed, reachable only by selecting an object that already
    // had a material -- so a fresh project could not get its first .mat at all,
    // and terrain (which has no MeshRenderer) could not reach one ever.
    //
    // The template is generated by the format's OWN writer rather than being a
    // string literal here, so a new field cannot appear in the format and be
    // missing from every material this menu creates.
    if (ImGui::MenuItem("Material (.mat)")) {
        std::snprintf(new_file_buf_, sizeof new_file_buf_, "%s", "new_material.mat");
        gws::assets::MaterialDesc fresh;
        fresh.name = "new_material";
        new_file_template_ = gws::assets::material_to_text(fresh);
        want_new_file_ = true;
    }
    ImGui::Separator();
    if (ImGui::BeginMenu("Script")) {
        file("Python (.py)", "new_script.py",  kTemplatePy);
        file("C# (.cs)",     "new_script.cs",  kTemplateCs);
        file("C++ (.cpp)",   "new_script.cpp", kTemplateCpp);
        ImGui::EndMenu();
    }
    // A new project has NO texture in it. Every path for applying one worked
    // and there was nothing to apply, which is a poor answer when a
    // checkerboard is forty lines of code. These three are the ones worth
    // having: a checker is the only pattern that makes a REFLECTION legible
    // (a flat colour reflects a flat colour, so a mirror and a matte surface
    // look identical), a grid reads scale and tiling on ground, and a UV chart
    // is the only one that shows a flipped or rotated UV set instead of hiding
    // it. Written as BMP -- no encoder, no dependency, and stb_image reads it.
    if (ImGui::BeginMenu("Texture")) {
        auto tex = [&](const char* label, const char* defname, texgen::Pattern p) {
            if (ImGui::MenuItem(label)) {
                std::snprintf(new_file_buf_, sizeof new_file_buf_, "%s", defname);
                new_file_template_ = texgen::bmp_for(p, 512, 8);
                want_new_file_ = true;
            }
        };
        tex("Checker",  "checker.bmp",  texgen::Pattern::Checker);
        tex("Grid",     "grid.bmp",     texgen::Pattern::Grid);
        tex("UV chart", "uv_chart.bmp", texgen::Pattern::UV);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Gameplay")) {
        file("Item Definitions (.items)", "items.items", kTemplateItems);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Misc")) {
        file("Text File (.txt)", "notes.txt", "");
        ImGui::EndMenu();
    }
}

void AssetBrowserPanel::render_modals() {
    // Open the New Folder / New File modals requested from a (now-closed) menu.
    if (want_new_folder_) { ImGui::OpenPopup("New Folder##ab"); want_new_folder_ = false; }
    if (want_new_file_)   { ImGui::OpenPopup("New File##ab");   want_new_file_   = false; }
    if (want_rename_)     { ImGui::OpenPopup("Rename##ab");     want_rename_     = false; }
    if (want_props_)      { ImGui::OpenPopup("Properties##ab"); want_props_      = false; }
    if (!op_error_.empty() && !ImGui::IsPopupOpen("Cannot do that##ab"))
        ImGui::OpenPopup("Cannot do that##ab");

    // A refused operation. The text comes straight from asset_file_ops.h, which
    // explains WHY rather than reporting a code -- most of these refusals are
    // Windows rules the user has no reason to know.
    if (ImGui::BeginPopupModal("Cannot do that##ab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 380.0f);
        ImGui::TextUnformatted(op_error_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            op_error_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Rename. Validation runs as the user types, so the reason a name is
    // refused appears before they commit to it rather than after.
    if (ImGui::BeginPopupModal("Rename##ab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s", pending_rename_.c_str());
        ImGui::SetNextItemWidth(320);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool submit = ImGui::InputText("##ab_rn", rename_buf_, sizeof rename_buf_,
                                             ImGuiInputTextFlags_EnterReturnsTrue);
        assetops::Error why = assetops::Error::None;
        const bool name_ok = assetops::is_valid_filename(rename_buf_, &why);
        if (!name_ok) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 320.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "%s", assetops::message(why));
            ImGui::PopTextWrapPos();
        }
        ImGui::BeginDisabled(!name_ok);
        if ((ImGui::Button("Rename", ImVec2(120, 0)) || (submit && name_ok)) && name_ok) {
            fs::path out;
            const assetops::Error err = assetops::rename(pending_rename_, rename_buf_, &out);
            report(err, out.empty() ? "Renamed." : "Renamed to " + out.filename().string());
            pending_rename_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pending_rename_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Properties. Read from the snapshot taken when the dialog opened.
    if (ImGui::BeginPopupModal("Properties##ab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!props_.valid) {
            ImGui::TextUnformatted("This item no longer exists.");
        } else if (ImGui::BeginTable("##ab_props", 2, ImGuiTableFlags_SizingStretchProp)) {
            auto row = [](const char* k, const std::string& v) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", k);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(v.c_str());
            };
            row("Name",     props_.name);
            row("Type",     props_.type);
            row("Location", props_.location);
            row("Size",     human_size(props_.size_bytes) +
                            (props_.is_dir ? " (" + std::to_string(props_.file_count) + " files, " +
                                             std::to_string(props_.dir_count) + " folders)" : ""));
            row("Modified", props_.modified.empty() ? "unknown" : props_.modified);
            row("Attributes", props_.read_only ? "Read-only" : "Normal");
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pending_props_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Delete confirmation.
    if (!pending_delete_.empty() && !ImGui::IsPopupOpen("Delete?##ab"))
        ImGui::OpenPopup("Delete?##ab");
    if (ImGui::BeginPopupModal("Delete?##ab", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Permanently delete\n%s ?", pending_delete_.c_str());
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            std::error_code ec;
            const uintmax_t n = fs::remove_all(pending_delete_, ec);
            if (ec) spdlog::warn("[AssetBrowser] delete failed: {}", ec.message());
            else    spdlog::info("[AssetBrowser] deleted {} item(s): {}", n, pending_delete_);
            pending_delete_.clear();
            dirty_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            pending_delete_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // New folder.
    if (ImGui::BeginPopupModal("New Folder##ab", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name##ab_nf", new_folder_buf_, sizeof new_folder_buf_);
        if (ImGui::Button("Create", ImVec2(120, 0)) && new_folder_buf_[0]) {
            std::error_code ec;
            fs::create_directory(current_ / new_folder_buf_, ec);
            if (ec) spdlog::warn("[AssetBrowser] create folder failed: {}", ec.message());
            dirty_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // New file (script / item-defs / text) from a template.
    if (ImGui::BeginPopupModal("New File##ab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Created in: %s", current_.filename().string().c_str());
        ImGui::SetNextItemWidth(280);
        const bool submit = ImGui::InputText("Name##ab_nfile", new_file_buf_, sizeof new_file_buf_,
                                             ImGuiInputTextFlags_EnterReturnsTrue);
        std::error_code ec;
        const bool exists = new_file_buf_[0] && fs::exists(current_ / new_file_buf_, ec);
        if (exists) ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "A file with that name already exists.");
        ImGui::BeginDisabled(!new_file_buf_[0] || exists);
        if ((ImGui::Button("Create", ImVec2(120, 0)) || submit) && new_file_buf_[0] && !exists) {
            const fs::path dst = current_ / new_file_buf_;
            std::ofstream out(dst, std::ios::binary | std::ios::trunc);
            if (out.is_open()) {
                out << new_file_template_;
                spdlog::info("[AssetBrowser] created '{}'", dst.string());
            } else {
                spdlog::warn("[AssetBrowser] could not create '{}'", dst.string());
            }
            dirty_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace schizo::editor
