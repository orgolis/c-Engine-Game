#pragma once
// ============================================================================
// terrain_brush -- what a sculpting brush does to a heightmap.
//
// The editor decides WHERE (the mouse ray on the terrain) and WHEN (while the
// button is held); everything about what happens to the heights is here, as
// plain functions on a TerrainComponent, so that it can be checked without a
// window: tools/terrainbrush_check.
//
// All positions are LOCAL to the terrain (world units from its centre), all
// heights are stored heights (before height_scale).
// ============================================================================

#include "terrain_component.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace schizo::scene {

enum class TerrainBrush : int {
    Raise = 0,
    Lower = 1,
    Smooth = 2,
    Flatten = 3,   // toward the height under the brush centre when the stroke began
    DigHole = 4,
    FillHole = 5,
    Noise = 6,     // rough the surface up: rock, dunes
    Terrace = 7,   // pull heights toward steps: rice fields, mesas
    Ramp = 8,      // a straight slope from where the stroke began to the brush
    Erode = 9,     // material slides off slopes steeper than the talus angle
};

inline const char* const kTerrainBrushNames[] = {"Raise",    "Lower",     "Smooth", "Flatten", "Dig Hole",
                                                 "Fill Hole", "Noise",     "Terrace", "Ramp",    "Erode"};
inline constexpr int kTerrainBrushCount = 10;

struct TerrainStroke {
    TerrainBrush brush = TerrainBrush::Raise;
    float x = 0.0f, z = 0.0f;     // brush centre
    float radius = 6.0f;
    float strength = 0.5f;
    float falloff = 0.6f;         // 0 hard edge .. 1 soft
    float dt = 0.016f;            // seconds this application stands for
    float target = 0.0f;          // Flatten: the height to go to. Ramp: the height at the start point
    float start_x = 0.0f, start_z = 0.0f;   // Ramp: where the stroke began
    float step = 2.0f;            // Terrace: height of one step
    float noise_scale = 4.0f;     // Noise: size of a bump, world units
    float talus = 0.8f;           // Erode: the steepest slope that holds (rise over run)
    uint32_t seed = 1;            // Noise: which bumps
};

namespace terrain_detail {

inline float hash2(int x, int z, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);   // 0..1
}

/// Smooth value noise in -1..1.
inline float value_noise(float x, float z, uint32_t seed) {
    const float fx = std::floor(x), fz = std::floor(z);
    const int   ix = static_cast<int>(fx), iz = static_cast<int>(fz);
    float       tx = x - fx, tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    const float a = hash2(ix, iz, seed), b = hash2(ix + 1, iz, seed);
    const float c = hash2(ix, iz + 1, seed), d = hash2(ix + 1, iz + 1, seed);
    return ((a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz) * 2.0f - 1.0f;
}

/// Three octaves, -1..1.
inline float fbm(float x, float z, uint32_t seed) {
    return (value_noise(x, z, seed) + 0.5f * value_noise(x * 2.03f, z * 2.03f, seed + 17u) +
            0.25f * value_noise(x * 4.01f, z * 4.01f, seed + 43u)) / 1.75f;
}

}  // namespace terrain_detail

