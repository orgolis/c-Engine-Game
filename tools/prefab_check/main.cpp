// ============================================================================
// prefab_check -- a prefab gives back the SUBTREE that was saved.
//
// The user's request was "save it with all properties and children", and both
// halves fail quietly in different ways:
//
//   * PROPERTIES: a prefab writer that captures the entity but not its
//     components produces a file that loads, names itself correctly, and is
//     an empty box. Nothing errors.
//   * CHILDREN: a writer that emits the subtree but keeps the ROOT's
//     PARENT_ID -- the id of whatever it was parented to in the scene it came
//     from -- produces a prefab that lands at the scene root anyway, because
//     the id resolves to nothing on load. It looks correct until the day that
//     id happens to exist in the target scene, and then the prefab silently
//     parents itself to a stranger.
//   * IDS: entities must get FRESH ids. Reusing the saved ones means
//     instantiating a prefab twice gives two entities claiming one id.
//
// Values here are deliberately non-default: against defaults, a writer that
// writes nothing and one that writes correctly give the same answer.
// ============================================================================
#include "scene_serializer.h"

#include "entity.h"
#include "mesh_renderer_component.h"
#include "particle_emitter_component.h"
#include "scene.h"
#include "transform.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

std::shared_ptr<scene::Entity> find_child(const std::shared_ptr<scene::Entity>& parent,
                                          const std::string& name) {
    for (const auto& c : parent->GetChildren())
        if (c->GetName() == name) return c;
    return nullptr;
}

