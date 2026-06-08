#pragma once

#include "primitive_meshes.h"
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/vulkan_render_graph.h"
#include "vulkan/vulkan_gltf_loader.h"
#include "vulkan/vulkan_texture.h"
#include "scene.h"
#include "entity.h"
#include "transform.h"
#include "mesh_component.h"
#include "mesh_renderer_component.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <spdlog/spdlog.h>

namespace schizo::editor {

/// Per-entity Material cache. Materials are rebuilt when the entity's color
/// changes (cheap-enough comparison; editor-only path so cost is negligible).
class EntityMaterialCache {
public:
    void clear() { entries_.clear(); }

    gws::renderer::gpu::Material* get_or_create(
        uint32_t entity_id,
        const glm::vec4& color,
        float metallic, float roughness,
        float occlusion,
        const glm::vec3& emissive,
        float alpha_cutoff,                                    // 0 → opaque (no discard)
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout layout,
        VkDescriptorPool pool)
    {
        auto it = entries_.find(entity_id);
        if (it != entries_.end()) {
            const Entry& e = it->second;
            if (color_eq(e.color, color) &&
                std::abs(e.metallic - metallic) < 1e-4f &&
                std::abs(e.roughness - roughness) < 1e-4f &&
                std::abs(e.occlusion - occlusion) < 1e-4f &&
                std::abs(e.emissive.r - emissive.r) < 1e-4f &&
                std::abs(e.emissive.g - emissive.g) < 1e-4f &&
                std::abs(e.emissive.b - emissive.b) < 1e-4f &&
                std::abs(e.alpha_cutoff - alpha_cutoff) < 1e-4f) {
                return e.material.get();
            }
        }
        gws::renderer::gpu::MaterialUniforms params{};
        params.base_color_factor = color;
        params.metallic_factor   = metallic;
        params.roughness_factor  = roughness;
        params.occlusion_strength = occlusion;
        // emissive_factor.a doubles as alpha_cutoff for the G-Buffer
        // shader's discard test (see MaterialUniforms comment).
        params.emissive_factor   = glm::vec4(emissive, alpha_cutoff);
        auto mat = gws::renderer::gpu::Material::create(
            device, layout, pool, params,
            nullptr, nullptr, nullptr, nullptr, nullptr);
        Entry e{ color, metallic, roughness, occlusion, emissive, alpha_cutoff, std::move(mat) };
        gws::renderer::gpu::Material* raw = e.material.get();
        entries_[entity_id] = std::move(e);
        return raw;
    }

    /// Drop entries for entities no longer in the scene to avoid leaking
    /// descriptor sets when the user deletes entities.
    void prune(const std::shared_ptr<schizo::scene::Scene>& scene) {
        if (!scene) { entries_.clear(); return; }
        // Make a defensive copy to avoid iterator invalidation if scene is modified
        std::vector<uint32_t> active_ids;
        try {
            for (const auto& ent : scene->GetEntities()) {
                if (ent) active_ids.push_back(ent->GetId());
            }
        } catch (...) {
            // Scene was modified during iteration; clear cache to be safe
            entries_.clear();
            return;
        }
        std::unordered_map<uint32_t, Entry> kept;
        for (uint32_t id : active_ids) {
            auto it = entries_.find(id);
            if (it != entries_.end())
                kept.emplace(it->first, std::move(it->second));
        }
        entries_ = std::move(kept);
    }

private:
    struct Entry {
        glm::vec4 color{1.0f};
        float     metallic     = 0.0f;
        float     roughness    = 0.8f;
        float     occlusion    = 1.0f;
        glm::vec3 emissive{0.0f};
        float     alpha_cutoff = 0.0f;
        std::unique_ptr<gws::renderer::gpu::Material> material;
    };
    std::unordered_map<uint32_t, Entry> entries_;

