#pragma once

// ============================================================================
// material_cache — .mat on disk → MaterialDesc → a GPU descriptor set.
//
// Two caches, because they are keyed on different things and that difference is
// the whole point:
//
//   MaterialAssetCache  path -> MaterialDesc.  Watches the file; an edit made in
//                       a text editor (or by another copy of this editor) shows
//                       up in the viewport without a reload, the same way meshes
//                       and textures already do.
//
//   MaterialGpuCache    content hash -> gpu::Material.  NOT keyed by entity.
//                       Fifty crates sharing one material used to allocate fifty
//                       descriptor sets holding byte-identical data, against a
//                       pool of 64 — so a scene could run the pool dry and start
//                       rendering untextured with one line in the log. Keyed by
//                       content, they share one set, and two entities that merely
//                       *happen* to match share it too.
//
// WHY A FRAME SWEEP RATHER THAN REFCOUNTS. The GPU cache cannot prune against
// the scene's entity list the way the old per-entity cache did: a material is no
// longer owned by an entity, and the same hash can arrive from an entity, a
// terrain layer, or an imported mesh in the same frame. Refcounting would mean
// every producer remembering to release, which is the shape that leaks when
// someone adds a third producer. Instead every lookup stamps the frame, and
// entries unused for a grace period are dropped. Dropping is safe only when the
// GPU is idle, so it happens through the same idle callback the mesh cache uses.
//
// SLOT sRGB IS NOT A DETAIL. Base colour and emissive are authored in sRGB;
// normal, metallic-roughness and occlusion are linear data that merely happen to
// be stored in an image. Loading a normal map as sRGB bends every normal toward
// the surface and reads as "the lighting is subtly wrong everywhere" — a bug
// that is nearly impossible to attribute after the fact. The per-slot flags
// below are the only place that decision is made.
// ============================================================================

#include "material_asset_cache.h"   // the Vulkan-free half: .mat -> MaterialDesc
#include "vulkan/vulkan_scene_material.h"
#include "vulkan/vulkan_texture.h"
#include "vulkan/vulkan_texture_manager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

namespace schizo::editor {

// ---------------------------------------------------------------------------
// MaterialGpuCache — MaterialDesc → gpu::Material, keyed by content
// ---------------------------------------------------------------------------

/// How many frames an unused GPU material is kept before it may be freed.
/// Long enough that an entity hidden for a moment does not pay a rebuild, and
/// far longer than the two or three frames this renderer keeps in flight — which
/// is what makes freeing safe without stalling the GPU (see end_frame).
inline constexpr uint64_t kMaterialGpuGraceFrames = 90;

/// Above this many live materials, the oldest are freed early rather than
/// waiting out the grace period. Exists because content keying makes a slider
/// drag mint one material per frame; without a cap a long drag walks the
/// descriptor pool to exhaustion, which surfaces as objects going untextured.
inline constexpr size_t kMaterialGpuSoftCap = 192;

/// An entry used this recently is never freed by the cap, at any cache size.
/// Comfortably above frames-in-flight, so the cap can never race the GPU.
inline constexpr uint64_t kMaterialGpuMinAgeFrames = 8;

class MaterialGpuCache {
public:
    /// Build (or fetch) the GPU material for `desc`. Materials with equal
    /// content share one descriptor set.
    ///
    /// Returns nullptr only when the descriptor pool is exhausted or a device
    /// call failed — the caller must skip the draw rather than bind garbage.
    gws::renderer::gpu::Material* get_or_create(
        const gws::assets::MaterialDesc& desc,
        gws::renderer::gpu::VulkanDevice* device,
        VkDescriptorSetLayout layout,
        VkDescriptorPool pool,
        gws::renderer::gpu::TextureManager* textures)
    {
        using namespace gws::renderer::gpu;
        const uint64_t key = desc.content_hash();

        auto it = entries_.find(key);
        if (it != entries_.end()) {
            it->second.last_used = frame_;
            return it->second.material.get();
        }

        // ---- Load the five maps, each with the colour space its DATA needs.
        // Getting this wrong is silent: an sRGB-decoded normal map still
        // renders, just with every normal pulled toward the surface.
        Entry e;
        auto load_map = [&](const std::string& path, bool srgb)
            -> std::shared_ptr<Texture> {
            if (path.empty() || !textures) return nullptr;
            TextureImportSettings st;
            st.srgb     = srgb;
            st.gen_mips = true;
            auto sp = textures->load(path, st);
            // The manager substitutes its shared white on failure. Mapping that
            // back to "no texture" matters: bound as an albedo map, shared white
            // is indistinguishable from success, so a broken path would silently
            // render as an untextured surface with the right tint.
            if (sp && sp.get() == textures->white()) {
                spdlog::warn("[MaterialCache] texture '{}' failed to load — slot "
                             "falls back to the engine default", path);
                return nullptr;
            }
            return sp;
        };
        e.tex[0] = load_map(desc.albedo_map,             /*srgb=*/true);
        e.tex[1] = load_map(desc.normal_map,             /*srgb=*/false);
        e.tex[2] = load_map(desc.metallic_roughness_map, /*srgb=*/false);
        e.tex[3] = load_map(desc.occlusion_map,          /*srgb=*/false);
        e.tex[4] = load_map(desc.emissive_map,           /*srgb=*/true);

        MaterialUniforms params{};
        params.base_color_factor  = desc.base_color;
        params.metallic_factor    = desc.metallic;
        params.roughness_factor   = desc.roughness;
        params.occlusion_strength = desc.occlusion;
        params.normal_scale       = desc.normal_scale;
        // emissive_factor.a doubles as the G-Buffer's alpha_cutoff (see
        // MaterialUniforms). Cutoff 0 means "opaque, no discard"; Blend needs a
        // non-zero cutoff anyway so it casts a binarised shadow, since it skips
        // the G-Buffer entirely and only the shadow pipeline sees it.
        const float cutoff =
            (desc.alpha_mode == gws::assets::MaterialAlphaMode::Cutout) ? desc.alpha_cutoff
          : (desc.alpha_mode == gws::assets::MaterialAlphaMode::Blend)  ? 0.5f
          : 0.0f;
        params.emissive_factor = glm::vec4(desc.emissive_linear(), cutoff);

        e.material = Material::create(
            device, layout, pool, params,
            e.tex[0].get(), e.tex[1].get(), e.tex[2].get(), e.tex[3].get(), e.tex[4].get(),
            textures ? textures->white()  : nullptr,
            textures ? textures->normal() : nullptr,
            textures ? textures->black()  : nullptr);
        if (!e.material) {
            // Do NOT cache the failure. Unlike a bad file path, pool exhaustion
            // is transient — the next sweep frees sets — and caching it would
            // make one bad frame permanent.
            spdlog::error("[MaterialCache] could not build GPU material '{}' "
                          "(descriptor pool exhausted?)", desc.name);
            return nullptr;
        }

        e.last_used = frame_;
        Material* raw = e.material.get();
        entries_.emplace(key, std::move(e));
        return raw;
    }

