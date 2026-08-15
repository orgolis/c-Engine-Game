#pragma once

// ============================================================================
// MaterialEditorPanel — the inspector's surface editor.
//
// WHAT THIS REPLACES. The previous version of this panel had a full six-slot
// texture tab, PBR sliders and a preset list, and an Apply button whose whole
// implementation was:
//
//     // TODO: Implement actual material assignment to entity
//     spdlog::info("[MaterialEditor] Applying material with albedo ...");
//
// Nothing it did reached the renderer. The inspector consequently grew a SECOND
// block underneath it — "Mesh Renderer (live)" — carrying a comment explaining
// that the panel above was visual only. Two material UIs, one of which lied.
//
// So this panel now edits the things the renderer actually reads, and there is
// only one of them. It has two modes, and which one is active is stated on
// screen rather than inferred:
//
//   MATERIAL ASSET  — the entity references a .mat. Edits go to the shared
//                     asset, so every entity using it changes at once. That is
//                     the point of an asset and also its sharp edge, so the
//                     panel says how many other entities are affected.
//
//   INLINE          — no .mat assigned. Edits go to this entity's
//                     MeshRendererComponent: a colour, PBR scalars and a base
//                     texture, which is the whole of what a component can carry.
//                     The other four map slots are disabled with the reason
//                     given, because a texture field that silently does nothing
//                     is worse than no field at all.
//
// LIVE EDITING AND THE DISK. Slider drags write to the material cache in memory
// (so the viewport updates on the frame you drag) and to the file only when the
// gesture ends. Writing on every frame would issue hundreds of writes per drag
// and continuously retrigger the file watcher's settle window.
// ============================================================================

#include "assets/material_desc.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace schizo::scene {
class Entity;
class Scene;
}

namespace schizo::editor {

class MaterialAssetCache;

class MaterialEditorPanel {
public:
    MaterialEditorPanel();
    ~MaterialEditorPanel();

    /// Draw the panel for `entity`. Returns true when something changed that
    /// should mark the scene modified.
    ///
    /// `scene` is optional and used only to report how many OTHER entities share
    /// the material being edited — editing a shared asset while believing it is
    /// per-object is the one mistake this UI can invite, so it is answered up
    /// front rather than discovered later.
    bool Render(const std::shared_ptr<schizo::scene::Entity>& entity,
                MaterialAssetCache* materials,
                const std::shared_ptr<schizo::scene::Scene>& scene = nullptr);

private:
    /// Working copy of the asset being edited, so a drag can update the cache
    /// each frame and the file only on release.
    gws::assets::MaterialDesc edit_;
    std::string               edit_path_;    // which .mat edit_ mirrors
    bool                      dirty_ = false; // edited in memory, not yet saved

    /// Path text for the "save as / create" field, kept across frames.
    char new_path_buf_[260] = {0};

    bool render_asset_mode(const std::shared_ptr<schizo::scene::Entity>& entity,
                           MaterialAssetCache* materials,
                           const std::shared_ptr<schizo::scene::Scene>& scene);
    bool render_inline_mode(const std::shared_ptr<schizo::scene::Entity>& entity,
                            MaterialAssetCache* materials);
    bool render_presets(gws::assets::MaterialDesc& target);
};

}  // namespace schizo::editor
