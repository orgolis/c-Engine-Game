// ============================================================================
// componentsync_check — every scene component kind reaches the ECS (3.6).
//
// Four of thirteen kinds used to. A gameplay system could not see a light,
// could not tell whether an entity had a collider, and could not read a
// camera's FOV — so gameplay had to be written against the editor's object
// model, which is what "the ECS is a shadow" meant in practice.
//
// Two properties matter here and they pull against each other:
//   * every kind must ARRIVE, with its values, not just its presence; and
//   * a component must NOT appear on entities that do not have one, or an ECS
//     query for "everything with a light" returns the whole scene and the
//     feature is useless in a different way.
//
// The direction assertions are symmetric, as with the Transform flip: a tagged
// entity flows ECS -> OOP, and an untagged one must still flow OOP -> ECS. A
// change that quietly flipped everything would be far worse than one that did
// nothing.
// ============================================================================
#include "ecs_bridge.h"

#include "ecs/components.h"
#include "ecs/scene_components.h"
#include "ecs/world.h"

#include "audio_components.h"
#include "camera_component.h"
#include "collider_component.h"
#include "entity.h"
#include "light_component.h"
#include "npc_agent_component.h"
#include "particle_emitter_component.h"
#include "scene.h"
#include "script_component.h"
#include "skinned_mesh_component.h"
#include "terrain_component.h"
#include "transform.h"
#include "water_component.h"

#include <cmath>
#include <cstdint>
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

bool near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }

}  // namespace

