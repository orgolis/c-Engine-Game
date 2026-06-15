#pragma once

// ====================
// Cooked-asset runtime loader  (Master Plan Stage 2.2 — closes the cook->runtime loop)
// ====================
//
// The runtime half of the asset pipeline: takes a `.pak` bundle produced by
// `tools/assetcook` and turns it into a GPU-resident `gws::renderer::gpu::Scene`
// the editor renders, WITHOUT ever parsing the source .obj/.gltf again.
//
//   source.obj --assetcook--> cooked/source.pak --[this]--> Scene (GPU meshes)
//
// "No runtime parse" is the point: the pak is memory-mapped (PakFile), the
// scene manifest + cooked meshes are read zero-copy (decompressed only when the
// bundle is LZ4/Zstd), and cooked vertices are expanded straight into renderer
// SceneVertex records via the fixed ImportedVertex layout (imported_scene.h) —
// no welding, no triangulation, no re-optimisation. The precomputed LOD chain
// in the cooked blob becomes the mesh's LODs verbatim (so cooked LODs ARE the
// runtime LODs — Stage 2 acceptance "cooked LODs match runtime-generated").

#include "vulkan/vulkan_scene_mesh.h"      // SceneVertex, Mesh, Submesh, MeshLod
#include "vulkan/vulkan_scene_material.h"  // Material, MaterialUniforms
#include "vulkan/vulkan_texture.h"         // Texture (cooked BC7 albedo upload)
#include "vulkan/vulkan_gltf_loader.h"     // Scene
#include "vulkan/vulkan_render_graph.h"    // DrawItem

#include "asset_pipeline/pak_file.h"
#include "asset_pipeline/scene_cook.h"     // SceneManifest, Tag_SceneManifest, scene_*_id
#include "asset_pipeline/mesh_cook.h"      // CookedMeshView
#include "asset_pipeline/texture_format.h" // CookedTextureView / TexFormat (runtime-safe)
#include "asset_pipeline/imported_scene.h" // ImportedVertex (cooked vertex layout)
#include "serialization/serialization.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace schizo::editor {

/// Diagnostics from a cooked-pak load — used by the startup self-check to
/// prove the loop ran on real content (and by callers that want to log it).
struct CookedSceneStats {
    uint32_t mesh_count    = 0;
    uint32_t material_count = 0;
    uint32_t node_count    = 0;
    uint32_t total_vertices = 0;
    uint32_t lod0_indices  = 0;   // index count of the first mesh's LOD 0
    uint32_t lod_tiers     = 0;   // LOD tiers on the first mesh (>=1)
    uint32_t meshlets      = 0;   // offline meshlets on the first mesh
    uint32_t linked_textures   = 0;  // materials referencing an albedo texture
    uint32_t resolved_textures = 0;  // …whose GUID is present in this bundle
    uint32_t uploaded_textures = 0;  // …actually uploaded to the GPU as BCn
    glm::vec3 bounds_min{0.0f};
    glm::vec3 bounds_max{0.0f};
};

