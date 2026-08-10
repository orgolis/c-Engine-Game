// ============================================================================
// undo_check — undo must affect the object you actually acted on.
//
// Two defects motivated this, and the first is worse than the second:
//
//   1. EVERY "Create X" command undid itself with GetEntityByName(). Names are
//      not unique — CreateEntity does not enforce it and GetEntityByName
//      returns the FIRST match — so creating two cubes and pressing undo
//      deleted the WRONG one, and renaming first made undo silently do nothing.
//      Undo that removes the wrong object is worse than no undo, because the
//      user trusts it and finds out later.
//
//   2. Deletion was not undoable at all. Undo covering "Create Cube" but not
//      "delete the thing I spent an hour on" is the wrong way round.
//
// This tests the SCENE-LEVEL contract those commands depend on — identity
// preserved across remove/re-add, and lookup by id rather than name. The
// command wiring itself lives in the editor's ImGui code and cannot be driven
// headlessly, so the invariant it relies on is what gets pinned here.
// ============================================================================
#include "scene.h"
#include "entity.h"
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

bool in_scene(const std::shared_ptr<scene::Scene>& s, uint32_t id) {
    for (const auto& e : s->GetEntities()) if (e && e->GetId() == id) return true;
    return false;
}

}  // namespace

int main() {
    std::printf("=== undo_check ===\n\n");

    // ---- 1. the bug that made undo dangerous -------------------------------
    // Two entities, same name. This is ordinary — nothing stops it.
    {
        auto scn = std::make_shared<scene::Scene>("t");
        auto first  = scn->CreateEntity("Cube");
        auto second = scn->CreateEntity("Cube");

        check(first->GetId() != second->GetId(), "two entities may share a name with distinct ids");
        check(scn->GetEntityByName("Cube")->GetId() == first->GetId(),
              "GetEntityByName returns the FIRST match — undoing by name hits the wrong entity");
        check(scn->GetEntityById(second->GetId()) == second,
              "GetEntityById resolves the entity that was actually created");
    }

    // ---- 2. a rename must not detach undo from its entity -------------------
    {
        auto scn = std::make_shared<scene::Scene>("t");
        auto ent = scn->CreateEntity("Cube");
        const uint32_t id = ent->GetId();
        ent->SetName("Boss Arena Pillar");

        check(scn->GetEntityByName("Cube") == nullptr,
              "after a rename, a by-name undo would find nothing and silently no-op");
        check(scn->GetEntityById(id) == ent, "the id still resolves after a rename");
    }

    // ---- 3. remove/re-add preserves IDENTITY, not just data ----------------
    // This is what makes delete-undo cheap and correct: RemoveEntity only
    // unregisters, so a shared_ptr held by the undo command keeps the very same
    // object alive — and anything keyed on its Transform pointer (the ECS
    // bridge) still matches after a restore.
    {
        auto scn = std::make_shared<scene::Scene>("t");
        auto ent = scn->CreateEntity("Victim");
        const uint32_t id = ent->GetId();
        auto* transform_before = ent->GetTransform();
        ent->GetTransform()->SetLocalPosition({3.0f, 4.0f, 5.0f});

        scn->RemoveEntity(ent);
        check(!in_scene(scn, id), "RemoveEntity unregisters the entity");
        check(scn->GetEntityById(id) == nullptr, "and it stops resolving by id");
        check(ent != nullptr && ent->GetName() == "Victim",
              "but the object itself survives — the undo command's shared_ptr owns it");

        scn->AddEntity(ent);
        check(in_scene(scn, id), "AddEntity restores it");
        check(scn->GetEntityById(id) == ent, "restored under the SAME id");
        check(ent->GetTransform() == transform_before,
              "same Transform pointer — ECS mappings keyed on it stay valid");
        const auto p = ent->GetTransform()->GetLocalPosition();
        check(p.x == 3.0f && p.y == 4.0f && p.z == 5.0f, "its state came back intact");
    }

    // ---- 4. repeated undo/redo must not degrade ----------------------------
    // Redo re-removes the same object rather than making a new one, so identity
    // has to hold across many cycles, not just the first.
    {
        auto scn = std::make_shared<scene::Scene>("t");
        auto ent = scn->CreateEntity("Cycled");
        const uint32_t id = ent->GetId();

        for (int i = 0; i < 25; ++i) {
            scn->RemoveEntity(ent);
            scn->AddEntity(ent);
        }
        check(scn->GetEntityById(id) == ent, "identity survives 25 delete/restore cycles");

        size_t count = 0;
        for (const auto& e : scn->GetEntities()) if (e && e->GetId() == id) ++count;
        check(count == 1, "and it is not duplicated in the scene");
    }

    // ---- 5. deleting one entity leaves the others alone --------------------
    // The command mirrors RemoveEntity exactly, so this pins what "exactly"
    // means: siblings untouched, and a child NOT removed with its parent.
    {
        auto scn = std::make_shared<scene::Scene>("t");
        auto a = scn->CreateEntity("A");
        auto b = scn->CreateEntity("B");
        auto child = scn->CreateEntity("Child");
        child->GetTransform()->SetParent(a->GetTransform());

        const size_t before = scn->GetEntities().size();
        scn->RemoveEntity(a);
        check(scn->GetEntities().size() == before - 1, "exactly one entity is removed");
        check(in_scene(scn, b->GetId()), "an unrelated sibling is untouched");
        check(in_scene(scn, child->GetId()),
              "a child is NOT removed with its parent — undo must not restore more than was deleted");
    }

    // ---- 6. degenerate input ------------------------------------------------
    {
        auto scn = std::make_shared<scene::Scene>("t");
        scn->RemoveEntity(nullptr);
        scn->AddEntity(nullptr);
        check(true, "null entity in either direction is harmless");

        auto ent = scn->CreateEntity("Once");
        const uint32_t id = ent->GetId();
        scn->RemoveEntity(ent);
        scn->RemoveEntity(ent);          // double-undo of the same command
        check(scn->GetEntityById(id) == nullptr, "removing twice leaves it removed, not corrupted");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL undo_check\n");
    return g_fail ? 1 : 0;
}
