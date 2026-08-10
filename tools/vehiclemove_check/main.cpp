// ============================================================================
// vehiclemove_check — driving a vehicle actually moves it in the scene.
//
// This is the ECS authority flip seen from the outside, on the one system that
// was genuinely broken by its absence. `drive_vehicle` integrates position and
// heading on the entity's ECS Transform every frame, and the editor exposes it
// to scripts as `drive(entity, throttle, steer, ...)`. Before the flip, the
// bridge copied the OOP scene over that result the same frame: a user's script
// called drive, the vehicle's speed climbed, the heading turned, and the thing
// on screen did not move an inch. No error, no warning, nothing to grep for.
//
// So the assertions here are about the OUTCOME rather than the tag: the scene
// transform must end up where the drive model says it should be. A test that
// only asserted "the tag is present" would still pass if the sync direction
// were wrong.
// ============================================================================
#include "ecs_bridge.h"

#include "ecs/components.h"
#include "ecs/gameplay_vehicles.h"
#include "ecs/world.h"
#include "entity.h"
#include "scene.h"
#include "transform.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

}  // namespace

int main() {
    std::printf("=== vehiclemove_check ===\n\n");

    // A car registered the way the editor registers one.
    ecs::VehicleDef def;
    def.id          = "testcar";
    def.accel       = 20.0f;
    def.max_speed   = 30.0f;
    def.turn_rate   = 90.0f;
    def.drag        = 5.0f;
    def.brake_decel = 40.0f;
    ecs::register_vehicle(def);

    auto scn = std::make_shared<scene::Scene>("t");
    auto car    = scn->CreateEntity("Car");
    auto crate  = scn->CreateEntity("Crate");     // a plain prop, must not be affected
    car->GetTransform()->SetLocalPosition({0.0f, 0.0f, 0.0f});
    crate->GetTransform()->SetLocalPosition({-5.0f, 0.0f, 0.0f});

    editor::EcsSceneBridge bridge;
    bridge.sync_and_run(scn);

    auto& w  = bridge.world();
    const auto ce = static_cast<ecs::Entity>(bridge.ecs_entity_id(car->GetTransform()));
    const auto xe = static_cast<ecs::Entity>(bridge.ecs_entity_id(crate->GetTransform()));

    ecs::Vehicle veh;
    veh.vehicle_id = "testcar";
    w.add<ecs::Vehicle>(ce, veh);

    // ---- 1. the bug this exists to prevent ---------------------------------
    // Drive forward for one simulated second, syncing every frame exactly as
    // the editor does. The car must have MOVED in the scene, not just in the
    // ECS -- moving only in the ECS is precisely the old broken behaviour.
    {
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 60; ++i) {
            ecs::drive_vehicle(w, ce, {1.0f, 0.0f, false, false}, dt);
            bridge.sync_and_run(scn);          // the frame that used to undo it
        }

        const glm::vec3 p = car->GetTransform()->GetLocalPosition();
        check(p.z > 1.0f, "a driven vehicle MOVES in the scene, not only in the ECS");
        check(std::abs(p.x) < 1e-3f && std::abs(p.y) < 1e-3f,
              "and it moves along its heading rather than drifting");

        const auto* t = w.try_get<ecs::Transform>(ce);
        check(std::abs(t->position.z - p.z) < 1e-4f,
              "the ECS transform and the scene transform agree afterwards");
        check(w.try_get<ecs::Vehicle>(ce)->speed > 1.0f,
              "the drive model itself ran (speed accumulated)");
    }

    // ---- 2. authority is claimed by driving, not by the caller -------------
    // The point of putting the claim inside drive_vehicle: a caller that never
    // heard of EcsAuthoritative still gets a vehicle that moves.
    {
        check(w.has<ecs::EcsAuthoritative>(ce),
              "driving claimed transform authority without the caller asking");
        check(!w.has<ecs::EcsAuthoritative>(xe),
              "and claimed it ONLY for the vehicle, not for every entity");
    }

    // ---- 3. the untouched prop is untouched --------------------------------
    // A flip that leaked would show up here as a crate that teleported.
    {
        check(glm::length(crate->GetTransform()->GetLocalPosition() -
                          glm::vec3(-5.0f, 0.0f, 0.0f)) < 1e-4f,
              "a non-vehicle entity is exactly where it was left");
    }

    // ---- 4. steering reaches the scene rotation ----------------------------
    // Position is the obvious half; a vehicle whose heading never reached the
    // renderer would drive sideways forever.
    {
        const glm::quat before = car->GetTransform()->GetLocalRotation();
        for (int i = 0; i < 30; ++i) {
            ecs::drive_vehicle(w, ce, {1.0f, 1.0f, false, false}, 1.0f / 60.0f);
            bridge.sync_and_run(scn);
        }
        const glm::quat after = car->GetTransform()->GetLocalRotation();
        check(std::abs(glm::dot(before, after)) < 0.999f,
              "steering changes the scene's rotation too, not just position");

        const auto* t = w.try_get<ecs::Transform>(ce);
        check(std::abs(std::abs(glm::dot(after, t->rotation)) - 1.0f) < 1e-3f,
              "and the scene rotation matches the drive model's heading");
    }

    // ---- 5. the editor can still take the wheel back -----------------------
    // Dragging a driven vehicle with the gizmo has to work, or the flip has
    // made the editor worse. Untagging hands it back.
    {
        w.remove<ecs::EcsAuthoritative>(ce);
        car->GetTransform()->SetLocalPosition({100.0f, 2.0f, 3.0f});
        bridge.sync_and_run(scn);
        const auto* t = w.try_get<ecs::Transform>(ce);
        check(std::abs(t->position.x - 100.0f) < 1e-4f,
              "releasing authority lets the editor move the vehicle again");
    }

    // ---- 6. and driving reclaims it ----------------------------------------
    {
        ecs::drive_vehicle(w, ce, {1.0f, 0.0f, false, false}, 1.0f / 60.0f);
        bridge.sync_and_run(scn);
        check(w.has<ecs::EcsAuthoritative>(ce),
              "driving again reclaims authority — the handover works both ways");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL vehiclemove_check\n");
    return g_fail ? 1 : 0;
}