    static bool color_eq(const glm::vec4& a, const glm::vec4& b) {
        const float eps = 1e-4f;
        return std::abs(a.r - b.r) < eps &&
               std::abs(a.g - b.g) < eps &&
               std::abs(a.b - b.b) < eps &&
               std::abs(a.a - b.a) < eps;
    }
};

/// Minimal Wavefront .obj parser — produces SceneVertex/index buffers and
/// computes flat normals when the file omits them. Triangulates polygons
/// with fan triangulation. Only the geometry section is consumed; materials
/// (mtllib / usemtl) are ignored — a default white material is used.
inline bool parse_obj_file(const std::string& path,
                           std::vector<gws::renderer::gpu::SceneVertex>& out_verts,
                           std::vector<uint32_t>& out_idx) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string tag; ls >> tag;
        if (tag == "v") {
            glm::vec3 p; ls >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (tag == "vn") {
            glm::vec3 n; ls >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (tag == "vt") {
            glm::vec2 t; ls >> t.x >> t.y;
            uvs.push_back(t);
        } else if (tag == "f") {
            // Collect all vertex specs on this line.
            std::vector<std::tuple<int,int,int>> face;
            std::string spec;
            while (ls >> spec) {
                int vi = 0, ti = 0, ni = 0;
                size_t a = spec.find('/');
                if (a == std::string::npos) {
                    vi = std::atoi(spec.c_str());
                } else {
                    vi = std::atoi(spec.substr(0, a).c_str());
                    size_t b = spec.find('/', a + 1);
                    if (b == std::string::npos) {
                        if (a + 1 < spec.size())
                            ti = std::atoi(spec.substr(a + 1).c_str());
                    } else {
                        if (b > a + 1)
                            ti = std::atoi(spec.substr(a + 1, b - a - 1).c_str());
                        if (b + 1 < spec.size())
                            ni = std::atoi(spec.substr(b + 1).c_str());
                    }
                }
                // OBJ indices are 1-based; negative means relative to end.
                auto resolve = [](int idx, int count) -> int {
                    if (idx > 0)  return idx - 1;
                    if (idx < 0)  return count + idx;
                    return -1;
                };
                face.emplace_back(
                    resolve(vi, (int)positions.size()),
                    resolve(ti, (int)uvs.size()),
                    resolve(ni, (int)normals.size())
                );
            }
            if (face.size() < 3) continue;

            // Fan-triangulate.
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                std::tuple<int,int,int> tri[3] = { face[0], face[i], face[i + 1] };

                // Compute the geometric normal of this triangle. We need it
                // for two reasons: (1) to supply per-vertex normals when the
                // OBJ doesn't ship any, and (2) to detect CW-vs-CCW winding
                // when it does. Some legacy exporters (older 3ds Max,
                // certain tools) write CW-wound OBJs; with back-face culling
                // on, those would disappear unless we fix the winding at
                // load time.
                glm::vec3 flat_n(0,1,0);
                glm::vec3 geom_n_raw(0,1,0);
                {
                    int ai = std::get<0>(tri[0]);
                    int bi = std::get<0>(tri[1]);
                    int ci = std::get<0>(tri[2]);
                    if (ai >= 0 && bi >= 0 && ci >= 0 &&
                        ai < (int)positions.size() &&
                        bi < (int)positions.size() &&
                        ci < (int)positions.size()) {
                        glm::vec3 e1 = positions[bi] - positions[ai];
                        glm::vec3 e2 = positions[ci] - positions[ai];
                        geom_n_raw = glm::cross(e1, e2);
                        float len = glm::length(geom_n_raw);
                        if (len > 1e-8f) flat_n = geom_n_raw / len;
                    }
                }

                bool have_n = std::get<2>(tri[0]) >= 0 &&
                              std::get<2>(tri[1]) >= 0 &&
                              std::get<2>(tri[2]) >= 0;

                // Winding check: if the file supplied normals and the
                // geometric normal of this triangle is anti-aligned with the
                // supplied normal, swap vertices 1 and 2 to flip winding.
                // Match what back-face culling expects (CCW from outside =
                // positive dot product with declared normal).
                if (have_n) {
                    int n0_idx = std::get<2>(tri[0]);
                    if (n0_idx >= 0 && n0_idx < (int)normals.size()) {
                        const glm::vec3& supplied = normals[n0_idx];
                        if (glm::dot(geom_n_raw, supplied) < 0.0f) {
                            std::swap(tri[1], tri[2]);
                        }
                    }
                }

                for (int k = 0; k < 3; ++k) {
                    int vi = std::get<0>(tri[k]);
                    int ti = std::get<1>(tri[k]);
                    int ni = std::get<2>(tri[k]);
                    if (vi < 0 || vi >= (int)positions.size()) continue;

                    gws::renderer::gpu::SceneVertex v{};
                    v.position = positions[vi];
                    v.normal   = (ni >= 0 && ni < (int)normals.size())
                               ? normals[ni] : flat_n;
                    v.uv       = (ti >= 0 && ti < (int)uvs.size())
                               ? uvs[ti] : glm::vec2(0.0f);
                    v.tangent  = glm::vec4(1, 0, 0, 1);
                    out_verts.push_back(v);
                    out_idx.push_back((uint32_t)out_verts.size() - 1);
                }
            }
        }
    }

