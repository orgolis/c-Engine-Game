#pragma once

#include "primitive_meshes.h"
#include "material_cache.h"       // .mat assets -> MaterialDesc -> gpu::Material
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/vulkan_terrain_material.h"
#include "vulkan/vulkan_render_graph.h"
#include "vulkan/vulkan_gltf_loader.h"
#include "vulkan/vulkan_texture.h"
#include "vulkan/vulkan_texture_manager.h"   // managed texture loading (Stage 2)
#include "cooked_mesh_loader.h"   // cooked .pak bundles (Stage 2 cook->runtime loop)
#include "asset_path_util.h"      // utf8_path / resolve_asset_path (shared w/ physics)
#include "scene.h"
#include "entity.h"
#include "transform.h"
#include "mesh_component.h"
#include "mesh_renderer_component.h"
#include "terrain_component.h"
#include "terrain_layer_material.h"   // one place decides a layer's surface
#include "ecs_bridge.h"   // authoritative ECS world matrices (Stage 1.4 step 2)
#include "assets/asset_watcher.h"   // shared hot-reload file watching
#include "jobs/task_runner.h"       // background OBJ parsing
#include "asset_pipeline/obj_import.h"   // OBJ + .mtl (shared with the cooker)

#include <chrono>
#include <functional>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <spdlog/spdlog.h>
#include <cstdint>

namespace schizo::editor {

/// Build the surface description for an entity's MeshRendererComponent.
///
/// One resolution rule, in one place: an assigned `.mat` supplies EVERYTHING,
/// and the component's inline fields are used only when there is no material
/// asset (or it failed to load). Two half-materials competing for the same
/// surface — some fields from the asset, some from the component — is precisely
/// the confusion this replaces, so there is no blending between them.
///
/// Alpha mode always comes from the COMPONENT even when an asset is assigned:
/// it decides which render pass the entity is drawn in, which is a property of
/// this placement rather than of the material. The same glass material can be
/// legitimately cutout on one entity and blended on another.
inline gws::assets::MaterialDesc resolve_entity_material(
    const schizo::scene::MeshRendererComponent* mr,
    MaterialAssetCache* materials)
{
    gws::assets::MaterialDesc d;
    if (!mr) return d;

    const gws::assets::MaterialDesc* asset =
        (materials && mr->HasMaterial()) ? materials->get(mr->GetMaterialPath()) : nullptr;
    if (asset) {
        d = *asset;
    } else {
        d.base_color         = mr->GetColor();
        d.metallic           = mr->GetMetallic();
        d.roughness          = mr->GetRoughness();
        d.occlusion          = mr->GetOcclusion();
        d.emissive           = mr->GetEmissive();
        d.emissive_intensity = mr->GetEmissiveIntensity();
        d.albedo_map         = mr->GetAlbedoTexturePath();
    }

    d.alpha_mode   = static_cast<gws::assets::MaterialAlphaMode>(mr->GetAlphaMode());
    d.alpha_cutoff = mr->GetAlphaCutoff();
    return d;
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
        VkDescriptorPool mat_pool,
        gws::renderer::gpu::TextureManager* textures = nullptr)
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

        // Scene files carry repo-root-relative paths; resolve independent of
        // the launch CWD (build-editor/bin vs repo root). The CACHE stays
        // keyed on the original path so scene identity is stable.
        const std::string disk_path = resolve_asset_path(path);

        std::string ext;
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        // ---- OBJ: parse on a worker, upload here -------------------------
        // parse_obj_file is pure CPU and is the expensive half for a large
        // model; Mesh::create / Material::create touch the device and must stay
        // on this thread. Splitting them is the whole reason the editor no
        // longer stalls on a big drop. glTF and .pak stay synchronous: the glTF
        // loader interleaves parsing with device calls, and .pak is already the
        // mmap zero-parse fast path.
        if ((ext == ".gltf" || ext == ".glb") && device && tasks_) {
            if (loading_.count(path)) return nullptr;
            loading_.insert(path);

            const std::string key = path;
            auto parsed = std::make_shared<std::shared_ptr<gws::renderer::gpu::ParsedGltfModel>>();

            tasks_->submit("Load " + std::filesystem::path(utf8_path(disk_path)).filename().string(),
                [disk_path, parsed](gws::tasks::TaskContext& ctx) {
                    ctx.set_progress(-1.0f, "parsing glTF");
                    auto m = gws::renderer::gpu::GltfLoader::parse(disk_path);
                    if (!ctx.cancelled()) *parsed = std::move(m);
                },
                [this, key, disk_path, parsed, device, mat_layout, mat_pool, textures]
                (const gws::tasks::TaskInfo& info) {
                    loading_.erase(key);
                    if (info.state != gws::tasks::TaskState::Succeeded || !*parsed) {
                        entries_[key] = nullptr;
                        std::error_code ec2;
                        const bool on_disk = std::filesystem::exists(utf8_path(disk_path), ec2);
                        spdlog::error("[AssetMeshCache] Failed to load '{}' (resolved '{}', file {})"
                                      " — the object keeps its primitive shape",
                                      key, disk_path,
                                      on_disk ? "present but parse failed" : "NOT FOUND on disk");
                        return;
                    }
                    // GPU work on the editor thread, per the TaskRunner contract.
                    auto built = gws::renderer::gpu::GltfLoader::build(
                        device, mat_layout, mat_pool, disk_path, *parsed, textures);
                    if (!built || built->draw_items.empty()) { entries_[key] = nullptr; return; }
                    spdlog::info("[AssetMeshCache] Loaded {} (async): {} draw items, {} meshes",
                                 key, built->draw_items.size(), built->meshes.size());
                    entries_[key] = std::move(built);
                });
            return nullptr;
        }

