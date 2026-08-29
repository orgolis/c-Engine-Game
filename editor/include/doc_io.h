#pragma once
// ============================================================================
// doc_io — persistence for the three authoring documents that had none (4.10).
//
// `MaterialGraph`, `AnimGraph` and `Sequence` were all built, all reachable and
// all tested, and not one of them could be saved. A material graph or a cutscene
// was lost the moment the editor closed. Phase 4's exit criterion says a
// developer can *author* those things without opening a C++ file; it never said
// "keep", and closing the phase at 9-of-9 did not close this.
//
// CONVENTIONS ARE TAKEN WHOLESALE from vfx_io.h, which took them from
// material_desc.h, rather than invented per document:
//
//   * a version stamp on line one;
//   * every field written, defaults included -- a partially written file makes
//     "did the editor fail to save this, or was it just default?" unanswerable
//     from the file alone;
//   * unknown keys ignored, missing keys defaulted;
//   * parsing ALWAYS succeeds, so a damaged file yields a usable empty document
//     rather than an error path nobody wrote a UI for. Only the loaders that
//     touch the filesystem report false, and only for a file they could not read
//     -- conflating "unparseable" with "absent" is how a missing asset becomes a
//     silently blank one;
//   * text, not binary: these live in version control, and a binary blob makes
//     every edit an unreviewable diff (the position settled in 3.7).
//
// TWO DECISIONS SPECIFIC TO THESE THREE:
//
// **Enums are written as NAMES, never as their integer value.** An index is a
// promise that nobody will ever reorder the enum, and the day someone inserts a
// NodeKind every saved graph silently becomes a different graph -- with no error,
// because every index still parses. This is the same argument that put a bus
// NAME in the audio source rather than a bus index.
//
// **Node and state IDs are remapped on load, not trusted.** The file's ids are
// read into a map and the document is rebuilt through its own public add_*
// API, so the graph's internal id counter stays consistent with what it holds.
// Writing ids straight into the vectors would leave the counter at 1 and the
// next node the user added would collide with a loaded one.
// ============================================================================

#include "anim_graph.h"
#include "sequence.h"
#include "shadergraph/material_graph.h"

#include <string>

namespace schizo::editor {

inline constexpr const char* kMaterialGraphExtension = ".matgraph";
inline constexpr const char* kAnimGraphExtension     = ".animgraph";
inline constexpr const char* kSequenceExtension      = ".seq";
inline constexpr int         kDocFormatVersion       = 1;

// ---- MaterialGraph ----
std::string material_graph_to_text(const gws::shadergraph::MaterialGraph& g);
void        material_graph_from_text(const std::string& text, gws::shadergraph::MaterialGraph& out);
bool        load_material_graph(const std::string& path, gws::shadergraph::MaterialGraph& out);
bool        save_material_graph(const std::string& path, const gws::shadergraph::MaterialGraph& g);

// ---- AnimGraph ----
std::string anim_graph_to_text(const AnimGraph& g);
void        anim_graph_from_text(const std::string& text, AnimGraph& out);
bool        load_anim_graph(const std::string& path, AnimGraph& out);
bool        save_anim_graph(const std::string& path, const AnimGraph& g);

// ---- Sequence ----
std::string sequence_to_text(const Sequence& s);
void        sequence_from_text(const std::string& text, Sequence& out);
bool        load_sequence(const std::string& path, Sequence& out);
bool        save_sequence(const std::string& path, const Sequence& s);

// ---- starter documents ----
// The exact text the Asset Browser's New menu writes. These live HERE, not
// inline in the panel, so docio_check can load the real template rather than a
// copy of it. v0.8.0 shipped an editor-extension template that could not run
// because its test built its own version and the two disagreed; the fix is to
// have one definition and point both at it.
std::string starter_material_graph_text();
std::string starter_anim_graph_text();
std::string starter_sequence_text();

}  // namespace schizo::editor