    // Global winding-orientation check. The per-triangle pass above already
    // flips individual triangles whose geometric normal points opposite to
    // their file-supplied normal — that's correct per-triangle but can leave
    // a mesh that's globally still CW-wound if the supplied normals are
    // also globally inverted (rare, but it happens with some legacy assets).
    //
    // Strategy: pick a single criterion based on what data the OBJ actually
    // ships, and apply ONE global flip if needed.
    //   - Has `vn` normals → vote each triangle by dot(geom_n, supplied_avg).
    //     Trustworthy because the file is telling us which way the surface
    //     should face.
    //   - No `vn` lines → fall back to a centroid heuristic (does the face
    //     point away from the mesh centroid?). Works for closed-convex
    //     shapes; doesn't help much for skin-thin meshes or open shells.
    //
    // Crucially, we DO NOT run the centroid heuristic when normals are
    // available — that can produce false flips on non-convex meshes (e.g.
    // city scenes where streets at low Y vote "inward" against the mesh
    // centroid even though they're correctly wound).
    if (!out_verts.empty() && !out_idx.empty() && !normals.empty()) {
        // Path 1: voting by supplied normal agreement.
        size_t agree = 0, disagree = 0;
        for (size_t i = 0; i + 2 < out_idx.size(); i += 3) {
            const auto& v0 = out_verts[out_idx[i  ]];
            const auto& v1 = out_verts[out_idx[i+1]];
            const auto& v2 = out_verts[out_idx[i+2]];
            const glm::vec3 geom_n = glm::cross(v1.position - v0.position,
                                                v2.position - v0.position);
            // Average the three vertex normals — more robust than just v0's
            // (which can disagree at sharp edges due to smoothing groups).
            const glm::vec3 supplied_avg = v0.normal + v1.normal + v2.normal;
            const float d = glm::dot(geom_n, supplied_avg);
            if (d > 0.0f) ++agree; else if (d < 0.0f) ++disagree;
        }
        if (disagree > agree) {
            spdlog::info("OBJ global winding: {} disagree vs {} agree with supplied normals — flipping all triangles",
                         disagree, agree);
            for (size_t i = 0; i + 2 < out_idx.size(); i += 3) {
                std::swap(out_idx[i+1], out_idx[i+2]);
            }
        } else {
            spdlog::info("OBJ global winding: {} agree vs {} disagree — keeping as-is",
                         agree, disagree);
        }
    } else if (!out_verts.empty() && !out_idx.empty()) {
        // Path 2: no supplied normals — centroid heuristic.
        glm::vec3 centroid(0.0f);
        for (const auto& v : out_verts) centroid += v.position;
        centroid /= static_cast<float>(out_verts.size());

        size_t outward = 0, inward = 0;
        for (size_t i = 0; i + 2 < out_idx.size(); i += 3) {
            const glm::vec3& p0 = out_verts[out_idx[i  ]].position;
            const glm::vec3& p1 = out_verts[out_idx[i+1]].position;
            const glm::vec3& p2 = out_verts[out_idx[i+2]].position;
            const glm::vec3 face_center = (p0 + p1 + p2) / 3.0f;
            const glm::vec3 geom_n = glm::cross(p1 - p0, p2 - p0);
            const glm::vec3 from_centroid = face_center - centroid;
            const float d = glm::dot(geom_n, from_centroid);
            if (d > 0.0f) ++outward; else if (d < 0.0f) ++inward;
        }
        if (inward > outward) {
            spdlog::info("OBJ centroid winding: {} inward vs {} outward (no vn lines) — flipping",
                         inward, outward);
            for (size_t i = 0; i + 2 < out_idx.size(); i += 3) {
                std::swap(out_idx[i+1], out_idx[i+2]);
            }
        }
    }

