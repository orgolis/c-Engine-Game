#pragma once

// ============================================================================
// vfx_io — the .vfx text format (item 4.3).
//
// Conventions are taken wholesale from assets/material_desc.h rather than
// invented: a version stamp, every field written including defaults, unknown
// keys ignored, missing keys defaulted, and a parse that always succeeds.
// Writing defaults matters because a partially-written file makes "did the
// editor fail to save this, or was it just default?" unanswerable from the file
// alone.
//
// Text rather than binary, per the position settled in 3.7: assets live in
// version control and a binary blob makes every edit an unreviewable diff.
//
// THE ONE ADDITION material_desc does not need: modules are an ORDERED
// sequence. STAGE= is a delimiter, and MODULE.n.* belongs to the stage most
// recently named. `n` groups one module's keys together for a human reading the
// file -- it is NOT a position, and the loader ignores its value, appending in
// the order KIND lines appear. Duplicated, skipped or descending indices
// therefore load correctly instead of silently reordering an effect.
// ============================================================================

#include "vfx/vfx_graph.h"

#include <string>

namespace schizo::vfx {

inline constexpr const char* kVfxExtension     = ".vfx";
inline constexpr int         kVfxFormatVersion = 1;

std::string vfx_to_text(const VfxGraph& g);

/// Always succeeds -- garbage yields a default graph. The caller that needs to
/// know whether a FILE existed is load_vfx(), and conflating "unparseable" with
/// "absent" is how a missing asset becomes a silently invisible effect.
VfxGraph vfx_from_text(const std::string& text);

/// False only when the file cannot be read.
bool load_vfx(const std::string& path, VfxGraph& out);
bool save_vfx(const std::string& path, const VfxGraph& g);

}  // namespace schizo::vfx
