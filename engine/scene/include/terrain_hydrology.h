#pragma once
// ============================================================================
// terrain_hydrology -- where water goes when it is let out on a terrain.
//
// Water starts at a spring and runs down the steepest way. Where it reaches a
// hollow with no way down, the hollow fills: a lake, up to the lowest point
// of its rim, and the water goes on from there. It ends at the edge of the
// terrain, at the sea (the terrain's own water level), or when it has run out
// of steps.
//
// The result is a description (a river as points going downhill, lakes as a
// level and an extent). Two more functions turn it into something to see:
// carve_river() cuts the river's bed into the heightmap, and
// river_surfaces() lists flat water surfaces to place along it, each a little
// lower than the one before. A flat surface only shows where the ground is
// below it, so a surface in a carved bed shows as water in the bed, and a
// surface at a lake's level shows as the lake, whatever its outline is.
//
// All positions are LOCAL to the terrain; heights are world heights relative
// to the terrain's origin (stored height times height scale).
// Checked by tools/terrainbrush_check.
// ============================================================================

#include "terrain_component.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace schizo::scene {

struct WaterRun {
    struct Point {
        float x = 0.0f, z = 0.0f;   // local position
        float bed = 0.0f;           // ground height there
    };
    struct Lake {
        float level = 0.0f;                        // its surface
        float min_x = 0.0f, min_z = 0.0f, max_x = 0.0f, max_z = 0.0f;
        float deepest_x = 0.0f, deepest_z = 0.0f;  // where it is deepest
        int   cells = 0;
    };
    std::vector<Point> river;        // downhill order; a lake's stretch is skipped
    std::vector<size_t> breaks;      // indices into `river` where a new stretch starts (after a lake)
    std::vector<Lake>  lakes;
    bool reached_edge = false;
    bool reached_sea = false;
};

struct HydrologySettings {
    bool  has_sea = false;
    float sea_level = 0.0f;
    int   max_steps = 20000;
    float max_lake_fraction = 0.35f;   // a lake that would cover more of the terrain than this ends the run
};

