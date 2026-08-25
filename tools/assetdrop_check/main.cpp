// ============================================================================
// assetdrop_check -- what dropping an asset on something MEANS.
//
// The rules live apart from any panel because a drop can land in several places
// (a hierarchy row, the viewport) and if each decided for itself, they would
// disagree. A texture that sets the albedo in one place and silently does
// nothing in another is not a bug anyone reports clearly.
//
// The failure modes asserted here are all silent ones:
//
//   * A texture dropped on a light, or a material on a camera, has nothing to
//     apply to. Doing nothing quietly is indistinguishable from a missed drop.
//   * A texture dropped on an object that shares its material repaints EVERY
//     object using it. Applying without saying so is the one outcome nobody
//     predicts.
//   * An .hdr classified as an ordinary texture would set an object's albedo to
//     an equirectangular sky, which renders -- as nonsense.
// ============================================================================

#include "asset_drop.h"

#include <cstdio>
#include <string>

namespace ad = schizo::editor::assetdrop;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

ad::Target object(bool with_mat = false, int sharers = 0) {
    ad::Target t;
    t.exists             = true;
    t.has_mesh_renderer  = true;
    t.has_material_asset = with_mat;
    t.material_sharers   = sharers;
    return t;
}
ad::Target terrain() {
    ad::Target t;
    t.exists = true;
    t.has_terrain = true;
    return t;
}
ad::Target bare() {           // a light, a camera, an empty
    ad::Target t;
    t.exists = true;
    return t;
}
ad::Target nothing() { return ad::Target{}; }

}  // namespace