namespace detail {

/// Map a cooked BC TexFormat to its Vulkan format. Albedo is sampled in sRGB;
/// BC5 (2-channel, e.g. normals) is always linear.
inline VkFormat bc_to_vk_format(assets::TexFormat f, bool srgb) {
    switch (f) {
        case assets::TexFormat::BC1: return srgb ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case assets::TexFormat::BC3: return srgb ? VK_FORMAT_BC3_SRGB_BLOCK      : VK_FORMAT_BC3_UNORM_BLOCK;
        case assets::TexFormat::BC5: return VK_FORMAT_BC5_UNORM_BLOCK;
        case assets::TexFormat::BC7: return srgb ? VK_FORMAT_BC7_SRGB_BLOCK      : VK_FORMAT_BC7_UNORM_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

/// Expand a cooked mesh blob into a GPU Mesh, preserving the cooked LOD chain.
/// `view` must already be open()'d over the cooked-mesh bytes.
inline std::unique_ptr<gws::renderer::gpu::Mesh> build_cooked_mesh(
    const assets::CookedMeshView& view,
    gws::renderer::gpu::VulkanDevice* device,
    CookedSceneStats* first_stats)
{
    using gws::renderer::gpu::SceneVertex;
    using gws::renderer::gpu::Submesh;
    using gws::renderer::gpu::MeshLod;

    const assets::CookedMeshHeader& h = *view.header;
    const uint32_t vcount = h.vertex_count;
    const uint32_t stride = h.vertex_stride;
    if (vcount == 0 || h.total_index_count == 0) return nullptr;

    // --- vertices: expand cooked stride-byte records into SceneVertex ---
    std::vector<SceneVertex> verts(vcount);
    const bool known_layout = (stride == sizeof(assets::ImportedVertex));
    if (!known_layout) {
        spdlog::warn("[CookedLoader] cooked mesh stride {} != ImportedVertex (32); "
                     "using position-only fallback", stride);
    }
    for (uint32_t i = 0; i < vcount; ++i) {
        const uint8_t* src = view.vertices + static_cast<size_t>(i) * stride;
        SceneVertex& dst = verts[i];
        if (known_layout) {
            const auto* iv = reinterpret_cast<const assets::ImportedVertex*>(src);
            dst.position = glm::vec3(iv->px, iv->py, iv->pz);
            dst.normal   = glm::vec3(iv->nx, iv->ny, iv->nz);
            dst.uv       = glm::vec2(iv->u,  iv->v);
        } else {
            // Position is always the first 3 floats; the rest is unknown.
            const float* p = reinterpret_cast<const float*>(src);
            dst.position = glm::vec3(p[0], p[1], p[2]);
            dst.normal   = glm::vec3(0.0f, 1.0f, 0.0f);
            dst.uv       = glm::vec2(0.0f);
        }
        dst.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);  // OBJ carries no tangents
    }

    // --- indices: the cooked blob already holds every LOD's range, packed ---
    std::vector<uint32_t> indices(view.indices,
                                  view.indices + h.total_index_count);

    // --- submesh: adopt the cooked LOD ranges verbatim ---
    Submesh sm{};
    sm.material_index = 0;
    for (uint32_t li = 0; li < h.lod_count; ++li) {
        const assets::CookedLod& cl = view.lods[li];
        MeshLod lod{};
        lod.index_offset = cl.index_offset;
        lod.index_count  = cl.index_count;
        // The cooked blob stores simplification error, not a screen distance.
        // Assign monotonically increasing thresholds (LOD0 always at 0) so the
        // runtime LOD selector falls back to coarser tiers with distance — same
        // convention the glTF path uses (0 / 15 / 30 …).
        lod.distance_threshold = (li == 0) ? 0.0f : 15.0f * static_cast<float>(li);
        sm.lods.push_back(lod);
    }
    if (sm.lods.empty()) {  // defensive: a malformed blob with no LOD table
        sm.lods.push_back({0, h.total_index_count, 0.0f});
    }

    auto mesh = gws::renderer::gpu::Mesh::create(device, verts, indices, {sm});
    if (!mesh) return nullptr;
    // Cooked content currently originates from OBJ, whose winding is not
    // standardised — draw both sides so back-faces never vanish.
    mesh->set_double_sided(true);

    if (first_stats) {
        first_stats->total_vertices += vcount;
        if (first_stats->lod0_indices == 0) {
            first_stats->lod0_indices = sm.lods[0].index_count;
            first_stats->lod_tiers    = static_cast<uint32_t>(sm.lods.size());
            first_stats->meshlets     = h.meshlet_count;
            first_stats->bounds_min   = glm::vec3(h.bounds_min[0], h.bounds_min[1], h.bounds_min[2]);
            first_stats->bounds_max   = glm::vec3(h.bounds_max[0], h.bounds_max[1], h.bounds_max[2]);
        }
    }
    return mesh;
}

}  // namespace detail

/// CPU-only inspection of a cooked scene bundle: memory-maps it, reads the
/// manifest + every cooked-mesh header, and fills `stats` — WITHOUT touching
/// the GPU. Used by the startup self-check to prove the bundle loads via mmap
/// with no source parse (and to size the spawned preview entity). Returns false
/// if the file isn't a scene bundle or is corrupt.
inline bool inspect_cooked_pak(const std::string& pak_path, CookedSceneStats& stats) {
    assets::PakFile pak;
    if (!pak.open(pak_path)) return false;
    const assets::PakReader& reader = pak.reader();

    assets::AssetId manifest_id = assets::invalid_asset;
    for (const auto& kv : reader.toc())
        if (kv.second.type_tag == assets::Tag_SceneManifest) { manifest_id = kv.first; break; }
    if (manifest_id == assets::invalid_asset) return false;

    gws::reflect::reflect<assets::CookedMaterial>();
    gws::reflect::reflect<assets::CookedNode>();
    gws::reflect::reflect<assets::SceneManifest>();

    std::vector<uint8_t> mb = reader.read(manifest_id);
    assets::SceneManifest manifest;
    if (mb.empty() || !gws::serialize::load(mb.data(), mb.size(), manifest)) return false;

    stats = CookedSceneStats{};
    stats.material_count = static_cast<uint32_t>(manifest.materials.size());
    stats.node_count     = static_cast<uint32_t>(manifest.nodes.size());

    // Texture GUID linking: each material's albedo_tex stores the source path;
    // its GUID = asset_id_from_path(path). Check that GUID resolves to a cooked
    // texture embedded in this same bundle.
    for (const assets::CookedMaterial& m : manifest.materials) {
        const std::string tex = m.albedo_tex;
        if (tex.empty()) continue;
        stats.linked_textures++;
        if (reader.has(assets::asset_id_from_path(tex))) stats.resolved_textures++;
    }

    glm::vec3 bmin(1e30f), bmax(-1e30f);
    bool first = true;
    for (assets::AssetId mid : manifest.mesh_ids) {
        std::vector<uint8_t> cb = reader.read(mid);
        assets::CookedMeshView v;
        if (cb.empty() || !v.open(cb.data(), cb.size())) continue;
        const assets::CookedMeshHeader& h = *v.header;
        stats.mesh_count++;
        stats.total_vertices += h.vertex_count;
        bmin = glm::min(bmin, glm::vec3(h.bounds_min[0], h.bounds_min[1], h.bounds_min[2]));
        bmax = glm::max(bmax, glm::vec3(h.bounds_max[0], h.bounds_max[1], h.bounds_max[2]));
        if (first) {
            stats.lod0_indices = (h.lod_count > 0) ? v.lods[0].index_count : h.total_index_count;
            stats.lod_tiers    = h.lod_count;
            stats.meshlets     = h.meshlet_count;
            first = false;
        }
    }
    if (stats.mesh_count == 0) return false;
    stats.bounds_min = bmin;
    stats.bounds_max = bmax;
    return true;
}

/// Load a cooked `.pak` bundle into a renderable Scene. Returns nullptr if the
/// file isn't a scene bundle (e.g. a `.tex.pak`) or on GPU upload failure.
inline std::unique_ptr<gws::renderer::gpu::Scene> load_cooked_pak_scene(
    const std::string& pak_path,
    gws::renderer::gpu::VulkanDevice* device,
    VkDescriptorSetLayout mat_layout,
    VkDescriptorPool mat_pool,
    CookedSceneStats* out_stats = nullptr)
{
    if (!device) { spdlog::error("[CookedLoader] null device"); return nullptr; }

    assets::PakFile pak;
    if (!pak.open(pak_path)) {
        spdlog::warn("[CookedLoader] could not mmap pak: {}", pak_path);
        return nullptr;
    }
    const assets::PakReader& reader = pak.reader();

    // Find the scene manifest by type tag (the pak is keyed by content-hash
    // AssetId, so we can't recompute the id without the scene name — scan).
    assets::AssetId manifest_id = assets::invalid_asset;
    for (const auto& kv : reader.toc()) {
        if (kv.second.type_tag == assets::Tag_SceneManifest) {
            manifest_id = kv.first; break;
        }
    }
    if (manifest_id == assets::invalid_asset) {
        spdlog::warn("[CookedLoader] {} has no scene manifest (not a scene bundle)", pak_path);
        return nullptr;
    }

    // Reflection must know these types before the archive can rebuild them.
    gws::reflect::reflect<assets::CookedMaterial>();
    gws::reflect::reflect<assets::CookedNode>();
    gws::reflect::reflect<assets::SceneManifest>();

    std::vector<uint8_t> manifest_bytes = reader.read(manifest_id);
    assets::SceneManifest manifest;
    if (manifest_bytes.empty() ||
        !gws::serialize::load(manifest_bytes.data(), manifest_bytes.size(), manifest)) {
        spdlog::warn("[CookedLoader] failed to deserialize manifest in {}", pak_path);
        return nullptr;
    }

    auto scene = std::make_unique<gws::renderer::gpu::Scene>();
    CookedSceneStats stats;

    // --- meshes (one per manifest.mesh_ids entry, in order) ---
    for (assets::AssetId mid : manifest.mesh_ids) {
        std::vector<uint8_t> mbytes = reader.read(mid);
        assets::CookedMeshView view;
        if (mbytes.empty() || !view.open(mbytes.data(), mbytes.size())) {
            spdlog::warn("[CookedLoader] bad cooked mesh {} in {}", mid, pak_path);
            scene->meshes.push_back(nullptr);   // keep index alignment with nodes
            continue;
        }
        scene->meshes.push_back(detail::build_cooked_mesh(view, device, &stats));
    }
    stats.mesh_count = static_cast<uint32_t>(scene->meshes.size());

    // --- materials (parameter block + cooked BC7 albedo bound by GUID) ---
    for (const assets::CookedMaterial& cm : manifest.materials) {
        gws::renderer::gpu::MaterialUniforms params{};
        params.base_color_factor = glm::vec4(cm.base_color[0], cm.base_color[1],
                                             cm.base_color[2], cm.base_color[3]);
        params.metallic_factor   = cm.metallic;
        params.roughness_factor  = cm.roughness;

        // Resolve the albedo texture by GUID and upload it straight as BCn (no
        // CPU decode) so the cooked material renders WITH its texture.
        gws::renderer::gpu::Texture* albedo = nullptr;
        if (cm.albedo_tex[0]) {
            stats.linked_textures++;
            const std::string tex = cm.albedo_tex;
            const assets::AssetId tid = assets::asset_id_from_path(tex);
            std::vector<uint8_t> tb = reader.read(tid);
            assets::CookedTextureView tv;
            if (!tb.empty() && tv.open(tb.data(), tb.size())) {
                stats.resolved_textures++;
                const auto fmt = static_cast<assets::TexFormat>(tv.header->format);
                const VkFormat vkfmt = detail::bc_to_vk_format(fmt, /*srgb=*/true);
                const size_t block_bytes = tb.size() - sizeof(assets::CookedTextureHeader);
                auto t = gws::renderer::gpu::Texture::create_compressed(
                    device, vkfmt, tv.header->width, tv.header->height,
                    tv.header->mip_count, tv.blocks, block_bytes);
                if (t) {
                    albedo = t.get();
                    scene->textures.push_back(std::move(t));
                    stats.uploaded_textures++;
                    spdlog::info("[CookedLoader] albedo BC7 uploaded: {}x{} {} mips -> material '{}'",
                                 tv.header->width, tv.header->height, tv.header->mip_count, cm.name);
                }
            }
        }

        auto mat = gws::renderer::gpu::Material::create(
            device, mat_layout, mat_pool, params,
            albedo, nullptr, nullptr, nullptr, nullptr);
        scene->materials.push_back(std::move(mat));
    }
    stats.material_count = static_cast<uint32_t>(scene->materials.size());
    stats.node_count     = static_cast<uint32_t>(manifest.nodes.size());

    // --- draw items from the node hierarchy ---
    // Compose world matrices by walking parents (nodes are emitted parent-
    // before-child by every importer, so a single forward pass suffices; a
    // node whose parent index is >= its own falls back to its local matrix).
    std::vector<glm::mat4> world(manifest.nodes.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < manifest.nodes.size(); ++i) {
        const assets::CookedNode& n = manifest.nodes[i];
        const glm::mat4 local = glm::make_mat4(n.local_xform);
        world[i] = (n.parent >= 0 && static_cast<size_t>(n.parent) < i)
                       ? world[n.parent] * local
                       : local;
        if (n.mesh < 0 || static_cast<size_t>(n.mesh) >= scene->meshes.size()) continue;
        gws::renderer::gpu::Mesh* mesh = scene->meshes[n.mesh].get();
        if (!mesh) continue;
        gws::renderer::gpu::Material* mat =
            (n.material >= 0 && static_cast<size_t>(n.material) < scene->materials.size())
                ? scene->materials[n.material].get() : nullptr;

        gws::renderer::gpu::DrawItem di;
        di.mesh          = mesh;
        di.material      = mat;
        di.model         = world[i];
        di.submesh_index = 0;
        di.is_blend      = false;
        scene->draw_items.push_back(di);
    }

    // Fallback: a bundle with meshes but no mesh-bearing nodes still renders.
    if (scene->draw_items.empty()) {
        for (auto& m : scene->meshes) {
            if (!m) continue;
            gws::renderer::gpu::DrawItem di;
            di.mesh = m.get();
            di.material = scene->materials.empty() ? nullptr : scene->materials[0].get();
            di.model = glm::mat4(1.0f);
            scene->draw_items.push_back(di);
        }
    }

    if (scene->draw_items.empty()) {
        spdlog::warn("[CookedLoader] {} produced no draw items", pak_path);
        return nullptr;
    }
    if (out_stats) *out_stats = stats;
    return scene;
}

}  // namespace schizo::editor