        if (ext == ".obj" && device && tasks_) {
            if (loading_.count(path)) return nullptr;   // already in flight
            loading_.insert(path);

            const std::string key = path;
            auto parsed = std::make_shared<ParsedObj>();

            tasks_->submit("Load " + std::filesystem::path(utf8_path(disk_path)).filename().string(),
                [disk_path, parsed](gws::tasks::TaskContext& ctx) {
                    ctx.set_progress(-1.0f, "parsing");
                    parsed->ok = schizo::assets::import_obj(disk_path, parsed->scene);
                    if (ctx.cancelled()) parsed->ok = false;
                },
                [this, key, disk_path, parsed, device, mat_layout, mat_pool, textures](const gws::tasks::TaskInfo& info) {
                    using namespace gws::renderer::gpu;
                    loading_.erase(key);
                    if (info.state != gws::tasks::TaskState::Succeeded || !parsed->ok ||
                        parsed->scene.meshes.empty()) {
                        // Cache the failure so a broken file is not re-parsed
                        // every frame — the same reason the sync path does it.
                        entries_[key] = nullptr;
                        // Same diagnosis the synchronous path gives: "NOT FOUND"
                        // means a path problem, "present" means parse/upload.
                        // An error that does not say which is a bug report
                        // nobody can act on.
                        std::error_code ec2;
                        const bool on_disk = std::filesystem::exists(utf8_path(disk_path), ec2);
                        spdlog::error("[AssetMeshCache] Failed to load '{}' (resolved '{}', file {})"
                                      " — the object keeps its primitive shape",
                                      key, disk_path,
                                      on_disk ? "present but parse failed" : "NOT FOUND on disk");
                        return;
                    }
                    auto built = build_obj_scene(parsed->scene, disk_path,
                                                 device, mat_layout, mat_pool, textures);
                    if (!built) { entries_[key] = nullptr; return; }
                    spdlog::info("[AssetMeshCache] Loaded {} (async): {} draw items, {} meshes",
                                 key, built->draw_items.size(), built->meshes.size());
                    entries_[key] = std::move(built);
                });

            // Null this frame: the caller already falls back to the entity's
            // primitive shape, so the model pops in when it is ready instead of
            // the editor freezing until it is.
            return nullptr;
        }

        std::unique_ptr<gws::renderer::gpu::Scene> scene;
        if (ext == ".gltf" || ext == ".glb") {
            if (device) {
                // Pass the TextureManager so glTF textures are deduplicated,
                // mipped, per-slot-sRGB-correct, hot-reloadable, and use the
                // shared default textures (cooked `.ctex` when present).
                scene = gws::renderer::gpu::GltfLoader::load(
                    device, mat_layout, mat_pool, disk_path, textures);
            } else {
                spdlog::error("[AssetMeshCache] Device is null for {}", path);
            }
        } else if (ext == ".obj") {
            if (device) {
                scene = load_obj_scene(disk_path, device, mat_layout, mat_pool, textures);
            } else {
                spdlog::error("[AssetMeshCache] Device is null for {}", path);
            }
        } else if (ext == ".pak") {
            // Cooked bundle: mmap + zero-parse load (the Stage 2 fast path).
            if (device) {
                scene = load_cooked_pak_scene(disk_path, device, mat_layout, mat_pool);
            } else {
                spdlog::error("[AssetMeshCache] Device is null for {}", path);
            }
        } else {
            spdlog::error("[AssetMeshCache] Unsupported mesh extension '{}' for '{}'", ext, path);
        }

        if (!scene) {
            // Report the ACTUAL cause: was the file found on disk, or did it parse
            // to nothing? "MISSING" => a path problem; "present" => a parse/upload
            // problem. This is what turns a silent "stays a cube" into a fixable log.
            std::error_code ec2;
            const bool on_disk = std::filesystem::exists(utf8_path(disk_path), ec2);
            spdlog::error("[AssetMeshCache] Failed to load '{}' (resolved '{}', file {}) — "
                          "the object keeps its primitive shape",
                          path, disk_path, on_disk ? "present but load failed" : "NOT FOUND on disk");
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

        // Watch the file this came from. Re-export the model from Blender and
        // the viewport follows — the reason this cache existed without reload
        // was that nothing invalidated it, not that reloading was hard.
        // Keyed on the ORIGINAL path so cache identity stays stable, but the
        // RESOLVED path is what gets watched, since that is the real file.
        if (!watched_.count(path)) {
            watched_.insert(path);
            const std::string key = path;
            watcher_.watch(disk_path, [this, key](const std::string&) {
                dirty_.push_back(key);
            });
        }

        const auto* raw = scene.get();
        entries_[path] = std::move(scene);
        return raw;
    }

    /// Drop cache entries whose source file changed on disk; the next
    /// get_or_load rebuilds them through the ordinary load path. Reloading by
    /// invalidation rather than in place means there is exactly ONE loader to
    /// keep correct, and a hot-reloaded mesh is byte-identical to one loaded at
    /// startup. Returns how many entries were dropped. Safe to call every frame.
    ///
    /// `idle_gpu` is called at most once, before the first entry is dropped:
    /// destroying a Scene frees GPU buffers that in-flight frames may still
    /// reference. Taken as a callback rather than a VulkanDevice* so this
    /// header keeps needing only a forward declaration of the device.
    size_t poll_reload(const std::function<void()>& idle_gpu) {
        using namespace std::chrono;
        watcher_.poll(duration<double>(steady_clock::now().time_since_epoch()).count());
        if (dirty_.empty()) return 0;

        std::vector<std::string> dirty;
        dirty.swap(dirty_);
        std::sort(dirty.begin(), dirty.end());
        dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());

