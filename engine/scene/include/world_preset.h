#pragma once
// ============================================================================
// world_preset -- a whole landscape from one choice: ground shaped and worn,
// rivers run from the high ground, biomes laid on it, textures and foliage
// placed. Everything it makes is ordinary components and objects, so all of
// it can be changed afterwards with the usual tools.
//
// The steps are the ones a person would take by hand, in the same order:
// generate hills, erode, let water out at the highest points (carving the
// beds), then biomes (whose moisture now sees the rivers), then apply them to
// the ground and the foliage.
//
// Checked by tools/terrainbrush_check (every preset is built and looked at).
// ============================================================================

#include "biome.h"
#include "entity.h"
#include "foliage.h"
#include "material_desc.h"
#include <cctype>
#include <cmath>
#include "scene.h"
#include "terrain_brush.h"
#include "terrain_component.h"
#include "terrain_hydrology.h"
#include "transform.h"
#include "water_component.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace schizo::scene {

struct WorldPreset {
    std::string name, description;
    int   resolution = 256;
    float size = 512.0f;
    float height = 35.0f;          // the tallest hills
    float feature = 128.0f;        // how wide a hill is
    int   erosion = 40;            // passes
    float talus = 0.9f;
    bool  sea = false;
    float sea_level = 0.0f;        // as a share of `height`
    int   rivers = 2;
    float river_width = 3.0f, river_depth = 1.5f;
    std::vector<Biome>        biomes;
    std::vector<FoliageLayer> foliage;
    glm::vec4 ground[4] = {glm::vec4(0.45f, 0.42f, 0.36f, 1), glm::vec4(0.35f, 0.55f, 0.20f, 1), glm::vec4(0.13f, 0.30f, 0.11f, 1),
                           glm::vec4(0.72f, 0.72f, 0.74f, 1)};   // texture layers 0-3 as plain colours
};

namespace world_detail {
inline FoliageLayer trees(FoliageKind kind, float density, uint32_t seed) {
    FoliageLayer l;
    l.kind = kind;
    l.name = kind == FoliageKind::Broadleaf ? "Broadleaf trees" : "Conifers";
    l.density = density;
    l.seed = seed;
    l.rule.enabled = true;
    l.rule.max_slope = 30.0f;
    if (kind == FoliageKind::Broadleaf) l.color_a = glm::vec3(0.26f, 0.42f, 0.14f);
    return l;
}
inline FoliageLayer grass(float density, uint32_t seed) {
    FoliageLayer l;
    l.name = "Grass";
    l.kind = FoliageKind::Grass;
    l.size = 0.6f;
    l.density = density;
    l.draw_distance = 70.0f;
    l.align_to_slope = true;
    l.color_a = glm::vec3(0.27f, 0.45f, 0.15f);
    l.seed = seed;
    return l;
}
inline FoliageLayer rocks(float density, uint32_t seed) {
    FoliageLayer l;
    l.name = "Rocks";
    l.kind = FoliageKind::Rock;
    l.size = 1.2f;
    l.density = density;
    l.sink = 0.3f;
    l.align_to_slope = true;
    l.color_a = glm::vec3(0.45f, 0.43f, 0.40f);
    l.seed = seed;
    return l;
}
inline Biome biome(const char* name, glm::vec3 color, float lo, float hi, int ground, std::vector<float> foliage) {
    Biome b;
    b.name = name;
    b.color = color;
    b.min_height = lo;
    b.max_height = hi;
    b.ground_layer = ground;
    b.foliage = std::move(foliage);
    return b;
}
}  // namespace world_detail