    /// Advance the frame counter and free materials nothing has referenced for
    /// the grace period. Call once per frame, after the draw list is built.
    ///
    /// NO DEVICE IDLE, deliberately. Freeing a descriptor set an in-flight
    /// command buffer still references is undefined behaviour, and the obvious
    /// guard is a vkDeviceWaitIdle before the first free — but the grace period
    /// already provides that guarantee far more cheaply: an entry is freed only
    /// after `kMaterialGpuGraceFrames` consecutive frames without a single
    /// lookup, which is more than an order of magnitude beyond the two or three
    /// frames this renderer keeps in flight. Idling instead would stall the
    /// whole GPU every time a slider drag aged out, which is precisely the
    /// moment the user is watching for smooth feedback.
    size_t end_frame() {
        ++frame_;
        size_t freed = 0;

        if (frame_ > kMaterialGpuGraceFrames) {
            const uint64_t cutoff = frame_ - kMaterialGpuGraceFrames;
            for (auto it = entries_.begin(); it != entries_.end();) {
                if (it->second.last_used > cutoff) { ++it; continue; }
                it = entries_.erase(it);
                ++freed;
            }
        }

        // Churn guard. Dragging a colour or roughness slider produces a NEW
        // content hash every frame — that is inherent to content keying, and it
        // means a long drag can mint hundreds of short-lived materials while the
        // grace period holds every one of them. Against a finite descriptor pool
        // that ends as allocation failure, and allocation failure looks like
        // objects turning untextured, which nobody would connect to a slider.
        //
        // So above a soft cap the oldest entries go early. Anything used within
        // the last few frames is never touched, so this can never free a set the
        // GPU might still be reading.
        if (entries_.size() > kMaterialGpuSoftCap) {
            std::vector<std::pair<uint64_t, uint64_t>> by_age;  // (last_used, key)
            by_age.reserve(entries_.size());
            for (const auto& [key, e] : entries_) by_age.emplace_back(e.last_used, key);
            std::sort(by_age.begin(), by_age.end());

            const uint64_t protect = (frame_ > kMaterialGpuMinAgeFrames)
                                   ? frame_ - kMaterialGpuMinAgeFrames : 0;
            size_t excess = entries_.size() - kMaterialGpuSoftCap;
            for (const auto& [last_used, key] : by_age) {
                if (excess == 0) break;
                if (last_used > protect) break;   // sorted: nothing older remains
                entries_.erase(key);
                ++freed;
                --excess;
            }
        }
        return freed;
    }

    /// Re-write the image bindings of every cached material. Call after a
    /// texture one of them references was hot-reloaded in place (same object,
    /// new VkImageView). The caller must ensure the GPU is idle.
    void rewrite_all_materials() {
        for (auto& [key, e] : entries_) {
            (void)key;
            if (e.material) e.material->rewrite_textures();
        }
    }

    size_t size() const { return entries_.size(); }
    void   clear() { entries_.clear(); }

private:
    struct Entry {
        // shared_ptr: these may be owned by the TextureManager and shared with
        // other materials. Held here so they outlive the descriptor set.
        std::shared_ptr<gws::renderer::gpu::Texture> tex[5];
        std::unique_ptr<gws::renderer::gpu::Material> material;
        uint64_t last_used = 0;
    };
    std::unordered_map<uint64_t, Entry> entries_;
    uint64_t frame_ = 0;
};

}  // namespace schizo::editor
