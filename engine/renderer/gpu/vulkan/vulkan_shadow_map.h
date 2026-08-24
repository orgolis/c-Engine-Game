/**
 * @file vulkan_shadow_map.h
 * @brief Vulkan implementation of shadow mapping for deferred rendering
 * 
 * Supports cascaded shadow mapping for directional lights and standard shadow mapping
 * for point and spot lights.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanShaderRegistry;
struct DrawItem;

/**
 * @enum ShadowMapType
 * @brief Type of shadow mapping
 */
enum class ShadowMapType {
    Cascaded2D,    // Cascaded shadow maps for directional lights
    Cubemap,       // Cubemap for point lights
    Standard2D,    // Standard 2D shadow map for spot lights
};

/// Which LOD tier the shadow pass draws, given the tier the G-buffer chose for
/// the same submesh at the same distance.
///
/// Ordinary meshes drop one tier. A shadow silhouette tolerates far less detail
/// than a shaded surface, so this is a deliberate and worthwhile saving.
///
/// TERRAIN MUST NOT. A terrain LOD tier is not a decimated copy of the same
/// shape -- it is a DIFFERENT HEIGHTFIELD, with peaks cut off and hollows
/// filled in (see kTerrainMaxLods in scene_render_bridge.h: four tiers, each
/// sampling the heightmap at a coarser step). Rendering a coarser tier into the
/// shadow map records depth for a surface that is not the surface being shaded,
/// and the depth comparison is then against the wrong height: false shadow
/// wherever the coarse tier rises above the fine one, light leaking wherever it
/// falls below. That reads as shadows sitting in the wrong place on terrain.
///
/// The bias itself predates terrain LOD by months and was harmless while every
/// terrain chunk had exactly one tier -- draw_submesh clamped the +1 away. It
/// only became a bug when terrain gained tiers.
inline size_t shadow_lod_for(size_t gbuffer_lod, bool is_terrain) {
    return is_terrain ? gbuffer_lod : gbuffer_lod + 1;
}

/**
 * @struct ShadowMapConfig
 * @brief Configuration for shadow map
 */
struct ShadowMapConfig {
    ShadowMapType type = ShadowMapType::Cascaded2D;
    uint32_t width = 2048;
    uint32_t height = 2048;
    uint32_t cascade_count = 3;      // For cascaded shadow maps
    float cascade_split_lambda = 0.95f;
    bool use_pcf = true;            // Percentage-closer filtering
    uint32_t pcf_samples = 9;       // 3x3 PCF

    /// Material descriptor-set layout (same one passed to the G-Buffer scene
    /// pipeline). Caster pipeline binds this at set=0 so its alpha-test
    /// fragment shader can sample the albedo texture and read the per-material
    /// alpha_cutoff (packed into emissive_factor.a). Pass VK_NULL_HANDLE to
    /// fall back to a pure depth-only caster (Cutout / Blend objects will
    /// then cast solid shadows).
    VkDescriptorSetLayout material_set_layout = VK_NULL_HANDLE;
};

/**
 * @class VulkanShadowMap
 * @brief Vulkan shadow map implementation
 */
class VulkanShadowMap {
public:
    VulkanShadowMap() = default;
    ~VulkanShadowMap();
    
    // Deleted copy operations
    VulkanShadowMap(const VulkanShadowMap&) = delete;
    VulkanShadowMap& operator=(const VulkanShadowMap&) = delete;
    
    // Allow move operations
    VulkanShadowMap(VulkanShadowMap&&) = default;
    VulkanShadowMap& operator=(VulkanShadowMap&&) = default;
    
    /**
     * @brief Create shadow map
     * @param device Vulkan device
     * @param config Shadow map configuration
     * @return Unique pointer to shadow map
     */
    static std::unique_ptr<VulkanShadowMap> create(VulkanDevice* device, 
                                                   const ShadowMapConfig& config);
    
    /**
     * @brief Begin shadow pass for directional light
     */
    void begin_directional_pass(VkCommandBuffer cmd, uint32_t cascade_index);
    
    /**
     * @brief End shadow pass
     */
    void end_pass(VkCommandBuffer cmd);
    
    /**
     * @brief Begin cubemap shadow pass for point light
     */
    void begin_cubemap_pass(VkCommandBuffer cmd, uint32_t face_index);
    
    /**
     * @brief Begin spot light shadow pass
     */
    void begin_spot_pass(VkCommandBuffer cmd);

    /**
     * @brief Iterate the supplied draw list with the depth-only pipeline,
     *        binding each item's mesh and pushing its MVP. Caller must have
     *        already called `begin_directional_pass` (or similar).
     *
     * Shadow stage uses one LOD coarser than the camera selection (an LOD
     * "bias" of +1) since shadow detail loss is much less visible than
     * primary-view detail loss. `light_position` drives the distance
     * heuristic (pass the camera position for now if you don't track a
     * separate shadow caster origin).
     */
    void draw_items(VkCommandBuffer cmd,
                    const glm::mat4& view,
                    const glm::mat4& proj,
                    const glm::vec3& light_position,
                    const DrawItem* draws,
                    size_t draw_count,
                    uint32_t* out_draw_calls = nullptr,
                    uint32_t* out_triangles  = nullptr);
    
    /**
     * @brief Get shadow map view for sampling
     */
    VkImageView get_shadow_view() const { return shadow_view_; }
    
    /**
     * @brief Get shadow map sampler
     */
    VkSampler get_shadow_sampler() const { return shadow_sampler_; }
    
    /**
     * @brief Get framebuffer handle
     */
    VkFramebuffer get_framebuffer() const { return framebuffer_; }
    
    /**
     * @brief Get render pass handle
     */
    VkRenderPass get_render_pass() const { return render_pass_; }
    
    /**
     * @brief Get light view-projection matrix for cascade
     */
    glm::mat4 get_light_vp_matrix(uint32_t cascade_index) const;
    
    /**
     * @brief Update cascade splits
     */
    void update_cascade_splits(const std::vector<float>& splits);
    
    /**
     * @brief Get width
     */
    uint32_t get_width() const { return config_.width; }
    
    /**
     * @brief Get height
     */
    uint32_t get_height() const { return config_.height; }

private:
    VulkanDevice* device_ = nullptr;
    ShadowMapConfig config_;
    
    // Shadow depth image
    VkImage shadow_image_ = VK_NULL_HANDLE;
    VkImageView shadow_view_ = VK_NULL_HANDLE;
    VkDeviceMemory shadow_memory_ = VK_NULL_HANDLE;
    VkSampler shadow_sampler_ = VK_NULL_HANDLE;
    
    // Render pass and framebuffer
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    
    // Cascade data
    std::vector<glm::mat4> cascade_view_matrices_;
    std::vector<glm::mat4> cascade_proj_matrices_;
    std::vector<float> cascade_splits_;

    // Depth-only caster pipeline. Reuses the SceneVertex layout, binds no
    // descriptor sets, and pushes a single mat4 (light-space MVP).
    std::unique_ptr<VulkanShaderRegistry> shader_registry_;
    VkPipeline       caster_pipeline_         = VK_NULL_HANDLE;
    VkPipelineLayout caster_pipeline_layout_  = VK_NULL_HANDLE;

    // Helper functions
    void create_shadow_image();
    void create_shadow_sampler();
    void create_render_pass();
    void create_framebuffer();
    void create_cascade_matrices();
    void create_caster_pipeline();
    void cleanup();
};

} // namespace gws::renderer::gpu