/// Apply one brush application. Returns true when anything changed; the
/// touched cell rect has then been reported to the terrain (MarkDirtyRect).
inline bool apply_terrain_brush(TerrainComponent& tc, const TerrainStroke& s) {
    const int   res = tc.GetResolution();
    const int   n = tc.VertsPerSide();
    const float half = tc.GetSize() * 0.5f;
    const float cell = tc.CellSize();
    const float R = s.radius;
    if (R <= 0.0f || cell <= 0.0f) return false;
    const int cx = static_cast<int>(std::floor((s.x + half) / cell));
    const int cz = static_cast<int>(std::floor((s.z + half) / cell));
    const int rc = static_cast<int>(std::ceil(R / cell)) + 1;
    bool      changed = false;

    if (s.brush == TerrainBrush::DigHole || s.brush == TerrainBrush::FillHole) {
        // Holes are per CELL: carve the mesh and the collision, or put it back.
        const bool dig = s.brush == TerrainBrush::DigHole;
        for (int iz = std::max(0, cz - rc); iz < std::min(res, cz + rc + 1); ++iz)
            for (int ix = std::max(0, cx - rc); ix < std::min(res, cx + rc + 1); ++ix) {
                const float ccx = -half + (ix + 0.5f) * cell, ccz = -half + (iz + 0.5f) * cell;
                if (std::sqrt((ccx - s.x) * (ccx - s.x) + (ccz - s.z) * (ccz - s.z)) > R) continue;
                if (tc.HasHole(ix, iz) != dig) {
                    tc.SetHole(ix, iz, dig);
                    changed = true;
                }
            }
        if (changed) tc.MarkDirtyRect(cx - rc, cz - rc, cx + rc, cz + rc);
        return changed;
    }

    const int x0 = std::max(0, cx - rc), x1 = std::min(res, cx + rc);
    const int z0 = std::max(0, cz - rc), z1 = std::min(res, cz + rc);
    if (x0 > x1 || z0 > z1) return false;
    std::vector<float>& H = tc.MutableHeights();
    auto at = [&](int ix, int iz) -> float& { return H[static_cast<size_t>(iz) * n + ix]; };
    auto weight = [&](int ix, int iz) {
        const float vx = -half + ix * cell, vz = -half + iz * cell;
        const float d = std::sqrt((vx - s.x) * (vx - s.x) + (vz - s.z) * (vz - s.z));
        return d > R ? 0.0f : std::pow(1.0f - d / R, 0.5f + s.falloff * 2.0f);
    };
    const float rate = s.strength, dt = s.dt;

    if (s.brush == TerrainBrush::Erode) {
        // Thermal erosion: where a vertex stands higher above a neighbour
        // than the talus slope allows, part of the excess moves down to it.
        // Material is conserved inside the brush. A few passes per
        // application, so a held brush visibly relaxes cliffs into scree.
        const float max_drop = s.talus * cell;
        const float move = std::min(0.5f, rate * dt * 4.0f);
        for (int pass = 0; pass < 3; ++pass) {
            for (int iz = z0; iz <= z1; ++iz)
                for (int ix = x0; ix <= x1; ++ix) {
                    const float w = weight(ix, iz);
                    if (w <= 0.0f) continue;
                    static const int kDX[4] = {1, -1, 0, 0}, kDZ[4] = {0, 0, 1, -1};
                    for (int k = 0; k < 4; ++k) {
                        const int jx = ix + kDX[k], jz = iz + kDZ[k];
                        if (jx < 0 || jz < 0 || jx > res || jz > res) continue;
                        const float excess = at(ix, iz) - at(jx, jz) - max_drop;
                        if (excess <= 0.0f) continue;
                        const float amount = excess * 0.5f * move * w;
                        at(ix, iz) -= amount;
                        at(jx, jz) += amount;
                        changed = true;
                    }
                }
        }
        if (changed) tc.MarkDirtyRect(x0 - 1, z0 - 1, x1 + 1, z1 + 1);
        return changed;
    }

    // Ramp: the line from the stroke's start to the brush, and the heights at its ends.
    const float rdx = s.x - s.start_x, rdz = s.z - s.start_z;
    const float rlen2 = rdx * rdx + rdz * rdz;
    const float end_h = s.brush == TerrainBrush::Ramp ? tc.SampleHeightLocal(s.x, s.z) / std::max(1e-3f, tc.GetHeightScale()) : 0.0f;

    for (int iz = z0; iz <= z1; ++iz)
        for (int ix = x0; ix <= x1; ++ix) {
            const float w = weight(ix, iz);
            if (w <= 0.0f) continue;
            float&      h = at(ix, iz);
            const float before = h;
            switch (s.brush) {
                case TerrainBrush::Raise: h += rate * w * dt * 12.0f; break;
                case TerrainBrush::Lower: h -= rate * w * dt * 12.0f; break;
                case TerrainBrush::Smooth: {
                    const float avg = (tc.HeightAt(ix - 1, iz) + tc.HeightAt(ix + 1, iz) + tc.HeightAt(ix, iz - 1) + tc.HeightAt(ix, iz + 1)) * 0.25f;
                    h += (avg - h) * std::min(1.0f, rate * w * dt * 6.0f);
                    break;
                }
                case TerrainBrush::Flatten: h += (s.target - h) * std::min(1.0f, rate * w * dt * 6.0f); break;
                case TerrainBrush::Noise: {
                    const float vx = -half + ix * cell, vz = -half + iz * cell;
                    const float sc = std::max(0.05f, s.noise_scale);
                    h += terrain_detail::fbm(vx / sc, vz / sc, s.seed) * rate * w * dt * 12.0f;
                    break;
                }
                case TerrainBrush::Terrace: {
                    const float st = std::max(0.01f, s.step);
                    const float level = std::floor(h / st + 0.5f) * st;
                    h += (level - h) * std::min(1.0f, rate * w * dt * 6.0f);
                    break;
                }
                case TerrainBrush::Ramp: break;   // along the whole line: below
                default: break;
            }
            if (h != before) changed = true;
        }

    if (s.brush == TerrainBrush::Ramp && rlen2 > 1e-6f) {
        // Every vertex within `radius` of the segment start->brush goes toward
        // the straight line between the two end heights.
        const int   ax0 = std::max(0, static_cast<int>(std::floor((std::min(s.x, s.start_x) - R + half) / cell)));
        const int   ax1 = std::min(res, static_cast<int>(std::ceil((std::max(s.x, s.start_x) + R + half) / cell)));
        const int   az0 = std::max(0, static_cast<int>(std::floor((std::min(s.z, s.start_z) - R + half) / cell)));
        const int   az1 = std::min(res, static_cast<int>(std::ceil((std::max(s.z, s.start_z) + R + half) / cell)));
        for (int iz = az0; iz <= az1; ++iz)
            for (int ix = ax0; ix <= ax1; ++ix) {
                const float vx = -half + ix * cell, vz = -half + iz * cell;
                const float t = std::clamp(((vx - s.start_x) * rdx + (vz - s.start_z) * rdz) / rlen2, 0.0f, 1.0f);
                const float px = s.start_x + rdx * t, pz = s.start_z + rdz * t;
                const float d = std::sqrt((vx - px) * (vx - px) + (vz - pz) * (vz - pz));
                if (d > R) continue;
                const float w = std::pow(1.0f - d / R, 0.5f + s.falloff * 2.0f);
                float&      h = at(ix, iz);
                const float want = s.target + (end_h - s.target) * t;
                const float before = h;
                h += (want - h) * std::min(1.0f, rate * w * dt * 6.0f);
                if (h != before) changed = true;
            }
        if (changed) tc.MarkDirtyRect(ax0 - 1, az0 - 1, ax1 + 1, az1 + 1);
        return changed;
    }

    if (changed) tc.MarkDirtyRect(cx - rc, cz - rc, cx + rc, cz + rc);
    return changed;
}

