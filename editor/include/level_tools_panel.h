#pragma once
// ============================================================================
// level_tools_panel — arrays, scatter, splines and greybox (4.7).
//
// The maths is in level_tools.h and tested there; this is only the surface.
// ============================================================================

#include "level_tools.h"

#include <cstdint>
#include <memory>

namespace schizo::scene { class Scene; }

namespace schizo::editor {

struct SnapSettings;

/// Draw the panel. Operations act on the entity with `selected_entity_id` and
/// honour `snap`, so placements land on the same grid the gizmo uses -- an
/// array that ignored snapping would need re-aligning by hand, which is the
/// work the tool exists to remove. `out_modified` is SET when entities are
/// created, never cleared.
void draw_level_tools_panel(bool& open,
                            const std::shared_ptr<schizo::scene::Scene>& scene,
                            uint32_t selected_entity_id,
                            const SnapSettings& snap,
                            Spline& spline,
                            bool& out_modified);

}  // namespace schizo::editor
