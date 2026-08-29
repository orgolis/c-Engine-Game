#pragma once
// ============================================================================
// document_bar — the one row every authoring panel needs once its document can
// be saved (4.10).
//
// Shared rather than written three times because the three panels would drift:
// the material graph would say "Save", the timeline "Save Sequence", one would
// show the path and another the filename, and one of them would eventually
// forget to mark itself clean. There is one row, so there is one behaviour.
//
// It deliberately does NOT perform the save. The panel does not know which
// serialiser its document needs, and giving it one would drag doc_io -- and so
// all three document headers -- into every panel. The bar raises a flag; the
// owner (main.cpp), which already holds both the document and its path, acts on
// it. That also keeps the panels free of the filesystem, which is what lets
// them be tested.
// ============================================================================

#include <string>

namespace schizo::editor {

struct DocumentFile {
    std::string path;              // empty == this document has no file yet
    std::string status;            // last save result, shown beside the button
    std::string saved_text;        // the document as last written to disk
    // Recomputed by the owner from a fresh serialise-and-compare rather than
    // raised by an edit. A flag has to be set at every mutation site and the one
    // that gets missed leaves the document permanently clean -- silently
    // discarding work on close, which is the exact failure this whole item
    // exists to end. A comparison cannot be forgotten at a call site.
    bool        dirty = false;
    bool        save_requested = false;   // raised by the bar, consumed by the owner
};

/// Draw the path + Save row at the top of a document panel.
/// `type_label` names the document in the "no file yet" hint ("material graph").
void draw_document_bar(DocumentFile& doc, const char* type_label);

}  // namespace schizo::editor