int main() {
    std::printf("=== componentsync_check ===\n\n");

    auto scn = std::make_shared<scene::Scene>("sync");

    // One entity per kind, so "only entities that have one get one" is
    // actually testable — a single entity carrying everything cannot show it.
    auto lamp    = scn->CreateEntity("Lamp");
    auto crate   = scn->CreateEntity("Crate");
    auto cam     = scn->CreateEntity("Cam");
    auto pond    = scn->CreateEntity("Pond");
    auto ground  = scn->CreateEntity("Ground");
    auto scripted= scn->CreateEntity("Scripted");
    auto speaker = scn->CreateEntity("Speaker");
    auto fire    = scn->CreateEntity("Fire");
    auto guard   = scn->CreateEntity("Guard");
    auto plain   = scn->CreateEntity("Plain");     // has none of them

    auto lc = lamp->AddComponent<scene::LightComponent>();
    lc->SetIntensity(3.5f);
    lc->SetRange(42.0f);
    lc->SetColor(glm::vec3(1.0f, 0.5f, 0.25f));

    auto cc = crate->AddComponent<scene::ColliderComponent>();
    cc->SetRadius(1.75f);
    cc->SetHeight(3.5f);

    auto camc = cam->AddComponent<scene::CameraComponent>();
    camc->SetFOV(77.0f);
    camc->SetNearPlane(0.25f);

    auto wc = pond->AddComponent<scene::WaterComponent>();
    wc->SetWaveHeight(0.75f);
    wc->SetClarity(0.9f);

    auto tc = ground->AddComponent<scene::TerrainComponent>();
    tc->Resize(16, 320.0f);

    auto sc = scripted->AddComponent<scene::ScriptComponent>();
    sc->SetScriptPath("scripts/patrol.py");

    auto as = speaker->AddComponent<scene::AudioSourceComponent>();
    as->SetVolume(0.35f);
    as->SetPitch(1.5f);

    auto* pe = fire->GetParticleEmitterComponent();
    pe->enabled    = true;
    pe->spawn_rate = 37.5f;
    pe->color_end  = glm::vec4(0.2f, 0.05f, 0.0f, 0.0f);

    auto* na = guard->GetNpcAgentComponent();
    na->enabled     = true;
    na->sight_range = 42.0f;
    na->move_speed  = 6.5f;

    editor::EcsSceneBridge bridge;
    bridge.sync_and_run(scn);
    auto& w = bridge.world();

    auto ent_of = [&](const std::shared_ptr<scene::Entity>& s) {
        return static_cast<ecs::Entity>(bridge.ecs_entity_id(s->GetTransform()));
    };

    // ---- 1. every kind arrives, WITH ITS VALUES ----------------------------
    // Presence alone would pass for a bridge that adds default-constructed
    // components, which is worse than nothing: it looks wired and reads zero.
    {
        const auto* l = w.try_get<ecs::LightComponent>(ent_of(lamp));
        check(l && near(l->intensity, 3.5f) && near(l->range, 42.0f) &&
              near(l->color.r, 1.0f),
              "Light reaches the ECS with its intensity, range and colour");

        const auto* c = w.try_get<ecs::Collider>(ent_of(crate));
        check(c && near(c->radius, 1.75f) && near(c->height, 3.5f),
              "Collider, with its shape dimensions");

        const auto* cm = w.try_get<ecs::CameraComponent>(ent_of(cam));
        check(cm && near(cm->fov, 77.0f) && near(cm->near_plane, 0.25f),
              "Camera, with its lens");

        const auto* wa = w.try_get<ecs::WaterComponent>(ent_of(pond));
        check(wa && near(wa->wave_height, 0.75f) && near(wa->clarity, 0.9f),
              "Water, with its surface parameters");

        const auto* t = w.try_get<ecs::TerrainComponent>(ent_of(ground));
        check(t && near(t->size, 320.0f), "Terrain, with its extent");

        const auto* s = w.try_get<ecs::ScriptComponent>(ent_of(scripted));
        check(s && s->script_asset != 0ull && ecs::flag_get(s->flags, 0),
              "Script, as a hash of its path plus its enabled flag");

        const auto* a = w.try_get<ecs::AudioSource>(ent_of(speaker));
        check(a && near(a->volume, 0.35f) && near(a->pitch, 1.5f),
              "AudioSource, with volume and pitch");

        const auto* p = w.try_get<ecs::ParticleEmitter>(ent_of(fire));
        check(p && near(p->spawn_rate, 37.5f) && near(p->color_end.a, 0.0f),
              "ParticleEmitter, including a zero alpha that a lazy copy drops");

        const auto* n = w.try_get<ecs::NpcAgent>(ent_of(guard));
        check(n && near(n->sight_range, 42.0f) && near(n->move_speed, 6.5f),
              "NpcAgent, with its perception and movement");
    }

    // ---- 2. components do NOT appear where they do not belong --------------
    // The failure that makes the whole thing useless in the other direction:
    // an ECS query for "every light" returning the entire scene.
    {
        const auto pe_ = ent_of(plain);
        check(!w.has<ecs::LightComponent>(pe_) && !w.has<ecs::Collider>(pe_) &&
              !w.has<ecs::CameraComponent>(pe_) && !w.has<ecs::WaterComponent>(pe_),
              "an entity with none of them gets none of them");
        check(!w.has<ecs::ParticleEmitter>(pe_) && !w.has<ecs::NpcAgent>(pe_),
              "including the always-embedded ones, which are gated on enabled");
        check(!w.has<ecs::LightComponent>(ent_of(crate)),
              "and a collider entity does not acquire a light");
    }

    // ---- 3. the tag flips the direction, per kind --------------------------
    //
    // Every ECS read below is guarded. The first version of this dereferenced
    // try_get() directly, so disabling the sync did not fail the test -- it
    // SEGFAULTED, with no output at all. That is a strictly worse outcome than
    // a red assertion: the suite reports a non-zero exit and you learn nothing
    // about which of twenty properties broke.
    {
        const auto le = ent_of(lamp);
        w.tag<ecs::EcsAuthoritative>(le);
        auto* el = w.try_get<ecs::LightComponent>(le);
        check(el != nullptr, "the tagged lamp still has its ECS light to write to");
        if (el) el->intensity = 9.25f;
        bridge.sync_and_run(scn);
        check(near(lc->GetIntensity(), 9.25f),
              "a TAGGED entity's ECS light write reaches the OOP component");

        const auto ce = ent_of(cam);
        w.tag<ecs::EcsAuthoritative>(ce);
        auto* ec = w.try_get<ecs::CameraComponent>(ce);
        if (ec) ec->fov = 33.0f;
        bridge.sync_and_run(scn);
        check(ec && near(camc->GetFOV(), 33.0f), "and the same for a camera's FOV");

        const auto ge = ent_of(guard);
        w.tag<ecs::EcsAuthoritative>(ge);
        auto* en = w.try_get<ecs::NpcAgent>(ge);
        if (en) en->move_speed = 11.0f;
        bridge.sync_and_run(scn);
        check(en && near(na->move_speed, 11.0f), "and an agent's move speed");
    }

    // ---- 4. UNTAGGED entities are completely unaffected --------------------
    // The assertion protecting everything that already works.
    {
        const auto we = ent_of(pond);
        auto* ew = w.try_get<ecs::WaterComponent>(we);
        if (ew) ew->wave_height = 99.0f;
        bridge.sync_and_run(scn);
        check(near(wc->GetWaveHeight(), 0.75f),
              "an untagged entity IGNORES an ECS write (the scene still wins)");
        ew = w.try_get<ecs::WaterComponent>(we);
        check(ew && near(ew->wave_height, 0.75f),
              "and the ECS value is overwritten from the scene, as before");
    }

    // ---- 5. it survives repeated frames ------------------------------------
    // A value that lands once and is lost on frame two passes a single-step
    // test and fails in the editor.
    {
        for (int i = 0; i < 20; ++i) bridge.sync_and_run(scn);
        check(near(lc->GetIntensity(), 9.25f),
              "the flipped light holds across twenty more syncs");
        const auto* ecol = w.try_get<ecs::Collider>(ent_of(crate));
        check(ecol && near(ecol->radius, 1.75f),
              "and an untagged collider still mirrors correctly");
    }

    // ---- 6. untagging restores the old direction ----------------------------
    {
        const auto le = ent_of(lamp);
        w.remove<ecs::EcsAuthoritative>(le);
        lc->SetIntensity(2.0f);
        bridge.sync_and_run(scn);
        const auto* back = w.try_get<ecs::LightComponent>(le);
        check(back && near(back->intensity, 2.0f),
              "removing the tag hands the light back to the editor");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL componentsync_check\n");
    return g_fail ? 1 : 0;
}