        size_t dropped = 0;
        bool idled = false;
        for (const auto& key : dirty) {
            auto it = entries_.find(key);
            if (it == entries_.end()) continue;
            if (!idled) {
                if (!idle_gpu) return 0;    // cannot free GPU resources safely
                idle_gpu();
                idled = true;
            }
            entries_.erase(it);
            ++dropped;
            spdlog::info("[AssetMeshCache] '{}' changed on disk — reloading", key);
        }
        return dropped;
    }

    /// Files seen changing but not yet settled, for an honest "importing..."
    /// indicator rather than a UI that looks like it missed the edit.
    size_t reloads_pending() const { return watcher_.pending_count(); }

    /// Opt in to background OBJ parsing. Without this the cache is fully
    /// synchronous — which is what headless tools want, and keeps the async
    /// path from being a hidden dependency of merely constructing the cache.
    void set_task_runner(gws::tasks::TaskRunner* runner) { tasks_ = runner; }

    /// Meshes currently being parsed on a worker.
    size_t loads_in_flight() const { return loading_.size(); }

    /// Re-write descriptor sets of every cached scene's materials. Call after a
    /// texture one of them references was hot-reloaded in place (its
    /// VkImageView changed). The caller must ensure the GPU is idle.
    void rewrite_all_materials() {
        for (auto& [path, scene] : entries_) {
            (void)path;
            if (!scene) continue;
            for (auto& m : scene->materials)
                if (m) m->rewrite_textures();
        }
    }

    void clear() {
        entries_.clear();
        watcher_.clear();
        watched_.clear();
        dirty_.clear();
        loading_.clear();
    }