/// Thermal erosion over the whole terrain: `passes` relaxations toward the
/// talus slope. Conserves material.
inline void erode_terrain(TerrainComponent& tc, int passes, float talus) {
    const int   res = tc.GetResolution(), n = tc.VertsPerSide();
    const float max_drop = talus * tc.CellSize();
    std::vector<float>& H = tc.MutableHeights();
    std::vector<float>  delta(H.size());
    for (int p = 0; p < passes; ++p) {
        std::fill(delta.begin(), delta.end(), 0.0f);
        for (int iz = 0; iz <= res; ++iz)
            for (int ix = 0; ix <= res; ++ix) {
                const size_t i = static_cast<size_t>(iz) * n + ix;
                static const int kDX[4] = {1, -1, 0, 0}, kDZ[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int jx = ix + kDX[k], jz = iz + kDZ[k];
                    if (jx < 0 || jz < 0 || jx > res || jz > res) continue;
                    const size_t j = static_cast<size_t>(jz) * n + jx;
                    const float  excess = H[i] - H[j] - max_drop;
                    if (excess <= 0.0f) continue;
                    const float amount = excess * 0.125f;   // an eighth per neighbour: stable with four
                    delta[i] -= amount;
                    delta[j] += amount;
                }
            }
        for (size_t i = 0; i < H.size(); ++i) H[i] += delta[i];
    }
    tc.MarkAllDirty();
}

