#pragma once

// ====================
// ECS draw submission  (Master Plan Stage 1.4 acceptance)
// ====================
//
// Build a draw list from the ECS in a SINGLE parallel pass: query every entity
// with LocalToWorld + MeshRenderer, and write one DrawRecord per entity into a
// pre-sized output (each task writes only its own slot — no shared mutation, no
// locks). This is the data-oriented draw-submission the OOP scene migrates onto:
// the renderer consumes DrawRecords instead of walking GameObject pointers.
//
//   std::vector<DrawRecord> draws;
//   collect_draws(world, draws);   // 100k entities -> one parallel_for pass

#include "world.h"
#include "components.h"
#include "parallel.h"
#include "jobs/job_system.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace schizo::ecs {

// Minimal per-draw record produced from the ECS each frame. (The renderer maps
// mesh/material ids to GPU resources; this struct is the data-oriented hand-off.)
struct DrawRecord {
    glm::mat4 model{1.0f};
    uint64_t  mesh     = 0;
    int32_t   material = -1;
    uint32_t  flags    = 0;
};

// Single-pass parallel draw collection. Returns the number of draws written.
inline size_t collect_draws(World& world, std::vector<DrawRecord>& out) {
    auto view = world.view<LocalToWorld, MeshRenderer>();
    std::vector<Entity> ents(view.begin(), view.end());
    out.resize(ents.size());

    World* w = &world;
    DrawRecord* dst = out.data();
    const Entity* src = ents.data();
    auto body = [w, dst, src](size_t i) {
        const Entity e = src[i];
        const LocalToWorld& l = w->get<LocalToWorld>(e);
        const MeshRenderer& m = w->get<MeshRenderer>(e);
        // Skip hidden (bit2) by emitting an invalid mesh id is wrong (would
        // shift indices); instead keep the slot but flag it. Renderer filters.
        dst[i] = DrawRecord{l.value, m.mesh, m.material, m.flags};
    };

    auto& js = gws::jobs::JobSystem::instance();
    if (js.initialized() && !ents.empty())
        js.parallel_for(0, ents.size(), body);
    else
        for (size_t i = 0; i < ents.size(); ++i) body(i);

    return out.size();
}

}  // namespace schizo::ecs
