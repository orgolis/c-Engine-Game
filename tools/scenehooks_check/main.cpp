// ============================================================================
// scenehooks_check — Scene::Update must call exactly the hooks it called
// before, on exactly the entities it called them on, while visiting only the
// entities that have something to run.
//
// Scene::Update used to walk every entity on every pass -- one per fixed step,
// then Update, then LateUpdate -- making a virtual call into every component's
// hook, almost always the empty default (nothing in the engine overrides
// OnFixedUpdate at all). At 20k entities that was ~6 ms a frame in edit mode,
// doing nothing. Now each pass walks a cached list of the entities whose
// components override that hook, rebuilt when the entity list or a component
// set changes, and a destroyed-entity scan runs only after a Destroy().
//
// Every cache has the same two failure modes, and both are silent:
//
//   * STALE: a component added, an entity added, a Destroy() -- and the cache
//     does not notice, so a hook that should run does not, or one that should
//     stop keeps running. That reads as a gameplay bug.
//   * USELESS: the cache rebuilds every frame anyway, so it is correct and
//     costs what it was meant to save. Nothing fails; it is just slow again.
//
// The assertions cover both, plus the compile-time "which hooks does this type
// override" decision the whole thing rests on.
// ============================================================================
#include "entity.h"
#include "light_component.h"
#include "mesh_renderer_component.h"
#include "collider_component.h"
#include "scene.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

using namespace schizo::scene;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

struct Calls { int fixed = 0, update = 0, late = 0; };

// One component per hook shape. Counters, so "called" and "not called" are
// both observable.
class UpdateOnly final : public Component {
public:
    explicit UpdateOnly(Calls* c) : c_(c) {}
    void OnUpdate(float) override { ++c_->update; }
private:
    Calls* c_;
};

class AllThree final : public Component {
public:
    explicit AllThree(Calls* c) : c_(c) {}
    void OnFixedUpdate(float) override { ++c_->fixed; }
    void OnUpdate(float) override      { ++c_->update; }
    void OnLateUpdate(float) override  { ++c_->late; }
private:
    Calls* c_;
};

class LateOnly final : public Component {
public:
    explicit LateOnly(Calls* c) : c_(c) {}
    void OnLateUpdate(float) override { ++c_->late; }
private:
    Calls* c_;
};

class Inert final : public Component {};   // overrides nothing

constexpr float kStep = 0.02f;              // SceneConfig's default fixed step

}  // namespace

