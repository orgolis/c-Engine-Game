// ============================================================
// Asset Browser implementation — see asset_browser_panel.h
// ============================================================

#include "asset_browser_panel.h"

#include "scene.h"

#include <imgui.h>
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
                ImGui::EndChild();
                ImGui::EndTable();
            }
        } else {
            ImGui::BeginChild("##ab_files", ImVec2(0, -status_h));
            render_entries();
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
                                     "Scripts", "Scenes", "Other"};
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
            default:
                return std::strcmp(e.type, "Mesh") && std::strcmp(e.type, "Texture") &&
                       std::strcmp(e.type, "Audio") && std::strcmp(e.type, "Script") &&
                       std::strcmp(e.type, "Scene");
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

void AssetBrowserPanel::render_context_menu(const AssetEntry& e) {
    if (ImGui::BeginPopupContextItem("##ab_item")) {
        ImGui::TextDisabled("%s", e.name.c_str());
        ImGui::Separator();
        if (!e.is_dir && ImGui::MenuItem("Open")) os_open(e.abs_path);
        if (e.is_dir && ImGui::MenuItem("Open Folder")) navigate(e.abs_path);
        if (ImGui::MenuItem("Reveal in Explorer")) os_reveal(e.abs_path);
        if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(e.rel_path.c_str());
        if (ImGui::MenuItem("Copy Absolute Path")) ImGui::SetClipboardText(e.abs_path.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Delete...")) pending_delete_ = e.abs_path;
        ImGui::EndPopup();
    }
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
    if (ImGui::BeginMenu("Script")) {
        file("Python (.py)", "new_script.py",  kTemplatePy);
        file("C# (.cs)",     "new_script.cs",  kTemplateCs);
        file("C++ (.cpp)",   "new_script.cpp", kTemplateCpp);
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
