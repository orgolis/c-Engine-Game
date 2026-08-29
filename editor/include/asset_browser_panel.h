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

#include "asset_file_ops.h"

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
    static constexpr const char* kPayloadMaterial = "MATERIAL_ASSET";
    static constexpr const char* kPayloadPrefab   = "PREFAB_ASSET";
    static constexpr const char* kPayloadOther   = "ASSET_ITEM";

    AssetBrowserPanel();
    ~AssetBrowserPanel();

    /// Render the browser. `scene` is unused by the browser itself but kept
    /// for signature compatibility; `open` drives the close button.
    void Render(const std::shared_ptr<schizo::scene::Scene>& scene, bool* open = nullptr);

    /// Shell handoffs, exposed because the Inspector offers the same two verbs
    /// on the same file and duplicating the ShellExecute calls would let the
    /// two drift. No-ops off Windows.
    static void OsOpen(const std::string& abs_path);
    static void OsReveal(const std::string& abs_path);

    /// Re-scan the current directory (main.cpp calls this after OS file drops).
    void RefreshAssets();

    /// Navigate to a directory (absolute or CWD-relative).
    void SetDirectory(const std::string& path);

    /// Re-detect roots against the current working directory and jump to its
    /// assets/. Call after a project becomes the CWD so the browser only shows
    /// the open project (project sandbox).
    void Reroot();

    /// Invoked when the user double-clicks a .scene file (main.cpp wires this
    /// to EditorScene::LoadScene).
    std::function<void(const std::string& runtime_relative_path)> on_open_scene;

    /// Invoked when the SELECTED entry changes (single click), so the Inspector
    /// can show the asset. Fires on the change only, never every frame -- the
    /// receiver reads the file, which is not something to do at frame rate.
    std::function<void(const AssetEntry&)> on_select_asset;

    /// Double-click on an authoring document (.matgraph / .animgraph / .seq).
    /// Return true when it was opened in-editor; false falls through to the OS,
    /// so a document type the editor has not wired yet still opens in a text
    /// editor instead of doing nothing at all.
    std::function<bool(const AssetEntry&)> on_open_document;

    /// Invoked when a scene-hierarchy entity is dropped onto the browser, to
    /// save it as a prefab. The panel supplies the DESTINATION FOLDER and a
    /// unique filename; the host resolves the id and writes the file, because
    /// the browser knows nothing about scenes and should not start now.
    /// Returns the name written, or empty on failure.
    std::function<std::string(uint32_t entity_id, const std::string& dest_dir)> on_drop_entity;

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
    int   sort_mode_ = 0;                    // 0 = Name, 1 = Type, 2 = Size
    bool  show_tree_ = true;
    bool  grid_view_ = true;                 // tiles (Unreal-style) vs list
    float tile_scale_ = 1.0f;                // grid tile zoom (0.6..2.0)
    bool  dirty_ = true;                     // re-list on next frame

    // Pending modal state
    std::string pending_delete_;             // abs path awaiting confirmation
    char        new_folder_buf_[128] = {};
    char        new_file_buf_[128]   = {};   // filename for the "New <file>" modal
    std::string new_file_template_;          // content written to a new file
    bool        want_new_folder_ = false;    // menu -> open the New Folder modal
    bool        want_new_file_   = false;    // menu -> open the New File modal

    // ---- Explorer-style file operations ----------------------------------
    // The operations themselves live in asset_file_ops.h, ImGui-free and under
    // test; what is kept here is only the UI state wrapped around them.
    assetops::Clipboard clipboard_;
    std::string pending_rename_;             // abs path being renamed
    char        rename_buf_[128] = {};
    bool        want_rename_ = false;
    std::string pending_props_;              // abs path shown in Properties
    assetops::Properties props_;             // cached: inspecting a folder walks it
    bool        want_props_ = false;
    std::string op_error_;                   // a refusal, shown until dismissed
    std::string status_;                     // transient success line
    double      status_until_ = 0.0;         // when to stop showing it

    /// Absolute path of the entry currently being dragged FROM this panel.
    ///
    /// The payload itself carries a runtime-relative path (or, for meshes, an
    /// absolute one) because that is what the scene consumers want. A move
    /// needs the absolute source regardless of type, and ImGui allows only one
    /// payload per drag, so the source is recorded here when the drag starts.
    /// Only ever read while a payload from this panel is being delivered.
    std::string drag_abs_path_;

    // ---- helpers ----
    void detect_roots();
    void list_current();                     // fills entries_ from current_ (+ search)
    void render_toolbar();
    void render_breadcrumbs();
    void render_tree();
    void render_tree_dir(const std::filesystem::path& dir, int depth);
    void render_entries();
    void render_context_menu(const AssetEntry& e);
    void render_background_menu();               // right-click on empty space
    void render_entity_drop_target();            // hierarchy entity -> .prefab
    void render_move_target(const std::filesystem::path& dst_dir);  // drop a file INTO a folder
    void render_edit_items(const AssetEntry* e); // cut/copy/paste/... shared by both menus
    void handle_shortcuts();                     // F2, Ctrl+C/X/V/D, Del
    void begin_rename(const AssetEntry& e);
    void begin_properties(const AssetEntry& e);
    void report(assetops::Error err, const std::string& ok_message);
    const AssetEntry* selected_entry() const;
    void render_new_menu();                  // "New" items (folder / script / item-defs / text)
    void render_modals();
    void navigate(const std::filesystem::path& dir);

    static const char* classify(const std::string& ext_lower);
    static bool search_skip_dir(const std::string& name);
    std::string runtime_relative(const std::filesystem::path& abs) const;
};

} // namespace schizo::editor
