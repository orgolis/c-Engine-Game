#pragma once
// ============================================================================
// biome -- regions of a terrain that look and feel different: meadow below,
// forest on the slopes, rock and snow on the peaks, marsh by the river.
//
// A BiomeComponent sits beside a TerrainComponent and holds biomes. Each says
// WHERE it is, by altitude and by moisture (wet near water, dry far from it),
// with a fade at each edge, and can be painted on by hand where the rules are
// not what the level needs. Each says WHAT it is:
//   - the texture layer its ground shows,
//   - how dense each foliage layer grows there (a multiplier per layer),
//   - its atmosphere: ambient light and fog, blended as the camera moves.
//
// The biome map is worked out on a 128 x 128 grid over the terrain: for every
// texel, a weight per biome, adding up to one. "Apply biomes" bakes it into
// the terrain's splatmap and the foliage layers' density masks (one undo
// step). The atmosphere and the scripts' "which biome am I in" read it live.
//
// Checked by tools/terrainbrush_check.
// ============================================================================

#include "foliage.h"
#include "terrain_brush.h"
#include "terrain_component.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <string>
#include <vector>

namespace schizo::scene {

inline constexpr int kBiomeRes = 128;   // biome and moisture map texels per side
inline constexpr int kMaxBiomes = 8;

struct Biome {
    std::string name = "Meadow";
    glm::vec3   color = glm::vec3(0.45f, 0.75f, 0.35f);   // how the editor shows it on the map
    float min_height = -1000.0f, max_height = 1000.0f, height_blend = 4.0f;
    float min_moisture = 0.0f, max_moisture = 1.0f, moisture_blend = 0.1f;
    float weight = 1.0f;                                   // against the other biomes where they overlap
    int   ground_layer = -1;                               // texture layer its ground shows; -1: leave the splat alone
    std::vector<float> foliage;                            // density multiplier per foliage layer; missing = 1
    // Atmosphere, blended in by how much of this biome is under the camera.
    bool      atmosphere = false;
    float     ambient_scale = 1.0f;                        // times the scene's ambient light
    float     fog_scale = 1.0f;                            // times the volumetric fog density
    glm::vec3 fog_color = glm::vec3(0.10f, 0.12f, 0.16f);  // the fog's ambient colour here
    std::vector<uint8_t> paint;                            // kBiomeRes^2 painted extra weight (0..255); empty = none
};

struct MoistureSettings {
    float reach = 40.0f;        // world units from water at which ground is dry
    float noise = 0.25f;        // how much patchiness is mixed in
    float noise_scale = 60.0f;  // world size of a wet or dry patch
    uint32_t seed = 3;
};

/// A rectangle of standing water in the terrain's local space (a lake or a
/// river surface): what makes ground wet.
struct WaterRect {
    float min_x, min_z, max_x, max_z, level;
};

/// 0 (dry) .. 1 (wet) per biome-map texel: how near water is, with noise.
/// Water is the terrain's own sea (where the ground is below it) and the
/// given rectangles (where the ground is below their level).
inline std::vector<float> compute_moisture(const TerrainComponent& tc, const std::vector<WaterRect>& water, const MoistureSettings& ms) {
    const int   n = kBiomeRes;
    const float size = tc.GetSize(), half = size * 0.5f, texel = size / n;
    std::vector<float> dist(static_cast<size_t>(n) * n, 1e30f);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const float lx = -half + (x + 0.5f) * texel, lz = -half + (z + 0.5f) * texel;
            const float h = tc.SampleHeightLocal(lx, lz);
            bool wet = tc.IsWaterEnabled() && h <= tc.GetWaterLevel();
            for (const WaterRect& w : water)
                wet = wet || (lx >= w.min_x && lx <= w.max_x && lz >= w.min_z && lz <= w.max_z && h <= w.level + 0.05f);
            if (wet) {
                dist[static_cast<size_t>(z) * n + x] = 0.0f;
                open.push({0.0f, z * n + x});
            }
        }
    // Distance over the map from the nearest wet texel (8 neighbours).
    while (!open.empty()) {
        const Item it = open.top();
        open.pop();
        if (it.first > dist[static_cast<size_t>(it.second)]) continue;
        const int x = it.second % n, z = it.second / n;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx, nz = z + dz;
                if ((!dx && !dz) || nx < 0 || nz < 0 || nx >= n || nz >= n) continue;
                const float d = it.first + texel * ((dx && dz) ? 1.41421356f : 1.0f);
                float&      cur = dist[static_cast<size_t>(nz) * n + nx];
                if (d < cur) {
                    cur = d;
                    open.push({d, nz * n + nx});
                }
            }
    }
    std::vector<float> m(dist.size());
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const size_t i = static_cast<size_t>(z) * n + x;
            float wetness = dist[i] >= 1e29f ? 0.0f : std::clamp(1.0f - dist[i] / std::max(1.0f, ms.reach), 0.0f, 1.0f);
            if (ms.noise > 0.0f) {
                const float lx = -half + (x + 0.5f) * texel, lz = -half + (z + 0.5f) * texel;
                const float sc = std::max(1.0f, ms.noise_scale);
                const float nv = terrain_detail::fbm(lx / sc, lz / sc, ms.seed) * 0.5f + 0.5f;
                wetness = wetness * (1.0f - ms.noise) + nv * ms.noise;
            }
            m[i] = std::clamp(wetness, 0.0f, 1.0f);
        }
    return m;
}

