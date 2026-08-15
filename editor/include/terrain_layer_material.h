#pragma once

// ============================================================================
// terrain_layer_material — how a terrain layer decides what surface it has.
//
// Split out of the GPU cache, and Vulkan-free, for one reason: this rule is
// exactly the kind that regresses silently. If "a material wins over the legacy
// texture" quietly inverts, nothing errors — the terrain just renders last
// month's texture, and the person who assigned the material concludes the
// feature is broken. Keeping it here means `terrainmat_check` can assert it
// without a GPU, and means the GPU cache and any future consumer cannot drift
// apart by each implementing their own version.
//
// THE COMPATIBILITY CLAUSE. A layer with no material falls back to its single
// albedo path with roughness 0.95 — not the MaterialDesc default of 1.0. That
// 0.95 is what terrain rendered with before layer materials existed (it was
// hardcoded in the terrain cache), so an existing project opens looking exactly
// as it did. Upgrading an engine must not restyle a game's terrain.
// ============================================================================

#include "assets/material_desc.h"
#include "material_asset_cache.h"
#include "terrain_component.h"

namespace schizo::editor {

/// Roughness a layer falls back to when it has no material asset. Matches the
/// value the terrain shader used unconditionally before layers had materials.
inline constexpr float kTerrainLegacyRoughness = 0.95f;

/// Resolve terrain layer `index`'s surface.
///
/// An assigned `.mat` supplies EVERYTHING — the same "an asset wins entirely"
/// rule MeshRendererComponent follows, so a material looks identical painted on
/// terrain or assigned to an object. A material that is assigned but fails to
/// load falls back to the legacy path rather than rendering nothing: a missing
/// file should degrade a layer, not erase the terrain.
///
/// Tiling is deliberately NOT part of the result. It lives on the component
/// because it describes this terrain's use of the material, not the material —
/// one rock has to repeat 8 times across a 100 m terrain and 300 times across a
/// 4 km one.
inline gws::assets::MaterialDesc resolve_terrain_layer_material(
    const schizo::scene::TerrainComponent& terrain,
    int index,
    MaterialAssetCache* materials)
{
    const std::string& mat_path = terrain.GetLayerMaterial(index);
    if (materials && !mat_path.empty()) {
        if (const gws::assets::MaterialDesc* m = materials->get(mat_path))
            return *m;
    }

    gws::assets::MaterialDesc d;
    d.albedo_map = terrain.GetLayerPath(index);
    d.roughness  = kTerrainLegacyRoughness;
    return d;
}

}  // namespace schizo::editor
