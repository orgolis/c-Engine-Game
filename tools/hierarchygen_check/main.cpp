// ============================================================================
// hierarchygen_check — every change to the SHAPE of the entity tree must bump
// detail::hierarchy_generation(), and nothing that leaves the shape alone may.
//
// The hierarchy panel walked every entity, every frame, to find the two dozen
// rows on screen: 1.44 ms at 20k entities. It now caches the flattened row
// list and rebuilds it only when this generation moves (or a node opens or
// closes). That cache is correct only if EVERY shape change bumps it: a missed
// one shows an entity under its old parent, or a deleted entity still in the
// list, until something else happens to invalidate. And it is useful only if
// nothing else bumps it -- a counter that moves every frame is a cache that
// rebuilds every frame.
// ============================================================================
#include "entity.h"
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

uint64_t gen() { return detail::hierarchy_generation(); }

}  // namespace

int main() {
    std::printf("=== hierarchygen_check ===\n\n");
    auto scn = std::make_shared<Scene>("hier");

    std::printf("Shape changes bump it:\n");
    uint64_t g = gen();
    auto a = scn->CreateEntity("a");
    check(gen() != g, "creating an entity");
    g = gen();
    auto b = scn->CreateEntity("b");
    auto c = scn->CreateEntity("c");
    g = gen();
    check(b->SetParent(a) && gen() != g, "parenting b under a");
    g = gen();
    check(c->SetParent(b) && gen() != g, "parenting c under b (a grandchild)");
    g = gen();
    check(c->SetParent(a) && gen() != g, "moving c to a different parent");
    g = gen();
    check(c->SetParent(nullptr) && gen() != g, "un-parenting c to the root");
    g = gen();
    a->AddChild(c);
    check(gen() != g, "AddChild");
    g = gen();
    a->RemoveChild(c);
    check(gen() != g, "RemoveChild");
    g = gen();
    scn->RemoveEntity(c);
    check(gen() != g, "removing an entity");
    g = gen();
    scn->Clear();
    check(gen() != g, "clearing the scene");

    std::printf("\nEverything else leaves it alone:\n");
    auto p = scn->CreateEntity("p");
    auto q = scn->CreateEntity("q");
    q->SetParent(p);
    g = gen();
    q->SetParent(p);
    check(gen() == g, "re-parenting to the SAME parent is not a change");
    check(!p->SetParent(q) && gen() == g, "a refused parenting (it would loop) changes nothing");
    q->SetName("renamed");
    q->SetActive(false);
    q->SetActive(true);
    q->GetTransform()->SetLocalPosition(glm::vec3(1.0f, 2.0f, 3.0f));
    for (int frame = 0; frame < 10; ++frame) scn->Update(0.016f);
    check(gen() == g, "renaming, (de)activating, moving and ten frames of Update");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL hierarchygen_check — the hierarchy panel can show a stale tree, or rebuild it every frame.\n");
    return g_fail ? 1 : 0;
}