/// How strongly a biome's rules claim a spot (before painting), 0..weight.
inline float biome_rule_fit(const Biome& b, float height, float moisture) {
    auto band = [](float v, float lo, float hi, float blend) {
        if (v >= lo && v <= hi) return 1.0f;
        const float d = v < lo ? lo - v : v - hi;
        return blend > 1e-4f ? std::max(0.0f, 1.0f - d / blend) : 0.0f;
    };
    return band(height, b.min_height, b.max_height, b.height_blend) * band(moisture, b.min_moisture, b.max_moisture, b.moisture_blend) *
           std::max(0.0f, b.weight);
}

/// The biome map: for each texel, one weight per biome, adding up to one
/// (all zero where no biome claims the ground). Painting adds on top of the
/// rules: a fully painted texel belongs to the painted biome.
inline std::vector<float> compute_biome_map(const TerrainComponent& tc, const std::vector<Biome>& biomes, const std::vector<float>& moisture) {
    const int    n = kBiomeRes, nb = static_cast<int>(biomes.size());
    const float  size = tc.GetSize(), half = size * 0.5f, texel = size / n;
    std::vector<float> w(static_cast<size_t>(n) * n * nb, 0.0f);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const size_t t = static_cast<size_t>(z) * n + x;
            const float  lx = -half + (x + 0.5f) * texel, lz = -half + (z + 0.5f) * texel;
            const float  h = tc.SampleHeightLocal(lx, lz);
            const float  mo = t < moisture.size() ? moisture[t] : 0.5f;
            float        sum = 0.0f, painted = 0.0f;
            for (int b = 0; b < nb; ++b) {
                const Biome& bi = biomes[static_cast<size_t>(b)];
                const float  p = bi.paint.empty() ? 0.0f : bi.paint[t] / 255.0f;
                painted = std::max(painted, p);
                float& v = w[t * nb + b];
                v = biome_rule_fit(bi, h, mo);
                sum += v;
            }
            // Paint wins in proportion to how much was painted.
            for (int b = 0; b < nb; ++b) {
                const Biome& bi = biomes[static_cast<size_t>(b)];
                const float  p = bi.paint.empty() ? 0.0f : bi.paint[t] / 255.0f;
                float&       v = w[t * nb + b];
                v = (sum > 0.0f ? v / sum : 0.0f) * (1.0f - painted) + p;
            }
            float total = 0.0f;
            for (int b = 0; b < nb; ++b) total += w[t * nb + b];
            if (total > 0.0f)
                for (int b = 0; b < nb; ++b) w[t * nb + b] /= total;
        }
    return w;
}

