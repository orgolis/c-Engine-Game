// ============================================================================
// ecsauthority_check — an ECS write to a Transform must survive the frame.
//
// The bridge mirrors the OOP scene into the ECS every frame. That single copy
// is why gameplay cannot move anything on its own: a system writes an ECS
// Transform, the next sync overwrites it from the OOP scene, and the write
// vanishes with nothing reporting an error. Everything has to go through the
// editor's OOP transform instead, which is what "the ECS is a shadow" means in
// practice.
//
// Phase 3.6 flips that. This covers the per-entity form of the flip, and the
// assertions are deliberately symmetric: the tagged entity must sync ECS -> OOP,
// and — just as important — the untagged one must still sync OOP -> ECS. A flip
// that quietly changed behaviour for everything would be a much worse bug than
// one that did nothing.
// ============================================================================
#include "ecs_bridge.h"

#include "ecs/components.h"
#include "ecs/world.h"
#include "entity.h"
#include "scene.h"
#include "transform.h"

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

bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return glm::length(a - b) < eps;
}

}  // namespace

int main() {
    std::printf("=== ecsauthority_check ===\n\n");

    auto scn = std::make_shared<scene::Scene>("t");
    auto shadowed = scn->CreateEntity("Shadowed");     // default: OOP authoritative
    auto owned    = scn->CreateEntity("EcsOwned");     // will be tagged

    shadowed->GetTransform()->SetLocalPosition({1.0f, 0.0f, 0.0f});
    owned->GetTransform()->SetLocalPosition({2.0f, 0.0f, 0.0f});

    editor::EcsSceneBridge bridge;
    bridge.sync_and_run(scn);

    const uint32_t sid = bridge.ecs_entity_id(shadowed->GetTransform());
    const uint32_t oid = bridge.ecs_entity_id(owned->GetTransform());
    check(sid != editor::kNoEcsEntity && oid != editor::kNoEcsEntity,
          "both entities exist in the ECS after a sync");

    auto& w = bridge.world();
    const auto se = static_cast<ecs::Entity>(sid);
    const auto oe = static_cast<ecs::Entity>(oid);

    // ---- 1. the default is unchanged: OOP wins -----------------------------
    // This is the assertion protecting everything that already works.
    {
        w.try_get<ecs::Transform>(se)->position = glm::vec3(99.0f, 0.0f, 0.0f);
        bridge.sync_and_run(scn);
        check(near(shadowed->GetTransform()->GetLocalPosition(), {1.0f, 0.0f, 0.0f}),
              "an untagged entity IGNORES an ECS write (the OOP scene still wins)");
        check(near(w.try_get<ecs::Transform>(se)->position, {1.0f, 0.0f, 0.0f}),
              "and the ECS value is overwritten from the scene, as before");
    }

    // ---- 2. THE FLIP: a tagged entity syncs the other way ------------------
    {
        w.tag<ecs::EcsAuthoritative>(oe);
        w.try_get<ecs::Transform>(oe)->position = glm::vec3(50.0f, 5.0f, -3.0f);
        bridge.sync_and_run(scn);

        check(near(owned->GetTransform()->GetLocalPosition(), {50.0f, 5.0f, -3.0f}),
              "a TAGGED entity's ECS write reaches the OOP transform");
        check(near(w.try_get<ecs::Transform>(oe)->position, {50.0f, 5.0f, -3.0f}),
              "and is not clobbered by the sync that used to overwrite it");
    }

    // ---- 3. it survives repeated frames ------------------------------------
    // A write that lands once and is lost on frame two would look like it
    // worked in a single-step test and fail in the editor.
    {
        for (int i = 0; i < 10; ++i) bridge.sync_and_run(scn);
        check(near(owned->GetTransform()->GetLocalPosition(), {50.0f, 5.0f, -3.0f}),
              "the value holds across ten more syncs, not just the first");
    }

    // ---- 4. rotation and scale flip too, not only position -----------------
    {
        auto* t = w.try_get<ecs::Transform>(oe);
        t->scale = glm::vec3(2.0f, 3.0f, 4.0f);
        t->rotation = glm::angleAxis(1.0f, glm::vec3(0, 1, 0));
        bridge.sync_and_run(scn);
        check(near(owned->GetTransform()->GetLocalScale(), {2.0f, 3.0f, 4.0f}),
              "scale flows ECS -> OOP as well");
        check(std::abs(glm::dot(owned->GetTransform()->GetLocalRotation(), t->rotation)) > 0.999f,
              "and rotation");
    }

    // ---- 5. untagging restores the old direction ---------------------------
    // The flip must be reversible, or an entity could never be handed back to
    // the editor.
    {
        w.remove<ecs::EcsAuthoritative>(oe);
        owned->GetTransform()->SetLocalPosition({7.0f, 0.0f, 0.0f});
        bridge.sync_and_run(scn);
        check(near(w.try_get<ecs::Transform>(oe)->position, {7.0f, 0.0f, 0.0f}),
              "removing the tag returns the entity to OOP authority");
    }

    // ---- 6. the two entities are independent -------------------------------
    // A per-entity flip that leaked into its neighbours would be worse than a
    // global one, because it would be intermittent.
    {
        w.tag<ecs::EcsAuthoritative>(oe);
        w.try_get<ecs::Transform>(oe)->position = glm::vec3(11.0f, 0.0f, 0.0f);
        w.try_get<ecs::Transform>(se)->position = glm::vec3(22.0f, 0.0f, 0.0f);
        bridge.sync_and_run(scn);
        check(near(owned->GetTransform()->GetLocalPosition(), {11.0f, 0.0f, 0.0f}),
              "the tagged entity follows its ECS transform");
        check(near(shadowed->GetTransform()->GetLocalPosition(), {1.0f, 0.0f, 0.0f}),
              "while its untagged neighbour is completely unaffected");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL ecsauthority_check\n");
    return g_fail ? 1 : 0;
}
