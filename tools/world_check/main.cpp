// ====================
// world_check — headless verification of world streaming (World-streaming pillar)
// ====================
//
//   1. WorldGrid: pos↔cell, nearest-point distance, radius query.
//   2. StreamingManager: loads a block around the focus, incremental load/unload
//      as the focus moves, radius HYSTERESIS (band cells persist; only cells
//      beyond the unload radius drop), load→loaded state, callbacks.
//   3. FloatingOrigin: no rebase under threshold; rebase past it preserves the
//      absolute world position exactly and re-centres the focus near origin.
//
// Pure CPU. Prints "world_check: ALL OK" + exits 0 on pass.

#include "world/world_grid.h"
#include "world/streaming_manager.h"
#include "world/floating_origin.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace schizo::world;

namespace {
int g_checks = 0;
bool check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    return ok;
}
// Message FIRST so the (variadic) condition may contain brace-init commas.
#define REQUIRE(msg, ...) do { if (!check((__VA_ARGS__), (msg))) { \
    std::printf("world_check: FAILED at '%s'\n", (msg)); return 1; } } while (0)
bool near(double a, double b, double e = 1e-4) { return std::abs(a - b) < e; }
bool has(const std::vector<CellCoord>& v, CellCoord c) {
    for (auto& e : v) if (e == c) return true; return false;
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== world_check: world streaming ===\n");

    // ---- 1. Grid math -----------------------------------------------------
    {
        WorldGrid g(64.0f);
        REQUIRE("pos in cell (0,0)", g.to_cell({10, 0, 10}) == CellCoord{0, 0});
        REQUIRE("negative + next cell", g.to_cell({-1, 0, 70}) == CellCoord{-1, 1});
        REQUIRE("focus inside cell → 0", near(g.nearest_dist({0, 0, 0}, CellCoord{0, 0}), 0.0));
        REQUIRE("nearest-point distance to an adjacent cell",
                near(g.nearest_dist({128, 0, 0}, CellCoord{0, 0}), 64.0));
        std::vector<CellCoord> cells;
        g.cells_in_radius({32, 0, 32}, 10.0f, cells);
        REQUIRE("small radius at cell centre → just that cell",
                cells.size() == 1 && cells[0] == CellCoord{0, 0});
    }

    // ---- 2. Streaming with hysteresis -------------------------------------
    {
        int loads = 0, unloads = 0;
        StreamingManager sm(WorldGrid(64.0f), /*load*/150.0f, /*unload*/280.0f);
        sm.set_callbacks([&](const CellCoord&) { ++loads; },
                         [&](const CellCoord&) { ++unloads; });

        sm.update({0, 0, 0});
        const size_t n0 = sm.active_count();
        REQUIRE("initial update loads a block + fires callbacks",
                n0 > 0 && loads == static_cast<int>(n0));
        REQUIRE("cells start in Loading", sm.state({0, 0}) == CellState::Loading);
        // Complete every load the manager actually requested this update.
        const std::vector<CellCoord> requested = sm.last_loads();
        for (const CellCoord& c : requested) sm.mark_loaded(c);
        REQUIRE("mark_loaded transitions Loading → Loaded", sm.loaded_count() == n0);
        REQUIRE("far-but-in-load-radius cell is loaded", sm.state({-3, 0}) == CellState::Loaded);

        loads = unloads = 0;
        sm.update({200, 0, 0});
        REQUIRE("cell beyond unload radius is dropped",
                sm.state({-3, 0}) == CellState::Unloaded && has(sm.last_unloads(), {-3, 0}));
        REQUIRE("band cell (load<d<unload) STAYS loaded — hysteresis, no thrash",
                sm.state({-2, 0}) == CellState::Loaded && !has(sm.last_unloads(), {-2, 0}));
        REQUIRE("new cell within load radius of the moved focus is loaded",
                sm.state({3, 0}) != CellState::Unloaded && has(sm.last_loads(), {3, 0}));
        REQUIRE("the move both loads and unloads", unloads > 0 && loads > 0);

        loads = unloads = 0;
        sm.update({205, 0, 0});
        REQUIRE("sub-cell nudge causes no load/unload churn", loads == 0 && unloads == 0);
    }

    // ---- 3. Floating origin -----------------------------------------------
    {
        FloatingOrigin fo(/*threshold*/1024.0, /*snap*/64.0);
        glm::vec3 shift;
        REQUIRE("no rebase under the threshold", !fo.update({500, 0, 300}, shift));
        REQUIRE("no shift under threshold", shift == glm::vec3(0));

        const glm::vec3 focus_local(1500.0f, 0.0f, 0.0f);
        const glm::vec3 entity_local(1600.0f, 5.0f, 20.0f);
        const glm::dvec3 focus_world_before  = fo.to_world(focus_local);
        const glm::dvec3 entity_world_before = fo.to_world(entity_local);

        REQUIRE("rebase fires past the threshold", fo.update(focus_local, shift));
        const glm::vec3 focus_after  = focus_local + shift;
        const glm::vec3 entity_after = entity_local + shift;
        REQUIRE("focus re-centred near local origin after rebase",
                glm::length(focus_after) < 1024.0f);

        const glm::dvec3 focus_world_after  = fo.to_world(focus_after);
        const glm::dvec3 entity_world_after = fo.to_world(entity_after);
        REQUIRE("focus ABSOLUTE world position preserved across rebase",
                near(focus_world_after.x, focus_world_before.x) &&
                near(focus_world_after.z, focus_world_before.z));
        REQUIRE("entity ABSOLUTE world position preserved across rebase",
                near(entity_world_after.x, entity_world_before.x) &&
                near(entity_world_after.y, entity_world_before.y) &&
                near(entity_world_after.z, entity_world_before.z));
        REQUIRE("rebase shift snapped to the cell grid",
                near(std::fmod(shift.x, 64.0f), 0.0f));
    }

    std::printf("world_check: ALL OK (%d checks)\n", g_checks);
    return 0;
}
