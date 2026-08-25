// ============================================================================
// sceneio_check — a scene file gives back the scene that was saved.
//
// The scene format had no round-trip test, which is how particle emitters and
// NPC agents came to be missing from it entirely. Both components were added
// this phase, both were reflected specifically so they could be serialized, and
// neither was ever written: configure an emitter, save, reopen, and every
// setting was silently back at its default.
//
// reflectio_check covers the reflection layer. This covers the thing that was
// actually broken -- the WIRING. A library that round-trips perfectly and a
// serializer that never calls it look identical from the library's side.
//
// Every value here is deliberately non-default. Against defaults, a serializer
// that writes nothing and one that writes correctly give the same answer.
// ============================================================================
#include "scene_serializer.h"

#include "entity.h"
#include "npc_agent_component.h"
#include "particle_emitter_component.h"
#include "scene.h"
#include "skinned_mesh_component.h"
#include "transform.h"

#include "mesh_renderer_component.h"
#include "terrain_component.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

}  // namespace

int main() {
    std::printf("=== sceneio_check ===\n\n");

    const std::string path =
        (std::filesystem::temp_directory_path() / "gws_sceneio_check.scene").string();

    // ---- build a scene worth saving -----------------------------------------
    {
        auto scn = std::make_shared<scene::Scene>("roundtrip");

        auto fire = scn->CreateEntity("Campfire");
        fire->GetTransform()->SetLocalPosition({3.0f, 1.0f, -2.0f});
        auto* pec = fire->GetParticleEmitterComponent();
        pec->enabled       = true;
        pec->emitting      = true;
        pec->spawn_rate    = 37.5f;
        pec->max_particles = 2048;
        pec->color_start   = glm::vec4(1.0f, 0.6f, 0.1f, 0.9f);
        pec->color_end     = glm::vec4(0.2f, 0.05f, 0.0f, 0.0f);

        auto guard = scn->CreateEntity("Guard");
        guard->GetTransform()->SetLocalPosition({-8.0f, 0.0f, 5.0f});
        auto* nac = guard->GetNpcAgentComponent();
        nac->enabled       = true;
        nac->target_name   = "Hero";      // NOT the default "Player"
        nac->sight_range   = 42.0f;
        nac->sight_fov_deg = 77.0f;
        nac->move_speed    = 6.5f;
        nac->patrol_radius = 12.0f;
        nac->attack_range  = 3.5f;
        nac->ability_index = 2;

        // Terrain per-layer surface. Values chosen to be nothing like the
        // defaults, so a field that silently fails to round-trip reads as its
        // default and the assertion catches it.
        auto ground = scn->CreateEntity("Ground");
        ground->AddComponent<scene::TerrainComponent>();
        auto tc = ground->GetComponent<scene::TerrainComponent>();
        tc->SetLayerPath(0, "assets/textures/rock.png");
        tc->SetLayerMetallic(0, 0.80f);
        tc->SetLayerRoughness(0, 0.15f);
        tc->SetLayerNormalScale(0, 2.50f);
        tc->SetTiling(0, 48.0f);
        tc->SetTriplanar(true);
        tc->SetTriplanarSharpness(9.5f);
        tc->SetLayerMetallic(2, 0.30f);
        tc->SetLayerRoughness(2, 0.60f);

        // Material-instance overrides: a shared .mat plus this object's own
        // tint and UV. The mask and the values are separate state, so both are
        // pushed off their defaults.
        auto crate = scn->CreateEntity("Crate");
        crate->AddComponent<scene::MeshRendererComponent>();
        auto mrc = crate->GetComponent<scene::MeshRendererComponent>();
        check(mrc != nullptr, "the crate has a mesh renderer to override on");
        mrc->SetMaterialPath("assets/materials/wood.mat");
        using MR = scene::MeshRendererComponent;
        mrc->SetMaterialOverride(MR::kOverrideBaseColor, true);
        mrc->SetMaterialOverride(MR::kOverrideUv, true);
        mrc->SetColor(glm::vec4(0.9f, 0.2f, 0.1f, 1.0f));
        mrc->SetUvScale(glm::vec2(3.0f, 7.0f));
        mrc->SetUvOffset(glm::vec2(-0.25f, 0.5f));

        check(editor::SceneSerializer::SaveScene(path, scn), "the scene saves");
    }

    // ---- load it back into a fresh scene ------------------------------------
    auto loaded = editor::SceneSerializer::LoadScene(path);
    check(loaded != nullptr, "and loads again");
    if (!loaded) {
        std::printf("\n%d passed, %d failed\nFAIL sceneio_check\n", g_pass, g_fail + 1);
        return 1;
    }

    // ---- terrain per-layer surface -----------------------------------------
    // Without these a terrain layer could only be dielectric and 0.95 rough,
    // because the fallback material hard-coded both -- so terrain could not be
    // made to reflect without authoring a .mat.
    if (auto ground = loaded->GetEntityByName("Ground")) {
        auto tc = ground->GetComponent<scene::TerrainComponent>();
        check(tc != nullptr, "the terrain entity round-trips");
        if (tc) {
            check(tc->GetTriplanar(), "triplanar survives the round trip");
            check(std::fabs(tc->GetTriplanarSharpness() - 9.5f) < 1e-3f,
                  "and its blend sharpness with it");
            check(std::fabs(tc->GetLayerMetallic(0)    - 0.80f) < 1e-3f, "layer 0 metallic survives");
            check(std::fabs(tc->GetLayerRoughness(0)   - 0.15f) < 1e-3f, "layer 0 roughness survives");
            check(std::fabs(tc->GetLayerNormalScale(0) - 2.50f) < 1e-3f, "layer 0 normal scale survives");
            check(std::fabs(tc->GetLayerMetallic(2)    - 0.30f) < 1e-3f, "layer 2 metallic survives");
            check(std::fabs(tc->GetLayerRoughness(2)   - 0.60f) < 1e-3f, "layer 2 roughness survives");

            // The albedo path must be untouched by the surface keys sharing
            // its prefix. Not the trap it looks like -- starts_with demands an
            // '=' right after the key, so no collision is actually reachable,
            // and reversing the reader's key order leaves this passing. Kept
            // anyway: it costs nothing and it pins the round trip of a field
            // that sits next to five new ones.
            check(tc->GetLayerPath(0) == "assets/textures/rock.png",
                  "the albedo path did not swallow a surface key");

            // Untouched layers keep the historical look exactly.
            check(std::fabs(tc->GetLayerRoughness(1) - 0.95f) < 1e-3f,
                  "an untouched layer still defaults to the pre-0.7.6 roughness");
            check(std::fabs(tc->GetLayerMetallic(1)) < 1e-3f,
                  "and to dielectric");
        }
    } else {
        check(false, "the terrain entity round-trips");
    }

    // ---- material instance overrides ---------------------------------------
    if (auto crate = loaded->GetEntityByName("Crate")) {
        auto mrc = crate->GetComponent<scene::MeshRendererComponent>();
        check(mrc != nullptr, "the overriding object round-trips");
        if (mrc) {
            using MR = scene::MeshRendererComponent;
            check(mrc->GetMaterialPath() == "assets/materials/wood.mat",
                  "it still points at the shared material");
            check(mrc->HasMaterialOverride(MR::kOverrideBaseColor), "its tint override survives");
            check(mrc->HasMaterialOverride(MR::kOverrideUv),        "its UV override survives");
            // The MASK is not the VALUES. A serializer that writes one and not
            // the other leaves an override switched on with a default value --
            // the object renders wrong rather than not at all, which is worse.
            check(std::fabs(mrc->GetColor().r - 0.9f) < 1e-3f,   "and the tint value with it");
            check(std::fabs(mrc->GetUvScale().y - 7.0f) < 1e-3f, "and the UV tiling");
            check(std::fabs(mrc->GetUvOffset().x + 0.25f) < 1e-3f,
                  "and a negative UV offset");
            // Nothing was silently switched on.
            check(!mrc->HasMaterialOverride(MR::kOverrideMetallic),
                  "an untouched field is NOT overridden");
            check(!mrc->HasMaterialOverride(MR::kOverrideRoughness),
                  "nor is another one");
        }
    } else {
        check(false, "the overriding object round-trips");
    }

    auto fire  = loaded->GetEntityByName("Campfire");
    auto guard = loaded->GetEntityByName("Guard");
    check(fire && guard, "with both entities present");
    if (!fire || !guard) {
        std::printf("\n%d passed, %d failed\nFAIL sceneio_check\n", g_pass, g_fail + 1);
        return 1;
    }

    // ---- the emitter came back ----------------------------------------------
    {
        const auto* pec = fire->GetParticleEmitterComponent();
        check(pec && pec->enabled, "the emitter survived the round trip at all");
        check(pec && std::abs(pec->spawn_rate - 37.5f) < 1e-3f,
              "with its spawn rate, not the default");
        check(pec && pec->max_particles == 2048u, "and its particle budget");
        check(pec && std::abs(pec->color_start.r - 1.0f) < 1e-3f &&
              std::abs(pec->color_end.a - 0.0f) < 1e-3f,
              "and both colours, including a zero alpha");
    }

    // ---- the agent came back -------------------------------------------------
    {
        const auto* nac = guard->GetNpcAgentComponent();
        check(nac && nac->enabled, "the NPC agent survived");
        check(nac && nac->target_name == "Hero",
              "including its target name — a std::string, which reflection "
              "cannot carry and the serializer writes itself");
        check(nac && std::abs(nac->sight_range - 42.0f) < 1e-3f &&
              std::abs(nac->attack_range - 3.5f) < 1e-3f,
              "and its perception and combat ranges");
        check(nac && nac->ability_index == 2, "and which ability it swings");
    }

    // ---- transforms still work ----------------------------------------------
    // Proof that adding these did not disturb what already worked.
    {
        check(glm::length(fire->GetTransform()->GetLocalPosition() -
                          glm::vec3(3.0f, 1.0f, -2.0f)) < 1e-3f,
              "and the transforms that already round-tripped still do");
    }

    // ---- a scene with neither is unchanged ----------------------------------
    // Both are written only when enabled, so existing scene files must not
    // start growing lines the moment this shipped.
    {
        auto plain = std::make_shared<scene::Scene>("plain");
        plain->CreateEntity("Box")->GetTransform()->SetLocalPosition({1.0f, 2.0f, 3.0f});
        const std::string p2 =
            (std::filesystem::temp_directory_path() / "gws_sceneio_plain.scene").string();
        editor::SceneSerializer::SaveScene(p2, plain);

        std::FILE* f = std::fopen(p2.c_str(), "rb");
        std::string text;
        if (f) {
            char buf[4096]; size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            std::fclose(f);
        }
        check(text.find("EMITTER.") == std::string::npos &&
              text.find("NPCAGENT.") == std::string::npos,
              "a scene using neither writes neither — existing files do not churn");
        std::filesystem::remove(p2);
    }

    std::filesystem::remove(path);
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL sceneio_check\n");
    return g_fail ? 1 : 0;
}
