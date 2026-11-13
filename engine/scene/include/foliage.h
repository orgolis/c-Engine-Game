#pragma once
// ============================================================================
// foliage -- trees, bushes, grass and rocks scattered over a terrain.
//
// A FoliageComponent sits on the same entity as a TerrainComponent and holds
// layers. A layer says WHAT (a built-in shape or a model file, its colours, a
// size range) and WHERE: how many per 100 m^2, a placement rule (height,
// slope, noise -- the same rule a texture layer uses), optionally only where a
// texture layer is painted, and a density mask that can be painted with a
// brush (more here, none there).
//
// Placement is deterministic: a jittered grid, each cell keeping its one
// candidate with a probability equal to the density there. The same settings
// and the same ground give the same forest, so nothing about instances is
// saved -- only the settings and the mask -- and a re-sculpted hill grows its
// trees back in the same places.
//
// Instances are kept in square cells (kFoliageCell world units), each with a
// bounding box, so the renderer can drop whole cells that are off screen or
// too far away before it looks at a single tree.
//
// Checked by tools/terrainbrush_check.
// ============================================================================

#include "terrain_brush.h"
#include "terrain_component.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::scene {

enum class FoliageKind : int { Conifer = 0, Broadleaf = 1, Bush = 2, Grass = 3, Rock = 4, Model = 5 };
inline const char* const kFoliageKindNames[] = {"Conifer tree", "Broadleaf tree", "Bush", "Grass", "Rock", "Model file"};
inline constexpr int kFoliageKindCount = 6;

inline constexpr float kFoliageCell = 32.0f;    // world units per culling cell
inline constexpr int   kFoliageMaskRes = 128;   // density mask texels per side

struct FoliageLayer {
    std::string name = "Trees";
    FoliageKind kind = FoliageKind::Conifer;
    std::string model_path;                            // kind Model
    glm::vec3   color_a = glm::vec3(0.18f, 0.38f, 0.16f);   // leaves / blades / stone
    glm::vec3   color_b = glm::vec3(0.35f, 0.24f, 0.14f);   // trunk
    float       density = 2.0f;                        // instances per 100 m^2 where everything allows it
    float       min_scale = 0.8f, max_scale = 1.3f;
    float       size = 6.0f;                           // height of a scale-1 instance, world units
    bool        align_to_slope = false;                // grass and rocks lean with the ground; trees grow up
    float       sink = 0.1f;                           // how far below the ground the base goes (as part of size)
    TerrainLayerRule rule;                             // where it grows; disabled = everywhere
    int         on_texture_layer = -1;                 // -1: anywhere; else only where that splat layer shows
    float       min_texture_weight = 0.5f;
    float       draw_distance = 300.0f;
    uint32_t    seed = 1;
    bool        casts_shadow = true;
    std::vector<uint8_t> mask;                         // kFoliageMaskRes^2, 255 = full density; empty = all full

    float mask_at(float u, float v) const {            // u, v in 0..1 across the terrain
        if (mask.empty()) return 1.0f;
        const int x = std::clamp(static_cast<int>(u * kFoliageMaskRes), 0, kFoliageMaskRes - 1);
        const int z = std::clamp(static_cast<int>(v * kFoliageMaskRes), 0, kFoliageMaskRes - 1);
        return mask[static_cast<size_t>(z) * kFoliageMaskRes + x] / 255.0f;
    }
};

struct FoliageInstance {
    glm::vec3 pos;      // local to the terrain, base of the instance
    glm::vec3 up;       // the direction it grows (world up, or the ground normal)
    float     yaw = 0.0f;
    float     scale = 1.0f;
    glm::mat4 local{1.0f};   // foliage_matrix(), worked out once when it is placed
};

struct FoliageCell {
    glm::vec3 min, max;                   // local bounds of everything in it
    std::vector<FoliageInstance> items;
};

/// The instances of one layer, in culling cells.
struct FoliageScatter {
    std::vector<FoliageCell> cells;
    size_t count = 0;
};

/// The model matrix of an instance, in the terrain's local space, for a
/// shape of height 1 standing on its base at the origin.
inline glm::mat4 foliage_matrix(const FoliageInstance& in, float size) {
    const glm::vec3 up = in.up;
    // A frame around `up` turned by the yaw.
    glm::vec3 side = std::fabs(up.y) < 0.99f ? glm::normalize(glm::cross(up, glm::vec3(0, 1, 0))) : glm::vec3(1, 0, 0);
    glm::vec3 fwd = glm::normalize(glm::cross(side, up));
    const float c = std::cos(in.yaw), s = std::sin(in.yaw);
    const glm::vec3 x = side * c + fwd * s, z = glm::cross(x, up);
    const float k = size * in.scale;
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x * k, 0.0f);
    m[1] = glm::vec4(up * k, 0.0f);
    m[2] = glm::vec4(z * k, 0.0f);
    m[3] = glm::vec4(in.pos, 1.0f);
    return m;
}

