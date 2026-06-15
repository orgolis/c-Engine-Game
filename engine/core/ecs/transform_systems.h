#pragma once

// ====================
// Transform systems  (Master Plan Stage 1.3 step 4)
// ====================
//
// The first real system converted end-to-end and run THROUGH the scheduler:
// transform -> LocalToWorld propagation. Declares reads<Transform> /
// writes<LocalToWorld> so the scheduler knows its data access (and can run it
// in parallel with non-conflicting systems). The body itself fans out over the
// job system via parallel_each, so it is parallel both across systems and
// across entities.
//
//   SystemScheduler sched;
//   register_transform_systems(sched);
//   sched.run(world);   // writes LocalToWorld for every Transform

#include "world.h"
#include "components.h"
#include "parallel.h"
#include "system_scheduler.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace schizo::ecs {

// Compose a flat (non-hierarchical) world matrix from a local Transform.
inline glm::mat4 compose_local(const Transform& t) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t.position);
    m = m * glm::mat4_cast(t.rotation);
    m = glm::scale(m, t.scale);
    return m;
}

// Flat LocalToWorld system: world == local (no parenting). Hierarchical
// propagation is layered on top by a Parent component / the editor bridge.
inline void local_to_world_system(World& world, EntityCommandBuffer&) {
    parallel_each<Transform, LocalToWorld>(
        world, [](Entity, Transform& t, LocalToWorld& l) {
            l.value = compose_local(t);
        });
}

// Register the transform system(s) with a scheduler.
inline void register_transform_systems(SystemScheduler& sched) {
    sched.add("LocalToWorld", &local_to_world_system)
        .reads<Transform>()
        .writes<LocalToWorld>();
}

}  // namespace schizo::ecs
