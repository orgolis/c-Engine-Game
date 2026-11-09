// ============================================================================
// componentstamp_check — every change to an entity's components must be seen
// by the change-driven ECS mirror.
//
// The mirror skips an entity whose Transform and content stamps predate the
// last sync (Transform::ChangeStamp, Entity::ContentStamp). That is only safe
// if EVERY way of changing component data stamps the entity. A missed one is
// silent: the ECS keeps the old value, and a gameplay system reads a light,
// collider or emitter that is no longer what the scene says.
//
// Two halves:
//   A. Source: every mutating member of every component class calls
//      MarkChanged(). New setters are held to it automatically.
//   B. Behaviour: in a scene big enough for the mirror's parallel (skipping)
//      path, change one entity through each component kind -- setters, the
//      embedded structs through their accessors, Entity::SetMesh, adding a
//      component -- sync once, and the ECS must hold the new value. Entities
//      nobody touched must not be stamped by a sync.
// ============================================================================
#include "ecs_bridge.h"

#include "ecs/components.h"
#include "ecs/scene_components.h"
#include "ecs/world.h"

#include "audio_components.h"
#include "camera_component.h"
#include "collider_component.h"
#include "entity.h"
#include "jobs/job_system.h"
#include "light_component.h"
#include "mesh_renderer_component.h"
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
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace schizo;
namespace fs = std::filesystem;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }

fs::path find_repo_root(const fs::path& exe_dir) {
    std::error_code ec;
    for (fs::path d = exe_dir; !d.empty(); d = d.parent_path()) {
        if (fs::is_directory(d / "engine" / "scene" / "include", ec) &&
            fs::is_directory(d / "tools", ec))
            return d;
        if (d == d.parent_path()) break;
    }
    return {};
}

