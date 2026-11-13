#pragma once

// ============================================================================
// Items editor -- a .items file (items and weapons) as a form, not as text.
// ============================================================================
//
// Opened from the Asset Browser. The left side lists the file's items and
// weapons; the right side edits the selected one. Saving writes the file back
// (ecs::items_doc_to_text: blocks keep their order, the comment lines at the
// top are kept, comments between blocks are not) and registers its contents
// at once, so Play and the Inspector see the change without a restart.

#include "document_bar.h"
#include "ecs/gameplay_item_file.h"

#include <string>

namespace schizo::editor {

inline constexpr const char* kItemsExtension = ".items";

struct ItemsPanelState {
    ecs::ItemsDoc doc;
    int           selected = 0;
    char          filter[64] = "";
};

/// Read a .items file into the document. False when it cannot be read.
bool load_items_doc(const std::string& path, ecs::ItemsDoc& out);
/// Write the document to `path` and register its items and weapons.
bool save_items_doc(const std::string& path, const ecs::ItemsDoc& doc);

/// The panel. `doc` is the file it edits (path, dirty, Save button).
void draw_items_panel(bool& open, ItemsPanelState& st, DocumentFile* doc);

}  // namespace schizo::editor