int main() {
    std::printf("=== scenehooks_check ===\n\n");

    std::printf("Which hooks a type overrides is decided correctly at compile time:\n");
    static_assert(detail::component_hooks<Inert>() == 0);
    check(detail::component_hooks<Inert>() == 0, "a component overriding nothing has no hooks");
    check(detail::component_hooks<UpdateOnly>() == kHookUpdate, "OnUpdate only");
    check(detail::component_hooks<LateOnly>() == kHookLateUpdate, "OnLateUpdate only");
    check(detail::component_hooks<AllThree>() == kHookAll, "all three");
    check(detail::component_hooks<MeshRendererComponent>() == 0 &&
          detail::component_hooks<ColliderComponent>() == 0,
          "a mesh and a collider -- the bulk of any scene -- have none");
    check(detail::component_hooks<LightComponent>() == kHookUpdate,
          "LightComponent overrides OnUpdate, and is seen to");

    auto scn = std::make_shared<Scene>("hooks");
    // Every counter lives as long as the scene. Components hold a pointer to
    // theirs, and a counter in an inner block would be reused stack memory by
    // the next block -- which is exactly how a first draft of this check
    // "found" a child of an inactive parent running.
    Calls ua, al, la, added, fresh, child_calls;
    auto e_upd  = scn->CreateEntity("upd");   e_upd->AddComponent<UpdateOnly>(&ua);
    auto e_all  = scn->CreateEntity("all");   e_all->AddComponent<AllThree>(&al);
    auto e_late = scn->CreateEntity("late");  e_late->AddComponent<LateOnly>(&la);
    for (int i = 0; i < 50; ++i) {
        auto e = scn->CreateEntity("inert");
        e->AddComponent<MeshRendererComponent>(MeshType::Cube);
        e->AddComponent<Inert>();
    }

    std::printf("\nEach hook runs where it is overridden, and only there:\n");
    scn->Update(kStep * 1.5f);   // one fixed step
    check(ua.update == 1 && ua.fixed == 0 && ua.late == 0, "UpdateOnly: Update, nothing else");
    check(al.update == 1 && al.fixed == 1 && al.late == 1, "AllThree: all three, once each");
    check(la.late == 1 && la.update == 0 && la.fixed == 0, "LateOnly: LateUpdate, nothing else");
    check(scn->HookedUpdateCount() == 2, "the OnUpdate list holds 2 entities, not 53");

    std::printf("\nFixed steps still follow accumulated time:\n");
    {
        const int before = al.fixed;
        scn->Update(kStep * 3.0f + 0.001f);
        check(al.fixed - before >= 3, "three steps' worth of time runs at least three fixed updates");
    }

    std::printf("\nAn unchanged scene does not rebuild the lists every frame:\n");
    {
        const uint64_t r0 = scn->HookedListRebuilds();
        for (int i = 0; i < 100; ++i) scn->Update(0.001f);
        check(scn->HookedListRebuilds() == r0, "100 unchanged updates -> no rebuild");
    }

    std::printf("\nChanges are picked up on the next update:\n");
    {
        auto plain = scn->CreateEntity("plain");          // no hooks yet
        scn->Update(0.001f);
        plain->AddComponent<UpdateOnly>(&added);           // a component set changes
        scn->Update(0.001f);
        check(added.update == 1, "a component added to an existing entity runs next update");

        auto late_comer = scn->CreateEntity("late comer");
        late_comer->AddComponent<UpdateOnly>(&fresh);
        scn->Update(0.001f);
        check(fresh.update == 1, "an entity added between updates runs");

        const int before = ua.update;
        e_upd->RemoveComponent(e_upd->GetComponent<UpdateOnly>());
        scn->Update(0.001f);
        check(ua.update == before, "a removed component stops running");
    }

    std::printf("\nActive state is still respected:\n");
    {
        const int before = al.update;
        e_all->SetActive(false);
        scn->Update(0.001f);
        check(al.update == before, "an inactive entity's hooks do not run");
        e_all->SetActive(true);
        scn->Update(0.001f);
        check(al.update == before + 1, "and run again once it is active");

        auto parent = scn->CreateEntity("parent");
        auto child  = scn->CreateEntity("child");
        parent->AddChild(child);
        child->AddComponent<UpdateOnly>(&child_calls);
        parent->SetActive(false);
        scn->Update(0.001f);
        check(child_calls.update == 0, "a child of an inactive parent does not run");
    }

    std::printf("\nRemoval and Destroy():\n");
    {
        const int before = la.late;
        scn->RemoveEntity(e_late);
        scn->Update(0.001f);
        check(la.late == before, "a removed entity's hooks stop running");

        const auto count = scn->GetEntityCount();
        auto doomed = scn->CreateEntity("doomed");
        scn->Update(0.001f);
        doomed->Destroy();
        scn->Update(0.001f);
        check(scn->GetEntityCount() == count, "a Destroy()ed entity is removed on the next update");
    }

    std::printf("\nA parent can never become its own descendant:\n");
    {
        // The hierarchy panel only refused dropping an entity onto ITSELF. Drop a
        // parent onto its child and SetParent built A -> B -> A, after which
        // IsActiveInHierarchy() and every world-matrix walk recursed forever.
        auto s3   = std::make_shared<Scene>("cycle");
        auto top  = s3->CreateEntity("top");
        auto mid  = s3->CreateEntity("mid");
        auto leaf = s3->CreateEntity("leaf");
        top->AddChild(mid);
        mid->AddChild(leaf);
        check(!top->SetParent(leaf), "parenting an entity under its grandchild is refused");
        check(!top->SetParent(top), "parenting an entity under itself is refused");
        check(top->GetParent() == nullptr && mid->GetParent() == top && leaf->GetParent() == mid,
              "and the hierarchy is left exactly as it was");
        check(top->IsActiveInHierarchy() && leaf->IsActiveInHierarchy(),
              "so walking it still terminates");
        auto other = s3->CreateEntity("other");
        check(leaf->SetParent(other) && leaf->GetParent() == other,
              "an ordinary re-parent still works");
        check(mid->SetParent(nullptr) && mid->GetParent() == nullptr, "and so does detaching");
    }

    std::printf("\nEntitiesWithAny<...> finds exactly the entities a full scan would:\n");
    {
        // A fresh scene, so the expected lists are easy to state.
        auto s2 = std::make_shared<Scene>("index");
        auto lamp1 = s2->CreateEntity("lamp1");  lamp1->AddComponent<LightComponent>();
        auto box   = s2->CreateEntity("box");    box->AddComponent<MeshRendererComponent>(MeshType::Cube);
        auto both  = s2->CreateEntity("both");
        both->AddComponent<ColliderComponent>(ColliderShape::Box);
        both->AddComponent<LightComponent>();
        auto lamp2 = s2->CreateEntity("lamp2");  lamp2->AddComponent<LightComponent>();

        auto names = [](const std::vector<std::shared_ptr<Entity>>& v) {
            std::string out;
            for (const auto& e : v) out += e->GetName() + ",";
            return out;
        };
        check(names(s2->EntitiesWithAny<LightComponent>()) == "lamp1,both,lamp2,",
              "lights, in scene order");
        check(names(s2->EntitiesWithAny<ColliderComponent, MeshRendererComponent>()) == "box,both,",
              "a union keeps scene order, and lists an entity once");

        const uint64_t r0 = s2->ComponentIndexRebuilds();
        for (int i = 0; i < 50; ++i) (void)s2->EntitiesWithAny<LightComponent>();
        check(s2->ComponentIndexRebuilds() == r0, "50 unchanged queries -> no rebuild");

        auto lamp3 = s2->CreateEntity("lamp3");  lamp3->AddComponent<LightComponent>();
        check(names(s2->EntitiesWithAny<LightComponent>()) == "lamp1,both,lamp2,lamp3,",
              "an added entity appears");
        box->AddComponent<LightComponent>();
        check(names(s2->EntitiesWithAny<LightComponent>()) == "lamp1,box,both,lamp2,lamp3,",
              "a component added to an existing entity appears, in its scene position");
        both->RemoveComponent(both->GetComponent<LightComponent>());
        check(names(s2->EntitiesWithAny<LightComponent>()) == "lamp1,box,lamp2,lamp3,",
              "a removed component disappears");
        s2->RemoveEntity(lamp2);
        check(names(s2->EntitiesWithAny<LightComponent>()) == "lamp1,box,lamp3,",
              "a removed entity disappears");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL scenehooks_check — Scene::Update ran the wrong hooks, missed a change, "
                    "or rebuilt its lists every frame.\n");
    return g_fail ? 1 : 0;
}