/// The presets the editor offers.
inline std::vector<WorldPreset> world_presets() {
    using namespace world_detail;
    std::vector<WorldPreset> out;
    {
        WorldPreset p;
        p.name = "Rolling hills";
        p.description = "Green hills and woods, two rivers, a little rock on the tops.";
        p.foliage = {trees(FoliageKind::Conifer, 1.2f, 1), trees(FoliageKind::Broadleaf, 0.8f, 5), grass(60.0f, 9), rocks(0.4f, 13)};
        Biome wet = biome("Wetland", glm::vec3(0.25f, 0.55f, 0.55f), -1000.0f, 12.0f, 1, {0.0f, 0.3f, 1.0f, 0.2f});
        wet.min_moisture = 0.7f;
        wet.weight = 2.0f;
        p.biomes = {biome("Lowland", glm::vec3(0.55f, 0.80f, 0.35f), -1000.0f, 12.0f, 1, {0.15f, 0.4f, 1.0f, 0.4f}), wet,
                    biome("Forest", glm::vec3(0.15f, 0.45f, 0.15f), 8.0f, 27.0f, 2, {1.0f, 1.0f, 0.3f, 0.3f}),
                    biome("Highland", glm::vec3(0.75f, 0.75f, 0.80f), 25.0f, 1000.0f, 3, {0.05f, 0.0f, 0.1f, 1.0f})};
        out.push_back(p);
    }
    {
        WorldPreset p;
        p.name = "Mountains";
        p.description = "Steep peaks with snow, pine forest below the tree line, one long river.";
        p.height = 140.0f;
        p.feature = 180.0f;
        p.erosion = 80;
        p.talus = 1.4f;
        p.rivers = 1;
        p.river_width = 4.0f;
        p.river_depth = 2.5f;
        p.ground[3] = glm::vec4(0.92f, 0.93f, 0.96f, 1);   // snow
        p.ground[0] = glm::vec4(0.40f, 0.38f, 0.36f, 1);   // rock
        p.foliage = {trees(FoliageKind::Conifer, 2.0f, 1), grass(40.0f, 9), rocks(1.0f, 13)};
        Biome rock = biome("Rock", glm::vec3(0.45f, 0.42f, 0.40f), 40.0f, 1000.0f, 0, {0.05f, 0.0f, 1.0f});
        rock.weight = 0.6f;
        Biome snow = biome("Snow", glm::vec3(0.95f, 0.95f, 1.0f), 95.0f, 1000.0f, 3, {0.0f, 0.0f, 0.3f});
        snow.weight = 3.0f;
        snow.atmosphere = true;
        snow.ambient_scale = 1.3f;
        snow.fog_scale = 2.0f;
        snow.fog_color = glm::vec3(0.75f, 0.78f, 0.85f);
        p.biomes = {biome("Valley", glm::vec3(0.50f, 0.75f, 0.35f), -1000.0f, 25.0f, 1, {0.3f, 1.0f, 0.3f}),
                    biome("Pine forest", glm::vec3(0.12f, 0.35f, 0.15f), 20.0f, 70.0f, 2, {1.0f, 0.3f, 0.3f}), rock, snow};
        out.push_back(p);
    }
    {
        WorldPreset p;
        p.name = "Islands";
        p.description = "Hilltops above the sea: sandy shores, woods inland.";
        p.height = 40.0f;
        p.feature = 110.0f;
        p.sea = true;
        p.sea_level = 0.45f;
        p.rivers = 0;
        p.ground[0] = glm::vec4(0.76f, 0.70f, 0.52f, 1);   // sand
        p.foliage = {trees(FoliageKind::Broadleaf, 1.5f, 5), grass(50.0f, 9), rocks(0.3f, 13)};
        p.biomes = {biome("Beach", glm::vec3(0.90f, 0.85f, 0.60f), -1000.0f, 0.45f * 40.0f + 1.5f, 0, {0.0f, 0.1f, 0.6f}),
                    biome("Woods", glm::vec3(0.20f, 0.50f, 0.20f), 0.45f * 40.0f + 1.0f, 1000.0f, 2, {1.0f, 1.0f, 0.3f})};
        out.push_back(p);
    }
    {
        WorldPreset p;
        p.name = "Plain";
        p.description = "Nearly flat ground with grass: room to build a level on.";
        p.height = 4.0f;
        p.feature = 200.0f;
        p.erosion = 0;
        p.rivers = 0;
        p.foliage = {grass(40.0f, 9)};
        p.biomes = {biome("Field", glm::vec3(0.55f, 0.80f, 0.35f), -1000.0f, 1000.0f, 1, {1.0f})};
        out.push_back(p);
    }
    return out;
}

/// What build_world made: the terrain, and the rivers' objects.
struct BuiltWorld {
    std::shared_ptr<Entity> terrain;
    std::vector<std::shared_ptr<Entity>> made;   // everything created, the terrain first
    int rivers = 0, lakes = 0;
};

