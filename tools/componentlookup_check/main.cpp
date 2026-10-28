// ============================================================================
// componentlookup_check — GetComponent<T>() must give the answer dynamic_cast
// gives, and must stay fast enough to call ten times per entity per frame.
//
// The scene asks every entity for every component kind, every frame: the ECS
// mirror alone probes about ten kinds per entity, mostly ones the entity does
// not have, and draw building asks again. As a dynamic_cast scan that cost
// ~75 ns per probe on MinGW -- a failed RTTI comparison there falls back to
// comparing mangled type names -- which was 15 ms a frame at 20k entities for
// the mirror alone.
//
// The fast path compares a type key recorded by AddComponent<T>, and only for
// a FINAL T, where "dynamic_cast<T> succeeds" and "the component is exactly a
// T" are the same statement. So the assertions are about the two ways that
// goes wrong without anyone noticing:
//
//   * the fast path returns a different component than the scan would --
//     a wrong or missing component reads as a gameplay bug, not a lookup bug;
//   * a component class loses `final`, and every lookup for it silently drops
//     back to the slow scan. Correct, so nothing ever fails -- it just costs
//     milliseconds again at scale.
// ============================================================================
#include "audio_components.h"
#include "camera_component.h"
#include "collider_component.h"
#include "entity.h"
#include "light_component.h"
#include "mesh_renderer.h"
#include "mesh_renderer_component.h"
#include "scene.h"
#include "script_component.h"
#include "terrain_component.h"
#include "transform_component.h"
#include "water_component.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace schizo::scene;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// The definition GetComponent<T> has to agree with: the first component that
// dynamic_casts to T, from the same list, in the same order.
template <typename T>
const Component* reference_first(const Entity& e) {
    for (const auto& c : e.GetComponents())
        if (c && dynamic_cast<const T*>(c.get())) return c.get();
    return nullptr;
}

template <typename T>
std::vector<const Component*> reference_all(const Entity& e) {
    std::vector<const Component*> out;
    for (const auto& c : e.GetComponents())
        if (c && dynamic_cast<const T*>(c.get())) out.push_back(c.get());
    return out;
}

template <typename T>
bool agrees(Entity& e) {
    const Component* want = reference_first<T>(e);
    const Component* got  = e.GetComponent<T>().get();
    const Component* gotc = static_cast<const Entity&>(e).GetComponent<T>().get();
    std::vector<const Component*> all;
    for (const auto& p : e.GetComponents<T>()) all.push_back(p.get());
    return got == want && gotc == want && all == reference_all<T>(e);
}

// Out of line on purpose. Inlined into a timing loop, the compiler specialises
// the scan for the one entity it is handed and times nearly nothing (it
// reported 0.2 ns, which also briefly suggested that `final` alone made
// dynamic_cast cheap -- under GCC 13 it does not: ~49 ns either way).
[[gnu::noinline]] const void* probe_fast(Entity& e) {
    return e.GetComponent<LightComponent>().get();
}
[[gnu::noinline]] const void* probe_scan(Entity& e) {
    for (const auto& c : e.GetComponents())
        if (auto p = std::dynamic_pointer_cast<LightComponent>(c)) return p.get();
    return nullptr;
}

template <typename T>
bool agrees_everywhere(const std::vector<std::shared_ptr<Entity>>& es) {
    for (const auto& e : es)
        if (!agrees<T>(*e)) return false;
    return true;
}

}  // namespace

