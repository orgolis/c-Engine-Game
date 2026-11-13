#pragma once
// ============================================================================
// foliage_render -- FoliageComponent instances into the frame's draw list.
//
// Placement lives in scene/foliage.h. This turns the instances that can be
// seen into DrawItems: whole culling cells are dropped first, by distance and
// against the camera frustum (grown by a margin, so a tree just off screen
// still casts its shadow on screen), and only then are trees visited. The
// G-buffer pass groups identical mesh + material draws into instanced draws,
// so a forest costs a handful of draw calls, not one per tree.
//
// A built-in kind is a few parts (trunk + crown, or one shape), each a unit
// primitive placed inside a 1-tall frame standing on its base, and coloured
// with the layer's two colours. A Model layer draws the model file's own
// meshes and materials.
// ============================================================================

#include "foliage.h"
#include "material_cache.h"
#include "primitive_meshes.h"
#include "scene.h"
#include "scene_render_bridge.h"

#include "vulkan/vulkan_render_graph.h"
#include "vulkan/culling.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace schizo::editor {

/// A cone, base radius 0.5 at y = -0.5, apex at y = +0.5 (the primitives' frame).
inline void gen_cone(std::vector<gws::renderer::gpu::SceneVertex>& v, std::vector<uint32_t>& idx, int segments = 12) {
    const float pi = 3.14159265358979323846f;
    const float slope = 0.5f / 1.0f;   // radius over height
    for (int s = 0; s < segments; ++s) {
        const float a0 = s * 2.0f * pi / segments, a1 = (s + 1) * 2.0f * pi / segments, am = (a0 + a1) * 0.5f;
        auto nrm = [&](float a) { return glm::normalize(glm::vec3(std::cos(a), slope, std::sin(a))); };
        const uint32_t b = static_cast<uint32_t>(v.size());
        const glm::vec4 t(1, 0, 0, 1);
        v.push_back({glm::vec3(0.0f, 0.5f, 0.0f), nrm(am), {0.5f, 1.0f}, t});
        v.push_back({glm::vec3(std::cos(a1) * 0.5f, -0.5f, std::sin(a1) * 0.5f), nrm(a1), {1.0f, 0.0f}, t});
        v.push_back({glm::vec3(std::cos(a0) * 0.5f, -0.5f, std::sin(a0) * 0.5f), nrm(a0), {0.0f, 0.0f}, t});
        idx.insert(idx.end(), {b, b + 1, b + 2});
    }
    const uint32_t c = static_cast<uint32_t>(v.size());
    v.push_back({glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(0, -1, 0), {0.5f, 0.5f}, glm::vec4(1, 0, 0, 1)});
    for (int s = 0; s <= segments; ++s) {
        const float a = s * 2.0f * pi / segments;
        v.push_back({glm::vec3(std::cos(a) * 0.5f, -0.5f, std::sin(a) * 0.5f), glm::vec3(0, -1, 0), {0, 0}, glm::vec4(1, 0, 0, 1)});
    }
    for (int s = 0; s < segments; ++s) idx.insert(idx.end(), {c, c + 1 + static_cast<uint32_t>(s), c + 2 + static_cast<uint32_t>(s)});
}

