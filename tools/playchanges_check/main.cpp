// ============================================================================
// playchanges_check — play mode must not eat your tuning pass.
//
// Verifies editor/src/play_mode_changes.cpp headless: no GPU, no window, no
// ImGui. The module was written so this is possible — the diff is pure state
// comparison over the scene and the authorable-component registry, and the UI
// only renders what it returns.
//
// What is actually being defended here is a SAFETY property as much as a
// feature. Play mode writes to live state; the whole design rests on "the
// authored scene is untouched unless a row is explicitly ticked". A regression
// that flips the default the other way would silently corrupt scenes during
// ordinary play-testing, and would be very hard to notice by hand. So the
// keep == false path is tested as carefully as the keep == true path.
// ============================================================================
#include "play_mode_changes.h"
#include "ecs_bridge.h"

#include "scene.h"
#include "entity.h"
#include "transform.h"

#include "ecs/world.h"
#include "ecs/components.h"
#include "ecs/authorable_components.h"

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

// Find one change row by entity name + component, or null.
const editor::PlayChange* find(const editor::PlayChangeReport& r,
                               const std::string& entity, const std::string& comp) {
    for (const auto& c : r.changes)
        if (c.entity_name == entity && c.component == comp) return &c;
    return nullptr;
}

ecs::Health* health_of(editor::EcsSceneBridge& bridge, scene::Entity* e) {
    const uint32_t id = bridge.ecs_entity_id(e->GetTransform());
    if (id == editor::kNoEcsEntity) return nullptr;
    return bridge.world().try_get<ecs::Health>(static_cast<ecs::Entity>(id));
}

}  // namespace