std::string read_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    std::printf("=== prefab_check ===\n\n");

    const std::string path =
        (std::filesystem::temp_directory_path() / "gws_prefab_check.prefab").string();

    uint32_t saved_root_id = 0;

    // ---- build a subtree worth saving --------------------------------------
    {
        auto scn = std::make_shared<scene::Scene>("source");

        // A parent OUTSIDE the subtree. The prefab root is parented to it, so
        // the writer has something real to wrongly emit.
        auto outsider = scn->CreateEntity("Outsider");

        auto root = scn->CreateEntity("Cart");
        root->SetParent(outsider);
        root->SetTag("vehicle");
        root->GetTransform()->SetLocalPosition({4.0f, 0.5f, -3.0f});
        root->GetTransform()->SetLocalScale({2.0f, 2.0f, 2.0f});

        auto wheel = scn->CreateEntity("Wheel");
        wheel->SetParent(root);
        wheel->GetTransform()->SetLocalPosition({0.75f, -0.25f, 0.0f});
        wheel->SetActive(false);   // non-default, and easy to drop silently

        auto lamp = scn->CreateEntity("Lamp");
        lamp->SetParent(root);
        lamp->GetTransform()->SetLocalPosition({0.0f, 1.5f, 0.0f});

        // A grandchild: a writer that walks only one level down loses this and
        // still produces a plausible-looking prefab.
        auto flame = scn->CreateEntity("Flame");
        flame->SetParent(lamp);
        flame->GetTransform()->SetLocalPosition({0.0f, 0.2f, 0.0f});
        auto* pec = flame->GetParticleEmitterComponent();
        if (pec) {
            pec->enabled       = true;
            pec->spawn_rate    = 37.5f;
            pec->max_particles = 512;
            pec->rate_scale    = 0.25f;
        }

        saved_root_id = root->GetId();
        check(editor::SceneSerializer::SavePrefab(path, root), "SavePrefab reports success");
        check(std::filesystem::exists(path), "the prefab file exists");
    }

    // ---- what got written --------------------------------------------------
    {
        const std::string text = read_all(path);
        check(text.find("PREFAB_NAME=Cart") != std::string::npos,
              "the header names the root");
        check(text.find("ENTITY_COUNT=4") != std::string::npos,
              "four entities written -- root, two children and the grandchild");
        check(text.find("Outsider") == std::string::npos,
              "the entity OUTSIDE the subtree is not written");

        // The root's own PARENT_ID must be absent. Present, it would resolve to
        // nothing on load today and to a stranger the day that id exists.
        const std::string root_parent = "PARENT_ID=" + std::to_string(saved_root_id ? saved_root_id : 1);
        (void)root_parent;
        size_t parents = 0;
        for (size_t at = text.find("PARENT_ID="); at != std::string::npos;
             at = text.find("PARENT_ID=", at + 1))
            ++parents;
        check(parents == 3,
              "exactly three PARENT_ID lines -- the root's is suppressed, the children keep theirs");
    }

    // ---- instantiate into a DIFFERENT scene ---------------------------------
    {
        auto target = std::make_shared<scene::Scene>("target");
        auto decoy  = target->CreateEntity("Decoy");   // occupies an id first
        (void)decoy;

        auto root = editor::SceneSerializer::LoadPrefab(path, target);
        check(root != nullptr, "LoadPrefab returns a root");
        if (!root) { std::printf("\n%d/%d checks passed\nFAILED\n", g_pass, g_pass + g_fail); return 1; }

        check(root->GetName() == "Cart", "the root is the entity that was saved");
        check(root->GetParent() == nullptr,
              "and it lands at the scene root, not parented to a stale id");
        check(root->GetTag() == "vehicle", "the root's tag survives");

        auto* rt = root->GetTransform();
        check(rt && near(rt->GetLocalPosition().x, 4.0f) &&
              near(rt->GetLocalPosition().y, 0.5f) && near(rt->GetLocalPosition().z, -3.0f),
              "the root's local position survives");
        check(rt && near(rt->GetLocalScale().x, 2.0f), "the root's scale survives");

        check(root->GetChildren().size() == 2, "both children are attached to the root");

        auto wheel = find_child(root, "Wheel");
        check(wheel != nullptr, "the first child is present by name");
        check(wheel && !wheel->IsActive(),
              "an INACTIVE child stays inactive -- the default is active, so this proves a write");
        check(wheel && near(wheel->GetTransform()->GetLocalPosition().x, 0.75f),
              "the child's local position survives");

        auto lamp = find_child(root, "Lamp");
        check(lamp != nullptr, "the second child is present");
        check(lamp && lamp->GetChildren().size() == 1,
              "the GRANDCHILD is present -- the walk is recursive, not one level deep");

        auto flame = lamp ? find_child(lamp, "Flame") : nullptr;
        check(flame != nullptr, "the grandchild is the right entity");
        auto* pec = flame ? flame->GetParticleEmitterComponent() : nullptr;
        check(pec != nullptr, "the grandchild kept its component");
        check(pec && pec->enabled, "the emitter is still enabled -- the default is OFF");
        check(pec && near(pec->spawn_rate, 37.5f),  "component field: spawn_rate");
        check(pec && pec->max_particles == 512u,    "component field: max_particles");
        check(pec && near(pec->rate_scale, 0.25f),  "component field: rate_scale");

        // Fresh ids. The saved root id must not have been forced onto anything.
        std::set<uint32_t> ids;
        bool unique = true;
        for (const auto& e : target->GetEntities())
            if (e && !ids.insert(e->GetId()).second) unique = false;
        check(unique, "every entity in the target scene has a distinct id");

        // Instantiate a SECOND copy: the case where reusing saved ids breaks.
        auto root2 = editor::SceneSerializer::LoadPrefab(path, target);
        check(root2 != nullptr, "the prefab instantiates a second time");
        check(root2 && root2.get() != root.get(), "and produces a different entity");
        check(root2 && root2->GetChildren().size() == 2, "the second copy has its children too");

        ids.clear();
        unique = true;
        for (const auto& e : target->GetEntities())
            if (e && !ids.insert(e->GetId()).second) unique = false;
        check(unique, "two copies of one prefab do not collide on ids");
        check(root2 && root2->GetChildren()[0]->GetParent().get() == root2.get(),
              "the second copy's children point at the SECOND root, not the first");
    }

    // ---- failure paths ------------------------------------------------------
    {
        auto scn = std::make_shared<scene::Scene>("fail");
        check(!editor::SceneSerializer::SavePrefab(path, nullptr),
              "saving a null entity fails rather than writing an empty file");
        check(editor::SceneSerializer::LoadPrefab(
                  (std::filesystem::temp_directory_path() / "gws_no_such.prefab").string(), scn)
                  == nullptr,
              "loading a missing prefab returns null");
        check(editor::SceneSerializer::LoadPrefab(path, nullptr) == nullptr,
              "loading into a null scene returns null");
    }

    // ---- a single entity with no children -----------------------------------
    {
        auto scn = std::make_shared<scene::Scene>("solo");
        auto lone = scn->CreateEntity("Lone");
        lone->GetTransform()->SetLocalPosition({1.0f, 2.0f, 3.0f});
        const std::string solo =
            (std::filesystem::temp_directory_path() / "gws_prefab_solo.prefab").string();
        check(editor::SceneSerializer::SavePrefab(solo, lone), "a childless entity saves");

        auto target = std::make_shared<scene::Scene>("solo_target");
        auto got = editor::SceneSerializer::LoadPrefab(solo, target);
        check(got != nullptr && got->GetName() == "Lone", "and instantiates");
        check(got && got->GetChildren().empty(), "with no children invented");
        check(got && near(got->GetTransform()->GetLocalPosition().y, 2.0f),
              "and its transform intact");
        std::error_code ec;
        std::filesystem::remove(solo, ec);
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);

    std::printf("\n%d/%d checks passed\n", g_pass, g_pass + g_fail);
    if (g_fail) { std::printf("FAILED\n"); return 1; }
    std::printf("OK\n");
    return 0;
}