/// Fill the whole terrain with fractal hills: a starting point for a world.
inline void generate_terrain_noise(TerrainComponent& tc, float amplitude, float feature_size, uint32_t seed) {
    const int   res = tc.GetResolution(), n = tc.VertsPerSide();
    const float half = tc.GetSize() * 0.5f, cell = tc.CellSize();
    const float sc = std::max(0.5f, feature_size);
    std::vector<float>& H = tc.MutableHeights();
    for (int iz = 0; iz <= res; ++iz)
        for (int ix = 0; ix <= res; ++ix) {
            const float x = (-half + ix * cell) / sc, z = (-half + iz * cell) / sc;
            float       h = 0.0f, a = 1.0f, f = 1.0f, norm = 0.0f;
            for (int o = 0; o < 5; ++o) {
                h += terrain_detail::value_noise(x * f, z * f, seed + static_cast<uint32_t>(o) * 31u) * a;
                norm += a;
                a *= 0.5f;
                f *= 2.02f;
            }
            H[static_cast<size_t>(iz) * n + ix] = (h / norm * 0.5f + 0.5f) * amplitude;
        }
    tc.MarkAllDirty();
}

// ---------------------------------------------------------------------------
// Heightmap files: 16-bit greyscale, the format every terrain tool reads.
// PGM (P5, maxval 65535, big-endian) is self-describing; RAW (.r16, little-
// endian, square) is what World Machine, Gaea and Unreal exchange.
// ---------------------------------------------------------------------------

/// The heights as 16-bit samples, (res+1)^2, lowest height 0 and highest 65535.
/// `lo` and `hi` receive the heights those stand for.
inline std::vector<uint16_t> terrain_to_r16(const TerrainComponent& tc, float& lo, float& hi) {
    const std::vector<float>& H = tc.Heights();
    lo = hi = H.empty() ? 0.0f : H[0];
    for (float h : H) {
        lo = std::min(lo, h);
        hi = std::max(hi, h);
    }
    const float span = hi - lo > 1e-6f ? hi - lo : 1.0f;
    std::vector<uint16_t> out(H.size());
    for (size_t i = 0; i < H.size(); ++i) out[i] = static_cast<uint16_t>(std::clamp((H[i] - lo) / span, 0.0f, 1.0f) * 65535.0f + 0.5f);
    return out;
}

/// Set the heights from `side` x `side` 16-bit samples, resampled (bilinear)
/// to the terrain's own resolution, 0 -> `lo` and 65535 -> `hi`.
inline bool terrain_from_r16(TerrainComponent& tc, const std::vector<uint16_t>& px, int side, float lo, float hi) {
    if (side < 2 || px.size() < static_cast<size_t>(side) * side) return false;
    const int res = tc.GetResolution(), n = tc.VertsPerSide();
    std::vector<float>& H = tc.MutableHeights();
    auto sample = [&](int x, int z) { return static_cast<float>(px[static_cast<size_t>(z) * side + x]) / 65535.0f; };
    for (int iz = 0; iz <= res; ++iz)
        for (int ix = 0; ix <= res; ++ix) {
            const float gx = static_cast<float>(ix) / res * (side - 1), gz = static_cast<float>(iz) / res * (side - 1);
            const int   sx = std::min(side - 2, static_cast<int>(gx)), sz = std::min(side - 2, static_cast<int>(gz));
            const float fx = gx - sx, fz = gz - sz;
            const float a = sample(sx, sz) + (sample(sx + 1, sz) - sample(sx, sz)) * fx;
            const float b = sample(sx, sz + 1) + (sample(sx + 1, sz + 1) - sample(sx, sz + 1)) * fx;
            H[static_cast<size_t>(iz) * n + ix] = lo + (a + (b - a) * fz) * (hi - lo);
        }
    tc.MarkAllDirty();
    return true;
}

