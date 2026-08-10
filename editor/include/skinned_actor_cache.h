#pragma once

// ============================================================================
// SkinnedActorCache — the GPU half of SkinnedMeshComponent.
//
// The component on the entity is data: a path, a clip index, a play flag. The
// skinned mesh, its material and its skinning dispatch are GPU objects, so they
// live here and are keyed by ENTITY id rather than by path.
//
// Keying by entity, not path, is the deliberate choice: two entities using the
// same character must animate independently, and a shared actor would make them
// share a pose. The cost is that the mesh data is duplicated per instance —
// real instancing (share the bind-pose mesh, keep per-entity bone matrices) is
// the obvious next step and is NOT done here. Better to have two characters
// that animate correctly than one that is memory-optimal.
//
// Mirrors AssetMeshCache: lazily built, failures cached so a broken path is not
// retried every frame, and everything released on clear() before the device goes
// away.
// ============================================================================

#include "imported_skinned_actor.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace schizo::editor {

class SkinnedActorCache {
public:
    /// Get (or build) the actor for `entity_id` showing `gltf_path`. Returns
    /// null while unbuildable — the caller then simply draws nothing for this
    /// entity, exactly as it does for a mesh that failed to load.
    ImportedSkinnedActor* get_or_create(uint32_t entity_id,
                                        const std::string& gltf_path,
                                        gws::renderer::gpu::VulkanDevice* device,
                                        VkDescriptorSetLayout mat_layout,
                                        VkDescriptorPool mat_pool,
                                        gws::renderer::gpu::TextureManager* textures) {
        auto it = entries_.find(entity_id);
        if (it != entries_.end()) {
            // Path changed on an existing entity (the user picked a different
            // character): drop the old actor and rebuild.
            if (it->second.path == gltf_path) return it->second.actor.get();
            entries_.erase(it);
        }
        if (gltf_path.empty() || !device) return nullptr;

        Entry e;
        e.path = gltf_path;
        std::string err;
        e.actor = ImportedSkinnedActor::create(device, mat_layout, mat_pool, textures,
                                               gltf_path, glm::vec3(0.0f), &err);
        if (!e.actor) {
            // Cache the failure. Without this a bad path re-parses a glTF every
            // frame, which is the difference between "the model is missing" and
            // "the editor is unusable".
            spdlog::error("[SkinnedActorCache] '{}' failed to load: {}", gltf_path,
                          err.empty() ? "unknown error" : err);
        }
        auto* raw = e.actor.get();
        entries_[entity_id] = std::move(e);
        return raw;
    }

    /// Drop actors for entities that no longer exist or no longer carry the
    /// component, so deleting a character releases its GPU memory.
    template <typename KeepPredicate>
    void prune(KeepPredicate keep) {
        for (auto it = entries_.begin(); it != entries_.end(); ) {
            if (keep(it->first)) ++it;
            else                 it = entries_.erase(it);
        }
    }

    void clear() { entries_.clear(); }
    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::string                          path;   // what the actor was built from
        std::unique_ptr<ImportedSkinnedActor> actor;  // null = build failed, do not retry
    };
    std::unordered_map<uint32_t, Entry> entries_;
};

}  // namespace schizo::editor