int main() {
    std::printf("=== playchanges_check ===\n\n");

    auto scn = std::make_shared<scene::Scene>("test");
    auto a   = scn->CreateEntity("Boss");
    auto b   = scn->CreateEntity("Crate");
    a->GetTransform()->SetLocalPosition({0.0f, 0.0f, 0.0f});
    b->GetTransform()->SetLocalPosition({5.0f, 0.0f, 0.0f});

    editor::EcsSceneBridge bridge;
    bridge.sync_and_run(scn);                    // creates the ECS entities

    // Give the boss a Health component to tune, as a game would.
    {
        const uint32_t id = bridge.ecs_entity_id(a->GetTransform());
        if (id == editor::kNoEcsEntity) {
            std::printf("  FAIL bridge did not map the entity — cannot continue\n");
            return 1;
        }
        bridge.world().add<ecs::Health>(static_cast<ecs::Entity>(id), ecs::Health{});
        auto* h = health_of(bridge, a.get());
        h->current = 100.0f;
        h->max     = 100.0f;
    }

    // ---- 1. a quiet play session reports nothing ---------------------------
    // The most important negative: if an idle play session produced rows, the
    // dialog would appear constantly and be trained away as noise.
    {
        editor::PlayModeChanges pc;
        pc.Capture(scn, &bridge);
        const auto r = pc.Diff(scn, &bridge);
        check(r.empty(), "no changes -> empty report");
        check(r.changes.empty(), "no spurious rows from an unchanged scene");
    }

    // ---- 2. transform edits are detected -----------------------------------
    {
        editor::PlayModeChanges pc;
        pc.Capture(scn, &bridge);
        b->GetTransform()->SetLocalPosition({5.0f, 3.5f, 0.0f});    // play moved it

        const auto r = pc.Diff(scn, &bridge);
        const auto* row = find(r, "Crate", "Transform");
        check(row != nullptr, "moved entity produces a Transform row");
        check(row && row->summary.find("moved") != std::string::npos,
              "transform summary says what happened");
        check(row && !row->keep, "rows default to keep == false (discard is the default)");
        check(find(r, "Boss", "Transform") == nullptr,
              "untouched entity produces no row");

        b->GetTransform()->SetLocalPosition({5.0f, 0.0f, 0.0f});    // restore
    }

    // ---- 3. component values are diffed field by field ---------------------
    {
        editor::PlayModeChanges pc;
        pc.Capture(scn, &bridge);
        health_of(bridge, a.get())->current = 42.0f;                 // tuned in play

        const auto r = pc.Diff(scn, &bridge);
        const auto* row = find(r, "Boss", "Health");
        check(row != nullptr, "changed component produces a row");
        check(row && row->summary.find("current") != std::string::npos,
              "summary names the field that changed");
        check(row && row->summary.find("100") != std::string::npos &&
                     row->summary.find("42")  != std::string::npos,
              "summary shows before -> after values");
    }

    // ---- 4. THE SAFETY PROPERTY: unticked rows change nothing ---------------
    {
        editor::PlayModeChanges pc;
        pc.Capture(scn, &bridge);
        health_of(bridge, a.get())->current = 7.0f;
        b->GetTransform()->SetLocalPosition({99.0f, 0.0f, 0.0f});
        auto r = pc.Diff(scn, &bridge);

        // Simulate the restore StopPlayback performs, then apply with nothing ticked.
        health_of(bridge, a.get())->current = 42.0f;
        b->GetTransform()->SetLocalPosition({5.0f, 0.0f, 0.0f});

        const size_t n = editor::PlayModeChanges::Apply(r.changes, scn, &bridge);
        check(n == 0, "Apply with no rows ticked applies nothing");
        check(std::fabs(health_of(bridge, a.get())->current - 42.0f) < 1e-4f,
              "unticked component keeps its authored value");
        check(std::fabs(b->GetTransform()->GetLocalPosition().x - 5.0f) < 1e-4f,
              "unticked transform keeps its authored value");
    }

    // ---- 5. ticked rows are applied ----------------------------------------
    {
        editor::PlayModeChanges pc;
        pc.Capture(scn, &bridge);
        health_of(bridge, a.get())->current = 63.0f;
        b->GetTransform()->SetLocalPosition({1.0f, 2.0f, 3.0f});
        auto r = pc.Diff(scn, &bridge);

        health_of(bridge, a.get())->current = 42.0f;                 // the restore
        b->GetTransform()->SetLocalPosition({5.0f, 0.0f, 0.0f});

        for (auto& c : r.changes) c.keep = true;
        const size_t n = editor::PlayModeChanges::Apply(r.changes, scn, &bridge);
        check(n == r.changes.size(), "Apply reports every ticked row");
        check(std::fabs(health_of(bridge, a.get())->current - 63.0f) < 1e-4f,
              "kept component value survives into the authored scene");
        const auto p = b->GetTransform()->GetLocalPosition();
        check(std::fabs(p.x - 1.0f) < 1e-4f && std::fabs(p.y - 2.0f) < 1e-4f,
              "kept transform survives into the authored scene");
    }

    // ---- 6. selective keep: one row in, one row out ------------------------
    // The reason the feature exists at all — an all-or-nothing keep would be no
    // better than the all-or-nothing discard it replaces.
    {
        editor::PlayModeChanges pc;
        pc.Capture(scn, &bridge);
        health_of(bridge, a.get())->current = 11.0f;
        b->GetTransform()->SetLocalPosition({8.0f, 8.0f, 8.0f});
        auto r = pc.Diff(scn, &bridge);

        health_of(bridge, a.get())->current = 63.0f;                 // the restore
        b->GetTransform()->SetLocalPosition({1.0f, 2.0f, 3.0f});

        for (auto& c : r.changes) c.keep = (c.component == "Health");
        editor::PlayModeChanges::Apply(r.changes, scn, &bridge);
        check(std::fabs(health_of(bridge, a.get())->current - 11.0f) < 1e-4f,
              "ticked row applied");
        check(std::fabs(b->GetTransform()->GetLocalPosition().x - 1.0f) < 1e-4f,
              "unticked row in the same batch NOT applied");
    }

    // ---- 7. components added / removed during play -------------------------
    {
        editor::PlayModeChanges pc;
        pc.Capture(scn, &bridge);
        const uint32_t bid = bridge.ecs_entity_id(b->GetTransform());
        bridge.world().add<ecs::Health>(static_cast<ecs::Entity>(bid), ecs::Health{});

        const auto r = pc.Diff(scn, &bridge);
        const auto* row = find(r, "Crate", "Health");
        check(row != nullptr, "component added during play is reported");
        check(row && row->summary.find("added") != std::string::npos,
              "added component says so");
    }
    {
        editor::PlayModeChanges pc;
        pc.Capture(scn, &bridge);                                    // Crate now HAS Health
        const uint32_t bid = bridge.ecs_entity_id(b->GetTransform());
        bridge.world().remove<ecs::Health>(static_cast<ecs::Entity>(bid));

        const auto r = pc.Diff(scn, &bridge);
        const auto* row = find(r, "Crate", "Health");
        check(row != nullptr, "component removed during play is reported");
        check(row && row->bytes.empty(), "removal row carries no payload");
    }

    // ---- 8. structural changes are counted, not silently dropped ------------
    // These cannot be applied as value edits. The contract is that they are
    // surfaced anyway, so the developer is never told "nothing changed" when
    // something did.
    {
        editor::PlayModeChanges pc;
        pc.Capture(scn, &bridge);
        auto spawned = scn->CreateEntity("Projectile");             // play spawned it
        (void)spawned;

        const auto r = pc.Diff(scn, &bridge);
        check(r.entities_spawned == 1, "entity spawned during play is counted");
        check(!r.empty(), "a spawn-only session still reports as non-empty");
        check(find(r, "Projectile", "Transform") == nullptr,
              "spawned entity is not offered as a keepable value row");
    }

    // ---- 9. no baseline -> no report ---------------------------------------
    // Diffing without Capture must be inert rather than treating everything as
    // new, which would offer to overwrite the whole scene.
    {
        editor::PlayModeChanges pc;
        const auto r = pc.Diff(scn, &bridge);
        check(r.empty(), "Diff without Capture returns an empty report");
        check(!pc.has_baseline(), "has_baseline() is false before Capture");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL playchanges_check\n");
    return g_fail ? 1 : 0;
}
