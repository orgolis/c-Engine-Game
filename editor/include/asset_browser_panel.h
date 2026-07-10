#pragma once

// ============================================================
// Asset Browser — full-project, Unity-style two-pane browser
// ============================================================
//
// Left: lazy directory tree over pinned roots (Runtime = the editor's working
// dir, source Assets, Scenes, Scripts, and the whole Project root). Right:
// the current folder's entries with type coloring, search (optionally
// recursive), a breadcrumb bar, and per-item context actions (open, reveal in
// Explorer, copy path, delete). Files are drag sources: payload
// `kPayloadType` carries the RUNTIME-RELATIVE path as a NUL-terminated string
// (self-contained — consumers never need the panel instance). Double-click
// opens: folders navigate, .scene files load into the editor, everything else
// opens with the OS default app (scripts land in your code editor).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace schizo::scene { class Scene; }

namespace schizo::editor {

struct AssetEntry {
    std::string abs_path;    // absolute, native separators
    std::string rel_path;    // relative to the runtime CWD (what gameplay code loads)
    std::string name;        // filename or folder name
    std::string ext;         // lowercased extension ("" for folders)
    const char* type = "File";   // "Folder", "Mesh", "Texture", "Audio", "Script", ...
    uint64_t    size = 0;
    bool        is_dir = false;
};

class AssetBrowserPanel {
public:
    // Drag-drop payload types, chosen by the dragged file's kind so drop
    // targets keep their type gating (a texture can't land on a mesh field).
    // ALL payloads carry the same data: a NUL-terminated RUNTIME-RELATIVE
    // path string — consumers cast Payload->Data to const char* and use it
    // directly (no panel lookup needed).
    static constexpr const char* kPayloadMesh    = "MESH_ASSET";
    static constexpr const char* kPayloadTexture = "TEXTURE_ASSET";
    static constexpr const char* kPayloadAudio   = "AUDIO_ASSET";
    static constexpr const char* kPayloadScript  = "SCRIPT_ASSET";
    static constexpr const char* kPayloadScene   = "SCENE_ASSET";
    static constexpr const char* kPayloadOther   = "ASSET_ITEM";

    AssetBrowserPanel();
    ~AssetBrowserPanel();

    /// Render the browser. `scene` is unused by the browser itself but kept
    /// for signature compatibility; `open` drives the close button.
    void Render(const std::shared_ptr<schizo::scene::Scene>& scene, bool* open = nullptr);

    /// Re-scan the current directory (main.cpp calls this after OS file drops).
    void RefreshAssets();

    /// Navigate to a directory (absolute or CWD-relative).
    void SetDirectory(const std::string& path);

    /// Invoked when the user double-clicks a .scene file (main.cpp wires this
    /// to EditorScene::LoadScene).
    std::function<void(const std::string& runtime_relative_path)> on_open_scene;

private:
    // ---- data ----
    struct Root { std::string label; std::filesystem::path path; };
    std::vector<Root>       roots_;
    std::filesystem::path   project_root_;   // repo root (detected from CWD)
    std::filesystem::path   cwd_;            // runtime working dir
    std::filesystem::path   current_;        // directory shown in the right pane
    std::vector<AssetEntry> entries_;        // current dir listing (or search hits)
    int                     selected_ = -1;

    char  search_buf_[128] = {};
    bool  search_recursive_ = false;
    int   type_filter_ = 0;                  // 0 = All
    bool  show_tree_ = true;
    bool  dirty_ = true;                     // re-list on next frame

    // Pending modal state
    std::string pending_delete_;             // abs path awaiting confirmation
    char        new_folder_buf_[128] = {};

    // ---- helpers ----
    void detect_roots();
    void list_current();                     // fills entries_ from current_ (+ search)
    void render_toolbar();
    void render_breadcrumbs();
    void render_tree();
    void render_tree_dir(const std::filesystem::path& dir, int depth);
    void render_entries();
    void render_context_menu(const AssetEntry& e);
    void render_modals();
    void navigate(const std::filesystem::path& dir);

    static const char* classify(const std::string& ext_lower);
    static bool search_skip_dir(const std::string& name);
    std::string runtime_relative(const std::filesystem::path& abs) const;
};

} // namespace schizo::editor