int main() {
    std::printf("=== componentlookup_check ===\n\n");

    auto scene = std::make_shared<Scene>("lookup");
    std::vector<std::shared_ptr<Entity>> es;

    // A spread of component sets, including the shapes the hot loops see most
    // (a mesh with a collider), an entity with nothing, and duplicates of one
    // kind -- "first found" has to mean the same first.
    {
        auto e = scene->CreateEntity("empty");                      es.push_back(e);
    }
    {
        auto e = scene->CreateEntity("cube");
        e->AddComponent<MeshRendererComponent>(MeshType::Cube);
        e->AddComponent<ColliderComponent>(ColliderShape::Box);     es.push_back(e);
    }
    {
        auto e = scene->CreateEntity("light");
        e->AddComponent<LightComponent>(LightType::Point);          es.push_back(e);
    }
    {
        auto e = scene->CreateEntity("everything");
        e->AddComponent<CameraComponent>();
        e->AddComponent<WaterComponent>();
        e->AddComponent<ScriptComponent>("a.lua");
        e->AddComponent<AudioSourceComponent>();
        e->AddComponent<LightComponent>(LightType::Spot);
        e->AddComponent<TerrainComponent>(8, 10.0f);                es.push_back(e);
    }
    {
        auto e = scene->CreateEntity("two lights");
        e->AddComponent<LightComponent>(LightType::Point, "first");
        e->AddComponent<ColliderComponent>(ColliderShape::Sphere);
        e->AddComponent<LightComponent>(LightType::Spot, "second");  es.push_back(e);
    }

    std::printf("GetComponent agrees with the dynamic_cast scan, for every kind:\n");
    check(agrees_everywhere<MeshRendererComponent>(es), "MeshRendererComponent");
    check(agrees_everywhere<ColliderComponent>(es),     "ColliderComponent");
    check(agrees_everywhere<LightComponent>(es),        "LightComponent (incl. the first of two)");
    check(agrees_everywhere<CameraComponent>(es),       "CameraComponent");
    check(agrees_everywhere<WaterComponent>(es),        "WaterComponent");
    check(agrees_everywhere<ScriptComponent>(es),       "ScriptComponent");
    check(agrees_everywhere<AudioSourceComponent>(es),  "AudioSourceComponent");
    check(agrees_everywhere<TerrainComponent>(es),      "TerrainComponent");
    check(agrees_everywhere<AudioListenerComponent>(es),
          "AudioListenerComponent (on nothing: every answer is null)");
    // Not final, so it takes the original scan -- and must still find things.
    check(agrees_everywhere<Component>(es), "Component itself (the non-final path)");

    {
        auto& two = *es[4];
        auto first = two.GetComponent<LightComponent>();
        check(first && first->GetType() == LightType::Point,
              "of two lights, the FIRST is returned (the point light, not the spot)");
        check(two.GetComponents<LightComponent>().size() == 2, "GetComponents finds both");
    }

    std::printf("\nRemoval is seen by the next lookup:\n");
    {
        auto& cube = *es[1];
        cube.RemoveComponent(cube.GetComponent<ColliderComponent>());
        check(cube.GetComponent<ColliderComponent>() == nullptr, "a removed collider is gone");
        check(cube.GetComponent<MeshRendererComponent>() != nullptr, "and the mesh is still there");
        check(agrees_everywhere<ColliderComponent>(es), "and the scan still agrees");
    }

    std::printf("\nEvery leaf component is final, so its lookups take the fast path:\n");
    // A class that loses `final` stays CORRECT -- it falls back to the scan --
    // which is exactly why this has to be asserted: nothing else would notice.
    check(std::is_final_v<AudioSourceComponent>,   "AudioSourceComponent");
    check(std::is_final_v<AudioListenerComponent>, "AudioListenerComponent");
    check(std::is_final_v<CameraComponent>,        "CameraComponent");
    check(std::is_final_v<ColliderComponent>,      "ColliderComponent");
    check(std::is_final_v<LightComponent>,         "LightComponent");
    check(std::is_final_v<DirectionalLightComponent>, "DirectionalLightComponent");
    check(std::is_final_v<GlobalLightComponent>,   "GlobalLightComponent");
    check(std::is_final_v<MeshRenderer>,           "MeshRenderer");
    check(std::is_final_v<SkinnedMeshRenderer>,    "SkinnedMeshRenderer");
    check(std::is_final_v<LineRenderer>,           "LineRenderer");
    check(std::is_final_v<MeshRendererComponent>,  "MeshRendererComponent");
    check(std::is_final_v<ScriptComponent>,        "ScriptComponent");
    check(std::is_final_v<TerrainComponent>,       "TerrainComponent");
    check(std::is_final_v<TransformComponent>,     "TransformComponent");
    check(std::is_final_v<WaterComponent>,         "WaterComponent");

    std::printf("\nWhat a probe costs (reported, not asserted -- timing on a CI box is noise):\n");
    {
        // The common case in the hot loops: asking a mesh+collider entity for
        // a kind it does not have.
        auto e = scene->CreateEntity("probe");
        e->AddComponent<MeshRendererComponent>(MeshType::Cube);
        e->AddComponent<ColliderComponent>(ColliderShape::Box);
        constexpr int kN = 200000;
        using clk = std::chrono::steady_clock;
        volatile const void* sink = nullptr;
        auto t0 = clk::now();
        for (int i = 0; i < kN; ++i) sink = probe_fast(*e);
        auto t1 = clk::now();
        for (int i = 0; i < kN; ++i) sink = probe_scan(*e);   // the old GetComponent body
        auto t2 = clk::now();
        (void)sink;
        const double fast = std::chrono::duration<double, std::nano>(t1 - t0).count() / kN;
        const double scan = std::chrono::duration<double, std::nano>(t2 - t1).count() / kN;
        std::printf("  miss on a 2-component entity: GetComponent %.1f ns, dynamic_cast scan %.1f ns\n",
                    fast, scan);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL componentlookup_check — GetComponent disagrees with dynamic_cast, "
                    "or a component lost its fast path.\n");
    return g_fail ? 1 : 0;
}