    return !out_verts.empty() && !out_idx.empty();
}

/// Lazily-loaded cache of mesh assets, keyed by file path. Dispatches on
/// extension: .gltf/.glb → tinygltf; .obj → built-in parser. Failure is
/// cached as nullptr so we don't retry every frame.
class AssetMeshCache {
public:
    const gws::renderer::gpu::Scene* get_or_load(
        const std::string& path,
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout mat_layout,
        VkDescriptorPool mat_pool)
    {
        auto it = entries_.find(path);
        if (it != entries_.end()) {
            // Validate cached entry - check for nullptr before returning
            if (it->second) {
                return it->second.get();
            }
            // Cached as nullptr (failed load); don't retry
            return nullptr;
        }

        std::string ext;
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        std::unique_ptr<gws::renderer::gpu::Scene> scene;
        if (ext == ".gltf" || ext == ".glb") {
            if (device) {
                scene = gws::renderer::gpu::GltfLoader::load(
                    device, mat_layout, mat_pool, path);
            } else {
                spdlog::error("[AssetMeshCache] Device is null for {}", path);
            }
        } else if (ext == ".obj") {
            if (device) {
                scene = load_obj_scene(path, device, mat_layout, mat_pool);
            } else {
                spdlog::error("[AssetMeshCache] Device is null for {}", path);
            }
        } else {
            spdlog::warn("[AssetMeshCache] Unsupported extension: {}", path);
        }

        if (!scene) {
            spdlog::warn("[AssetMeshCache] Failed to load: {}", path);
            entries_[path] = nullptr;  // Cache the failure
            return nullptr;
        }
        
        // Validate loaded scene before caching
        if (scene->draw_items.empty() || scene->meshes.empty()) {
            spdlog::warn("[AssetMeshCache] Loaded {} but has no draw items or meshes", path);
            entries_[path] = nullptr;
            return nullptr;
        }
        
        spdlog::info("[AssetMeshCache] Loaded {}: {} draw items, {} meshes",
                     path, scene->draw_items.size(), scene->meshes.size());
        const auto* raw = scene.get();
        entries_[path] = std::move(scene);
        return raw;
    }

    void clear() { entries_.clear(); }

private:
    static std::unique_ptr<gws::renderer::gpu::Scene> load_obj_scene(
        const std::string& path,
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout mat_layout,
        VkDescriptorPool mat_pool)
    {
        using namespace gws::renderer::gpu;
        std::vector<SceneVertex> verts;
        std::vector<uint32_t>   idx;
        if (!parse_obj_file(path, verts, idx)) return nullptr;

        Submesh sm{}; sm.material_index = 0;
        sm.lods.push_back({0, (uint32_t)idx.size(), 0.0f});
        auto mesh = Mesh::create(device, verts, idx, {sm});
        if (!mesh) return nullptr;
        // OBJ files have no standardised winding convention — flag the mesh
        // double-sided so the G-Buffer renderer uses the cull-none pipeline.
        mesh->set_double_sided(true);

        MaterialUniforms params{};
        params.base_color_factor = glm::vec4(0.85f, 0.85f, 0.85f, 1.0f);
        params.metallic_factor   = 0.0f;
        params.roughness_factor  = 0.7f;
        auto mat = Material::create(device, mat_layout, mat_pool, params,
                                    nullptr, nullptr, nullptr, nullptr, nullptr);
        if (!mat) return nullptr;

        auto out = std::make_unique<Scene>();
        out->meshes.push_back(std::move(mesh));
        out->materials.push_back(std::move(mat));
        DrawItem di;
        di.mesh          = out->meshes[0].get();
        di.material      = out->materials[0].get();
        di.model         = glm::mat4(1.0f);
        di.submesh_index = 0;
        out->draw_items.push_back(di);
        return out;
    }

    std::unordered_map<std::string, std::unique_ptr<gws::renderer::gpu::Scene>> entries_;
};