namespace foliage_detail {
inline float rnd(uint32_t a, uint32_t b, uint32_t seed, uint32_t salt) {
    uint32_t h = a * 0x8DA6B343u ^ b * 0xD8163841u ^ seed * 0xCB1AB31Fu ^ salt * 0x165667B1u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000);   // 0 .. <1
}
}  // namespace foliage_detail

/// How much of the layer's full density grows at a local position: 0..1.
inline float foliage_density_at(const TerrainComponent& tc, const FoliageLayer& l, float lx, float lz) {
    const float size = tc.GetSize(), half = size * 0.5f;
    const float u = (lx + half) / size, v = (lz + half) / size;
    if (u < 0.0f || v < 0.0f || u > 1.0f || v > 1.0f) return 0.0f;
    float d = l.mask_at(u, v);
    if (d <= 0.0f) return 0.0f;
    // A hole in the terrain has no ground to grow on.
    const int cx = std::clamp(static_cast<int>(u * tc.GetResolution()), 0, tc.GetResolution() - 1);
    const int cz = std::clamp(static_cast<int>(v * tc.GetResolution()), 0, tc.GetResolution() - 1);
    if (tc.HasHole(cx, cz)) return 0.0f;
    if (l.rule.enabled) {
        const float h = tc.SampleHeightLocal(lx, lz);
        const float slope = terrain_slope_degrees(tc, lx, lz);
        d *= std::min(1.0f, terrain_rule_fit(l.rule, h, slope, lx, lz, l.seed * 7919u + 3u) / std::max(1e-3f, l.rule.weight));
    }
    if (l.on_texture_layer >= 0 && l.on_texture_layer < kTerrainLayers) {
        const int   res = tc.SplatResolution();
        const int   sx = std::clamp(static_cast<int>(u * res), 0, res - 1), sz = std::clamp(static_cast<int>(v * res), 0, res - 1);
        const float w = tc.Splat()[(static_cast<size_t>(sz) * res + sx) * 4 + l.on_texture_layer] / 255.0f;
        // Full where the layer is at least min weight, fading to none below half of it.
        const float lo = l.min_texture_weight * 0.5f;
        d *= std::clamp((w - lo) / std::max(1e-3f, l.min_texture_weight - lo), 0.0f, 1.0f);
    }
    return d;
}

/// Place a layer's instances on a terrain.
inline FoliageScatter scatter_foliage(const TerrainComponent& tc, const FoliageLayer& l, size_t max_instances = 400000) {
    using foliage_detail::rnd;
    FoliageScatter out;
    const float size = tc.GetSize(), half = size * 0.5f;
    if (l.density <= 0.0f || size <= 0.0f) return out;
    // One candidate per grid square of this area holds the asked density.
    const float spacing = std::sqrt(100.0f / l.density);
    const int   steps = std::max(1, static_cast<int>(std::ceil(size / spacing)));
    const int   cells_per_side = std::max(1, static_cast<int>(std::ceil(size / kFoliageCell)));
    out.cells.resize(static_cast<size_t>(cells_per_side) * cells_per_side);
    for (FoliageCell& c : out.cells) {
        c.min = glm::vec3(1e30f);
        c.max = glm::vec3(-1e30f);
    }
    const float top = l.size * std::max(l.min_scale, l.max_scale);
    for (int gz = 0; gz < steps; ++gz)
        for (int gx = 0; gx < steps; ++gx) {
            const uint32_t ux = static_cast<uint32_t>(gx), uz = static_cast<uint32_t>(gz);
            const float    lx = -half + (gx + rnd(ux, uz, l.seed, 1)) * spacing;
            const float    lz = -half + (gz + rnd(ux, uz, l.seed, 2)) * spacing;
            if (lx > half || lz > half) continue;
            const float keep = foliage_density_at(tc, l, lx, lz);
            if (keep <= 0.0f || rnd(ux, uz, l.seed, 3) >= keep) continue;
            FoliageInstance in;
            in.yaw = rnd(ux, uz, l.seed, 4) * 6.2831853f;
            in.scale = l.min_scale + (l.max_scale - l.min_scale) * rnd(ux, uz, l.seed, 5);
            in.up = glm::vec3(0.0f, 1.0f, 0.0f);
            if (l.align_to_slope) {
                const float e = tc.CellSize();
                const float dx = tc.SampleHeightLocal(lx + e, lz) - tc.SampleHeightLocal(lx - e, lz);
                const float dz = tc.SampleHeightLocal(lx, lz + e) - tc.SampleHeightLocal(lx, lz - e);
                in.up = glm::normalize(glm::vec3(-dx, 2.0f * e, -dz));
            }
            in.pos = glm::vec3(lx, tc.SampleHeightLocal(lx, lz) - l.sink * l.size * in.scale, lz);
            in.local = foliage_matrix(in, l.size);
            const int cx = std::clamp(static_cast<int>((lx + half) / kFoliageCell), 0, cells_per_side - 1);
            const int cz = std::clamp(static_cast<int>((lz + half) / kFoliageCell), 0, cells_per_side - 1);
            FoliageCell& c = out.cells[static_cast<size_t>(cz) * cells_per_side + cx];
            const float  r = top * 0.5f;
            c.min = glm::min(c.min, in.pos - glm::vec3(r, 0.0f, r));
            c.max = glm::max(c.max, in.pos + glm::vec3(r, top, r));
            c.items.push_back(in);
            if (++out.count >= max_instances) return out;
        }
    return out;
}

