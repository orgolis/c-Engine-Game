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
    if (g_fail) {
        std::printf("terrainbrush_check: %d FAILED, %d passed\n", g_fail, g_pass);
        return 1;
    }
    std::printf("terrainbrush_check: ALL OK (%d checks)\n", g_pass);
    return 0;
}
