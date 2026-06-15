#pragma once

// ====================
// Scene Cook  (Pillar C2 / Master Plan Stage 2.2)
// ====================
//
// The cooker's core loop: turn an ImportedScene into a single content-
// addressed bundle the runtime loads with no parsing —
//   * each mesh -> cook_mesh() -> a CookedMesh blob (id = "scene/mesh/N")
//   * the manifest (materials + node hierarchy) -> serialized POD vectors
//     (id = "scene/manifest")
// The runtime opens the pak, reads the manifest, and pulls cooked meshes by
// id on demand.

#include "imported_scene.h"
#include "mesh_cook.h"
#include "pak.h"
#include "serialization/serialization_traits.h"

#include <string>
#include <vector>

namespace schizo::assets {

enum SceneTypeTag : uint32_t {
    Tag_CookedMesh    = 100,
    Tag_SceneManifest = 101,
    Tag_CookedTexture = 102,   // referenced material textures embedded by GUID
};

// A pre-cooked asset to embed in the scene bundle alongside the meshes — used
// to pack the material-referenced textures into the same pak, keyed by their
// content-addressed GUID (asset_id_from_path of the source path). The runtime
// recovers the GUID from CookedMaterial::albedo_tex and pulls the texture from
// the same bundle: this is the material -> texture link.
struct BundleAsset {
    AssetId              id  = invalid_asset;
    uint32_t             tag = 0;
    std::vector<uint8_t> bytes;
};

// The serializable manifest (everything but the heavy mesh geometry).
struct SceneManifest {
    std::vector<CookedMaterial> materials;
    std::vector<CookedNode>     nodes;
    std::vector<uint64_t>       mesh_ids;  // AssetId per ImportedScene.meshes[i]
};

}  // namespace schizo::assets

GWS_REFLECT_BEGIN(schizo::assets::SceneManifest)
    GWS_REFLECT_FIELD(materials)
    GWS_REFLECT_FIELD(nodes)
    GWS_REFLECT_FIELD(mesh_ids)
GWS_REFLECT_END()

namespace schizo::assets {

inline AssetId scene_mesh_id(std::string_view scene_name, size_t index) {
    std::string key(scene_name);
    key += "/mesh/";
    key += std::to_string(index);
    return asset_id_from_path(key);
}
inline AssetId scene_manifest_id(std::string_view scene_name) {
    std::string key(scene_name);
    key += "/manifest";
    return asset_id_from_path(key);
}

// Cook a scene into a bundle. Returns the bundle bytes. `codec` compresses each
// cooked mesh + the manifest per-entry (LZ4 = fast-load, Zstd = best ratio).
inline std::vector<uint8_t> cook_scene(
    const ImportedScene& scene,
    gws::compress::Codec codec = gws::compress::Codec::None,
    const std::vector<BundleAsset>& extras = {}) {
    // Register the manifest types once (so they're findable for load).
    gws::reflect::reflect<CookedMaterial>();
    gws::reflect::reflect<CookedNode>();
    gws::reflect::reflect<SceneManifest>();

    PakWriter w;
    SceneManifest manifest;
    manifest.materials = scene.materials;
    manifest.nodes     = scene.nodes;

    for (size_t i = 0; i < scene.meshes.size(); ++i) {
        const ImportedMeshData& m = scene.meshes[i];
        ImportedMesh im;
        im.vertices      = m.vertices.data();
        im.vertex_count  = static_cast<uint32_t>(m.vertices.size() / m.vertex_stride);
        im.vertex_stride = m.vertex_stride;
        im.indices       = m.indices.data();
        im.index_count   = static_cast<uint32_t>(m.indices.size());

        std::vector<uint8_t> cooked = cook_mesh(im);
        AssetId id = scene_mesh_id(scene.name, i);
        w.add(id, Tag_CookedMesh, cooked, codec);
        manifest.mesh_ids.push_back(id);
    }

    std::vector<uint8_t> manifest_bytes = gws::serialize::save(manifest);
    w.add(scene_manifest_id(scene.name), Tag_SceneManifest, manifest_bytes, codec);

    // Embed referenced textures (and any other extras) keyed by their GUID.
    for (const BundleAsset& e : extras)
        if (e.id != invalid_asset && !e.bytes.empty())
            w.add(e.id, e.tag, e.bytes, codec);

    return w.finalize();
}

}  // namespace schizo::assets
