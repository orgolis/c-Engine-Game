#pragma once

#include "primitive_meshes.h"
#include "vulkan/vulkan_scene_material.h"
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
#include "ecs_bridge.h"   // authoritative ECS world matrices (Stage 1.4 step 2)
#include "assets/asset_watcher.h"   // shared hot-reload file watching
#include "jobs/task_runner.h"       // background OBJ parsing

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
        const std::string& albedo_path,                        // "" → flat colour only
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout layout,
        VkDescriptorPool pool,
        gws::renderer::gpu::TextureManager* textures = nullptr)
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
                std::abs(e.alpha_cutoff - alpha_cutoff) < 1e-4f &&
                e.albedo_path == albedo_path) {
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
        // Optional base-colour texture, loaded through the manager (mipped,
        // sRGB, deduplicated, hot-reloadable). Held on the Entry so it outlives
        // the material's descriptor set. `color` still multiplies the texel.
        std::shared_ptr<gws::renderer::gpu::Texture> albedo;
        const gws::renderer::gpu::Texture* base = nullptr;
        if (textures && !albedo_path.empty()) {
            gws::renderer::gpu::TextureImportSettings st;
            st.srgb = true; st.gen_mips = true;
            albedo = textures->load(albedo_path, st);
            base = albedo ? albedo.get() : nullptr;
        }

        // Route unbound slots to the manager's shared engine-wide defaults so
        // every primitive material doesn't mint its own 1×1 white/normal/black.
        const gws::renderer::gpu::Texture* dw = textures ? textures->white()  : nullptr;
        const gws::renderer::gpu::Texture* dn = textures ? textures->normal() : nullptr;
        const gws::renderer::gpu::Texture* db = textures ? textures->black()  : nullptr;
        auto mat = gws::renderer::gpu::Material::create(
            device, layout, pool, params,
            base, nullptr, nullptr, nullptr, nullptr,
            dw, dn, db);
        Entry e{ color, metallic, roughness, occlusion, emissive, alpha_cutoff,
                 albedo_path, std::move(albedo), std::move(mat) };
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
        std::string albedo_path;
        std::shared_ptr<gws::renderer::gpu::Texture> albedo_tex;  // keeps texture alive
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
    // utf8_path: non-ASCII filenames (Cyrillic etc.) fail with a narrow ifstream.
    std::ifstream file(utf8_path(path));
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
                    parsed->ok = parse_obj_file(disk_path, parsed->verts, parsed->idx);
                    if (ctx.cancelled()) parsed->ok = false;
                },
                [this, key, disk_path, parsed, device, mat_layout, mat_pool](const gws::tasks::TaskInfo& info) {
                    using namespace gws::renderer::gpu;
                    loading_.erase(key);
                    if (info.state != gws::tasks::TaskState::Succeeded || !parsed->ok ||
                        parsed->verts.empty() || parsed->idx.empty()) {
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
                    auto built = build_obj_scene(std::move(parsed->verts), std::move(parsed->idx),
                                                 device, mat_layout, mat_pool);
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
                scene = load_obj_scene(disk_path, device, mat_layout, mat_pool);
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
        std::vector<gws::renderer::gpu::SceneVertex> verts;
        std::vector<uint32_t>                        idx;
        bool ok = false;
    };

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
        return build_obj_scene(std::move(verts), std::move(idx), device, mat_layout, mat_pool);
    }

    // Everything after the parse. Device-touching, so it runs on the editor
    // thread in both the sync and async paths — one implementation, so an
    // async-loaded mesh is byte-identical to a synchronously loaded one.
    static std::unique_ptr<gws::renderer::gpu::Scene> build_obj_scene(
        std::vector<gws::renderer::gpu::SceneVertex>&& verts,
        std::vector<uint32_t>&& idx,
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout mat_layout,
        VkDescriptorPool mat_pool)
    {
        using namespace gws::renderer::gpu;
        if (verts.empty() || idx.empty()) return nullptr;

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

/// Per-terrain-entity splat material cache (Phase C). Builds a Material whose
/// 5 texture slots are reinterpreted by the terrain G-buffer shader as
/// [splatmap, layer0, layer1, layer2, layer3], plus per-layer tiling packed in
/// the material's base_color_factor. Rebuilds when the splatmap, layer paths,
/// or tiling change (TerrainComponent::SplatVersion bumps on each).
class TerrainGpuCache {
public:
    /// Returns a terrain Material ready to bind, or nullptr if the splat
    /// texture couldn't be built. Layer slots with no/failed texture fall back
    /// to a shared 1×1 white (so unpainted layers read white, not the PBR
    /// fallbacks Material::create would otherwise inject into those slots).
    gws::renderer::gpu::Material* get_or_build(
        uint32_t entity_id,
        const schizo::scene::TerrainComponent* tc,
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout mat_layout,
        VkDescriptorPool mat_pool,
        gws::renderer::gpu::TextureManager* textures = nullptr)
    {
        using namespace gws::renderer::gpu;
        // Earthy base for unset layers (UNORM so the bytes are sampled as
        // linear directly): 0.42,0.48,0.34 — the same green the pre-splat
        // terrain material used, so a freshly-added terrain still looks like
        // ground rather than stark white.
        if (!base_tex_) {
            const uint8_t earth[4] = { 107, 122, 87, 255 };
            base_tex_ = Texture::create_from_pixels(device, earth, 1, 1, /*srgb=*/false);
        }

        Entry& e = entries_[entity_id];
        bool paths_changed = false;
        for (int i = 0; i < schizo::scene::kTerrainLayers; ++i)
            if (e.layer_paths[i] != tc->GetLayerPath(i)) { paths_changed = true; break; }

        if (e.material && e.splat_version == tc->SplatVersion() && !paths_changed)
            return e.material.get();

        // Rebuild. Free the old material first to return its descriptor set to
        // the pool before allocating the new one (keeps peak set count low).
        e.material.reset();

        e.splat_tex = Texture::create_from_pixels(
            device, tc->Splat().data(),
            static_cast<uint32_t>(tc->SplatResolution()),
            static_cast<uint32_t>(tc->SplatResolution()), /*srgb=*/false);
        if (!e.splat_tex) return nullptr;

        const Texture* layer_ptrs[schizo::scene::kTerrainLayers];
        for (int i = 0; i < schizo::scene::kTerrainLayers; ++i) {
            e.layer_paths[i] = tc->GetLayerPath(i);
            e.layer_tex[i].reset();
            if (!e.layer_paths[i].empty()) {
                if (textures) {
                    // Managed path: deduplicated across terrains/meshes, GPU
                    // mip chain, sRGB, anisotropic tiling sampler, cooked
                    // `.ctex` when present, hot-reloadable. The manager falls
                    // back to its shared white on failure — map that back to
                    // "no texture" so the earthy base is used instead.
                    TextureImportSettings st;
                    st.srgb = true; st.gen_mips = true;
                    auto sp = textures->load(e.layer_paths[i], st);
                    if (sp && sp.get() != textures->white()) e.layer_tex[i] = std::move(sp);
                } else {
                    e.layer_tex[i] = Texture::create_from_file(device, e.layer_paths[i], /*srgb=*/true);
                }
            }
            layer_ptrs[i] = e.layer_tex[i] ? e.layer_tex[i].get() : base_tex_.get();
        }

        MaterialUniforms params{};
        params.base_color_factor = glm::vec4(tc->GetTiling(0), tc->GetTiling(1),
                                             tc->GetTiling(2), tc->GetTiling(3));
        params.metallic_factor   = 0.0f;
        params.roughness_factor  = 0.95f;
        params.occlusion_strength = 1.0f;
        params.normal_scale      = 1.0f;
        params.emissive_factor   = glm::vec4(0.0f);
        // Slot order matches the terrain shader: binding1=splat, 2..5=layers.
        e.material = Material::create(device, mat_layout, mat_pool, params,
                                      e.splat_tex.get(),
                                      layer_ptrs[0], layer_ptrs[1],
                                      layer_ptrs[2], layer_ptrs[3]);
        e.splat_version = tc->SplatVersion();
        return e.material.get();
    }

    /// Re-write descriptor sets of every terrain material. Call after a layer
    /// texture was hot-reloaded in place (its VkImageView changed). The caller
    /// must ensure the GPU is idle.
    void rewrite_all_materials() {
        for (auto& [id, e] : entries_) {
            (void)id;
            if (e.material) e.material->rewrite_textures();
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
    void clear() { entries_.clear(); }

private:
    struct Entry {
        std::unique_ptr<gws::renderer::gpu::Texture> splat_tex;
        // shared_ptr: layer textures may be owned by the TextureManager (and
        // shared with other terrains/materials); the legacy no-manager path's
        // unique_ptr converts into it.
        std::array<std::shared_ptr<gws::renderer::gpu::Texture>, schizo::scene::kTerrainLayers> layer_tex;
        std::array<std::string, schizo::scene::kTerrainLayers> layer_paths;
        std::unique_ptr<gws::renderer::gpu::Material> material;
        uint64_t splat_version = 0;
    };
    std::unordered_map<uint32_t, Entry> entries_;
    std::unique_ptr<gws::renderer::gpu::Texture> base_tex_; // shared earthy fallback for empty layers
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
    TerrainMeshCache& terrain_cache,
    TerrainGpuCache& terrain_gpu_cache,
    gws::renderer::gpu::VulkanDevice* device,
    VkDescriptorSetLayout mat_layout,
    VkDescriptorPool mat_pool,
    std::vector<gws::renderer::gpu::DrawItem>& out_opaque,
    std::vector<gws::renderer::gpu::DrawItem>& out_transparent,
    gws::renderer::gpu::TextureManager* textures = nullptr,
    const schizo::editor::EcsSceneBridge* ecs = nullptr)
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
                // Splat material (splatmap + 4 tiling layers) → terrain
                // pipeline. Falls back to a plain green material if the splat
                // texture couldn't be built (terrain still renders, untextured).
                gws::renderer::gpu::Material* tmat = terrain_gpu_cache.get_or_build(
                    ent->GetId(), tc.get(), device, mat_layout, mat_pool, textures);
                bool is_terrain = tmat != nullptr;
                if (!tmat) {
                    tmat = mat_cache.get_or_create(
                        ent->GetId(), glm::vec4(0.42f, 0.48f, 0.34f, 1.0f),
                        0.0f, 0.95f, 1.0f, glm::vec3(0.0f), 0.0f,
                        /*albedo_path=*/std::string(),
                        device, mat_layout, mat_pool, textures);
                }
                if (tmat) {
                    for (const auto& cm : chunk_meshes) {
                        if (!cm) continue;   // fully carved chunk
                        gws::renderer::gpu::DrawItem di;
                        di.mesh          = cm.get();
                        di.material      = tmat;
                        di.model         = model;
                        di.submesh_index = 0;
                        di.is_blend      = false;
                        di.is_terrain    = is_terrain;
                        out_opaque.push_back(di);
                    }
                }
            }
            continue;   // terrain entity handled
        }

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
                mc->mesh_path, device, mat_layout, mat_pool, textures);
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
            mr->GetOcclusion(), mr->GetEmissiveLinear(),
            alpha_cutoff, mr->GetAlbedoTexturePath(),
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