int main() {
    std::printf("=== assetdrop_check ===\n\n");

    std::printf("Extensions classify to the right kind:\n");
    {
        check(ad::kind_of("a/b/rock.mat")   == ad::Kind::Material, ".mat");
        check(ad::kind_of("checker.bmp")    == ad::Kind::Texture,  ".bmp");
        check(ad::kind_of("t.PNG")          == ad::Kind::Texture,  "extension match is case-insensitive");
        check(ad::kind_of("sky.hdr")        == ad::Kind::Hdr,      ".hdr is NOT a plain texture");
        check(ad::kind_of("spin.py")        == ad::Kind::Script,   ".py");
        check(ad::kind_of("Car.obj")        == ad::Kind::Mesh,     ".obj");
        check(ad::kind_of("hit.wav")        == ad::Kind::Audio,    ".wav");
        check(ad::kind_of("tree.prefab")    == ad::Kind::Prefab,   ".prefab");
        check(ad::kind_of("main.scene")     == ad::Kind::Scene,    ".scene");
        check(ad::kind_of("notes.txt")      == ad::Kind::Unknown,  ".txt has no drop action");
        check(ad::kind_of("noextension")    == ad::Kind::Unknown,  "a name with no dot");
        // ".mat" must not match "automatic" or similar -- the split is on the
        // LAST dot, and a substring search would be wrong here.
        check(ad::kind_of("my.material.mat") == ad::Kind::Material, "a name with several dots");
        check(ad::kind_of("format.matter")  == ad::Kind::Unknown,  "'.matter' is not '.mat'");
    }

    std::printf("\nA material needs something that renders geometry:\n");
    {
        check(ad::decide(ad::Kind::Material, object()).action == ad::Action::SetObjectMaterial,
              "onto a mesh object: applied");
        check(ad::decide(ad::Kind::Material, terrain()).action == ad::Action::SetTerrainLayerMaterial,
              "onto terrain: goes to the current paint layer, not to a renderer it lacks");

        const ad::Decision on_light = ad::decide(ad::Kind::Material, bare(), "Sun");
        check(on_light.action == ad::Action::Reject, "onto a light: refused");
        check(on_light.message.find("Sun") != std::string::npos,
              "and the refusal names the object, so it is clear WHICH drop failed");
        check(!on_light.message.empty(), "a refusal is never silent");

        check(ad::decide(ad::Kind::Material, nothing()).action == ad::Action::Reject,
              "onto empty space: refused");
    }

    std::printf("\nA texture goes where it will not surprise anyone:\n");
    {
        const ad::Decision inline_case = ad::decide(ad::Kind::Texture, object(/*with_mat=*/false));
        check(inline_case.action == ad::Action::SetInlineAlbedo,
              "no material assigned: sets the object's OWN albedo, which affects nothing else");
        check(!inline_case.warns, "and warns about nothing, because nothing else changed");

        const ad::Decision solo = ad::decide(ad::Kind::Texture, object(true, /*sharers=*/0));
        check(solo.action == ad::Action::SetMaterialAlbedo,
              "material assigned, used by nobody else: applied to the material");
        check(!solo.warns, "no warning when it is nobody else's material");

        const ad::Decision shared = ad::decide(ad::Kind::Texture, object(true, /*sharers=*/7));
        check(shared.action == ad::Action::SetMaterialAlbedo,
              "material shared with others: still applied, not refused");
        check(shared.warns, "but flagged as affecting more than the target");
        check(shared.message.find("7") != std::string::npos,
              "and the message states HOW MANY others -- the number is the whole point");

        const ad::Decision one = ad::decide(ad::Kind::Texture, object(true, 1));
        check(one.message.find("1 other object.") != std::string::npos ||
              one.message.find("1 other object ") != std::string::npos ||
              one.message.find("1 other object") != std::string::npos,
              "one sharer reads as 'object', not 'objects'");
        check(shared.message.find("7 other objects") != std::string::npos,
              "seven read as 'objects'");

        check(ad::decide(ad::Kind::Texture, terrain()).action == ad::Action::SetTerrainLayerTexture,
              "onto terrain: the current paint layer");
        check(ad::decide(ad::Kind::Texture, bare()).action == ad::Action::Reject,
              "onto a light: refused rather than quietly ignored");
    }

    std::printf("\nAn .hdr is the environment, so it ignores what it lands on:\n");
    {
        const ad::Decision on_obj = ad::decide(ad::Kind::Hdr, object(), "Crate");
        check(on_obj.action == ad::Action::SetSceneSky,
              "dropped on an object: still sets the SCENE sky");
        check(on_obj.message.find("scene") != std::string::npos,
              "and says so, because the object did not change and the user watched it not change");
        check(ad::decide(ad::Kind::Hdr, nothing()).action == ad::Action::SetSceneSky,
              "dropped on empty space: same");
        check(ad::decide(ad::Kind::Hdr, terrain()).action == ad::Action::SetSceneSky,
              "dropped on terrain: same");
    }

    std::printf("\nScripts, meshes and sounds attach to an object:\n");
    {
        check(ad::decide(ad::Kind::Script, object()).action == ad::Action::AddScript, "script onto an object");
        check(ad::decide(ad::Kind::Script, bare()).action == ad::Action::AddScript,
              "script onto a light too -- a script needs no mesh, unlike a material");
        check(ad::decide(ad::Kind::Script, nothing()).action == ad::Action::Reject,
              "script onto empty space: refused");

        check(ad::decide(ad::Kind::Mesh, object()).action == ad::Action::SetMesh, "mesh onto an object");
        check(ad::decide(ad::Kind::Mesh, nothing()).action == ad::Action::Reject, "mesh onto empty space");

        check(ad::decide(ad::Kind::Audio, object()).action == ad::Action::AttachAudio, "sound onto an object");
        check(ad::decide(ad::Kind::Audio, nothing()).action == ad::Action::Reject, "sound onto empty space");
    }

    std::printf("\nPrefabs and scenes:\n");
    {
        check(ad::decide(ad::Kind::Prefab, object()).action == ad::Action::InstantiatePrefab,
              "prefab onto an object: instantiated under it");
        check(ad::decide(ad::Kind::Prefab, nothing()).action == ad::Action::InstantiatePrefab,
              "prefab onto empty space: instantiated at the root");

        const ad::Decision scene_on_obj = ad::decide(ad::Kind::Scene, object());
        check(scene_on_obj.action == ad::Action::Reject,
              "a scene cannot be applied to an object");
        check(scene_on_obj.message.find("Double-click") != std::string::npos,
              "and the refusal says what to do instead");
        check(ad::decide(ad::Kind::Scene, nothing()).action == ad::Action::LoadScene,
              "a scene onto empty space opens it");
    }

    std::printf("\nEvery decision explains itself:\n");
    {
        const ad::Kind kinds[] = {ad::Kind::Material, ad::Kind::Texture, ad::Kind::Hdr,
                                  ad::Kind::Script, ad::Kind::Mesh, ad::Kind::Audio,
                                  ad::Kind::Prefab, ad::Kind::Scene, ad::Kind::Unknown};
        const ad::Target targets[] = {object(), object(true, 3), terrain(), bare(), nothing()};
        bool all_spoke = true;
        for (ad::Kind k : kinds)
            for (const ad::Target& t : targets)
                if (ad::decide(k, t).message.empty()) all_spoke = false;
        check(all_spoke,
              "no combination of kind and target produces a silent result -- 45 combinations");

        check(ad::decide(ad::Kind::Unknown, object()).action == ad::Action::Reject,
              "an unknown file type is refused, not guessed at");
    }

    std::printf("\n%d/%d checks passed\n", g_pass, g_pass + g_fail);
    if (g_fail) { std::printf("FAILED\n"); return 1; }
    std::printf("OK\n");
    return 0;
}