/// Follow water from a spring at (lx, lz).
inline WaterRun trace_water(const TerrainComponent& tc, float lx, float lz, const HydrologySettings& hs = {}) {
    WaterRun    run;
    const int   res = tc.GetResolution(), n = tc.VertsPerSide();
    const float half = tc.GetSize() * 0.5f, cell = tc.CellSize(), scale = tc.GetHeightScale();
    const std::vector<float>& H = tc.Heights();
    // The ground as the water sees it: a lake it has filled is a flat surface
    // at the lake's level, so water that leaves a lake cannot run back into it.
    std::vector<float> eff(H.size());
    for (size_t i = 0; i < H.size(); ++i) eff[i] = H[i] * scale;
    auto h = [&](int x, int z) { return eff[static_cast<size_t>(z) * n + x]; };
    auto wx = [&](int x) { return -half + x * cell; };
    auto on_edge = [&](int x, int z) { return x <= 0 || z <= 0 || x >= res || z >= res; };

    int cx = std::clamp(static_cast<int>(std::lround((lx + half) / cell)), 0, res);
    int cz = std::clamp(static_cast<int>(std::lround((lz + half) / cell)), 0, res);
    std::vector<uint8_t> in_lake(H.size(), 0);
    run.river.push_back({wx(cx), wx(cz), h(cx, cz)});
    const int max_lake = std::max(8, static_cast<int>(hs.max_lake_fraction * static_cast<float>(H.size())));

    for (int step = 0; step < hs.max_steps; ++step) {
        if (hs.has_sea && h(cx, cz) <= hs.sea_level) {
            run.reached_sea = true;
            break;
        }
        if (on_edge(cx, cz)) {
            run.reached_edge = true;
            break;
        }
        // The steepest way down among the eight neighbours.
        int   bx = cx, bz = cz;
        float best = 0.0f;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dz) continue;
                const int   x = cx + dx, z = cz + dz;
                const float drop = (h(cx, cz) - h(x, z)) / ((dx && dz) ? 1.41421356f : 1.0f);
                if (drop > best) {
                    best = drop;
                    bx = x;
                    bz = z;
                }
            }
        if (best > 1e-6f) {
            cx = bx;
            cz = bz;
            run.river.push_back({wx(cx), wx(cz), h(cx, cz)});
            continue;
        }

        // A hollow. Fill it from its lowest point outward, always taking the
        // lowest ground on the shore next, until ground LOWER than the water
        // has been reached: that is over the rim, and the water runs on there.
        using Item = std::pair<float, int>;   // height, vertex index
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> shore;
        std::vector<uint8_t> seen(H.size(), 0);
        std::vector<int>     filled;
        const int start = cz * n + cx;
        shore.push({h(cx, cz), start});
        seen[static_cast<size_t>(start)] = 1;
        float level = h(cx, cz);
        int   out_x = -1, out_z = -1;
        bool  off_edge = false;
        while (!shore.empty()) {
            const Item it = shore.top();
            shore.pop();
            const int x = it.second % n, z = it.second / n;
            if (it.first < level - 1e-6f) {   // lower than the lake: the way out
                out_x = x;
                out_z = z;
                break;
            }
            level = it.first;
            filled.push_back(it.second);
            if (on_edge(x, z)) {              // the lake reaches the edge of the world and runs off it
                off_edge = true;
                break;
            }
            if (static_cast<int>(filled.size()) > max_lake) break;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, nz = z + dz;
                    if ((!dx && !dz) || nx < 0 || nz < 0 || nx > res || nz > res) continue;
                    const size_t ni = static_cast<size_t>(nz) * n + nx;
                    if (seen[ni]) continue;
                    seen[ni] = 1;
                    shore.push({h(nx, nz), static_cast<int>(ni)});
                }
        }
        WaterRun::Lake lake;
        lake.level = level;
        lake.min_x = lake.min_z = std::numeric_limits<float>::max();
        lake.max_x = lake.max_z = std::numeric_limits<float>::lowest();
        float deepest = std::numeric_limits<float>::max();
        for (int vi : filled) {
            const int x = vi % n, z = vi / n;
            in_lake[static_cast<size_t>(vi)] = 1;
            eff[static_cast<size_t>(vi)] = level;
            lake.min_x = std::min(lake.min_x, wx(x));
            lake.max_x = std::max(lake.max_x, wx(x));
            lake.min_z = std::min(lake.min_z, wx(z));
            lake.max_z = std::max(lake.max_z, wx(z));
            const float real = H[static_cast<size_t>(vi)] * scale;
            if (real < deepest) {
                deepest = real;
                lake.deepest_x = wx(x);
                lake.deepest_z = wx(z);
            }
        }
        lake.cells = static_cast<int>(filled.size());
        if (lake.cells > 1) run.lakes.push_back(lake);
        if (off_edge) {
            run.reached_edge = true;
            break;
        }
        if (out_x < 0) break;   // too big to fill, or nowhere to go
        cx = out_x;
        cz = out_z;
        run.breaks.push_back(run.river.size());
        run.river.push_back({wx(cx), wx(cz), h(cx, cz)});
    }
    return run;
}

