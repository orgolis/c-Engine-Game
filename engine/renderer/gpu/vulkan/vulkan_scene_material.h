/**
 * @file vulkan_scene_material.h
 * @brief PBR material for the deferred scene pipeline (Phase 5 Week 1).
 *
 * Owns:
 *  - a small uniform buffer (`MaterialUniforms`, std140-friendly)
 *  - one descriptor set bound to set=1 of the geometry pipeline:
 *      - binding 0: uniform buffer (factors)
 *      - bindings 1-5: combined image samplers
 *        (baseColor, normal, metallicRoughness, occlusion, emissive)
 *
 * Descriptor-set *layout* and *pool* are externally owned. Use
 * `Material::create_descriptor_set_layout()` and `Material::create_descriptor_pool()`
 * to build them, then thread them into `Material::create()` and into the
 * geometry pipeline at construction time.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

namespace gws::renderer::gpu {

class VulkanDevice;
class Texture;

/// std140-aligned uniform block for PBR material factors. 48 bytes;
/// fits comfortably under the 16 KiB minimum guarantee for UBOs.
///
/// emissive_factor.a is repurposed as `alpha_cutoff` for alpha-tested
/// transparency: when > 0, the G-Buffer fragment shader discards pixels
/// whose sampled alpha is below this threshold. 0 means "opaque, no
/// discard" — the previous behaviour.
struct MaterialUniforms {
    glm::vec4 base_color_factor    = glm::vec4(1.0f); // RGB + alpha
    // PBR convention: default = dielectric (non-metal), fully rough. The
    // previous metallic_factor=1.0 default made every untextured primitive
    // a perfect mirror, which is why surfaces looked uniformly reflective
    // even when the user expected "absolutely non-reflective".
    float     metallic_factor      = 0.0f;
    float     roughness_factor     = 1.0f;
    float     occlusion_strength   = 1.0f;
    float     normal_scale         = 1.0f;
    glm::vec4 emissive_factor      = glm::vec4(0.0f); // RGB + alpha_cutoff (a)
};
static_assert(sizeof(MaterialUniforms) == 48, "MaterialUniforms layout drift");

class Material {
public:
    Material() = default;
    ~Material();

    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&&) noexcept;
    Material& operator=(Material&&) noexcept;

    /// One-shot helper to build the descriptor-set layout used by every
    /// Material instance and by the geometry pipeline. Caller owns the
    /// returned layout (destroy with `vkDestroyDescriptorSetLayout`).
    static VkDescriptorSetLayout create_descriptor_set_layout(VkDevice device);

    /// One-shot helper to build a descriptor pool sized for `max_materials`
    /// instances of this layout. Caller owns the pool.
    static VkDescriptorPool create_descriptor_pool(VkDevice device, uint32_t max_materials);

    /// Build a material. Any null texture pointer falls back to a 1×1
    /// default appropriate for the slot (white for color/MR/AO, flat-blue
    /// normal, black for emissive).
    static std::unique_ptr<Material> create(VulkanDevice* device,
                                            VkDescriptorSetLayout layout,
                                            VkDescriptorPool pool,
                                            const MaterialUniforms& params,
                                            const Texture* base_color,
                                            const Texture* normal,
                                            const Texture* metallic_roughness,
                                            const Texture* occlusion,
                                            const Texture* emissive);

    /// Bind this material's descriptor set. The pipeline layout must have
    /// been built with the same layout returned by `create_descriptor_set_layout`.
    void bind(VkCommandBuffer cmd, VkPipelineLayout pipeline_layout, uint32_t set_index) const;

    VkDescriptorSet descriptor_set() const { return descriptor_set_; }

    /// CPU-side copy of the PBR factors this material was built with.
    /// Used by the RT reflection pass to shade hit points (which can't
    /// read the GPU UBO conveniently in a ray query).
    const MaterialUniforms& params() const { return params_; }

private:
    VulkanDevice*    device_         = nullptr;
    VkDescriptorPool pool_           = VK_NULL_HANDLE; // Not owned; for free path
    VkDescriptorSet  descriptor_set_ = VK_NULL_HANDLE;
    VkBuffer         ubo_            = VK_NULL_HANDLE;
    VkDeviceMemory   ubo_memory_     = VK_NULL_HANDLE;
    MaterialUniforms params_{};

    // Default fallback textures owned by the material when the caller
    // didn't supply one. We keep them on the material so the descriptor
    // set's image-sampler bindings stay live.
    std::unique_ptr<Texture> fallback_white_;
    std::unique_ptr<Texture> fallback_normal_;
    std::unique_ptr<Texture> fallback_black_;

    void destroy();
};

} // namespace gws::renderer::gpu
