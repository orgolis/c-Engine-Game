#pragma once

// Job-parallel ECS iteration (Master Plan Stage 1.3, lightweight). Runs a
// system body over every entity matching <Ts...> across the job system.
//
// CONTRACT: the body may READ/WRITE the components of the entity it is given,
// but must NOT make structural changes (create/destroy/add/remove) — those
// go through a deferred command buffer (added with the full scheduler). This
// keeps concurrent iteration safe: different entities touch different memory.

#include "world.h"
#include "jobs/job_system.h"

#include <vector>

namespace schizo::ecs {

template <typename... Ts, typename Fn>
void parallel_each(World& world, Fn fn, size_t grain = 0) {
    auto& js   = gws::jobs::JobSystem::instance();
    auto  view = world.view<Ts...>();

    if (!js.initialized()) {  // no pool -> run serially
        view.each([&](Entity e, Ts&... cs) { fn(e, cs...); });
        return;
    }

    // Snapshot the matching entities, then fan out. (A storage-direct split
    // avoids this copy; this is the simple correct v1.)
    std::vector<Entity> entities(view.begin(), view.end());
    World* w = &world;
    js.parallel_for(0, entities.size(), [w, &fn, &entities](size_t i) {
        Entity e = entities[i];
        fn(e, w->get<Ts>(e)...);
    }, grain);
}

}  // namespace schizo::ecs
