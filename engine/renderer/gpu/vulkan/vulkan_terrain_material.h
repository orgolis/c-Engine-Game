/**
 * @file vulkan_terrain_material.h
 * @brief Per-layer PBR material for the terrain splat pipeline.
 *
 * WHY THIS IS NOT `Material`. The scene material is six bindings: a UBO and five
 * samplers. Terrain needs a splat map plus THREE maps for each of four layers —
 * fourteen bindings — and there is no way to fold that into five samplers. The
 * alternatives were both worse:
 *
 *   - Widen the shared layout to fourteen. The scene pipeline, the transparent
 *     pass and the shadow caster all build against it, so every ordinary
 *     material in the project would carry eight sampler descriptors it never
 *     uses, and the descriptor pool would size for the worst case everywhere.
 *
 *   - Keep reinterpreting the five slots. That is what the previous terrain did
 *     ([splat, 4×albedo]), and it is exactly why a terrain layer could not have
 *     surface relief and why every terrain in the engine had roughness 0.95.
 *
 * So terrain owns its own layout, its own pool and its own pipeline layout. The
 * cost is one more descriptor-set layout to keep in step with one more shader;
 * the benefit is that neither side constrains the other.
 *
 * THE SHADOW AND RAY-TRACING PROBLEM. Terrain draws also reach the shadow caster
 * (which binds a material at set 0 to alpha-test) and the RT instance buffer
 * (which reads `Material::params()` to shade hit points). Neither can consume a
 * fourteen-binding set. Rather than teach both about terrain, a terrain draw
 * carries a small ORDINARY `Material` alongside this one — see
 * `DrawItem::terrain_material`. That also fixes a latent bug: the RT pass used
 * to read a terrain material's `base_color_factor`, which for terrain held four
 * per-layer TILING numbers, so terrain reflections were shaded with a colour
 * made of the numbers 16, 16, 16, 16.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <memory>

namespace gws::renderer::gpu {

class VulkanDevice;
class Texture;

/// Blendable terrain layers. Four because the splat map is RGBA and each channel
/// is one layer's weight; going beyond four means a second splat texture, which
/// is a different feature.
inline constexpr int kTerrainMaterialLayers = 4;

/// std140-compatible uniform block for terrain surface parameters.
///
/// Everything is vec4-shaped on purpose: std140 pads a `float[4]` to a 16-byte
/// stride, so an array of floats costs exactly as much as an array of vec4s
/// while hiding three quarters of itself as invisible padding. Packing four
/// related values into each vec4 makes the same bytes readable from both sides.
struct TerrainMaterialUniforms {
    /// Per-layer UV tiling — how many times layer i repeats across the terrain.
    /// x = layer 0 … w = layer 3.
    glm::vec4 tiling{16.0f, 16.0f, 16.0f, 16.0f};

    /// Per-layer base colour, multiplying that layer's albedo texture.
    std::array<glm::vec4, kTerrainMaterialLayers> tint{
        glm::vec4(1.0f), glm::vec4(1.0f), glm::vec4(1.0f), glm::vec4(1.0f)};

    /// Per-layer (metallic, roughness, normal_scale, occlusion).
    /// Defaults are dielectric and fully rough — bare ground, not wet stone.
    std::array<glm::vec4, kTerrainMaterialLayers> mrno{
        glm::vec4(0.0f, 1.0f, 1.0f, 1.0f), glm::vec4(0.0f, 1.0f, 1.0f, 1.0f),
        glm::vec4(0.0f, 1.0f, 1.0f, 1.0f), glm::vec4(0.0f, 1.0f, 1.0f, 1.0f)};
};
static_assert(sizeof(TerrainMaterialUniforms) == 144, "TerrainMaterialUniforms layout drift");

/// Textures for one terrain layer. A null slot binds the engine default for
/// that map — shared white for albedo and metal/rough, flat blue for normal —
/// which is the identity in each case, so "this layer has no normal map" costs
/// nothing and needs no shader branch.
struct TerrainLayerTextures {
    const Texture* albedo             = nullptr;
    const Texture* normal             = nullptr;
    const Texture* metallic_roughness = nullptr;
};

class TerrainMaterial {
public:
    TerrainMaterial() = default;
    ~TerrainMaterial();

    TerrainMaterial(const TerrainMaterial&)            = delete;
    TerrainMaterial& operator=(const TerrainMaterial&) = delete;
    TerrainMaterial(TerrainMaterial&&) noexcept;
    TerrainMaterial& operator=(TerrainMaterial&&) noexcept;

    /// Descriptor-set layout used by every TerrainMaterial and by the terrain
    /// pipeline. Fourteen bindings: UBO, splat map, then 4 albedo, 4 normal and
    /// 4 metal/rough samplers. Caller owns the result.
    static VkDescriptorSetLayout create_descriptor_set_layout(VkDevice device);

    /// Pool sized for `max_materials` terrain materials. Terrain materials are
    /// far rarer than scene materials (one per terrain entity, not one per
    /// object), but each consumes thirteen image descriptors — so this pool is
    /// deliberately separate from the scene one rather than sharing a budget
    /// where one terrain costs as much as thirteen objects.
    static VkDescriptorPool create_descriptor_pool(VkDevice device, uint32_t max_materials);

    /// Build a terrain material. `splat` is required — without per-texel weights
    /// there is nothing to blend, and defaulting it to white would silently make
    /// every layer fully present everywhere.
    static std::unique_ptr<TerrainMaterial> create(
        VulkanDevice* device,
        VkDescriptorSetLayout layout,
        VkDescriptorPool pool,
        const TerrainMaterialUniforms& params,
        const Texture* splat,
        const std::array<TerrainLayerTextures, kTerrainMaterialLayers>& layers,
        const Texture* default_white,
        const Texture* default_normal);

    /// Overwrite the uniform block without rebuilding the descriptor set.
    ///
    /// This is what makes tuning a terrain layer's roughness cheap: only the
    /// TEXTURES decide the descriptor set, so changing a factor is a 144-byte
    /// memcpy into a host-coherent buffer rather than a new allocation. Safe to
    /// call while frames are in flight — the buffer is coherent and the worst
    /// case is one frame rendering with the new value a frame early.
    void update_uniforms(const TerrainMaterialUniforms& params);

    /// Re-write the fourteen bindings from the textures this material currently
    /// references. Call after one of them was hot-reloaded in place (its view()
    /// changed but its identity did not). The caller must ensure the GPU is idle.
    void rewrite_textures();

    void bind(VkCommandBuffer cmd, VkPipelineLayout pipeline_layout, uint32_t set_index) const;

    VkDescriptorSet descriptor_set() const { return descriptor_set_; }
    const TerrainMaterialUniforms& params() const { return params_; }

private:
    VulkanDevice*    device_         = nullptr;
    VkDescriptorPool pool_           = VK_NULL_HANDLE;  // not owned
    VkDescriptorSet  descriptor_set_ = VK_NULL_HANDLE;
    VkBuffer         ubo_            = VK_NULL_HANDLE;
    VkDeviceMemory   ubo_memory_     = VK_NULL_HANDLE;
    void*            ubo_mapped_     = nullptr;   // persistently mapped, host-coherent
    TerrainMaterialUniforms params_{};

    /// Resolved textures for bindings 1..13, in binding order:
    /// [0] splat, [1..4] albedo, [5..8] normal, [9..12] metal/rough.
    /// Non-owning — they point at the caller's textures or shared defaults.
    static constexpr int kBindingCount = 1 + kTerrainMaterialLayers * 3;
    const Texture* tex_[kBindingCount] = {};

    void destroy();
};

}  // namespace gws::renderer::gpu