/// Paint a layer's density mask with a round brush at a local position:
/// `amount` > 0 grows more, < 0 clears. Returns true when it changed.
inline bool paint_foliage_mask(const TerrainComponent& tc, FoliageLayer& l, float lx, float lz, float radius, float amount,
                               float falloff) {
    if (l.mask.empty()) l.mask.assign(static_cast<size_t>(kFoliageMaskRes) * kFoliageMaskRes, 255);
    const float size = tc.GetSize(), half = size * 0.5f;
    const float cx = (lx + half) / size * kFoliageMaskRes, cz = (lz + half) / size * kFoliageMaskRes;
    const float tr = radius / size * kFoliageMaskRes;
    if (tr <= 0.0f) return false;
    bool changed = false;
    for (int z = std::max(0, static_cast<int>(cz - tr)); z <= std::min(kFoliageMaskRes - 1, static_cast<int>(cz + tr)); ++z)
        for (int x = std::max(0, static_cast<int>(cx - tr)); x <= std::min(kFoliageMaskRes - 1, static_cast<int>(cx + tr)); ++x) {
            const float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (z + 0.5f - cz) * (z + 0.5f - cz)) / tr;
            if (d > 1.0f) continue;
            const float    w = std::pow(1.0f - d, 0.5f + falloff * 2.0f);
            uint8_t&       px = l.mask[static_cast<size_t>(z) * kFoliageMaskRes + x];
            const uint8_t  was = px;
            px = static_cast<uint8_t>(std::clamp(static_cast<float>(px) + amount * w * 255.0f, 0.0f, 255.0f));
            changed = changed || px != was;
        }
    return changed;
}

/// What a FoliageComponent's cached scatter was built from: when any of it
/// changes, the scatter is rebuilt.
struct FoliageStamp {
    uint64_t terrain_version = 0, splat_version = 0, settings = 0;
    bool operator==(const FoliageStamp& o) const {
        return terrain_version == o.terrain_version && splat_version == o.splat_version && settings == o.settings;
    }
};

class FoliageComponent final : public Component {
public:
    const std::vector<FoliageLayer>& Layers() const { return layers_; }
    /// For changing the layers. Counts as a change: the scatter is rebuilt.
    std::vector<FoliageLayer>& MutableLayers() { MarkChanged(); ++settings_; return layers_; }
    /// After changing a layer through a reference kept from MutableLayers().
    void Touch() { MarkChanged(); ++settings_; }
    uint64_t Settings() const { return settings_; }

    /// The scatter of each layer, rebuilt when the terrain or the settings changed.
    const std::vector<FoliageScatter>& Scatter(const TerrainComponent& tc) {
        const FoliageStamp now{tc.Version(), tc.SplatVersion(), settings_};
        if (!(now == built_) || scatter_.size() != layers_.size()) {
            scatter_.clear();
            for (const FoliageLayer& l : layers_) scatter_.push_back(scatter_foliage(tc, l));
            built_ = now;
            ++scatter_version_;
        }
        return scatter_;
    }
    uint64_t ScatterVersion() const { return scatter_version_; }

private:
    std::vector<FoliageLayer>   layers_;
    uint64_t                    settings_ = 1;
    std::vector<FoliageScatter> scatter_;
    FoliageStamp                built_{};
    uint64_t                    scatter_version_ = 0;
};

}  // namespace schizo::scene
