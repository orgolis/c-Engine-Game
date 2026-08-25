#pragma once

// ============================================================================
// Asset Inspector — what the Inspector panel shows when the selection is a
// FILE rather than an entity.
//
// Selecting an asset used to do nothing outside the browser: the Inspector kept
// saying "No entity selected" while a file sat highlighted two panels away.
//
// Everything shown here is captured ONCE, when the selection changes. Both the
// facts (a folder's size means walking its subtree) and the preview (reading a
// file) are far too expensive to redo at frame rate, and neither changes while
// the user looks at it. `Refresh()` exists for the cases that do change --
// after a rename, or an edit made elsewhere in the editor.
// ============================================================================

#include "asset_file_ops.h"

#include <functional>
#include <fstream>
#include <string>

namespace schizo::editor {

/// What the host editor can do with an asset. Left empty, the matching button
/// simply is not drawn -- the inspector never offers an action nobody wired.
struct AssetInspectorActions {
    std::function<void(const std::string& rel_path)> open_scene;
    std::function<void(const std::string& rel_path)> edit_material;
    std::function<void(const std::string& abs_path)> reveal_in_explorer;
    std::function<void(const std::string& abs_path)> open_external;
};

class AssetInspector {
public:
    /// Point the inspector at a file. Re-selecting the same path is a no-op, so
    /// clicking an already-selected row does not re-read it.
    void Select(const std::string& abs_path, const std::string& rel_path, const char* type) {
        if (abs_path == abs_path_) return;
        abs_path_ = abs_path;
        rel_path_ = rel_path;
        type_     = type ? type : "File";
        Refresh();
    }

    void Clear() {
        abs_path_.clear();
        rel_path_.clear();
        preview_.clear();
        props_ = {};
    }

    /// Re-read facts and preview for the current selection.
    void Refresh() {
        props_ = assetops::inspect(abs_path_, type_.c_str());
        load_preview();
    }

    bool HasSelection() const { return !abs_path_.empty(); }
    const std::string& abs_path() const { return abs_path_; }
    const std::string& rel_path() const { return rel_path_; }
    const assetops::Properties& properties() const { return props_; }
    const std::string& preview() const { return preview_; }
    bool preview_truncated() const { return preview_truncated_; }
    bool previewable() const { return previewable_; }
    const std::string& type() const { return type_; }

    /// A file is previewable when it is text. Detected by scanning for a NUL
    /// rather than by extension: an unknown text format should still preview,
    /// and a .dat full of binary should not, whatever it is called.
    static bool looks_like_text(const std::string& bytes) {
        for (char c : bytes)
            if (c == '\0') return false;
        return true;
    }

private:
    static constexpr size_t kPreviewBytes = 8 * 1024;

    void load_preview() {
        preview_.clear();
        preview_truncated_ = false;
        previewable_       = false;
        if (!props_.valid || props_.is_dir) return;

        std::ifstream f(abs_path_, std::ios::binary);
        if (!f.is_open()) return;

        std::string buf(kPreviewBytes, '\0');
        f.read(&buf[0], static_cast<std::streamsize>(kPreviewBytes));
        buf.resize(static_cast<size_t>(f.gcount()));
        if (!looks_like_text(buf)) return;

        previewable_       = true;
        preview_           = buf;
        preview_truncated_ = props_.size_bytes > kPreviewBytes;
    }

    std::string          abs_path_;
    std::string          rel_path_;
    std::string          type_ = "File";
    assetops::Properties props_;
    std::string          preview_;
    bool                 preview_truncated_ = false;
    bool                 previewable_       = false;
};

} // namespace schizo::editor