private:
    // Background parsing. Optional: without a runner the cache stays fully
    // synchronous, which is what the headless tools want.
    gws::tasks::TaskRunner*         tasks_ = nullptr;
    std::unordered_set<std::string> loading_;   // in flight, do not resubmit

    // Hot reload: one shared watcher, same settling behaviour as every other
    // reloadable asset type.
    gws::assets::AssetWatcher watcher_;
    std::unordered_set<std::string> watched_;   // paths already registered
    std::vector<std::string>        dirty_;     // cache keys to drop next poll

    // CPU-side result of an OBJ parse, handed from a worker to the editor thread.
    struct ParsedObj {
        schizo::assets::ImportedScene scene;
        bool ok = false;
    };

    static std::unique_ptr<gws::renderer::gpu::Scene> load_obj_scene(
        const std::string& path,
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout mat_layout,
        VkDescriptorPool mat_pool,
        gws::renderer::gpu::TextureManager* textures)
    {
        schizo::assets::ImportedScene imp;
        if (!schizo::assets::import_obj(path, imp)) return nullptr;
        return build_obj_scene(imp, path, device, mat_layout, mat_pool, textures);
    }

    // Everything after the parse. Device-touching, so it runs on the editor
    // thread in both the sync and async paths — one implementation, so an
    // async-loaded mesh is byte-identical to a synchronously loaded one.
    //
    // This consumes the asset-pipeline importer's ImportedScene rather than the
    // editor's old geometry-only parser. That parser ignored `mtllib`/`usemtl`
    // outright and built ONE grey material with all five texture slots null, so
    // no OBJ could ever show a texture from any direction — while a complete
    // OBJ+MTL importer (map_Kd, per-material submeshes, Ns→roughness, welded
    // indices) already existed for the cook pipeline. Sharing it means an OBJ
    // looks the same in the viewport as it will after cooking, and there is one
    // OBJ parser to keep correct instead of two that disagree.
    static std::unique_ptr<gws::renderer::gpu::Scene> build_obj_scene(
        const schizo::assets::ImportedScene& imp,
        const std::string& source_path,
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout mat_layout,
        VkDescriptorPool mat_pool,
        gws::renderer::gpu::TextureManager* textures)
    {
        using namespace gws::renderer::gpu;
        if (imp.meshes.empty()) return nullptr;

        // The importer emits one ImportedMeshData per material bucket, each
        // carrying a COPY of the shared vertex pool (the offline cook drops the
        // unused vertices per mesh; nothing does that here). Uploading N copies
        // of the same pool would multiply a large model's VRAM by its material
        // count, so identical pools are detected and shared, and only genuinely
        // different ones are appended. Detected rather than assumed: if the
        // importer ever stops sharing, this still produces correct geometry.
        const auto& first = imp.meshes[0];
        const uint32_t stride = first.vertex_stride;
        if (stride != sizeof(schizo::assets::ImportedVertex) || first.vertices.empty()) {
            spdlog::error("[AssetMeshCache] '{}': unexpected vertex stride {} — expected {}",
                          source_path, stride, sizeof(schizo::assets::ImportedVertex));
            return nullptr;
        }
        bool pools_identical = true;
        for (size_t i = 1; i < imp.meshes.size() && pools_identical; ++i)
            pools_identical = imp.meshes[i].vertices.size() == first.vertices.size() &&
                              0 == std::memcmp(imp.meshes[i].vertices.data(),
                                               first.vertices.data(), first.vertices.size());

        std::vector<SceneVertex> verts;
        std::vector<uint32_t>    idx;
        // Submesh i corresponds to imp.meshes[i]; the DrawItem for a node picks
        // its submesh by the node's mesh index, so materials stay attached to
        // the geometry the .mtl assigned them to.
        std::vector<Submesh>     submeshes;

        auto append_pool = [&](const schizo::assets::ImportedMeshData& m) -> uint32_t {
            const uint32_t base = static_cast<uint32_t>(verts.size());
            const auto* src = reinterpret_cast<const schizo::assets::ImportedVertex*>(m.vertices.data());
            const size_t count = m.vertices.size() / sizeof(schizo::assets::ImportedVertex);
            verts.reserve(verts.size() + count);
            for (size_t i = 0; i < count; ++i) {
                SceneVertex v{};
                v.position = glm::vec3(src[i].px, src[i].py, src[i].pz);
                v.normal   = glm::vec3(src[i].nx, src[i].ny, src[i].nz);
                v.uv       = glm::vec2(src[i].u, src[i].v);
                v.tangent  = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);  // filled in below
                verts.push_back(v);
            }
            return base;
        };

        if (pools_identical) append_pool(first);
        for (size_t mi = 0; mi < imp.meshes.size(); ++mi) {
            const auto& m = imp.meshes[mi];
            const uint32_t base = pools_identical ? 0u : append_pool(m);
            Submesh sm{};
            sm.material_index = static_cast<uint32_t>(mi);   // rewritten from nodes below
            const uint32_t first_index = static_cast<uint32_t>(idx.size());
            idx.reserve(idx.size() + m.indices.size());
            for (uint32_t i : m.indices) idx.push_back(i + base);
            sm.lods.push_back({first_index, static_cast<uint32_t>(m.indices.size()), 0.0f});
            submeshes.push_back(std::move(sm));
        }
        if (verts.empty() || idx.empty()) return nullptr;

        // Tangents. OBJ carries none, and the old loader wrote a constant
        // (1,0,0,1) into every vertex — which is fine while nothing samples a
        // normal map and silently wrong the moment something does, because the
        // TBN basis then bears no relation to the UV layout. Now that a material
        // can name a normal map, they have to be real.
        generate_tangents(verts, idx);

        auto mesh = Mesh::create(device, verts, idx, submeshes);
        if (!mesh) return nullptr;
        // OBJ files have no standardised winding convention — flag the mesh
        // double-sided so the G-Buffer renderer uses the cull-none pipeline.
        mesh->set_double_sided(true);

        auto out = std::make_unique<Scene>();
        out->meshes.push_back(std::move(mesh));

        // One gpu::Material per CookedMaterial, including its map_Kd texture.
        // Texture paths from a .mtl are relative to the .obj, which the importer
        // has already resolved; they still go through the TextureManager so they
        // are mipped, sRGB-correct, deduplicated and hot-reloadable like every
        // other texture in the editor.
        const Texture* dw = textures ? textures->white()  : nullptr;
        const Texture* dn = textures ? textures->normal() : nullptr;
        const Texture* db = textures ? textures->black()  : nullptr;
        std::vector<std::shared_ptr<Texture>> kept_textures;
        for (const auto& cm : imp.materials) {
            MaterialUniforms params{};
            params.base_color_factor = glm::vec4(cm.base_color[0], cm.base_color[1],
                                                 cm.base_color[2], cm.base_color[3]);
            params.metallic_factor   = cm.metallic;
            params.roughness_factor  = std::clamp(cm.roughness, 0.04f, 1.0f);

            std::shared_ptr<Texture> albedo;
            if (textures && cm.albedo_tex[0]) {
                TextureImportSettings st;
                st.srgb = true; st.gen_mips = true;
                albedo = textures->load(cm.albedo_tex, st);
                if (albedo && albedo.get() == textures->white()) {
                    // The manager substitutes shared white on failure, which as
                    // an albedo map is indistinguishable from success. Say so:
                    // a .mtl pointing at a texture that did not ship with the
                    // model is the single most common OBJ import complaint.
                    spdlog::warn("[AssetMeshCache] '{}': material '{}' references "
                                 "texture '{}' which failed to load",
                                 source_path, cm.name, cm.albedo_tex);
                    albedo.reset();
                }
            }
            auto mat = Material::create(device, mat_layout, mat_pool, params,
                                        albedo.get(), nullptr, nullptr, nullptr, nullptr,
                                        dw, dn, db);
            if (!mat) {
                spdlog::error("[AssetMeshCache] '{}': could not create material '{}'",
                              source_path, cm.name);
                return nullptr;
            }
            if (albedo) kept_textures.push_back(std::move(albedo));
            out->materials.push_back(std::move(mat));
        }
        if (out->materials.empty()) return nullptr;
        out->textures = std::move(kept_textures);

        // One DrawItem per node: node.mesh selects the submesh, node.material
        // selects the material the .mtl assigned to it.
        for (const auto& node : imp.nodes) {
            if (node.mesh < 0 || node.mesh >= static_cast<int>(submeshes.size())) continue;
            const int mi = (node.material >= 0 &&
                            node.material < static_cast<int>(out->materials.size()))
                         ? node.material : 0;
            DrawItem di;
            di.mesh          = out->meshes[0].get();
            di.material      = out->materials[mi].get();
            di.model         = glm::mat4(1.0f);
            di.submesh_index = static_cast<uint32_t>(node.mesh);
            out->draw_items.push_back(di);
        }
        if (out->draw_items.empty()) return nullptr;
        spdlog::info("[AssetMeshCache] '{}': {} submesh(es), {} material(s), {} textured",
                     source_path, submeshes.size(), out->materials.size(),
                     out->textures.size());
        return out;
    }

    /// Per-vertex tangents from the UV layout (Lengyel's method): accumulate each
    /// triangle's tangent onto its three vertices, then Gram-Schmidt against the
    /// normal. Degenerate UVs (a face whose texture coordinates are collinear)
    /// contribute nothing rather than a NaN — one such face would otherwise
    /// poison an entire smoothing group with black shading.
    static void generate_tangents(std::vector<gws::renderer::gpu::SceneVertex>& verts,
                                  const std::vector<uint32_t>& idx) {
        std::vector<glm::vec3> tan(verts.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> bitan(verts.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const uint32_t i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];
            if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;
            const glm::vec3 e1 = verts[i1].position - verts[i0].position;
            const glm::vec3 e2 = verts[i2].position - verts[i0].position;
            const glm::vec2 d1 = verts[i1].uv - verts[i0].uv;
            const glm::vec2 d2 = verts[i2].uv - verts[i0].uv;
            const float det = d1.x * d2.y - d2.x * d1.y;
            if (std::abs(det) < 1e-12f) continue;      // degenerate UVs
            const float r = 1.0f / det;
            const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
            const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;
            tan[i0] += t; tan[i1] += t; tan[i2] += t;
            bitan[i0] += b; bitan[i1] += b; bitan[i2] += b;
        }
        for (size_t v = 0; v < verts.size(); ++v) {
            const glm::vec3 n = verts[v].normal;
            glm::vec3 t = tan[v] - n * glm::dot(n, tan[v]);      // Gram-Schmidt
            if (glm::dot(t, t) < 1e-12f) {
                // No usable UV gradient here (unwrapped seam, untextured face).
                // Any tangent perpendicular to the normal is as good as another;
                // pick one deterministically so the mesh is reproducible.
                const glm::vec3 axis = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0)
                                                            : glm::vec3(0, 1, 0);
                t = glm::normalize(glm::cross(axis, n));
            } else {
                t = glm::normalize(t);
            }
            // Handedness: which way the bitangent runs, so mirrored UV islands
            // light correctly instead of inverting.
            const float w = (glm::dot(glm::cross(n, t), bitan[v]) < 0.0f) ? -1.0f : 1.0f;
            verts[v].tangent = glm::vec4(t, w);
        }
    }

    std::unordered_map<std::string, std::unique_ptr<gws::renderer::gpu::Scene>> entries_;
};