/// A tuft of grass: five blades, each a narrow upright quad tapering to a
/// point, fanned round the centre. Drawn double-sided. Normals lean up, so a
/// meadow is lit like the ground it grows on rather than like walls.
inline void gen_grass_tuft(std::vector<gws::renderer::gpu::SceneVertex>& v, std::vector<uint32_t>& idx) {
    const float pi = 3.14159265358979323846f;
    for (int b = 0; b < 5; ++b) {
        const float a = b * pi / 5.0f + 0.3f * (b % 2);
        const glm::vec3 dir(std::cos(a), 0.0f, std::sin(a));
        const glm::vec3 lean = glm::vec3(std::sin(a * 3.0f), 0.0f, std::cos(a * 2.0f)) * 0.18f;   // blades bend apart
        const glm::vec3 n = glm::normalize(glm::vec3(-dir.z, 0.0f, dir.x) * 0.35f + glm::vec3(0, 1, 0));
        const float w = 0.06f;
        const uint32_t i = static_cast<uint32_t>(v.size());
        const glm::vec4 t(dir, 1.0f);
        v.push_back({dir * -w + glm::vec3(0, -0.5f, 0), n, {0, 0}, t});
        v.push_back({dir * w + glm::vec3(0, -0.5f, 0), n, {1, 0}, t});
        v.push_back({dir * (w * 0.5f) + lean * 0.5f + glm::vec3(0, 0.0f, 0), n, {0.75f, 0.5f}, t});
        v.push_back({dir * (-w * 0.5f) + lean * 0.5f + glm::vec3(0, 0.0f, 0), n, {0.25f, 0.5f}, t});
        v.push_back({lean + glm::vec3(0, 0.5f, 0), n, {0.5f, 1}, t});
        idx.insert(idx.end(), {i, i + 1, i + 2, i, i + 2, i + 3, i + 3, i + 2, i + 4});
    }
}

/// A conifer's crown as ONE mesh: two cones stacked, drawn straight in the
/// tree's own frame (base at y = 0, top at y = 1) so it needs no placement.
inline void gen_conifer_crown(std::vector<gws::renderer::gpu::SceneVertex>& v, std::vector<uint32_t>& idx) {
    auto stack = [&](float y0, float y1, float w) {
        std::vector<gws::renderer::gpu::SceneVertex> cv;
        std::vector<uint32_t>                        ci;
        gen_cone(cv, ci, 10);
        const uint32_t base = static_cast<uint32_t>(v.size());
        const float    h = y1 - y0;
        for (auto x : cv) {
            x.position = glm::vec3(x.position.x * w, (x.position.y + 0.5f) * h + y0, x.position.z * w);
            x.normal = glm::normalize(glm::vec3(x.normal.x / w, x.normal.y / h, x.normal.z / w));
            v.push_back(x);
        }
        for (uint32_t i : ci) idx.push_back(base + i);
    };
    stack(0.15f, 0.65f, 0.5f);
    stack(0.42f, 1.0f, 0.34f);
}

class FoliageRenderer {
public:
    void initialize(gws::renderer::gpu::VulkanDevice* device) {
        using namespace gws::renderer::gpu;
        auto build = [&](void (*gen)(std::vector<SceneVertex>&, std::vector<uint32_t>&)) {
            std::vector<SceneVertex> v;
            std::vector<uint32_t>    i;
            gen(v, i);
            Submesh sm;
            sm.material_index = 0;
            sm.lods.push_back({0, static_cast<uint32_t>(i.size()), 0.0f});
            return Mesh::create(device, v, i, {sm});
        };
        cone_ = build([](std::vector<SceneVertex>& v, std::vector<uint32_t>& i) { gen_cone(v, i); });
        crown_ = build(gen_conifer_crown);
        tuft_ = build(gen_grass_tuft);
        if (tuft_) tuft_->set_double_sided(true);
    }

    /// Free the GPU meshes. Before the device goes: the renderer outlives it.
    void release() {
        cone_.reset();
        crown_.reset();
        tuft_.reset();
    }

    struct Stats {
        size_t cells_seen = 0, instances = 0, draws = 0;
        bool   over_budget = false;   // the nearest `budget` instances were drawn, the rest not
    };
    /// At most this many instances a frame, nearest cells first. Each is a
    /// draw item through the general culling and sorting path, so the frame
    /// cost grows with it; a GPU-instanced foliage pass is what lifts this.
    size_t budget = 40000;
    const Stats& stats() const { return stats_; }