/// Cut the river's bed into the terrain: along each stretch the bed is made to
/// go steadily downhill (never up), `depth` below the ground it ran over at
/// the middle and rising to the banks over `width`. Returns true when the
/// terrain changed.
inline bool carve_river(TerrainComponent& tc, WaterRun& run, float width, float depth) {
    if (run.river.size() < 2 || width <= 0.0f || depth <= 0.0f) return false;
    const int   res = tc.GetResolution(), n = tc.VertsPerSide();
    const float half = tc.GetSize() * 0.5f, cell = tc.CellSize();
    const float scale = tc.GetHeightScale() > 1e-3f ? tc.GetHeightScale() : 1.0f;
    std::vector<float>& H = tc.MutableHeights();
    bool changed = false;
    int  rx0 = res, rz0 = res, rx1 = 0, rz1 = 0;
    auto is_break = [&](size_t i) { return std::find(run.breaks.begin(), run.breaks.end(), i) != run.breaks.end(); };
    float bed = run.river[0].bed - depth;
    for (size_t i = 0; i < run.river.size(); ++i) {
        WaterRun::Point& p = run.river[i];
        // Downhill only: a bump in the ground does not become a dam.
        bed = is_break(i) ? p.bed - depth : std::min(bed, p.bed - depth);
        p.bed = bed;
        const int r = static_cast<int>(std::ceil(width / cell)) + 1;
        const int vx = static_cast<int>(std::lround((p.x + half) / cell)), vz = static_cast<int>(std::lround((p.z + half) / cell));
        for (int z = std::max(0, vz - r); z <= std::min(res, vz + r); ++z)
            for (int x = std::max(0, vx - r); x <= std::min(res, vx + r); ++x) {
                const float dx = (-half + x * cell) - p.x, dz = (-half + z * cell) - p.z;
                const float d = std::sqrt(dx * dx + dz * dz);
                if (d > width) continue;
                const float t = d / width;                       // 0 middle .. 1 bank
                const float want = (bed + depth * t * t) / scale; // a rounded bed
                float&      hh = H[static_cast<size_t>(z) * n + x];
                if (want < hh) {
                    hh = want;
                    changed = true;
                    rx0 = std::min(rx0, x); rz0 = std::min(rz0, z);
                    rx1 = std::max(rx1, x); rz1 = std::max(rz1, z);
                }
            }
    }
    if (changed) tc.MarkDirtyRect(rx0 - 1, rz0 - 1, rx1 + 1, rz1 + 1);
    return changed;
}

/// A flat water surface to place: centre (local x, z), level, and side length.
struct WaterSurface {
    float x = 0.0f, z = 0.0f, level = 0.0f;
    float size_x = 1.0f, size_z = 1.0f;
    bool  lake = false;
    int   stretch = 0;   // river surfaces: which stretch (0 before the first lake, 1 after it, ...)
};

/// The surfaces that show a run as water: one per lake, at its level and over
/// its extent, and a chain along the river, each lower than the last. `fill`
/// is how deep the water stands in the bed. A new surface is started whenever
/// the bed has dropped by `max_drop`, so the chain steps down in small falls
/// on a slope and runs long and level where the ground is flat.
inline std::vector<WaterSurface> river_surfaces(const WaterRun& run, float width, float fill, float max_drop = 0.25f,
                                                size_t max_count = 600, int min_lake_cells = 8) {
    std::vector<WaterSurface> out;
    for (const WaterRun::Lake& l : run.lakes) {
        if (l.cells < min_lake_cells) continue;   // a puddle: the river's own water covers it
        WaterSurface s;
        s.lake = true;
        s.level = l.level;
        s.x = (l.min_x + l.max_x) * 0.5f;
        s.z = (l.min_z + l.max_z) * 0.5f;
        s.size_x = (l.max_x - l.min_x) + width * 2.0f;
        s.size_z = (l.max_z - l.min_z) + width * 2.0f;
        out.push_back(s);
    }
    if (run.river.empty()) return out;
    size_t from = 0;
    int    stretch = 0;
    for (size_t i = 1; i <= run.river.size() && out.size() < max_count; ++i) {
        const bool end = i == run.river.size();
        const bool gap = !end && std::find(run.breaks.begin(), run.breaks.end(), i) != run.breaks.end();
        if (!end && !gap && run.river[from].bed - run.river[i].bed < max_drop) continue;
        // One surface over river[from .. i): level with the water at its lower end.
        const size_t last = i - 1;
        float min_x = run.river[from].x, max_x = min_x, min_z = run.river[from].z, max_z = min_z;
        for (size_t k = from; k <= last; ++k) {
            min_x = std::min(min_x, run.river[k].x); max_x = std::max(max_x, run.river[k].x);
            min_z = std::min(min_z, run.river[k].z); max_z = std::max(max_z, run.river[k].z);
        }
        WaterSurface s;
        s.level = run.river[last].bed + fill;
        s.stretch = stretch;
        s.x = (min_x + max_x) * 0.5f;
        s.z = (min_z + max_z) * 0.5f;
        s.size_x = (max_x - min_x) + width * 2.0f;
        s.size_z = (max_z - min_z) + width * 2.0f;
        out.push_back(s);
        from = gap ? i : last;   // the next one starts where this one ended, so they overlap
        if (gap) ++stretch;
    }
    return out;
}

}  // namespace schizo::scene