/// Bake a biome map into the terrain's splatmap (each biome's ground layer,
/// blended by weight; ground no biome with a ground layer claims is left as
/// it was) and into the foliage layers' density masks.
inline void apply_biomes(TerrainComponent& tc, const std::vector<Biome>& biomes, const std::vector<float>& map, FoliageComponent* fc) {
    const int n = kBiomeRes, nb = static_cast<int>(biomes.size());
    if (nb == 0 || map.size() < static_cast<size_t>(n) * n * nb) return;
    auto weights_at = [&](float u, float v, int b) {   // bilinear over the biome map
        const float gx = std::clamp(u * n - 0.5f, 0.0f, n - 1.0f), gz = std::clamp(v * n - 0.5f, 0.0f, n - 1.0f);
        const int   x0 = std::min(n - 2, static_cast<int>(gx)), z0 = std::min(n - 2, static_cast<int>(gz));
        const float fx = gx - x0, fz = gz - z0;
        auto at = [&](int x, int z) { return map[(static_cast<size_t>(z) * n + x) * nb + b]; };
        const float a = at(x0, z0) + (at(x0 + 1, z0) - at(x0, z0)) * fx;
        const float c = at(x0, z0 + 1) + (at(x0 + 1, z0 + 1) - at(x0, z0 + 1)) * fx;
        return a + (c - a) * fz;
    };
    // Ground.
    const int sres = tc.SplatResolution();
    std::vector<uint8_t>& splat = tc.MutableSplat();
    for (int z = 0; z < sres; ++z)
        for (int x = 0; x < sres; ++x) {
            const float u = (x + 0.5f) / sres, v = (z + 0.5f) / sres;
            float       layer[kTerrainLayers] = {0, 0, 0, 0}, claimed = 0.0f;
            for (int b = 0; b < nb; ++b) {
                const int gl = biomes[static_cast<size_t>(b)].ground_layer;
                if (gl < 0 || gl >= kTerrainLayers) continue;
                const float w = weights_at(u, v, b);
                layer[gl] += w;
                claimed += w;
            }
            if (claimed <= 1e-4f) continue;
            uint8_t* px = &splat[(static_cast<size_t>(z) * sres + x) * 4];
            // What no biome claims keeps the texture it had.
            const float keep = std::max(0.0f, 1.0f - claimed);
            int         used = 0;
            for (int c = 0; c < kTerrainLayers; ++c) {
                px[c] = static_cast<uint8_t>(std::clamp((layer[c] + px[c] / 255.0f * keep) * 255.0f + 0.5f, 0.0f, 255.0f));
                used += px[c];
            }
            int big = 0;
            for (int c = 1; c < kTerrainLayers; ++c)
                if (px[c] > px[big]) big = c;
            px[big] = static_cast<uint8_t>(std::clamp(static_cast<int>(px[big]) + 255 - used, 0, 255));
        }
    tc.MarkSplatDirty();
    // Foliage: each layer's density mask is how much of it the biomes there want.
    if (fc) {
        std::vector<FoliageLayer>& layers = fc->MutableLayers();
        for (size_t li = 0; li < layers.size(); ++li) {
            std::vector<uint8_t> mask(static_cast<size_t>(kFoliageMaskRes) * kFoliageMaskRes, 255);
            for (int z = 0; z < kFoliageMaskRes; ++z)
                for (int x = 0; x < kFoliageMaskRes; ++x) {
                    const float u = (x + 0.5f) / kFoliageMaskRes, v = (z + 0.5f) / kFoliageMaskRes;
                    float       d = 0.0f, claimed = 0.0f;
                    for (int b = 0; b < nb; ++b) {
                        const Biome& bi = biomes[static_cast<size_t>(b)];
                        const float  w = weights_at(u, v, b);
                        d += w * (li < bi.foliage.size() ? bi.foliage[li] : 1.0f);
                        claimed += w;
                    }
                    d += std::max(0.0f, 1.0f - claimed);   // unclaimed ground: as the layer itself says
                    mask[static_cast<size_t>(z) * kFoliageMaskRes + x] = static_cast<uint8_t>(std::clamp(d, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
            layers[li].mask = std::move(mask);
        }
    }
}

/// Paint a biome onto the map: `amount` > 0 adds it, < 0 removes paint.
inline bool paint_biome(const TerrainComponent& tc, Biome& b, float lx, float lz, float radius, float amount, float falloff) {
    if (b.paint.empty()) b.paint.assign(static_cast<size_t>(kBiomeRes) * kBiomeRes, 0);
    const float size = tc.GetSize(), half = size * 0.5f;
    const float cx = (lx + half) / size * kBiomeRes, cz = (lz + half) / size * kBiomeRes, tr = radius / size * kBiomeRes;
    if (tr <= 0.0f) return false;
    bool changed = false;
    for (int z = std::max(0, static_cast<int>(cz - tr)); z <= std::min(kBiomeRes - 1, static_cast<int>(cz + tr)); ++z)
        for (int x = std::max(0, static_cast<int>(cx - tr)); x <= std::min(kBiomeRes - 1, static_cast<int>(cx + tr)); ++x) {
            const float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (z + 0.5f - cz) * (z + 0.5f - cz)) / tr;
            if (d > 1.0f) continue;
            uint8_t&      px = b.paint[static_cast<size_t>(z) * kBiomeRes + x];
            const uint8_t was = px;
            px = static_cast<uint8_t>(std::clamp(px + amount * std::pow(1.0f - d, 0.5f + falloff * 2.0f) * 255.0f, 0.0f, 255.0f));
            changed = changed || px != was;
        }
    return changed;
}

class BiomeComponent final : public Component {
public:
    const std::vector<Biome>& Biomes() const { return biomes_; }
    std::vector<Biome>&       MutableBiomes() { MarkChanged(); ++settings_; return biomes_; }
    const MoistureSettings&   Moisture() const { return moisture_; }
    void SetMoisture(const MoistureSettings& m) { MarkChanged(); moisture_ = m; ++settings_; }
    void Touch() { MarkChanged(); ++settings_; }

    /// The biome map for the terrain and the water given, kept until either
    /// (or the biomes) change. `water_key` is anything that changes when the
    /// water does (the caller's sum of its rectangles, say).
    const std::vector<float>& Map(const TerrainComponent& tc, const std::vector<WaterRect>& water, uint64_t water_key) {
        if (tc.Version() != terrain_version_ || settings_ != built_settings_ || water_key != water_key_ || map_.empty()) {
            moisture_map_ = compute_moisture(tc, water, moisture_);
            map_ = compute_biome_map(tc, biomes_, moisture_map_);
            terrain_version_ = tc.Version();
            built_settings_ = settings_;
            water_key_ = water_key;
            ++map_version_;
        }
        return map_;
    }
    const std::vector<float>& MoistureMap() const { return moisture_map_; }
    uint64_t MapVersion() const { return map_version_; }

    /// Each biome's weight at a local position, from the last map built.
    std::vector<float> WeightsAt(const TerrainComponent& tc, float lx, float lz) const {
        const int nb = static_cast<int>(biomes_.size());
        std::vector<float> w(static_cast<size_t>(nb), 0.0f);
        if (map_.size() < static_cast<size_t>(kBiomeRes) * kBiomeRes * nb || nb == 0) return w;
        const float size = tc.GetSize(), half = size * 0.5f;
        const int   x = std::clamp(static_cast<int>((lx + half) / size * kBiomeRes), 0, kBiomeRes - 1);
        const int   z = std::clamp(static_cast<int>((lz + half) / size * kBiomeRes), 0, kBiomeRes - 1);
        for (int b = 0; b < nb; ++b) w[static_cast<size_t>(b)] = map_[(static_cast<size_t>(z) * kBiomeRes + x) * nb + b];
        return w;
    }
    /// The biome with the most weight at a local position; -1 where none.
    int DominantAt(const TerrainComponent& tc, float lx, float lz, float* weight = nullptr) const {
        const std::vector<float> w = WeightsAt(tc, lx, lz);
        int best = -1;
        float bw = 0.0f;
        for (size_t b = 0; b < w.size(); ++b)
            if (w[b] > bw) { bw = w[b]; best = static_cast<int>(b); }
        if (weight) *weight = bw;
        return best;
    }

private:
    std::vector<Biome>  biomes_;
    MoistureSettings    moisture_;
    uint64_t            settings_ = 1, built_settings_ = 0, terrain_version_ = 0, water_key_ = 0, map_version_ = 0;
    std::vector<float>  moisture_map_, map_;
};

}  // namespace schizo::scene