    /// Append the visible foliage of every active FoliageComponent in `scene`.
    void append(const std::shared_ptr<schizo::scene::Scene>& scene, const gws::renderer::gpu::CameraData& cam,
                const PrimitiveMeshCache& prims, MaterialGpuCache& mats, AssetMeshCache& assets,
                gws::renderer::gpu::VulkanDevice* device, VkDescriptorSetLayout mat_layout, VkDescriptorPool mat_pool,
                gws::renderer::gpu::TextureManager* textures, std::vector<gws::renderer::gpu::DrawItem>& out) {
        using namespace schizo::scene;
        using gws::renderer::gpu::AABB;
        stats_ = Stats{};
        if (!scene || !cone_ || !crown_) return;
        const gws::renderer::gpu::Frustum frustum = gws::renderer::gpu::Frustum::from_matrix(cam.proj * cam.view);
        for (const auto& ent : scene->EntitiesWithAny<FoliageComponent>()) {
            if (!ent || !ent->IsActiveInHierarchy()) continue;
            auto fc = ent->GetComponent<FoliageComponent>();
            auto tc = ent->GetComponent<TerrainComponent>();
            if (!fc || !tc || fc->Layers().empty()) continue;
            const std::vector<FoliageScatter>& sc = fc->Scatter(*tc);
            const glm::mat4 world = ent->GetTransform()->GetWorldMatrix();
            const glm::vec3 origin = glm::vec3(world[3]);
            for (size_t li = 0; li < fc->Layers().size() && li < sc.size(); ++li) {
                const FoliageLayer& l = fc->Layers()[li];
                const std::vector<Part> parts = parts_of(l, prims, mats, device, mat_layout, mat_pool, textures);
                const gws::renderer::gpu::Scene* model = nullptr;
                if (l.kind == FoliageKind::Model) {
                    if (l.model_path.empty()) continue;
                    model = assets.get_or_load(l.model_path, device, mat_layout, mat_pool, textures);
                    if (!model || model->draw_items.empty()) continue;
                } else if (parts.empty()) {
                    continue;
                }
                const float far2 = l.draw_distance * l.draw_distance;
                const float margin = l.size * 2.0f + 20.0f;   // keep trees whose shadow may reach the screen
                // The cells that can be seen, nearest first, so a budget cuts the far ones.
                visible_.clear();
                for (size_t ci = 0; ci < sc[li].cells.size(); ++ci) {
                    const FoliageCell& cell = sc[li].cells[ci];
                    if (cell.items.empty()) continue;
                    const glm::vec3 mn = cell.min + origin, mx = cell.max + origin;
                    const glm::vec3 d = glm::clamp(cam.position, mn, mx) - cam.position;
                    const float     dist2 = glm::dot(d, d);
                    if (dist2 > far2) continue;
                    if (!frustum.is_visible(AABB(mn - glm::vec3(margin), mx + glm::vec3(margin)))) continue;
                    visible_.push_back({dist2, static_cast<uint32_t>(ci)});
                }
                std::sort(visible_.begin(), visible_.end());
                for (const auto& vc : visible_) {
                    if (stats_.instances >= budget) {
                        stats_.over_budget = true;
                        break;
                    }
                    const FoliageCell& cell = sc[li].cells[vc.second];
                    ++stats_.cells_seen;
                    for (const FoliageInstance& in : cell.items) {
                        const glm::vec3 p = in.pos + origin;
                        const glm::vec3 dp = p - cam.position;
                        const float     dist2 = glm::dot(dp, dp);
                        if (dist2 > far2) continue;
                        // Thin out toward the draw distance instead of a hard
                        // edge: past 60% of it, a tree is kept with falling odds.
                        const float f = std::sqrt(dist2) / l.draw_distance;
                        if (f > 0.6f) {
                            const float keep = 1.0f - (f - 0.6f) / 0.4f;
                            const float r = std::fmod(std::fabs(in.yaw) * 97.13f, 1.0f);   // stable per tree
                            if (r > keep) continue;
                        }
                        ++stats_.instances;
                        const glm::mat4 m = world * in.local;
                        if (model) {
                            for (const auto& src : model->draw_items) {
                                gws::renderer::gpu::DrawItem di = src;
                                di.model = m * src.model;
                                di.is_blend = false;
                                di.casts_shadow = l.casts_shadow;
                                out.push_back(di);
                                ++stats_.draws;
                            }
                        } else {
                            for (const Part& part : parts) {
                                if (part.detail && f > 0.3f) continue;
                                gws::renderer::gpu::DrawItem di;
                                di.mesh = part.mesh;
                                di.material = part.material;
                                di.model = m * part.local;
                                di.casts_shadow = l.casts_shadow;
                                out.push_back(di);
                                ++stats_.draws;
                            }
                        }
                    }
                }
            }
        }
    }

private:
    struct Part {
        gws::renderer::gpu::Mesh*     mesh = nullptr;
        gws::renderer::gpu::Material* material = nullptr;
        glm::mat4                     local{1.0f};
        bool                          detail = false;   // a trunk: drawn only near, where it is more than a few pixels
    };

