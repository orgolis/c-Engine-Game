/**
 * @file skinned_gltf_loader.h
 * @brief Import a rigged glTF/GLB (skin + skeleton + animations) into the
 *        engine's CPU animation types + the GPU skinning input format.
 *
 * Produces device-free data: `SkinnedVertex[]` (bind pose, joints remapped to
 * bone ids), indices, per-bone inverse-bind matrices, a `schizo::renderer::
 * Skeleton`, and one `AnimationClip` per glTF animation. The caller builds the
 * GPU `SkinnedMesh` from this (needs a device) and plays a clip on the skeleton
 * each frame, exactly like the procedural SkinnedAnimDemo — but from a real asset.
 */
#pragma once

#include "vulkan/vulkan_skinned_mesh.h"  // SkinnedVertex
#include "vulkan/vulkan_scene_mesh.h"     // Submesh
#include "skeleton.h"
#include "animation.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace gws::renderer::gpu {

struct LoadedSkinnedModel {
    bool valid = false;
    std::string error;

    std::vector<SkinnedVertex> vertices;      // bind pose; .joints are bone ids
    std::vector<uint32_t>      indices;
    std::vector<Submesh>       submeshes;     // may be empty (single implicit submesh)
    std::vector<glm::mat4>     inverse_bind;  // one per bone, indexed by bone id

    std::shared_ptr<schizo::renderer::Skeleton> skeleton;
    std::vector<std::shared_ptr<schizo::renderer::AnimationClip>> animations;

    uint32_t bone_count()   const { return skeleton ? skeleton->GetBoneCount() : 0; }
    uint32_t vertex_count() const { return static_cast<uint32_t>(vertices.size()); }
};

/// Load the first skinned mesh (+ its skeleton and all animations) from a
/// `.gltf` or `.glb`. On failure returns `{valid=false, error=...}`.
LoadedSkinnedModel load_skinned_gltf(const std::string& path);

} // namespace gws::renderer::gpu