/// Build a preset into `scene`. Each ground colour becomes a material file
/// under `materials_dir` (named after the preset); pass an empty directory to
/// leave the texture layers as they are.
inline BuiltWorld build_world(Scene& scene, const WorldPreset& p, uint32_t seed, const std::string& materials_dir) {
    BuiltWorld w;
    auto e = scene.CreateEntity(p.name);
    if (!e) return w;
    w.terrain = e;
    w.made.push_back(e);
    auto tc = e->AddComponent<TerrainComponent>(p.resolution, p.size);
    generate_terrain_noise(*tc, p.height, p.feature, seed);
    if (p.erosion > 0) erode_terrain(*tc, p.erosion, p.talus);
    if (p.sea) {
        tc->SetWaterEnabled(true);
        tc->SetWaterLevel(p.sea_level * p.height);
    }
    if (!materials_dir.empty()) {
        for (int i = 0; i < kTerrainLayers; ++i) {
            gws::assets::MaterialDesc md;
            std::string slug;
            for (char c : p.name) slug += (std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_');
            md.name = slug + "_ground" + std::to_string(i);
            md.base_color = p.ground[i];
            md.roughness = 0.92f;
            const std::string path = materials_dir + "/" + md.name + ".mat";
            if (gws::assets::save_material(path, md)) tc->SetLayerMaterial(i, path);
        }
    }

    // Rivers: from the highest points, each well away from the others.
    HydrologySettings hs;
    hs.has_sea = p.sea;
    hs.sea_level = tc->GetWaterLevel();
    std::vector<glm::vec2> springs;
    const int n = tc->VertsPerSide();
    const float half = tc->GetSize() * 0.5f, cell = tc->CellSize();
    for (int r = 0; r < p.rivers; ++r) {
        size_t best = 0;
        float  bh = -1e30f;
        for (size_t i = 0; i < tc->Heights().size(); ++i) {
            const glm::vec2 at(-half + static_cast<float>(i % n) * cell, -half + static_cast<float>(i / n) * cell);
            bool far_enough = std::fabs(at.x) < half * 0.85f && std::fabs(at.y) < half * 0.85f;   // not on the rim
            for (const glm::vec2& s : springs) far_enough = far_enough && glm::length(at - s) > tc->GetSize() * 0.3f;
            if (far_enough && tc->Heights()[i] > bh) {
                bh = tc->Heights()[i];
                best = i;
            }
        }
        if (bh < -1e29f) break;
        const glm::vec2 spring(-half + static_cast<float>(best % n) * cell, -half + static_cast<float>(best / n) * cell);
        springs.push_back(spring);
        WaterRun run = trace_water(*tc, spring.x, spring.y, hs);
        if (run.river.size() < 8) continue;
        carve_river(*tc, run, p.river_width, p.river_depth);
        auto river = scene.CreateEntity(p.name + " river " + std::to_string(++w.rivers));
        if (!river) continue;
        w.made.push_back(river);
        river->GetTransform()->SetWorldPosition(e->GetTransform()->GetWorldPosition());
        int k = 0;
        for (const WaterSurface& s : river_surfaces(run, p.river_width, p.river_depth * 0.7f, 0.6f, 240)) {
            auto part = scene.CreateEntity(river->GetName() + (s.lake ? " lake " : " water ") + std::to_string(++k));
            if (!part) continue;
            river->AddChild(part);
            part->GetTransform()->SetWorldPosition(e->GetTransform()->GetWorldPosition() + glm::vec3(s.x, s.level, s.z));
            if (auto wc = part->AddComponent<WaterComponent>()) {
                wc->SetSize(glm::vec2(s.size_x, s.size_z));
                wc->SetWaveHeight(s.lake ? 0.05f : 0.02f);
                wc->SetWaveScale(s.lake ? 8.0f : 3.0f);
                wc->SetClarity(s.lake ? 3.0f : 1.5f);
            }
            w.made.push_back(part);
            if (s.lake) ++w.lakes;
        }
    }

    // Foliage and biomes, the biomes seeing the rivers as water.
    auto fc = e->AddComponent<FoliageComponent>();
    fc->MutableLayers() = p.foliage;
    for (FoliageLayer& l : fc->MutableLayers()) l.seed += seed * 101u;
    auto bc = e->AddComponent<BiomeComponent>();
    bc->MutableBiomes() = p.biomes;
    std::vector<WaterRect> water;
    uint64_t key = 1;
    const glm::vec3 base = e->GetTransform()->GetWorldPosition();
    for (const auto& m : w.made)
        if (auto wc = m->GetComponent<WaterComponent>()) {
            const glm::vec3 c = m->GetTransform()->GetWorldPosition() - base;
            const glm::vec2 hs2 = wc->GetSize() * 0.5f;
            water.push_back(WaterRect{c.x - hs2.x, c.z - hs2.y, c.x + hs2.x, c.z + hs2.y, c.y});
            ++key;
        }
    apply_biomes(*tc, bc->Biomes(), bc->Map(*tc, water, key), fc.get());
    return w;
}

}  // namespace schizo::scene
