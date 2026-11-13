// ====================
// terrainbrush_check -- what each sculpting brush does to a heightmap.
//
// The brushes used to live inside the editor's frame loop, where the only test
// was a person dragging the mouse. They are plain functions now
// (scene/terrain_brush.h); this asserts the properties a person relies on:
// a brush stays inside its radius, Flatten and Terrace arrive at their target
// instead of overshooting, erosion moves material without creating or
// destroying it, a ramp is straight, and a heightmap written to a file reads
// back as the same terrain.
//
// Pure CPU. Prints "terrainbrush_check: ALL OK (N checks)" and exits 0.
// ====================

#include "terrain_brush.h"
#include "terrain_hydrology.h"
#include "foliage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace schizo::scene;

namespace {
int g_pass = 0, g_fail = 0;
void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what.c_str());
    if (ok) ++g_pass; else ++g_fail;
}
double total(const TerrainComponent& t) {
    double s = 0.0;
    for (float h : t.Heights()) s += h;
    return s;
}
float max_slope(const TerrainComponent& t) {
    float m = 0.0f;
    const int r = t.GetResolution();
    for (int z = 0; z <= r; ++z)
        for (int x = 0; x < r; ++x) {
            m = std::max(m, std::fabs(t.HeightAt(x + 1, z) - t.HeightAt(x, z)) / t.CellSize());
            m = std::max(m, std::fabs(t.HeightAt(z, x + 1) - t.HeightAt(z, x)) / t.CellSize());
        }
    return m;
}
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== terrainbrush_check: sculpting brushes ===\n");
    {
        TerrainComponent t(64, 64.0f);   // 1 unit cells, centre at (0, 0) = vertex (32, 32)
        int x0, z0, x1, z1;
        t.ConsumeDirtyRect(x0, z0, x1, z1);
        TerrainStroke s;
        s.brush = TerrainBrush::Raise;
        s.radius = 5.0f;
        s.strength = 1.0f;
        const bool changed = apply_terrain_brush(t, s);
        check(changed && t.HeightAt(32, 32) > 0.1f && t.HeightAt(32, 32) > t.HeightAt(35, 32) && t.HeightAt(35, 32) > 0.0f,
              "Raise lifts most at the centre and less toward the edge");
        check(t.HeightAt(38, 32) == 0.0f && t.HeightAt(32, 26) == 0.0f, "nothing outside the radius moves");
        const bool rect = t.ConsumeDirtyRect(x0, z0, x1, z1);
        check(rect && x0 <= 27 && x1 >= 37 && x0 > 10 && x1 < 54, "the brush reports the cells it touched, not the whole terrain");
        const float top = t.HeightAt(32, 32);
        s.brush = TerrainBrush::Lower;
        apply_terrain_brush(t, s);
        check(std::fabs(t.HeightAt(32, 32)) < 1e-5f && top > 0.0f, "Lower undoes Raise exactly");
    }
    {
        TerrainComponent t(64, 64.0f);
        for (int z = 0; z <= 64; ++z)
            for (int x = 0; x <= 64; ++x) t.SetHeightAt(x, z, static_cast<float>((x * 7 + z * 13) % 5));
        TerrainStroke s;
        s.brush = TerrainBrush::Flatten;
        s.radius = 8.0f;
        s.strength = 5.0f;
        s.falloff = 0.0f;
        s.target = 2.5f;
        for (int i = 0; i < 400; ++i) apply_terrain_brush(t, s);
        bool flat = true, overshoot = false;
        for (int z = 29; z <= 35; ++z)
            for (int x = 29; x <= 35; ++x) {
                flat = flat && std::fabs(t.HeightAt(x, z) - 2.5f) < 0.02f;
            }
        s.dt = 10.0f;   // a frame that took ten seconds must not throw the surface past the target
        t.SetHeightAt(32, 32, 0.0f);
        apply_terrain_brush(t, s);
        overshoot = t.HeightAt(32, 32) > 2.5f + 1e-4f;
        check(flat, "Flatten arrives at the target height");
        check(!overshoot, "and never goes past it, however long the frame was");
    }
    {
        TerrainComponent t(64, 64.0f);
        for (int z = 0; z <= 64; ++z)
            for (int x = 0; x <= 64; ++x) t.SetHeightAt(x, z, x * 0.37f);
        TerrainStroke s;
        s.brush = TerrainBrush::Terrace;
        s.radius = 10.0f;
        s.strength = 5.0f;
        s.falloff = 0.0f;
        s.step = 2.0f;
        for (int i = 0; i < 600; ++i) apply_terrain_brush(t, s);
        bool stepped = true;
        for (int x = 27; x <= 37; ++x) {
            const float h = t.HeightAt(x, 32);
            stepped = stepped && std::fabs(h - std::round(h / 2.0f) * 2.0f) < 0.05f;
        }
        check(stepped, "Terrace pulls a slope onto steps of the chosen height");
    }
    {
        TerrainComponent t(64, 64.0f);
        TerrainStroke s;
        s.brush = TerrainBrush::Noise;
        s.radius = 12.0f;
        s.strength = 1.0f;
        s.noise_scale = 3.0f;
        apply_terrain_brush(t, s);
        float lo = 0.0f, hi = 0.0f;
        for (float h : t.Heights()) { lo = std::min(lo, h); hi = std::max(hi, h); }
        TerrainComponent u(64, 64.0f);
        apply_terrain_brush(u, s);
        check(lo < -0.01f && hi > 0.01f, "Noise pushes some of the surface up and some down");
        check(t.Heights() == u.Heights(), "the same seed gives the same bumps");
    }
    {
        // A ramp from a low start to a plateau.
        TerrainComponent t(64, 64.0f);
        for (int z = 0; z <= 64; ++z)
            for (int x = 40; x <= 64; ++x) t.SetHeightAt(x, z, 10.0f);
        TerrainStroke s;
        s.brush = TerrainBrush::Ramp;
        s.radius = 3.0f;
        s.strength = 5.0f;
        s.falloff = 0.0f;
        s.start_x = -20.0f; s.start_z = 0.0f;   // vertex (12, 32), height 0
        s.target = 0.0f;
        s.x = 12.0f; s.z = 0.0f;                // vertex (44, 32), height 10
        for (int i = 0; i < 600; ++i) apply_terrain_brush(t, s);
        bool straight = true;
        for (int x = 13; x <= 43; ++x) {
            const float want = 10.0f * static_cast<float>(x - 12) / 32.0f;
            straight = straight && std::fabs(t.HeightAt(x, 32) - want) < 0.15f;
        }
        check(straight, "Ramp makes a straight slope between the start of the stroke and the brush");
        check(t.HeightAt(28, 40) == 0.0f, "and leaves what is beside the ramp alone");
    }
    {
        // A cliff relaxes to the talus slope, and no material is made or lost.
        TerrainComponent t(64, 64.0f);
        for (int z = 0; z <= 64; ++z)
            for (int x = 32; x <= 64; ++x) t.SetHeightAt(x, z, 12.0f);
        const double before = total(t);
        const float  steep = max_slope(t);
        erode_terrain(t, 400, 0.8f);
        const double after = total(t);
        check(steep > 10.0f && max_slope(t) < 0.9f, "erosion wears a cliff down to the slope that holds");
        check(std::fabs(after - before) < 1e-2 * std::fabs(before), "and moves material without creating or destroying it");

        TerrainComponent b(64, 64.0f);
        for (int z = 0; z <= 64; ++z)
            for (int x = 32; x <= 64; ++x) b.SetHeightAt(x, z, 12.0f);
        TerrainStroke s;
        s.brush = TerrainBrush::Erode;
        s.radius = 6.0f;
        s.strength = 2.0f;
        const double bb = total(b);
        for (int i = 0; i < 200; ++i) apply_terrain_brush(b, s);
        check(std::fabs(b.HeightAt(32, 32) - b.HeightAt(31, 32)) < 6.0f && b.HeightAt(31, 60) == 0.0f,
              "the Erode brush softens the cliff under it and not elsewhere");
        check(std::fabs(total(b) - bb) < 1e-2 * std::fabs(bb), "the Erode brush conserves material too");
    }
    {
        TerrainComponent t(32, 32.0f);
        TerrainStroke s;
        s.brush = TerrainBrush::DigHole;
        s.radius = 2.0f;
        const bool dug = apply_terrain_brush(t, s);
        const bool hole = t.HasHole(16, 16);
        s.brush = TerrainBrush::FillHole;
        apply_terrain_brush(t, s);
        check(dug && hole && !t.HasHole(16, 16) && !t.AnyHoles(), "Dig Hole carves cells and Fill Hole puts them back");
    }
    {
        TerrainComponent t(128, 256.0f);
        generate_terrain_noise(t, 40.0f, 60.0f, 7u);
        float lo = 1e9f, hi = -1e9f;
        for (float h : t.Heights()) { lo = std::min(lo, h); hi = std::max(hi, h); }
        check(lo >= 0.0f && hi <= 40.0f && hi - lo > 10.0f, "generated hills stay inside the asked height and use a good part of it");

        // To a file and back, at the same and at another resolution.
        for (const char* path : {"terrainbrush_check.pgm", "terrainbrush_check.r16"}) {
            float flo = 0.0f, fhi = 0.0f;
            const bool wrote = save_terrain_heightmap(t, path, &flo, &fhi);
            TerrainComponent back(128, 256.0f);
            std::string why;
            const bool read = load_terrain_heightmap(back, path, flo, fhi, &why);
            float worst = 0.0f;
            for (size_t i = 0; i < t.Heights().size(); ++i) worst = std::max(worst, std::fabs(t.Heights()[i] - back.Heights()[i]));
            check(wrote && read && worst < (fhi - flo) / 30000.0f, std::string("a heightmap survives ") + path + " to within one 16-bit step");
            TerrainComponent half(64, 256.0f);
            const bool read2 = load_terrain_heightmap(half, path, flo, fhi);
            check(read2 && std::fabs(half.HeightAt(32, 32) - t.HeightAt(64, 64)) < 0.01f, "and is resampled into a terrain of another resolution");
            std::remove(path);
        }
        TerrainComponent bad(16, 16.0f);
        std::string why;
        check(!load_terrain_heightmap(bad, "no_such_file.r16", 0.0f, 1.0f, &why) && !why.empty(), "a file that is not there is refused with a reason");
    }
    {
        // Layers by rule: a mountain with grass low, rock on the steep flank, snow on top.
        TerrainComponent t(128, 256.0f);
        for (int z = 0; z <= 128; ++z)
            for (int x = 0; x <= 128; ++x) {
                const float d = std::sqrt(static_cast<float>((x - 64) * (x - 64) + (z - 64) * (z - 64))) * 2.0f;   // world units from the peak
                t.SetHeightAt(x, z, d < 20.0f ? 80.0f : std::max(0.0f, 80.0f - (d - 20.0f) * 2.0f));           // plateau, 63 degree flank, plain
            }
        TerrainLayerRule rock;
        rock.enabled = true;
        rock.min_slope = 35.0f;
        TerrainLayerRule snow;
        snow.enabled = true;
        snow.min_height = 70.0f;
        snow.max_slope = 30.0f;
        snow.slope_blend = 2.0f;
        t.SetLayerRule(1, rock);
        t.SetLayerRule(2, snow);
        const uint64_t v0 = t.SplatVersion();
        apply_terrain_layer_rules(t);
        const int res = t.SplatResolution();
        auto px = [&](float lx, float lz) {
            const int x = static_cast<int>((lx + 128.0f) / 256.0f * res), z = static_cast<int>((lz + 128.0f) / 256.0f * res);
            return &t.Splat()[(static_cast<size_t>(z) * res + x) * 4];
        };
        const uint8_t* plain = px(110.0f, 0.0f);
        const uint8_t* flank = px(40.0f, 0.0f);
        const uint8_t* peak = px(2.0f, 2.0f);
        check(plain[0] == 255 && plain[1] == 0 && plain[2] == 0, "flat low ground is left to the base layer");
        check(flank[1] > 200 && flank[2] < 30, "the steep flank is claimed by the slope rule");
        check(peak[2] > 200 && peak[1] < 30, "the flat top is claimed by the height rule");
        bool whole = true;
        for (size_t i = 0; i < t.Splat().size(); i += 4)
            whole = whole && (t.Splat()[i] + t.Splat()[i + 1] + t.Splat()[i + 2] + t.Splat()[i + 3]) == 255;
        check(whole, "every texel's weights add up to exactly one whole");
        check(t.SplatVersion() != v0, "applying rules tells the renderer the splatmap changed");

        rock.noise = 1.0f;
        rock.noise_scale = 6.0f;
        t.SetLayerRule(1, rock);
        apply_terrain_layer_rules(t);
        int lo = 255, hi = 0;
        for (float a = 0.0f; a < 6.2f; a += 0.05f) {
            const uint8_t* p = px(std::cos(a) * 40.0f, std::sin(a) * 40.0f);
            lo = std::min<int>(lo, p[1]);
            hi = std::max<int>(hi, p[1]);
        }
        check(hi - lo > 80, "noise breaks a rule's layer into patches instead of a solid band");
        check(std::fabs(terrain_slope_degrees(t, 40.0f, 0.0f) - 63.4f) < 3.0f && terrain_slope_degrees(t, 110.0f, 0.0f) < 1.0f,
              "the slope the rules see is the slope of the ground");
    }
    {
        // A valley tilted toward +x with a basin halfway down: water from the
        // top runs down, fills the basin to its rim, spills, and leaves at the edge.
        TerrainComponent t(128, 128.0f);   // 1 unit cells, local -64..64
        for (int z = 0; z <= 128; ++z)
            for (int x = 0; x <= 128; ++x) {
                const float lx = x - 64.0f, lz = z - 64.0f;
                float h = 40.0f - lx * 0.25f + std::fabs(lz) * 0.5f;   // downhill toward +x, a V across z
                const float bd = std::sqrt((lx - 0.0f) * (lx - 0.0f) + lz * lz);
                if (bd < 10.0f) h -= (10.0f - bd) * 0.8f;              // a bowl at the middle, 8 deep
                t.SetHeightAt(x, z, h);
            }
        const WaterRun run = trace_water(t, -60.0f, 0.0f);
        bool downhill = true;
        for (size_t i = 1; i < run.river.size(); ++i) {
            const bool after_lake = std::find(run.breaks.begin(), run.breaks.end(), i) != run.breaks.end();
            if (!after_lake && run.river[i].bed > run.river[i - 1].bed + 1e-4f) downhill = false;
        }
        check(downhill && run.river.size() > 20, "water runs downhill, never up, between lakes");
        check(run.lakes.size() == 1 && std::fabs(run.lakes[0].deepest_x) < 2.0f && std::fabs(run.lakes[0].deepest_z) < 2.0f,
              "it fills the basin it reaches into a lake, deepest at the bowl's centre");
        const float rim = 40.0f - 10.0f * 0.25f;   // the ground just past the bowl on the downhill side
        check(!run.lakes.empty() && run.lakes[0].level > rim - 1.5f && run.lakes[0].level < rim + 1.0f,
              "the lake rises to the lowest point of its rim, no higher");
        check(run.reached_edge && !run.reached_sea && run.river.back().x > 60.0f, "then it spills over and runs on to the edge of the land");

        HydrologySettings sea;
        sea.has_sea = true;
        sea.sea_level = 30.0f;
        const WaterRun to_sea = trace_water(t, -60.0f, 0.0f, sea);
        check(to_sea.reached_sea && to_sea.river.back().bed <= 30.0f + 1e-4f && to_sea.river.back().x < 60.0f,
              "with a sea it stops where the ground goes below the sea level");

        WaterRun cut = run;
        TerrainComponent& tt = t;
        const float before = tt.SampleHeightLocal(30.0f, 0.0f);
        const bool carved = carve_river(tt, cut, 3.0f, 1.5f);
        bool bed_down = true;
        for (size_t i = 1; i < cut.river.size(); ++i) {
            const bool after_lake = std::find(cut.breaks.begin(), cut.breaks.end(), i) != cut.breaks.end();
            if (!after_lake && cut.river[i].bed > cut.river[i - 1].bed + 1e-4f) bed_down = false;
        }
        check(carved && tt.SampleHeightLocal(30.0f, 0.0f) < before - 1.0f && tt.SampleHeightLocal(30.0f, 20.0f) == t.SampleHeightLocal(30.0f, 20.0f),
              "carving cuts a bed along the river and leaves the land beside it");
        check(bed_down, "the carved bed only ever goes down");

        const std::vector<WaterSurface> surf = river_surfaces(cut, 3.0f, 1.0f, 0.5f);
        int lakes = 0;
        bool stepping = true, outflow_below_lake = true;
        float prev = 1e9f;
        int   stretch = -1;
        for (const WaterSurface& s : surf) {
            if (s.lake) { ++lakes; continue; }
            if (s.stretch != stretch) { stretch = s.stretch; prev = 1e9f; }
            if (s.level > prev + 1e-4f) stepping = false;
            if (s.stretch == 1 && !cut.lakes.empty() && s.level > cut.lakes[0].level + 1e-3f) outflow_below_lake = false;
            prev = s.level;
        }
        check(lakes == 1 && surf.size() > 5 && stepping, "the water to show is the lake and a chain along each stretch of river, each lower than the one before");
        check(outflow_below_lake, "the river leaving the lake stands no higher than the lake");
    }
    {
        // A closed bowl with no way out: the run ends in a lake and does not loop.
        TerrainComponent t(64, 64.0f);
        for (int z = 0; z <= 64; ++z)
            for (int x = 0; x <= 64; ++x) {
                const float d = std::sqrt(static_cast<float>((x - 32) * (x - 32) + (z - 32) * (z - 32)));
                t.SetHeightAt(x, z, d * 0.5f);
            }
        const WaterRun run = trace_water(t, 20.0f, 0.0f);
        check(!run.lakes.empty() && !run.reached_edge && run.river.size() < 200,
              "water in a bowl whose rim is the edge of the world fills toward it and stops");
    }
    {
        // Real, bumpy ground: water from the highest point finds its way off
        // the land without ever going back into a lake it left.
        for (uint32_t seed : {3u, 7u, 11u}) {
            TerrainComponent t(128, 256.0f);
            generate_terrain_noise(t, 35.0f, 64.0f, seed);
            erode_terrain(t, 40, 0.9f);
            size_t top = 0;
            for (size_t i = 0; i < t.Heights().size(); ++i)
                if (t.Heights()[i] > t.Heights()[top]) top = i;
            const float sx = -128.0f + static_cast<float>(top % 129) * 2.0f, sz = -128.0f + static_cast<float>(top / 129) * 2.0f;
            const WaterRun run = trace_water(t, sx, sz);
            check(run.reached_edge && run.river.size() < 2000,
                  "seed " + std::to_string(seed) + ": water from the summit reaches the edge in " + std::to_string(run.river.size()) +
                      " steps through " + std::to_string(run.lakes.size()) + " hollows");
        }
    }
    {
        // Foliage: how many, where, the same every time, and in culling cells.
        TerrainComponent t(128, 256.0f);   // 65,536 m^2
        FoliageLayer trees;
        trees.density = 2.0f;              // per 100 m^2 -> about 1,310
        const FoliageScatter s = scatter_foliage(t, trees);
        check(s.count > 1200 && s.count < 1420, "a flat terrain gets the asked density: " + std::to_string(s.count) + " trees for ~1,310");
        const FoliageScatter again = scatter_foliage(t, trees);
        bool same = again.count == s.count;
        for (size_t c = 0; same && c < s.cells.size(); ++c)
            for (size_t i = 0; same && i < s.cells[c].items.size(); ++i)
                same = s.cells[c].items[i].pos == again.cells[c].items[i].pos && s.cells[c].items[i].yaw == again.cells[c].items[i].yaw;
        check(same, "the same settings place the same trees in the same places");
        FoliageLayer other = trees;
        other.seed = 2;
        const FoliageScatter s2 = scatter_foliage(t, other);
        check(s2.count > 1200 && !s2.cells.empty() && !s.cells[0].items.empty() && !s2.cells[0].items.empty() &&
                  s2.cells[0].items[0].pos != s.cells[0].items[0].pos,
              "another seed places them elsewhere");
        bool inside = true, boxed = true;
        int  cell_count = 0;
        for (const FoliageCell& c : s.cells) {
            if (!c.items.empty()) ++cell_count;
            for (const FoliageInstance& in : c.items) {
                inside = inside && std::fabs(in.pos.x) <= 128.0f && std::fabs(in.pos.z) <= 128.0f;
                boxed = boxed && in.pos.x >= c.min.x && in.pos.x <= c.max.x && in.pos.z >= c.min.z && in.pos.z <= c.max.z &&
                        in.pos.y >= c.min.y - 1e-3f && in.pos.y + trees.size * in.scale <= c.max.y + 1e-3f;
            }
        }
        check(inside && boxed && cell_count == 64, "every tree is on the terrain and inside its culling cell's box (8 x 8 cells)");

        // Where: a rule, a texture layer, a painted mask, a hole.
        for (int z = 0; z <= 128; ++z)
            for (int x = 0; x <= 128; ++x) t.SetHeightAt(x, z, x * 0.5f);   // height 0 at the west edge, 64 at the east
        FoliageLayer low = trees;
        low.rule.enabled = true;
        low.rule.max_height = 20.0f;
        low.rule.height_blend = 0.0f;
        const FoliageScatter sl = scatter_foliage(t, low);
        bool all_low = sl.count > 100;
        for (const FoliageCell& c : sl.cells)
            for (const FoliageInstance& in : c.items) all_low = all_low && in.pos.y + low.sink * low.size * in.scale <= 20.0f + 1e-3f;
        check(all_low, "a rule keeps trees below the tree line");

        FoliageLayer on_paint = trees;
        on_paint.on_texture_layer = 1;
        t.PaintSplatLocal(0.0f, 0.0f, 30.0f, 1, 1.0f, 0.0f);
        const FoliageScatter sp = scatter_foliage(t, on_paint);
        bool near = sp.count > 10;
        for (const FoliageCell& c : sp.cells)
            for (const FoliageInstance& in : c.items) near = near && std::sqrt(in.pos.x * in.pos.x + in.pos.z * in.pos.z) < 31.0f;
        check(near, "grass told to grow on a texture layer grows only where that layer is painted");

        FoliageLayer cleared = trees;
        for (int i = 0; i < 10; ++i) paint_foliage_mask(t, cleared, 0.0f, 0.0f, 40.0f, -1.0f, 0.0f);   // the brush held a moment
        const FoliageScatter sc = scatter_foliage(t, cleared);
        bool clearing = sc.count > 1000;
        for (const FoliageCell& c : sc.cells)
            for (const FoliageInstance& in : c.items) clearing = clearing && std::sqrt(in.pos.x * in.pos.x + in.pos.z * in.pos.z) > 37.0f;
        check(clearing, "a clearing painted into the mask has no trees, and the rest of the forest stays");

        t.SetHole(64, 64, true);
        const FoliageScatter sh = scatter_foliage(t, trees);
        bool none_in_hole = true;
        for (const FoliageCell& c : sh.cells)
            for (const FoliageInstance& in : c.items)
                none_in_hole = none_in_hole && !(in.pos.x >= 0.0f && in.pos.x < 2.0f && in.pos.z >= 0.0f && in.pos.z < 2.0f);
        check(none_in_hole, "nothing grows in a hole in the terrain");

        // Standing on the ground, and the cache follows the terrain.
        FoliageComponent fc;
        fc.MutableLayers().push_back(trees);
        const FoliageInstance first = fc.Scatter(t)[0].cells[0].items[0];
        const float ground = t.SampleHeightLocal(first.pos.x, first.pos.z);
        check(first.pos.y < ground && first.pos.y > ground - trees.size, "a tree's base is at the ground, sunk in a little");
        const uint64_t v1 = fc.ScatterVersion();
        fc.Scatter(t);
        const bool cached = fc.ScatterVersion() == v1;
        t.Flatten(10.0f);
        const float moved = fc.Scatter(t)[0].cells[0].items[0].pos.y;
        check(cached && fc.ScatterVersion() == v1 + 1 && moved > 9.0f, "the scatter is kept until the terrain changes, then follows it");

        const glm::mat4 m = foliage_matrix(first, trees.size);
        check(m == first.local, "the matrix kept with the instance is the one foliage_matrix gives");
        const glm::vec3 tip = glm::vec3(m * glm::vec4(0, 1, 0, 1));
        check(std::fabs(tip.y - first.pos.y - trees.size * first.scale) < 1e-3f && std::fabs(tip.x - first.pos.x) < 1e-3f,
              "an upright tree's matrix stands it on its base, as tall as its size times its scale");
    }
    if (g_fail) {
        std::printf("terrainbrush_check: %d FAILED, %d passed\n", g_fail, g_pass);
        return 1;
    }
    std::printf("terrainbrush_check: ALL OK (%d checks)\n", g_pass);
    return 0;
}
