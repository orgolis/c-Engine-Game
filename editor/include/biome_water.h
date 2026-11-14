#pragma once
// ============================================================================
// biome_water -- the standing water a terrain's biome map treats as wet:
// every active WaterComponent over the terrain (lakes and rivers made by the
// water tool, or placed by hand), as rectangles in the terrain's local space.
// ============================================================================

#include "biome.h"
#include "entity.h"
#include "scene.h"
#include "transform.h"
#include "water_component.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace schizo::editor {

/// The water over `terrain`, and a key that changes when any of it changes.
inline std::vector<schizo::scene::WaterRect> biome_water_for(schizo::scene::Scene& scene, const schizo::scene::Entity& terrain,
                                                            uint64_t& key) {
    std::vector<schizo::scene::WaterRect> out;
    key = 1469598103934665603ull;
    auto tc = terrain.GetComponent<schizo::scene::TerrainComponent>();
    if (!tc) return out;
    const glm::vec3 base = terrain.GetTransform()->GetWorldPosition();
    const float     half = tc->GetSize() * 0.5f;
    for (const auto& e : scene.EntitiesWithAny<schizo::scene::WaterComponent>()) {
        if (!e || !e->IsActiveInHierarchy()) continue;
        auto wc = e->GetComponent<schizo::scene::WaterComponent>();
        if (!wc) continue;
        const glm::vec3 p = e->GetTransform()->GetWorldPosition() - base;
        auto add = [&](const schizo::scene::WaterRect& r) {
            if (r.max_x < -half || r.min_x > half || r.max_z < -half || r.min_z > half) return;
            out.push_back(r);
            uint64_t bits[3];
            static_assert(sizeof(float) * 5 <= sizeof(bits), "");
            std::memset(bits, 0, sizeof bits);
            std::memcpy(bits, &r, sizeof r);
            for (uint64_t b : bits) key = (key ^ b) * 1099511628211ull;
        };
        if (wc->IsRiver()) {
            // A river: a small rect round each point of its path.
            for (const glm::vec4& q : wc->GetPath())
                add(schizo::scene::WaterRect{p.x + q.x - q.w, p.z + q.y - q.w, p.x + q.x + q.w, p.z + q.y + q.w, p.y + q.z});
            continue;
        }
        const glm::vec2 s = wc->GetSize() * 0.5f;
        add(schizo::scene::WaterRect{p.x - s.x, p.z - s.y, p.x + s.x, p.z + s.y, p.y});
    }
    return out;
}

}  // namespace schizo::editor
