// ============================================================================
// ecssync_check — the ECS mirror's bookkeeping across syncs: what it keeps,
// what it drops, and that it never serves a matrix for something gone.
//
// The mirror used to rebuild its Transform -> index map and its
// Transform -> world-matrix map from scratch every frame. That was slow (~4.5
// ms at 20k entities) but it could not go stale: anything not active this
// frame simply was not in the new maps. The persistent version keeps one slot
// per transform across frames and only walks the table for removals when the
// counts say something was removed. That is where it can go wrong quietly:
//
//   * a removed or deactivated entity keeps its slot, so world_matrix() keeps
//     answering for it and rendering draws something that is not there;
//   * one entity removed and another added in the SAME frame leaves the
//     counts equal -- the case a count-based shortcut is most likely to miss;
//   * a reused entity gets a new ECS identity every frame, which breaks
//     every system that keys state on the entity;
//   * the hierarchy resolve, now on persistent scratch arrays, keeps a stale
//     parent matrix after the parent moves.
//
// And the verification pass, now sampled, must still check everything when
// the set changes -- the editor's one-time "N/N match" log relies on it.
// ============================================================================
#include "ecs_bridge.h"

#include "entity.h"
#include "scene.h"
#include "transform.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool same(const glm::mat4& a, const glm::mat4& b) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::fabs(a[c][r] - b[c][r]) > 1e-4f) return false;
    return true;
}

// The mirror's answer for `e` matches the scene's own world matrix.
bool mirrored(const editor::EcsSceneBridge& br, const std::shared_ptr<scene::Entity>& e) {
    const glm::mat4* m = br.world_matrix(e->GetTransform());
    return m && same(*m, e->GetTransform()->GetWorldMatrix());
}

}  // namespace

int main() {
    std::printf("=== ecssync_check ===\n\n");

    auto scn = std::make_shared<scene::Scene>("sync");
    auto a = scn->CreateEntity("A");
    auto b = scn->CreateEntity("B");
    auto c = scn->CreateEntity("C");
    auto d = scn->CreateEntity("D");
    a->GetTransform()->SetLocalPosition(glm::vec3(1.0f, 2.0f, 3.0f));
    a->AddChild(b);
    b->GetTransform()->SetLocalPosition(glm::vec3(0.0f, 1.0f, 0.0f));
    c->GetTransform()->SetLocalPosition(glm::vec3(-4.0f, 0.0f, 0.0f));
    d->GetTransform()->SetLocalPosition(glm::vec3(0.0f, 0.0f, 9.0f));

    editor::EcsSceneBridge br;

    std::printf("First sync:\n");
    br.sync_and_run(scn);
    check(br.created() == 4 && br.reused() == 0 && br.destroyed() == 0, "four entities created");
    check(mirrored(br, a) && mirrored(br, b) && mirrored(br, c) && mirrored(br, d),
          "every world matrix matches the scene, the child included");
    check(br.checked() == br.entity_count() && br.verified() == br.checked(),
          "a changed set is verified in full (the editor's N/N log relies on it)");
    const uint32_t id_a = br.ecs_entity_id(a->GetTransform());
    const uint32_t id_b = br.ecs_entity_id(b->GetTransform());

    std::printf("\nAn unchanged sync keeps identity:\n");
    br.sync_and_run(scn);
    check(br.created() == 0 && br.reused() == 4 && br.destroyed() == 0, "all four reused");
    check(br.ecs_entity_id(a->GetTransform()) == id_a &&
          br.ecs_entity_id(b->GetTransform()) == id_b,
          "each keeps the same ECS entity across syncs");

    std::printf("\nMoving a parent moves the child's mirrored matrix:\n");
    a->GetTransform()->SetLocalPosition(glm::vec3(10.0f, 0.0f, 0.0f));
    br.sync_and_run(scn);
    check(mirrored(br, a) && mirrored(br, b),
          "child follows the moved parent (no stale scratch matrix)");

    std::printf("\nA removed entity stops being answered for:\n");
    scn->RemoveEntity(c);          // `c` stays alive here, so its address is not reused
    br.sync_and_run(scn);
    check(br.destroyed() == 1 && br.created() == 0, "one destroyed");
    check(br.world_matrix(c->GetTransform()) == nullptr, "world_matrix(removed) is null");
    check(br.ecs_entity_id(c->GetTransform()) == editor::kNoEcsEntity, "and it has no ECS entity");
    check(mirrored(br, a) && mirrored(br, b) && mirrored(br, d), "the rest are untouched");

    std::printf("\nRemove one and add one in the same frame (counts stay equal):\n");
    scn->RemoveEntity(d);
    auto e = scn->CreateEntity("E");
    e->GetTransform()->SetLocalPosition(glm::vec3(5.0f, 5.0f, 5.0f));
    br.sync_and_run(scn);
    check(br.created() == 1 && br.destroyed() == 1, "one created, one destroyed");
    check(br.world_matrix(d->GetTransform()) == nullptr, "the removed one is NOT still answered for");
    check(mirrored(br, e), "the added one is mirrored");
    check(br.entity_count() == 3, "three active entities, not four");

    std::printf("\nDeactivating drops it; reactivating brings it back:\n");
    e->SetActive(false);
    br.sync_and_run(scn);
    check(br.world_matrix(e->GetTransform()) == nullptr, "inactive -> not answered for");
    e->SetActive(true);
    br.sync_and_run(scn);
    check(mirrored(br, e), "active again -> mirrored again");

    std::printf("\nSteady state verifies a slice, and the slices cover everything:\n");
    {
        std::vector<std::shared_ptr<scene::Entity>> many;
        for (int i = 0; i < 100; ++i) {
            auto m = scn->CreateEntity("M");
            m->GetTransform()->SetLocalPosition(glm::vec3(float(i), 0.0f, 0.0f));
            many.push_back(m);
        }
        br.sync_and_run(scn);                       // set changed: full
        const size_t n = br.entity_count();
        check(br.checked() == n, "the sync that added 100 checked all of them");
        size_t total = 0, worst_slice = 0;
        bool   all_ok = true;
        for (int f = 0; f < 32; ++f) {
            br.sync_and_run(scn);
            total += br.checked();
            if (br.checked() > worst_slice) worst_slice = br.checked();
            all_ok = all_ok && br.verified() == br.checked();
        }
        check(worst_slice < n, "an unchanged sync checks fewer than all of them");
        check(total == n, "32 unchanged syncs check every entity exactly once");
        check(all_ok, "and every checked matrix matched");
    }

    std::printf("\nScene gone: everything is torn down:\n");
    br.sync_and_run(nullptr);
    check(br.world_matrix(a->GetTransform()) == nullptr, "no answers without a scene");

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL ecssync_check — the ECS mirror kept, dropped or served the wrong thing.\n");
    return g_fail ? 1 : 0;
}