/// Walk the scene's entities and build a draw list from those with renderable
/// components. Priority: if `MeshComponent::mesh_path` is set, load that
/// asset; else fall back to `MeshRendererComponent`'s primitive type.
/// Entities with neither (lights, empty parents) are skipped.
///
/// Output is split by alpha mode: AlphaMode::Blend entities go to
/// `out_transparent` (rendered by the forward transparent pass), everything
/// else goes to `out_opaque` (rendered by the deferred G-Buffer pass).
inline void build_draw_items(
    const std::shared_ptr<schizo::scene::Scene>& scene,
    const PrimitiveMeshCache& meshes,
    EntityMaterialCache& mat_cache,
    AssetMeshCache& asset_cache,
    gws::renderer::gpu::VulkanDevice* device,
    VkDescriptorSetLayout mat_layout,
    VkDescriptorPool mat_pool,
    std::vector<gws::renderer::gpu::DrawItem>& out_opaque,
    std::vector<gws::renderer::gpu::DrawItem>& out_transparent)
{
    out_opaque.clear();
    out_transparent.clear();
    if (!scene) return;

    for (const auto& ent : scene->GetEntities()) {
        if (!ent || !ent->IsActiveInHierarchy()) continue;

        const glm::mat4 model = ent->GetTransform()->GetWorldMatrix();

        // Resolve the entity's alpha mode for the primitive path. For the
        // asset path we route per-DrawItem based on the loaded glTF's
        // per-material alphaMode (see below).
        schizo::scene::AlphaMode am = schizo::scene::AlphaMode::Opaque;
        if (auto mr = ent->GetComponent<schizo::scene::MeshRendererComponent>()) {
            am = mr->GetAlphaMode();
        }

        const auto* mc = ent->GetMeshComponent();
        if (mc && !mc->mesh_path.empty()) {
            const auto* loaded = asset_cache.get_or_load(
                mc->mesh_path, device, mat_layout, mat_pool);
            if (loaded && !loaded->draw_items.empty()) {
                // If the entity has an explicit MeshRendererComponent with
                // AlphaMode::Blend, treat every sub-draw as blend (manual
                // override). Otherwise honour the per-material flag the
                // GltfLoader stamped onto each DrawItem.
                const bool entity_force_blend =
                    (am == schizo::scene::AlphaMode::Blend);
                for (const auto& src : loaded->draw_items) {
                    gws::renderer::gpu::DrawItem di = src;
                    di.model = model * src.model;
                    const bool blend = entity_force_blend || src.is_blend;
                    auto& target = blend ? out_transparent : out_opaque;
                    di.is_blend = blend;
                    target.push_back(di);
                }
                continue; // asset path takes precedence over primitive
            }
            // Fall through if load failed.
        }

        // Primitive path uses the entity-level AlphaMode (no per-submesh).
        auto& target = (am == schizo::scene::AlphaMode::Blend)
                           ? out_transparent
                           : out_opaque;

        // Primitive-driven mesh (hierarchy "+ Add Entity").
        auto mr = ent->GetComponent<schizo::scene::MeshRendererComponent>();
        if (!mr) continue;

        gws::renderer::gpu::Mesh* mesh = meshes.get(mr->GetMeshType());
        if (!mesh) continue;

        const glm::vec4& col = mr->GetColor();
        // alpha_cutoff serves two pipelines:
        //  - G-Buffer (opaque/cutout): discards fragments below cutoff.
        //  - Shadow caster: same — but Blend objects also pass through the
        //    shadow alpha-test pipeline, so they need a non-zero cutoff
        //    (0.5) to cast binarised shadows. Blend skips the G-Buffer
        //    entirely, so the cutoff there is irrelevant.
        float alpha_cutoff =
            (am == schizo::scene::AlphaMode::Cutout) ? mr->GetAlphaCutoff()
          : (am == schizo::scene::AlphaMode::Blend)  ? 0.5f
          : 0.0f;
        gws::renderer::gpu::Material* mat = mat_cache.get_or_create(
            ent->GetId(), col,
            mr->GetMetallic(), mr->GetRoughness(),
            mr->GetOcclusion(), mr->GetEmissive(),
            alpha_cutoff,
            device, mat_layout, mat_pool);
        if (!mat) continue;

        gws::renderer::gpu::DrawItem di;
        di.mesh          = mesh;
        di.material      = mat;
        di.model         = model;
        di.submesh_index = 0;
        di.is_blend      = (am == schizo::scene::AlphaMode::Blend);
        target.push_back(di);
    }
}

} // namespace schizo::editor