/// Write the heights as a 16-bit heightmap: ".pgm" (P5) or anything else as
/// raw little-endian samples. `lo`/`hi` receive the heights 0 and 65535 stand for.
inline bool save_terrain_heightmap(const TerrainComponent& tc, const std::string& path, float* lo = nullptr, float* hi = nullptr) {
    float a = 0.0f, b = 0.0f;
    const std::vector<uint16_t> px = terrain_to_r16(tc, a, b);
    if (lo) *lo = a;
    if (hi) *hi = b;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const bool pgm = path.size() >= 4 && path.compare(path.size() - 4, 4, ".pgm") == 0;
    const int  n = tc.VertsPerSide();
    if (pgm) f << "P5\n" << n << " " << n << "\n65535\n";
    for (uint16_t v : px) {
        const char le[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        if (pgm) { f.put(le[1]); f.put(le[0]); }   // PGM is big-endian
        else { f.put(le[0]); f.put(le[1]); }
    }
    return static_cast<bool>(f);
}

/// Read a 16-bit heightmap (P5 PGM with maxval 65535, or raw square samples)
/// into the terrain, between the heights `lo` and `hi`.
inline bool load_terrain_heightmap(TerrainComponent& tc, const std::string& path, float lo, float hi, std::string* error = nullptr) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail("cannot open the file");
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t at = 0;
    int    side = 0;
    bool   big = false;
    if (bytes.size() > 2 && bytes[0] == 'P' && bytes[1] == '5') {
        // P5 <width> <height> <maxval> then one whitespace byte, with # comments allowed.
        int    vals[3] = {0, 0, 0};
        size_t i = 2;
        for (int k = 0; k < 3; ++k) {
            for (;;) {
                while (i < bytes.size() && (bytes[i] == ' ' || bytes[i] == '\n' || bytes[i] == '\r' || bytes[i] == '\t')) ++i;
                if (i < bytes.size() && bytes[i] == '#') {
                    while (i < bytes.size() && bytes[i] != '\n') ++i;
                    continue;
                }
                break;
            }
            while (i < bytes.size() && bytes[i] >= '0' && bytes[i] <= '9') vals[k] = vals[k] * 10 + (bytes[i++] - '0');
        }
        if (vals[0] != vals[1] || vals[0] < 2) return fail("the heightmap must be square");
        if (vals[2] != 65535) return fail("the heightmap must be 16-bit (maxval 65535)");
        side = vals[0];
        at = i + 1;
        big = true;
    } else {
        const size_t count = bytes.size() / 2;
        side = static_cast<int>(std::lround(std::sqrt(static_cast<double>(count))));
        if (side < 2 || static_cast<size_t>(side) * side != count) return fail("a raw heightmap must be square, 16 bits per sample");
    }
    if (bytes.size() < at + static_cast<size_t>(side) * side * 2) return fail("the file is shorter than its size says");
    std::vector<uint16_t> px(static_cast<size_t>(side) * side);
    for (size_t i = 0; i < px.size(); ++i) {
        const uint8_t b0 = static_cast<uint8_t>(bytes[at + i * 2]), b1 = static_cast<uint8_t>(bytes[at + i * 2 + 1]);
        px[i] = big ? static_cast<uint16_t>((b0 << 8) | b1) : static_cast<uint16_t>((b1 << 8) | b0);
    }
    return terrain_from_r16(tc, px, side, lo, hi) || fail("could not apply the heightmap");
}

}  // namespace schizo::scene
