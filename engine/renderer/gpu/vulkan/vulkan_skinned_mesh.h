/**
 * @file vulkan_skinned_mesh.h
 * @brief GPU compute-skinned mesh (Animation Stage 5).
 *
 * A skinned mesh keeps its bind-pose vertices (+ per-vertex joint indices &
 * weights) in an input SSBO. Each frame `skin()` uploads the current bone
 * matrices and dispatches a compute pass that linear-blend-skins every vertex
 * into an ORDINARY SceneVertex vertex buffer (`output_mesh()`). That output is
 * a normal Mesh — hand its pointer to a DrawItem and the existing G-buffer,
 * shadow, culling, and (future) RT paths render the skinned result unchanged.
 */
#pragma once

#include "vulkan_scene_mesh.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace gws::renderer::gpu {

class VulkanDevice;

/// Authoring-friendly skinned vertex (packed to a std430 layout internally).
struct SkinnedVertex {
    glm::vec3  position{0.0f};
    glm::vec3  normal{0.0f, 1.0f, 0.0f};
    glm::vec2  uv{0.0f};
    glm::vec4  tangent{1.0f, 0.0f, 0.0f, 1.0f};
    glm::uvec4 joints{0u};           // up to 4 bone indices
    glm::vec4  weights{0.0f};        // matching weights (should sum to 1)
};

class SkinnedMesh {
public:
    /// Build a skinned mesh. `inverse_bind_matrices[j]` maps mesh space into
    /// bone j's local bind space; its length defines the bone count. The output
    /// Mesh is initialised from the bind pose (bounds/meshlets bake from it).
    static std::unique_ptr<SkinnedMesh> create(
        VulkanDevice* device,
        const std::vector<SkinnedVertex>& vertices,
        const std::vector<uint32_t>& indices,
        std::vector<Submesh> submeshes,
        std::vector<glm::mat4> inverse_bind_matrices);

    SkinnedMesh() = default;
    ~SkinnedMesh();
    SkinnedMesh(const SkinnedMesh&) = delete;
    SkinnedMesh& operator=(const SkinnedMesh&) = delete;

    /// Record the skinning dispatch. `bone_global` is each bone's world matrix
    /// (Skeleton::GetWorldMatrix); must be `bone_count()` long. Inserts buffer
    /// barriers so the compute write is ordered against the vertex-attribute
    /// read. Call before the geometry stage each frame.
    void skin(VkCommandBuffer cmd, const std::vector<glm::mat4>& bone_global);

    /// The skinned output as an ordinary Mesh (feed to a DrawItem).
    Mesh*    output_mesh() const { return output_.get(); }
    uint32_t bone_count() const  { return bone_count_; }
    uint32_t vertex_count() const { return vertex_count_; }

private:
    bool init(VulkanDevice* device,
              const std::vector<SkinnedVertex>& vertices,
              const std::vector<uint32_t>& indices,
              std::vector<Submesh> submeshes);
    void destroy();

    VulkanDevice*          device_ = nullptr;
    std::unique_ptr<Mesh>  output_;                 // skinned SceneVertex buffer
    std::vector<glm::mat4> inverse_bind_;
    uint32_t vertex_count_ = 0;
    uint32_t bone_count_   = 0;

    VkBuffer       in_ssbo_    = VK_NULL_HANDLE;     // GpuSkinnedVertex[] (bind pose)
    VkDeviceMemory in_mem_     = VK_NULL_HANDLE;
    VkBuffer       bone_ssbo_  = VK_NULL_HANDLE;     // mat4[] skinning matrices
    VkDeviceMemory bone_mem_   = VK_NULL_HANDLE;
    void*          bone_mapped_ = nullptr;

    VkDescriptorSetLayout dsl_      = VK_NULL_HANDLE;
    VkDescriptorPool      pool_     = VK_NULL_HANDLE;
    VkDescriptorSet       set_      = VK_NULL_HANDLE;
    VkPipelineLayout      layout_   = VK_NULL_HANDLE;
    VkPipeline            pipeline_ = VK_NULL_HANDLE;
};

} // namespace gws::renderer::gpu