// Part A. A member defined inline in a component header, not const, not a
// getter/query/hook, must contain MarkChanged() in its body.
void source_part(const fs::path& root) {
    const char* headers[] = {
        "audio_components.h", "camera_component.h", "collider_component.h",
        "light_component.h", "mesh_renderer_component.h", "script_component.h",
        "terrain_component.h", "water_component.h", "transform_component.h",
    };
    const std::regex sig(
        R"(^\s+(?:virtual\s+)?(?:[A-Za-z_:<>]+(?:<[^>]*>)?\s*&?\s+)([A-Za-z_]+)\(([^)]*)\)\s*(const\s*)?(?:override\s*)?\{)");
    const char* exempt[] = {"OnAttach", "OnDetach", "OnEnable", "OnDisable", "OnUpdate",
                            "OnFixedUpdate", "OnLateUpdate",
                            // Hands the renderer the pending dirty rect; a
                            // read-and-clear of a work queue, not content.
                            "ConsumeDirtyRect"};
    size_t mutators = 0;
    std::vector<std::string> missing;
    for (const char* h : headers) {
        std::ifstream in(root / "engine" / "scene" / "include" / h);
        if (!in) { check(false, std::string(h) + ": cannot be read"); continue; }
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        std::istringstream lines(text);
        std::string line;
        size_t offset = 0;
        while (std::getline(lines, line)) {
            const size_t line_start = offset;
            offset += line.size() + 1;
            std::smatch m;
            if (!std::regex_search(line, m, sig)) continue;
            const std::string name = m[1];
            if (m[3].matched) continue;                                   // const member
            bool skip = name.rfind("Get", 0) == 0 || name.rfind("Is", 0) == 0 ||
                        name.rfind("Has", 0) == 0;
            for (const char* e : exempt) skip = skip || name == e;
            if (text.find("class " + name + " ") != std::string::npos ||
                text.find("class " + name + "\n") != std::string::npos) skip = true;   // ctor
            if (skip) continue;
            // Body: from this line's '{' to its matching '}'.
            size_t b = line_start + line.find('{');
            int depth = 0;
            size_t e = b;
            for (; e < text.size(); ++e) {
                if (text[e] == '{') ++depth;
                else if (text[e] == '}' && --depth == 0) break;
            }
            ++mutators;
            if (text.substr(b, e - b).find("MarkChanged()") == std::string::npos)
                missing.push_back(std::string(h) + ": " + name);
        }
    }
    for (const auto& s : missing) std::printf("       missing MarkChanged(): %s\n", s.c_str());
    check(mutators > 100, "found the component mutators (" + std::to_string(mutators) + ")");
    check(missing.empty(), "every one of them calls MarkChanged() (" +
                           std::to_string(missing.size()) + " missing)");
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("=== componentstamp_check ===\n\n");
    (void)argc;

    std::printf("A. Source: every component mutator stamps its entity:\n");
    const fs::path root = find_repo_root(fs::absolute(fs::path(argv[0])).parent_path());
    if (root.empty()) std::printf("  SKIP source half: needs a source checkout\n");
    else              source_part(root);

    std::printf("\nB. Behaviour on the mirror's skipping path:\n");
    gws::jobs::JobSystem::instance().init();
    auto scn = std::make_shared<scene::Scene>("stamps");
    // Filler: enough entities that the mirror takes its parallel path, which
    // is the only one that skips.
    std::vector<std::shared_ptr<scene::Entity>> filler;
    for (int i = 0; i < 1500; ++i) {
        auto f = scn->CreateEntity("F" + std::to_string(i));
        f->AddComponent<scene::MeshRendererComponent>();
        filler.push_back(f);
    }
    auto lamp    = scn->CreateEntity("Lamp");
    auto crate   = scn->CreateEntity("Crate");
    auto cam     = scn->CreateEntity("Cam");
    auto pond    = scn->CreateEntity("Pond");
    auto ground  = scn->CreateEntity("Ground");
    auto scripted= scn->CreateEntity("Scripted");
    auto speaker = scn->CreateEntity("Speaker");
    auto fire    = scn->CreateEntity("Fire");
    auto guard   = scn->CreateEntity("Guard");
    auto shape   = scn->CreateEntity("Shape");
    auto model   = scn->CreateEntity("Model");
    auto late    = scn->CreateEntity("Late");

    auto lc   = lamp->AddComponent<scene::LightComponent>();
    auto cc   = crate->AddComponent<scene::ColliderComponent>();
    auto camc = cam->AddComponent<scene::CameraComponent>();
    auto wc   = pond->AddComponent<scene::WaterComponent>();
    auto tc   = ground->AddComponent<scene::TerrainComponent>();
    tc->Resize(16, 100.0f);
    auto sc   = scripted->AddComponent<scene::ScriptComponent>();
    auto as   = speaker->AddComponent<scene::AudioSourceComponent>();
    fire->GetParticleEmitterComponent()->enabled = true;
    guard->GetNpcAgentComponent()->enabled = true;
    auto mr   = shape->AddComponent<scene::MeshRendererComponent>();

    editor::EcsSceneBridge br;
    br.sync_and_run(scn);
    br.sync_and_run(scn);                 // a still sync: everything skippable now
    auto& w = br.world();
    auto ent_of = [&](const std::shared_ptr<scene::Entity>& s) {
        return static_cast<ecs::Entity>(br.ecs_entity_id(s->GetTransform()));
    };

    const uint64_t before = scene::Transform::ChangeStampCounter();
    br.sync_and_run(scn);
    bool untouched = true;
    for (const auto& f : filler) untouched = untouched && f->ContentStamp() < before;
    check(untouched, "a sync does not stamp the entities it reads");

    // One change per kind, then a single sync.
    lc->SetIntensity(9.25f);
    cc->SetRadius(2.5f);
    camc->SetFOV(33.0f);
    wc->SetWaveHeight(1.25f);
    tc->Resize(16, 222.0f);
    sc->SetScriptPath("scripts/other.py");
    as->SetVolume(0.125f);
    fire->GetParticleEmitterComponent()->spawn_rate = 81.0f;
    guard->GetNpcAgentComponent()->sight_range = 17.0f;
    mr->SetMeshType(scene::MeshType::Sphere);
    model->SetMesh("models/thing.glb");
    late->AddComponent<scene::LightComponent>()->SetIntensity(4.5f);
    const uint64_t script_before = [&] {
        const auto* s = w.try_get<ecs::ScriptComponent>(ent_of(scripted));
        return s ? s->script_asset : 0ull;
    }();
    br.sync_and_run(scn);

    const auto* l = w.try_get<ecs::LightComponent>(ent_of(lamp));
    check(l && near(l->intensity, 9.25f), "a light setter reaches the ECS");
    const auto* c = w.try_get<ecs::Collider>(ent_of(crate));
    check(c && near(c->radius, 2.5f), "a collider setter");
    const auto* cm = w.try_get<ecs::CameraComponent>(ent_of(cam));
    check(cm && near(cm->fov, 33.0f), "a camera setter");
    const auto* wa = w.try_get<ecs::WaterComponent>(ent_of(pond));
    check(wa && near(wa->wave_height, 1.25f), "a water setter");
    const auto* t = w.try_get<ecs::TerrainComponent>(ent_of(ground));
    check(t && near(t->size, 222.0f), "a terrain resize");
    const auto* s = w.try_get<ecs::ScriptComponent>(ent_of(scripted));
    check(s && s->script_asset != script_before, "a script path change");
    const auto* a = w.try_get<ecs::AudioSource>(ent_of(speaker));
    check(a && near(a->volume, 0.125f), "an audio setter");
    const auto* p = w.try_get<ecs::ParticleEmitter>(ent_of(fire));
    check(p && near(p->spawn_rate, 81.0f), "a particle field written through the accessor");
    const auto* n = w.try_get<ecs::NpcAgent>(ent_of(guard));
    check(n && near(n->sight_range, 17.0f), "an NPC field written through the accessor");
    const auto* m = w.try_get<ecs::MeshRenderer>(ent_of(shape));
    check(m && m->primitive == static_cast<uint32_t>(scene::MeshType::Sphere),
          "a mesh-renderer setter");
    const auto* mm = w.try_get<ecs::MeshRenderer>(ent_of(model));
    check(mm && mm->mesh != 0ull, "Entity::SetMesh");
    const auto* ll = w.try_get<ecs::LightComponent>(ent_of(late));
    check(ll && near(ll->intensity, 4.5f), "a component added after the scene settled");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL componentstamp_check\n");
    gws::jobs::JobSystem::instance().shutdown();
    return g_fail ? 1 : 0;
}