    /// A unit primitive (radius 0.5, height 1, centred) placed so it spans
    /// heights `y0`..`y1` of the 1-tall frame with the given width.
    static glm::mat4 place(float y0, float y1, float width_x, float width_z) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, (y0 + y1) * 0.5f, 0.0f));
        return glm::scale(m, glm::vec3(width_x, y1 - y0, width_z));
    }

    std::vector<Part> parts_of(const schizo::scene::FoliageLayer& l, const PrimitiveMeshCache& prims, MaterialGpuCache& mats,
                               gws::renderer::gpu::VulkanDevice* device, VkDescriptorSetLayout mat_layout, VkDescriptorPool mat_pool,
                               gws::renderer::gpu::TextureManager* textures) {
        using schizo::scene::FoliageKind;
        auto mat = [&](const glm::vec3& c, float rough) {
            gws::assets::MaterialDesc d;
            d.name = "foliage";
            d.base_color = glm::vec4(c, 1.0f);
            d.roughness = rough;
            return mats.get_or_create(d, device, mat_layout, mat_pool, textures);
        };
        std::vector<Part> p;
        switch (l.kind) {
            case FoliageKind::Conifer:
                p.push_back({crown_.get(), mat(l.color_a, 0.85f), glm::mat4(1.0f), false});
                p.push_back({prims.cylinder.get(), mat(l.color_b, 0.9f), place(0.0f, 0.3f, 0.07f, 0.07f), true});
                break;
            case FoliageKind::Broadleaf:
                p.push_back({prims.sphere.get(), mat(l.color_a, 0.85f), place(0.35f, 1.0f, 0.75f, 0.7f), false});
                p.push_back({prims.cylinder.get(), mat(l.color_b, 0.9f), place(0.0f, 0.5f, 0.09f, 0.09f), true});
                break;
            case FoliageKind::Bush:
                p.push_back({prims.sphere.get(), mat(l.color_a, 0.9f), place(0.0f, 1.0f, 1.4f, 1.2f)});
                break;
            case FoliageKind::Grass:
                p.push_back({tuft_.get(), mat(l.color_a, 0.8f), place(0.0f, 1.0f, 1.0f, 1.0f)});
                break;
            case FoliageKind::Rock:
                p.push_back({prims.sphere.get(), mat(l.color_a, 0.95f), place(0.0f, 1.0f, 1.6f, 1.3f)});
                break;
            case FoliageKind::Model: break;
        }
        p.erase(std::remove_if(p.begin(), p.end(), [](const Part& x) { return !x.mesh || !x.material; }), p.end());
        return p;
    }

    std::unique_ptr<gws::renderer::gpu::Mesh> cone_, crown_, tuft_;
    std::vector<std::pair<float, uint32_t>>   visible_;   // (distance squared, cell) this frame
    Stats stats_;
};

}  // namespace schizo::editor