/// Cells per side of one terrain CHUNK. Terrains are meshed as a grid of
/// chunk meshes so a sculpt edit only rebuilds the chunks it touched — the
/// key to scaling terrains up (a 1024² terrain is 256 chunks; a brush stroke
/// touches a handful). Also gives per-chunk frustum culling for free.
inline constexpr int kTerrainChunkCells = 64;

/// Build ONE chunk of a terrain heightmap as a GPU mesh. The chunk covers
/// cells [cx0, cx0+cells) x [cz0, cz0+cells); vertices are world-local to the
/// terrain entity (same space as the old full-terrain mesh). Cells flagged as
/// HOLES are skipped (carved out of the surface — caves go through here).
/// Returns nullptr when every cell in the chunk is a hole.
inline std::unique_ptr<gws::renderer::gpu::Mesh> build_terrain_chunk_mesh(
    const schizo::scene::TerrainComponent* tc,
    gws::renderer::gpu::VulkanDevice* device,
    int cx0, int cz0, int cells)
{
    using namespace gws::renderer::gpu;
    const int   res   = tc->GetResolution();
    const float half  = tc->GetSize() * 0.5f;
    const float cell  = tc->CellSize();
    const float scale = tc->GetHeightScale();
    const int   cx1   = std::min(cx0 + cells, res);   // exclusive cell bounds
    const int   cz1   = std::min(cz0 + cells, res);
    const int   nx    = cx1 - cx0 + 1;                // verts per side (x)
    const int   nz    = cz1 - cz0 + 1;

    std::vector<SceneVertex> verts(static_cast<size_t>(nx) * nz);
    for (int z = 0; z < nz; ++z) {
        for (int x = 0; x < nx; ++x) {
            const int gx = cx0 + x, gz = cz0 + z;     // global grid coords
            const float hl = tc->HeightAt(gx - 1, gz) * scale;
            const float hr = tc->HeightAt(gx + 1, gz) * scale;
            const float hd = tc->HeightAt(gx, gz - 1) * scale;
            const float hu = tc->HeightAt(gx, gz + 1) * scale;
            SceneVertex v{};
            v.position = glm::vec3(-half + gx * cell,
                                   tc->HeightAt(gx, gz) * scale,
                                   -half + gz * cell);
            v.normal   = glm::normalize(glm::vec3(hl - hr, 2.0f * cell, hd - hu));
            v.uv       = glm::vec2(static_cast<float>(gx) / res,
                                   static_cast<float>(gz) / res);
            v.tangent  = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            verts[static_cast<size_t>(z) * nx + x] = v;
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(static_cast<size_t>(cx1 - cx0) * (cz1 - cz0) * 6);
    for (int z = 0; z < cz1 - cz0; ++z) {
        for (int x = 0; x < cx1 - cx0; ++x) {
            if (tc->HasHole(cx0 + x, cz0 + z)) continue;   // carved cell
            const uint32_t i0 = static_cast<uint32_t>(z) * nx + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + nx;
            const uint32_t i3 = i2 + 1;
            // CCW from +Y (same winding convention as gen_plane).
            idx.insert(idx.end(), {i0, i3, i1, i0, i2, i3});
        }
    }
    if (idx.empty()) return nullptr;   // fully carved chunk
    Submesh sm{};
    sm.material_index = 0;
    sm.lods.push_back({0, static_cast<uint32_t>(idx.size()), 0.0f});
    return Mesh::create(device, verts, idx, {sm});
}

/// Per-terrain-entity CHUNKED GPU mesh cache. On version change, rebuilds
/// only the chunks intersecting the component's accumulated dirty rect
/// (sculpt brushes report their bounds; Resize/Flatten dirty everything).
class TerrainMeshCache {
public:
    /// Chunk meshes for this terrain (nullptr entries = fully-holed chunks).
    const std::vector<std::unique_ptr<gws::renderer::gpu::Mesh>>& get_or_build(
        uint32_t entity_id,
        schizo::scene::TerrainComponent* tc,
        gws::renderer::gpu::VulkanDevice* device)
    {
        Entry& e = entries_[entity_id];
        const int res = tc->GetResolution();
        const int chunks = (res + kTerrainChunkCells - 1) / kTerrainChunkCells;

        const bool layout_changed = (e.chunks != chunks);
        if (layout_changed) {
            e.meshes.clear();
            e.meshes.resize(static_cast<size_t>(chunks) * chunks);
            e.chunks = chunks;
        }
        if (e.version != tc->Version() || layout_changed) {
            int x0, z0, x1, z1;
            const bool have_rect = tc->ConsumeDirtyRect(x0, z0, x1, z1);
            const bool full = layout_changed || !have_rect;
            const int c_x0 = full ? 0 : std::max(0, (x0 - 1) / kTerrainChunkCells);
            const int c_z0 = full ? 0 : std::max(0, (z0 - 1) / kTerrainChunkCells);
            const int c_x1 = full ? chunks - 1
                                  : std::min(chunks - 1, (x1 + 1) / kTerrainChunkCells);
            const int c_z1 = full ? chunks - 1
                                  : std::min(chunks - 1, (z1 + 1) / kTerrainChunkCells);
            for (int cz = c_z0; cz <= c_z1; ++cz)
                for (int cx = c_x0; cx <= c_x1; ++cx)
                    e.meshes[static_cast<size_t>(cz) * chunks + cx] =
                        build_terrain_chunk_mesh(tc, device,
                                                 cx * kTerrainChunkCells,
                                                 cz * kTerrainChunkCells,
                                                 kTerrainChunkCells);
            e.version = tc->Version();
        }
        return e.meshes;
    }

    void prune(const std::shared_ptr<schizo::scene::Scene>& scene) {
        if (!scene) { entries_.clear(); return; }
        std::unordered_map<uint32_t, Entry> kept;
        for (const auto& ent : scene->GetEntities())
            if (ent) {
                auto it = entries_.find(ent->GetId());
                if (it != entries_.end()) kept.emplace(it->first, std::move(it->second));
            }
        entries_ = std::move(kept);
    }
    void clear() { entries_.clear(); }

private:
    struct Entry {
        std::vector<std::unique_ptr<gws::renderer::gpu::Mesh>> meshes;  // chunks*chunks
        int      chunks  = 0;
        uint64_t version = 0;
    };
    std::unordered_map<uint32_t, Entry> entries_;
};

/// Per-terrain-entity GPU cache: the splat texture, the per-layer materials and
/// the TerrainMaterial descriptor set built from them.
///
/// WHAT CHANGED. This used to build an ordinary 6-binding `Material` whose five
/// sampler slots the terrain shader reinterpreted as [splatmap, layer0..3
/// albedo]. A terrain layer was therefore one colour texture and a tiling
/// number, and terrain roughness was hardcoded at 0.95 for every square metre of
/// every terrain in the engine. Layers now reference `.mat` assets and get
/// albedo, normal and metal/rough maps plus their own tint and PBR factors,
/// through a terrain-specific 14-binding layout.
///
/// TWO MATERIALS PER TERRAIN, on purpose. `terrain` is what the G-Buffer binds.
/// `proxy` is an ordinary 6-binding material that the SHADOW pass binds (it
/// alpha-tests at set 0) and the RT pass reads (`Material::params()`), neither
/// of which can consume a 14-binding set. The proxy is opaque white so the
/// shadow alpha test always passes — terrain is never cut out — and carries a
/// representative roughness so ray-traced reflections of terrain are plausible
/// rather than nonsense. It is an approximation, and a stated one: a single
/// proxy cannot represent four spatially varying layers.
class TerrainGpuCache {
public:
    /// What the G-Buffer binds, plus the proxy the shadow and RT passes need.
    /// Either pointer may be null if the build failed, in which case the caller
    /// falls back to rendering the terrain untextured.
    struct Built {
        gws::renderer::gpu::TerrainMaterial* terrain = nullptr;
        gws::renderer::gpu::Material*        proxy   = nullptr;
    };

    Built get_or_build(
        uint32_t entity_id,
        const schizo::scene::TerrainComponent* tc,
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout terrain_layout,
        VkDescriptorPool terrain_pool,
        VkDescriptorSetLayout mat_layout,
        VkDescriptorPool mat_pool,
        gws::renderer::gpu::TextureManager* textures,
        MaterialAssetCache* material_assets)
    {
        using namespace gws::renderer::gpu;
        if (terrain_layout == VK_NULL_HANDLE || terrain_pool == VK_NULL_HANDLE)
            return {};

        // Earthy base for layers with no albedo (UNORM so the bytes are sampled
        // as linear directly): the same green the pre-splat terrain material
        // used, so a freshly-added terrain still looks like ground rather than
        // stark white.
        if (!base_tex_) {
            const uint8_t earth[4] = { 107, 122, 87, 255 };
            base_tex_ = Texture::create_from_pixels(device, earth, 1, 1, /*srgb=*/false);
        }

        Entry& e = entries_[entity_id];

        // Resolve every layer to a description FIRST, so what gets rebuilt is
        // decided against resolved content rather than against paths. A .mat
        // edited on disk changes no path at all — keying the rebuild on paths is
        // exactly how "I changed the material and nothing happened" happens.
        std::array<gws::assets::MaterialDesc, schizo::scene::kTerrainLayers> descs;
        for (int i = 0; i < schizo::scene::kTerrainLayers; ++i)
            descs[i] = resolve_terrain_layer_material(*tc, i, material_assets);

        // The descriptor set depends only on the TEXTURES; the factors live in
        // the UBO. Splitting the two is what makes dragging a layer's roughness
        // slider a 144-byte memcpy instead of a descriptor-set rebuild.
        uint64_t tex_key = 1469598103934665603ull;
        auto mix = [&tex_key](const std::string& str) {
            for (unsigned char c : str) tex_key = (tex_key ^ c) * 1099511628211ull;
            tex_key = (tex_key ^ 0xFFu) * 1099511628211ull;
        };
        for (const auto& d : descs) {
            mix(d.albedo_map); mix(d.normal_map); mix(d.metallic_roughness_map);
        }

        TerrainMaterialUniforms params{};
        params.tiling = glm::vec4(tc->GetTiling(0), tc->GetTiling(1),
                                  tc->GetTiling(2), tc->GetTiling(3));
        for (int i = 0; i < schizo::scene::kTerrainLayers; ++i) {
            params.tint[i] = descs[i].base_color;
            params.mrno[i] = glm::vec4(descs[i].metallic, descs[i].roughness,
                                       descs[i].normal_scale, descs[i].occlusion);
        }

        const bool splat_changed = (e.splat_version != tc->SplatVersion());
        const bool tex_changed   = (e.texture_key != tex_key);
        if (e.terrain && !tex_changed && !splat_changed) {
            // Cheap path: factors only, no descriptor work.
            const uint64_t pk = params_hash(params);
            if (e.params_key != pk) {
                e.terrain->update_uniforms(params);
                e.params_key = pk;
            }
            return { e.terrain.get(), e.proxy.get() };
        }

        // Free the old descriptor set before allocating the new one, so peak set
        // count stays at one per terrain rather than two.
        e.terrain.reset();

        if (splat_changed || !e.splat_tex) {
            e.splat_tex = Texture::create_from_pixels(
                device, tc->Splat().data(),
                static_cast<uint32_t>(tc->SplatResolution()),
                static_cast<uint32_t>(tc->SplatResolution()), /*srgb=*/false);
        }
        if (!e.splat_tex) return {};

        // Load each layer's three maps with the colour space its DATA needs.
        // A normal map decoded as sRGB still renders — with every normal bent
        // toward the surface, which reads as "the terrain lighting is subtly
        // wrong everywhere" and is near-impossible to attribute afterwards.
        auto load = [&](const std::string& path, bool srgb)
            -> std::shared_ptr<Texture> {
            if (path.empty()) return nullptr;
            if (!textures)
                return Texture::create_from_file(device, path, srgb);
            TextureImportSettings st;
            st.srgb = srgb; st.gen_mips = true;
            auto sp = textures->load(path, st);
            // The manager substitutes shared white on failure, which as an
            // albedo map is indistinguishable from success. Map it back to
            // "no texture" so the earthy base shows instead of a white terrain.
            if (sp && sp.get() == textures->white()) return nullptr;
            return sp;
        };

        std::array<TerrainLayerTextures, schizo::scene::kTerrainLayers> layer_tex{};
        for (int i = 0; i < schizo::scene::kTerrainLayers; ++i) {
            e.layer_albedo[i] = load(descs[i].albedo_map,             /*srgb=*/true);
            e.layer_normal[i] = load(descs[i].normal_map,             /*srgb=*/false);
            e.layer_mr[i]     = load(descs[i].metallic_roughness_map, /*srgb=*/false);
            layer_tex[i].albedo = e.layer_albedo[i] ? e.layer_albedo[i].get()
                                                    : base_tex_.get();
            layer_tex[i].normal = e.layer_normal[i] ? e.layer_normal[i].get() : nullptr;
            layer_tex[i].metallic_roughness = e.layer_mr[i] ? e.layer_mr[i].get() : nullptr;
        }

        e.terrain = TerrainMaterial::create(
            device, terrain_layout, terrain_pool, params,
            e.splat_tex.get(), layer_tex,
            textures ? textures->white()  : nullptr,
            textures ? textures->normal() : nullptr);
        if (!e.terrain) return {};

        // The shadow / RT proxy. Built once and reused for the terrain's life:
        // it carries no per-layer detail, so nothing about a layer edit can
        // change it.
        if (!e.proxy) {
            MaterialUniforms p{};
            p.base_color_factor = glm::vec4(1.0f);   // opaque: shadow alpha test passes
            p.metallic_factor   = 0.0f;
            p.roughness_factor  = 0.95f;             // representative ground
            p.emissive_factor   = glm::vec4(0.0f);   // .a = 0 -> no discard
            e.proxy = Material::create(device, mat_layout, mat_pool, p,
                                       nullptr, nullptr, nullptr, nullptr, nullptr,
                                       textures ? textures->white()  : nullptr,
                                       textures ? textures->normal() : nullptr,
                                       textures ? textures->black()  : nullptr);
        }

        e.texture_key   = tex_key;
        e.params_key    = params_hash(params);
        e.splat_version = tc->SplatVersion();

        // Say what each layer actually resolved to, once per rebuild rather than
        // per frame. Without this the terrain path is completely silent: whether
        // a layer picked up its material, fell back to a texture, or found
        // neither is invisible, and "my terrain material did nothing" has no
        // evidence to work from. This is also the line that would have shown a
        // terrain silently rendering through the fallback scene pipeline.
        {
            std::string summary;
            for (int i = 0; i < schizo::scene::kTerrainLayers; ++i) {
                if (i) summary += ", ";
                summary += std::to_string(i) + "=";
                if (tc->HasLayerMaterial(i)) {
                    summary += "mat:" + tc->GetLayerMaterial(i);
                    if (descs[i].albedo_map.empty() && descs[i].normal_map.empty())
                        summary += "(no maps)";
                } else if (!descs[i].albedo_map.empty()) {
                    summary += "tex:" + descs[i].albedo_map;
                } else {
                    summary += "base";
                }
            }
            spdlog::info("[Terrain {}] material rebuilt — layers: {}", entity_id, summary);
        }
        return { e.terrain.get(), e.proxy.get() };
    }

    /// Re-write descriptor sets of every terrain material. Call after a layer
    /// texture was hot-reloaded in place (its VkImageView changed). The caller
    /// must ensure the GPU is idle.
    void rewrite_all_materials() {
        for (auto& [id, e] : entries_) {
            (void)id;
            if (e.terrain) e.terrain->rewrite_textures();
            if (e.proxy)   e.proxy->rewrite_textures();
        }
    }

    void prune(const std::shared_ptr<schizo::scene::Scene>& scene) {
        if (!scene) { entries_.clear(); return; }
        std::unordered_map<uint32_t, Entry> kept;
        for (const auto& ent : scene->GetEntities())
            if (ent) {
                auto it = entries_.find(ent->GetId());
                if (it != entries_.end()) kept.emplace(it->first, std::move(it->second));
            }
        entries_ = std::move(kept);
    }
    void clear() { entries_.clear(); base_tex_.reset(); }

private:
    /// Hash of the uniform block, so a frame that needs no descriptor work can
    /// still tell whether the 144 bytes actually changed.
    static uint64_t params_hash(const gws::renderer::gpu::TerrainMaterialUniforms& p) {
        uint64_t h = 1469598103934665603ull;
        const auto* b = reinterpret_cast<const unsigned char*>(&p);
        for (size_t i = 0; i < sizeof(p); ++i) h = (h ^ b[i]) * 1099511628211ull;
        return h;
    }

    struct Entry {
        std::unique_ptr<gws::renderer::gpu::Texture> splat_tex;
        // shared_ptr: layer textures may be owned by the TextureManager and
        // shared with other terrains and meshes.
        std::array<std::shared_ptr<gws::renderer::gpu::Texture>, schizo::scene::kTerrainLayers> layer_albedo;
        std::array<std::shared_ptr<gws::renderer::gpu::Texture>, schizo::scene::kTerrainLayers> layer_normal;
        std::array<std::shared_ptr<gws::renderer::gpu::Texture>, schizo::scene::kTerrainLayers> layer_mr;
        std::unique_ptr<gws::renderer::gpu::TerrainMaterial> terrain;
        std::unique_ptr<gws::renderer::gpu::Material>        proxy;
        uint64_t texture_key   = 0;
        uint64_t params_key    = 0;
        uint64_t splat_version = 0;
    };
    std::unordered_map<uint32_t, Entry> entries_;
    std::unique_ptr<gws::renderer::gpu::Texture> base_tex_;  // earthy fallback albedo
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
    MaterialGpuCache& mat_cache,
    AssetMeshCache& asset_cache,
    TerrainMeshCache& terrain_cache,
    TerrainGpuCache& terrain_gpu_cache,
    gws::renderer::gpu::VulkanDevice* device,
    VkDescriptorSetLayout mat_layout,
    VkDescriptorPool mat_pool,
    std::vector<gws::renderer::gpu::DrawItem>& out_opaque,
    std::vector<gws::renderer::gpu::DrawItem>& out_transparent,
    gws::renderer::gpu::TextureManager* textures = nullptr,
    const schizo::editor::EcsSceneBridge* ecs = nullptr,
    MaterialAssetCache* material_assets = nullptr,
    VkDescriptorSetLayout terrain_layout = VK_NULL_HANDLE,
    VkDescriptorPool terrain_pool = VK_NULL_HANDLE)
{
    out_opaque.clear();
    out_transparent.clear();
    if (!scene) return;

    for (const auto& ent : scene->GetEntities()) {
        if (!ent || !ent->IsActiveInHierarchy()) continue;

        // Authoritative: read the world matrix the ECS transform system
        // computed this frame. Falls back to the OOP matrix for entities not
        // in the shadow (e.g. before the first sync). These are verified
        // equal, so this changes the data path, not the pixels.
        schizo::scene::Transform* tf = ent->GetTransform();
        const glm::mat4* ecs_model = ecs ? ecs->world_matrix(tf) : nullptr;
        const glm::mat4 model = ecs_model ? *ecs_model : tf->GetWorldMatrix();

        // Terrain takes precedence over mesh/primitive components. Chunked:
        // one DrawItem per (non-fully-holed) chunk mesh.
        if (auto tc = ent->GetComponent<schizo::scene::TerrainComponent>()) {
            const auto& chunk_meshes =
                terrain_cache.get_or_build(ent->GetId(), tc.get(), device);
            if (!chunk_meshes.empty()) {
                // The terrain material drives the splat pipeline; the proxy is
                // what the shadow caster binds and the RT pass reads, neither of
                // which can consume a 14-binding set.
                const TerrainGpuCache::Built built = terrain_gpu_cache.get_or_build(
                    ent->GetId(), tc.get(), device,
                    terrain_layout, terrain_pool, mat_layout, mat_pool,
                    textures, material_assets);

                // Without a terrain material — no terrain layout on this device,
                // or the splat texture failed to build — fall back to a plain
                // green surface on the ordinary scene pipeline. The terrain is
                // then untextured but present, castable and walkable, which is a
                // far better failure than terrain that vanishes.
                gws::renderer::gpu::Material* proxy = built.proxy;
                const bool is_terrain = (built.terrain != nullptr && proxy != nullptr);
                if (!proxy) {
                    gws::assets::MaterialDesc fallback;
                    fallback.name       = "Terrain (fallback)";
                    fallback.base_color = glm::vec4(0.42f, 0.48f, 0.34f, 1.0f);
                    fallback.roughness  = 0.95f;
                    proxy = mat_cache.get_or_create(fallback, device, mat_layout,
                                                    mat_pool, textures);
                }
                if (proxy) {
                    for (const auto& cm : chunk_meshes) {
                        if (!cm) continue;   // fully carved chunk
                        gws::renderer::gpu::DrawItem di;
                        di.mesh             = cm.get();
                        di.material         = proxy;
                        di.terrain_material = is_terrain ? built.terrain : nullptr;
                        di.model            = model;
                        di.submesh_index    = 0;
                        di.is_blend         = false;
                        di.is_terrain       = is_terrain;
                        out_opaque.push_back(di);
                    }
                }
            }
            continue;   // terrain entity handled
        }

        // Resolve the entity's alpha mode for the primitive path. For the
        // asset path we route per-DrawItem based on the loaded glTF's
        // per-material alphaMode (see below).
        auto mr = ent->GetComponent<schizo::scene::MeshRendererComponent>();
        schizo::scene::AlphaMode am = mr ? mr->GetAlphaMode()
                                         : schizo::scene::AlphaMode::Opaque;

        const auto* mc = ent->GetMeshComponent();
        if (mc && !mc->mesh_path.empty()) {
            const auto* loaded = asset_cache.get_or_load(
                mc->mesh_path, device, mat_layout, mat_pool, textures);
            if (loaded && !loaded->draw_items.empty()) {
                // Entity material override. An imported model ships its own
                // materials and they are usually why it looks right, so this is
                // OPT-IN: without it, tinting an entity would silently discard
                // whatever the artist authored in the source file. With it, one
                // material replaces every submesh's — which is what "put this
                // texture on that object" means for a model that arrived with
                // no usable material at all (every OBJ, before the .mtl path).
                gws::renderer::gpu::Material* override_mat = nullptr;
                if (mr && mr->GetOverrideAssetMaterial()) {
                    override_mat = mat_cache.get_or_create(
                        resolve_entity_material(mr.get(), material_assets),
                        device, mat_layout, mat_pool, textures);
                }

                // If the entity has an explicit MeshRendererComponent with
                // AlphaMode::Blend, treat every sub-draw as blend (manual
                // override). Otherwise honour the per-material flag the
                // GltfLoader stamped onto each DrawItem.
                const bool entity_force_blend =
                    (am == schizo::scene::AlphaMode::Blend);
                for (const auto& src : loaded->draw_items) {
                    gws::renderer::gpu::DrawItem di = src;
                    di.model = model * src.model;
                    if (override_mat) di.material = override_mat;
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
        if (!mr) continue;

        gws::renderer::gpu::Mesh* mesh = meshes.get(mr->GetMeshType());
        if (!mesh) continue;

        // One resolution rule for both paths: an assigned .mat wins, otherwise
        // the component's inline fields. The alpha cutoff the G-Buffer and the
        // shadow caster need is derived inside the cache from the alpha mode,
        // so it cannot drift between the two call sites the way it used to.
        gws::renderer::gpu::Material* mat = mat_cache.get_or_create(
            resolve_entity_material(mr.get(), material_assets),
            device, mat_layout, mat_pool, textures);
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
