#pragma once

// ============================================================================
// What it MEANS to drop an asset on something.
//
// Dropping a .mat, a texture, a script or an HDR onto an object is the gesture
// people reach for first, and until now almost every drop target in the editor
// was a specific named field in the Inspector -- so the gesture only worked if
// you already knew which panel, which section and which text box.
//
// The rule table lives here, apart from any panel, for one reason: there are
// several places a drop can land (a hierarchy row, the viewport, later others)
// and if each decided for itself what a texture means, they would disagree.
// A texture that sets the albedo in one place and silently does nothing in
// another is not a bug anyone reports clearly.
//
// EVERY decision carries a message, including the refusals. A drop that does
// nothing and says nothing is indistinguishable from a drop that missed.
// ============================================================================

#include <algorithm>
#include <cctype>
#include <string>

namespace schizo::editor::assetdrop {

// ---------------------------------------------------------------------------
// What was dragged
// ---------------------------------------------------------------------------
enum class Kind { Unknown, Material, Texture, Hdr, Script, Mesh, Audio, Prefab, Scene };

/// Classify by extension. HDR is deliberately SEPARATE from Texture: an .hdr is
/// an environment map, and dropping one on an object almost never means "make
/// this object's albedo an equirectangular sky".
inline Kind kind_of(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return Kind::Unknown;
    std::string e = path.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (e == ".mat")    return Kind::Material;
    if (e == ".hdr")    return Kind::Hdr;
    if (e == ".prefab") return Kind::Prefab;
    if (e == ".scene")  return Kind::Scene;
    if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" ||
        e == ".tga" || e == ".dds" || e == ".ktx")   return Kind::Texture;
    if (e == ".py" || e == ".cs" || e == ".cpp" || e == ".lua") return Kind::Script;
    if (e == ".obj" || e == ".gltf" || e == ".glb" || e == ".fbx" ||
        e == ".usd" || e == ".usda" || e == ".usdc" || e == ".usdz") return Kind::Mesh;
    if (e == ".wav" || e == ".mp3" || e == ".ogg" || e == ".flac")   return Kind::Audio;
    return Kind::Unknown;
}

// ---------------------------------------------------------------------------
// What it landed on
// ---------------------------------------------------------------------------

/// A description of the drop target, filled in by the caller from the real
/// scene. Kept as plain data so the rules can be tested without a scene, a
/// renderer or an ImGui context.
struct Target {
    bool exists             = false;  ///< false = dropped on empty space
    bool has_mesh_renderer  = false;
    bool has_terrain        = false;
    bool has_material_asset = false;  ///< a .mat is assigned to the renderer
    int  material_sharers   = 0;      ///< OTHER entities using that same .mat
};

// ---------------------------------------------------------------------------
// What should happen
// ---------------------------------------------------------------------------
enum class Action {
    Reject,
    SetObjectMaterial,        ///< .mat  -> the renderer's material path
    SetTerrainLayerMaterial,  ///< .mat  -> the terrain's current paint layer
    SetInlineAlbedo,          ///< tex   -> the object's own albedo (no .mat assigned)
    SetMaterialAlbedo,        ///< tex   -> the assigned .mat's albedo (SHARED)
    SetTerrainLayerTexture,   ///< tex   -> the current paint layer's texture
    SetSceneSky,              ///< .hdr  -> the scene's sky, wherever it landed
    AddScript,                ///< script-> a ScriptComponent
    SetMesh,                  ///< mesh  -> the entity's mesh
    AttachAudio,              ///< audio -> an AudioSource
    InstantiatePrefab,
    LoadScene,
};

struct Decision {
    Action      action = Action::Reject;
    std::string message;          ///< always set; shown to the user either way
    bool        warns  = false;   ///< true when it succeeded but changed more than the target
};

inline Decision reject(std::string why) { return {Action::Reject, std::move(why), false}; }
inline Decision ok(Action a, std::string what, bool warns = false) {
    return {a, std::move(what), warns};
}

/// The rule table. `name` is the target's display name, used only in messages.
inline Decision decide(Kind k, const Target& t, const std::string& name = "this object") {
    switch (k) {
        // An HDR is scene-wide by nature, so it ignores what it landed on
        // entirely rather than being refused on an object. Refusing would be
        // technically defensible and useless: there is exactly one thing a
        // person means by dragging a sky into a scene.
        case Kind::Hdr:
            return ok(Action::SetSceneSky, "Set the scene sky. An .hdr is the environment, "
                                           "not a surface -- it applies to the scene, not to " + name + ".");

        case Kind::Prefab:
            return ok(Action::InstantiatePrefab,
                      t.exists ? "Instantiated under " + name + "."
                               : "Instantiated at the scene root.");

        case Kind::Scene:
            if (t.exists)
                return reject("A scene cannot be applied to an object. Double-click it in the "
                              "browser to open it instead.");
            return ok(Action::LoadScene, "Opening the scene.");

        case Kind::Material:
            if (!t.exists) return reject("Drop a material on an object or a terrain to apply it.");
            if (t.has_terrain)
                return ok(Action::SetTerrainLayerMaterial,
                          "Applied to the terrain's current paint layer. Pick a different layer "
                          "in the Inspector to change which one a drop lands on.");
            if (!t.has_mesh_renderer)
                return reject(name + " has no mesh to put a material on. Materials apply to "
                                     "objects that render geometry.");
            return ok(Action::SetObjectMaterial, "Applied the material to " + name + ".");

        case Kind::Texture:
            if (!t.exists) return reject("Drop a texture on an object or a terrain to apply it.");
            if (t.has_terrain)
                return ok(Action::SetTerrainLayerTexture,
                          "Applied to the terrain's current paint layer.");
            if (!t.has_mesh_renderer)
                return reject(name + " has no mesh to texture. Textures apply to objects that "
                                     "render geometry.");
            if (!t.has_material_asset)
                // No shared asset in the way, so this is a per-object change and
                // cannot surprise anyone else.
                return ok(Action::SetInlineAlbedo, "Set the base colour texture on " + name + ".");
            if (t.material_sharers > 0)
                // Applied, not refused -- but the count is stated, because a
                // material is a shared asset and silently repainting twenty
                // objects is the one outcome nobody predicts.
                return ok(Action::SetMaterialAlbedo,
                          "Set the base colour on the material used by " + name + " -- this also "
                          "changes " + std::to_string(t.material_sharers) + " other object" +
                          (t.material_sharers == 1 ? "" : "s") + " using it.",
                          /*warns=*/true);
            return ok(Action::SetMaterialAlbedo,
                      "Set the base colour on the material used by " + name + ".");

        case Kind::Script:
            if (!t.exists) return reject("Drop a script on an object to attach it.");
            return ok(Action::AddScript, "Attached the script to " + name + ".");

        case Kind::Mesh:
            if (!t.exists) return reject("Drop a mesh on an object to give it that shape.");
            return ok(Action::SetMesh, "Applied the mesh to " + name + ".");

        case Kind::Audio:
            if (!t.exists) return reject("Drop a sound on an object to play it from there.");
            return ok(Action::AttachAudio, "Attached the sound to " + name + ".");

        case Kind::Unknown:
            break;
    }
    return reject("The editor has no action for that kind of file.");
}

} // namespace schizo::editor::assetdrop
