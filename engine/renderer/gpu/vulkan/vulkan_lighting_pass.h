/**
 * @file vulkan_lighting_pass.h
 * @brief Vulkan implementation of deferred lighting pass
 * 
 * Implements the lighting computation for deferred rendering using G-Buffer data.
 * Supports directional, point, and spot lights with optional shadow mapping.
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

namespace gws::renderer::gpu {

class VulkanDevice;
class VulkanGBuffer;
class VulkanShaderRegistry;
class Texture;

/**
 * @enum LightType
 * @brief Type of light source
 */
enum class LightType {
    Directional,    // Directional light (sun)
    Point,          // Point light (omni)
    Spot,           // Spot light
};

/**
 * @struct Light
 * @brief Light data structure
 */
struct Light {
    glm::vec4 position;          // xyz = position, w = type (0=directional, 1=point, 2=spot)
    glm::vec4 direction;         // xyz = direction, w = intensity
    glm::vec4 color_radius;      // xyz = color, w = radius (for point lights)
    glm::vec4 attenuation;       // x = constant, y = linear, z = quadratic, w = spot_angle
    glm::mat4 shadow_matrix;     // For shadow mapping
    uint32_t shadow_map_index;   // Which shadow map to use (if any)
    uint32_t casts_shadow;       // 1 if this light casts shadow, 0 otherwise
    uint32_t _pad[2];
};

/**
 * @struct LightingConfig
 * @brief Configuration for lighting pass
 */
struct LightingConfig {
    uint32_t max_lights = 128;
    uint32_t max_shadow_casting_lights = 8;
    glm::vec3 ambient_color = glm::vec3(0.1f);
    float global_ambient = 0.1f;
    bool enable_ibl = true;
    bool enable_shadows = true;
};

/**
 * @class VulkanLightingPass
 * @brief Vulkan deferred lighting pass implementation
 */
class VulkanLightingPass {
public:
    VulkanLightingPass() = default;
    ~VulkanLightingPass();
    
    // Deleted copy operations
    VulkanLightingPass(const VulkanLightingPass&) = delete;
    VulkanLightingPass& operator=(const VulkanLightingPass&) = delete;
    
    // Allow move operations
    VulkanLightingPass(VulkanLightingPass&&) = default;
    VulkanLightingPass& operator=(VulkanLightingPass&&) = default;
    
    /**
     * @brief Create lighting pass
     * @param device Vulkan device
     * @param config Lighting configuration
     * @param gbuffer G-Buffer for lighting data
     * @return Unique pointer to lighting pass
     */
    static std::unique_ptr<VulkanLightingPass> create(VulkanDevice* device,
                                                      const LightingConfig& config,
                                                      VulkanGBuffer* gbuffer);
    
    /**
     * @brief Add a light to the scene
     */
    void add_light(const Light& light);

    /// Convenience: add a directional (sun-style) light. Direction is
    /// normalised internally; intensity is stored in direction.w.
    /// @return Index of the inserted light, or UINT32_MAX if the cap was hit.
    uint32_t add_directional_light(const glm::vec3& direction,
                                   const glm::vec3& color,
                                   float intensity,
                                   bool casts_shadow = true,
                                   const glm::mat4& shadow_view_proj = glm::mat4(1.0f));

    /// Convenience: add an omnidirectional point light with smooth-falloff
    /// quadratic attenuation.
    uint32_t add_point_light(const glm::vec3& position,
                             const glm::vec3& color,
                             float intensity,
                             float radius,
                             bool casts_shadow = false);

    /// Convenience: add a spot light. @p inner_cone_cos and @p outer_cone_cos
    /// describe the cosine of the inner/outer cone half-angles for smooth
    /// edge falloff (the outer cosine is packed into attenuation.w to match
    /// the lighting shader).
    uint32_t add_spot_light(const glm::vec3& position,
                            const glm::vec3& direction,
                            const glm::vec3& color,
                            float intensity,
                            float range,
                            float outer_cone_cos,
                            float inner_cone_cos = 1.0f,
                            bool casts_shadow = false,
                            const glm::mat4& cookie_vp = glm::mat4(1.0f),
                            bool has_cookie = false);

    /// Bind the cookie (gobo) texture sampled by cookie-enabled spot lights.
    /// Pass VK_NULL_HANDLE to clear. Owned by the caller (the editor).
    void set_cookie(VkImageView view, VkSampler sampler);

    /// Convenience: add a rectangular area light shaded with LTC. `center` is
    /// the rect centre; `right`/`up` are the half-edge vectors (so the rect
    /// spans center ± right ± up). `two_sided` lights both faces.
    /// @return Index of the inserted light, or UINT32_MAX if the cap was hit.
    uint32_t add_area_light(const glm::vec3& center,
                            const glm::vec3& right,
                            const glm::vec3& up,
                            const glm::vec3& color,
                            float intensity,
                            float range,
                            bool two_sided = false);

    /**
     * @brief Update light at index
     */
    void update_light(uint32_t index, const Light& light);
    
    /**
     * @brief Remove light at index
     */
    void remove_light(uint32_t index);
    
    /**
     * @brief Clear all lights
     */
    void clear_lights();
    
    /**
     * @brief Get light count
     */
    uint32_t get_light_count() const { return light_count_; }
    
    /**
     * @brief Get light at index
     */
    const Light& get_light(uint32_t index) const;
    
    /**
     * @brief Begin lighting pass
     */
    void begin_pass(VkCommandBuffer cmd, uint32_t width, uint32_t height);
    
    /**
     * @brief End lighting pass
     */
    void end_pass(VkCommandBuffer cmd);
    
    /**
     * @brief Render lighting pass
     */
    void render(VkCommandBuffer cmd);
    
    /**
     * @brief Set ambient light
     */
    void set_ambient_light(float intensity);
    
    /**
     * @brief Get ambient light
     */
    float get_ambient_light() const { return config_.global_ambient; }
    
    /**
     * @brief Resize render target
     */
    void resize(uint32_t width, uint32_t height);

    /// Bind a 2D-array shadow map view (cascaded directional shadows).
    /// Triggers a descriptor-set update so the binding is live for the
    /// next render. Pass VK_NULL_HANDLE to fall back to the dummy 1×1.
    void set_directional_shadow_map(VkImageView view, VkSampler sampler);

    /// Bind a cubemap shadow map view (point-light shadows). Triggers a
    /// descriptor-set update; pass VK_NULL_HANDLE to fall back to the dummy.
    void set_point_shadow_map(VkImageView view, VkSampler sampler);

    /// Bind the three IBL textures baked by VulkanEnvironmentMap::bake_ibl.
    /// Pass any null to clear; when cleared, the shader's `iblPrefilterMips`
    /// push-constant is forced to 0 and the IBL term doesn't contribute.
    /// `prefilter_mips` is the last valid mip level (max LOD for roughness).
    void set_ibl_textures(VkImageView irradiance_view, VkSampler irradiance_sampler,
                          VkImageView prefilter_view,  VkSampler prefilter_sampler,
                          VkImageView brdf_lut_view,   VkSampler brdf_lut_sampler,
                          uint32_t prefilter_mips);

    /// IBL contribution scale [0, 1]. 0 = ignore IBL (use legacy ambient).
    /// 1 = use only the split-sum IBL term as ambient. Default 1.
    void set_ibl_intensity(float intensity) { ibl_intensity_ = intensity; }

    /// How far light spreads into a shadow. 0 keeps the old hard edge; larger
    /// widens the penumbra -- a bigger PCF radius for the shadow-map path, and
    /// a wider sampling cone for the ray-query path, which is otherwise binary
    /// and has no gradient at all.
    /// Rays per pixel for ray-query soft shadows (1..8, default 8).
    ///
    /// The most expensive single number in the renderer: 8 rays per pixel per
    /// light at 1080p is ~16.6M traversals a frame, and the lighting pass measured
    /// 10-12 ms on an RTX 3060 with an 18-draw scene. 1 ray is a hard edge.
    void set_shadow_ray_count(int n) { shadow_ray_count_ = n < 1 ? 1 : (n > 8 ? 8 : n); }
    int  shadow_ray_count() const { return shadow_ray_count_; }
    void set_shadow_softness(float s) { shadow_softness_ = s < 0.0f ? 0.0f : s; }
    float shadow_softness() const { return shadow_softness_; }

    /// How much light still reaches a fully occluded point -- the light that
    /// arrives by bouncing rather than directly. 0 is the old pure black, which
    /// with a low ambient turns every shadow into a flat silhouette.
    void set_shadow_persistence(float p) { shadow_persistence_ = p < 0.0f ? 0.0f : p; }
    float shadow_persistence() const { return shadow_persistence_; }

    /// Toggle the hardware ray-tracing shadow path. When on AND `set_tlas`
    /// has bound a non-null acceleration structure, the lighting shader's
    /// `directionalShadow` switches from PCF-sampled shadow maps to an
    /// inline `rayQueryEXT` against the TLAS — no shadow-map aliasing,
    /// no acne, no swimming as the camera moves.
    void set_rt_enabled(bool enabled) { rt_enabled_ = enabled; }
    bool is_rt_enabled() const { return rt_enabled_; }

    /// Toggle ray-traced ambient occlusion. When on (and RT is active),
    /// the shader replaces the screen-space SSAO texture sample with N
    /// hemisphere rays against the TLAS — sees real off-screen geometry,
    /// so sealed rooms / caves get genuinely dark. Costs more than SSAO.
    void set_rt_ao_enabled(bool enabled) { rt_ao_enabled_ = enabled; }
    bool is_rt_ao_enabled() const { return rt_ao_enabled_; }

    /// Bind the scene TLAS at descriptor binding 13. Pass VK_NULL_HANDLE
    /// to clear (which also forces the shader's RT branch off via the
    /// push-constant flag). Updates the descriptor set immediately.
    void set_tlas(VkAccelerationStructureKHR tlas);

    /// Bind the environment cubemap (typically the base map owned by
    /// VulkanEnvironmentMap). Sampled by the lighting shader's sky branch
    /// for pixels where the G-Buffer wrote no geometry. Pass nullptr to
    /// disable — the sky branch then renders solid black.
    void set_env_cubemap(VkImageView view, VkSampler sampler);

    /// Bind the SSAO occlusion texture written by VulkanSsaoPass.
    /// R8_UNORM, sampled per-pixel and multiplied into the ambient term.
    /// Pass VK_NULL_HANDLE for either argument to fall back to the dummy
    /// (white) view — the shader multiply then becomes a no-op.
    void set_ssao_texture(VkImageView view, VkSampler sampler);
    /// Cloud shadow map — the sun (directional light) is multiplied by its
    /// transmittance so clouds darken the ground. Unbound / params.w=0 = lit.
    void set_cloud_shadow(VkImageView view, VkSampler sampler);
    void set_cloud_shadow_params(const glm::vec4& p) { cloud_params_ = p; }

    /// Set the camera world-space position used for view-direction reconstruction.
    void set_camera_position(const glm::vec3& position) { camera_position_ = position; }

    /// Set the camera's view + projection so the sky branch can
    /// reconstruct world-space view rays per pixel.
    void set_view_projection(const glm::mat4& view, const glm::mat4& proj) {
        view_matrix_ = view;
        proj_matrix_ = proj;
    }

    /// HDR output of this pass (R16G16B16A16_SFLOAT, in SHADER_READ_ONLY_OPTIMAL
    /// after `end_pass`). Downstream passes (post-processing) sample this.
    VkImageView get_output_view() const { return output_view_; }

    /// Was a directional shadow map bound? Useful for tests / debug overlays.
    bool has_directional_shadow_map() const {
        return directional_shadow_view_ != VK_NULL_HANDLE;
    }
    bool has_point_shadow_map() const {
        return point_shadow_view_ != VK_NULL_HANDLE;
    }

    /// Resources exposed for forward passes (e.g. transparent) that want to
    /// reuse the same light data and shadow textures as deferred shading.
    /// The effective_* getters return the live shadow map when one is bound
    /// and the internal dummy 1×1 otherwise, so callers can write descriptor
    /// sets unconditionally.
    VkBuffer    get_light_buffer() const { return light_buffer_; }
    uint32_t    get_max_lights() const { return config_.max_lights; }
    VkImageView get_effective_directional_shadow_view() const {
        return directional_shadow_view_ != VK_NULL_HANDLE
                   ? directional_shadow_view_
                   : dummy_shadow_2d_array_view_;
    }
    VkSampler   get_effective_directional_shadow_sampler() const {
        return directional_shadow_sampler_ != VK_NULL_HANDLE
                   ? directional_shadow_sampler_
                   : shadow_sampler_;
    }
    VkImageView get_effective_point_shadow_view() const {
        return point_shadow_view_ != VK_NULL_HANDLE
                   ? point_shadow_view_
                   : dummy_shadow_cube_view_;
    }
    VkSampler   get_effective_point_shadow_sampler() const {
        return point_shadow_sampler_ != VK_NULL_HANDLE
                   ? point_shadow_sampler_
                   : shadow_sampler_;
    }

private:
    VulkanDevice* device_ = nullptr;
    VulkanGBuffer* gbuffer_ = nullptr;
    LightingConfig config_;
    
    // Lights
    std::vector<Light> lights_;
    uint32_t light_count_ = 0;
    
    // GPU resources
    VkBuffer light_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory light_buffer_memory_ = VK_NULL_HANDLE;
    
    // Pipeline resources
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    
    // Output
    VkImage output_image_ = VK_NULL_HANDLE;
    VkImageView output_view_ = VK_NULL_HANDLE;
    VkDeviceMemory output_memory_ = VK_NULL_HANDLE;
    
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    // Sampler used when reading G-Buffer attachments in the fragment shader.
    VkSampler gbuffer_sampler_ = VK_NULL_HANDLE;

    // Sampler used when reading shadow map textures (depth-format, linear filter).
    VkSampler shadow_sampler_ = VK_NULL_HANDLE;

    // Dummy 1×1 shadow textures used when no real shadow map is bound.
    // The PBR fragment shader unconditionally samples both shadowMap (2D
    // array) and pointShadowMap (cube), so the descriptor set must always
    // have something valid bound at bindings 5 + 6.
    VkImage      dummy_shadow_2d_array_image_  = VK_NULL_HANDLE;
    VkImageView  dummy_shadow_2d_array_view_   = VK_NULL_HANDLE;
    VkDeviceMemory dummy_shadow_2d_array_mem_  = VK_NULL_HANDLE;
    VkImage      dummy_shadow_cube_image_      = VK_NULL_HANDLE;
    VkImageView  dummy_shadow_cube_view_       = VK_NULL_HANDLE;
    VkDeviceMemory dummy_shadow_cube_mem_      = VK_NULL_HANDLE;
    // 1x1 R8_UNORM image cleared to 1.0 — SSAO sampler fallback so the
    // shader's `ambient *= ssao` is a no-op when SSAO isn't bound.
    VkImage        dummy_ssao_image_  = VK_NULL_HANDLE;
    VkImageView    dummy_ssao_view_   = VK_NULL_HANDLE;
    VkDeviceMemory dummy_ssao_mem_    = VK_NULL_HANDLE;

    // Camera position pushed to the shader each frame for view-direction
    // computation. Set via `set_camera_position`; defaults to origin.
    glm::vec3 camera_position_ = glm::vec3(0.0f);

    // Owned shader registry for compiling the lighting shaders at startup.
    std::unique_ptr<VulkanShaderRegistry> shader_registry_;

    // Shadow map descriptors (owned by VulkanShadowMap, not destroyed here).
    VkImageView directional_shadow_view_ = VK_NULL_HANDLE;
    VkSampler directional_shadow_sampler_ = VK_NULL_HANDLE;
    VkImageView point_shadow_view_ = VK_NULL_HANDLE;
    VkSampler point_shadow_sampler_ = VK_NULL_HANDLE;

    // IBL descriptors (owned by VulkanEnvironmentMap, not destroyed here).
    VkImageView ibl_irradiance_view_   = VK_NULL_HANDLE;
    VkSampler   ibl_irradiance_sampler_ = VK_NULL_HANDLE;
    VkImageView ibl_prefilter_view_    = VK_NULL_HANDLE;
    VkSampler   ibl_prefilter_sampler_ = VK_NULL_HANDLE;
    VkImageView ibl_brdf_lut_view_     = VK_NULL_HANDLE;
    VkSampler   ibl_brdf_lut_sampler_  = VK_NULL_HANDLE;
    uint32_t    ibl_prefilter_mips_    = 0;
    // RT toggle — Phase 1 scaffold. Phase 3 reads this and switches the
    // shadow path; Phase 4 reads it to skip SSAO sampling in favour of an
    // RT AO ray query. Always false unless the device supports RT AND
    // the caller flips this on.
    bool                        rt_enabled_    = false;
    bool                        rt_ao_enabled_ = false;
    VkAccelerationStructureKHR  tlas_          = VK_NULL_HANDLE;
    // 0.4 default: leaves room for direct lights to dominate well-lit
    // areas (so a placed sun reads as the primary illuminant) while
    // limiting how aggressively the sky bleeds into enclosed spaces.
    // Open outdoor scenes can push this up to ~0.8 from the inspector;
    // for true PBR-correct lighting on a clear outdoor scene, 1.0.
    float       ibl_intensity_         = 0.4f;
    float       shadow_softness_       = 0.35f;
    int   shadow_ray_count_ = 8;   // cone rays for RT soft shadows (see the setter)   // a visible penumbra by default
    float       shadow_persistence_    = 0.05f;   // shadows are dark, not voids

    // Environment cubemap (owned by VulkanEnvironmentMap).
    VkImageView env_cubemap_view_    = VK_NULL_HANDLE;
    VkSampler   env_cubemap_sampler_ = VK_NULL_HANDLE;

    // SSAO occlusion texture (owned by VulkanSsaoPass).
    VkImageView ssao_view_    = VK_NULL_HANDLE;
    VkSampler   ssao_sampler_ = VK_NULL_HANDLE;

    // Cloud shadow map (owned by VulkanCloudPass).
    VkImageView cloud_shadow_view_    = VK_NULL_HANDLE;
    VkSampler   cloud_shadow_sampler_ = VK_NULL_HANDLE;

    // LTC lookup textures for rectangular area lights (bindings 15/16). Owned
    // here (created from the embedded ltc_matrix.h tables).
    std::unique_ptr<Texture> ltc1_tex_;   // inverse-M
    std::unique_ptr<Texture> ltc2_tex_;   // GGX norm / fresnel / horizon

    // Spot-light cookie/gobo (binding 17). View+sampler owned by the editor;
    // falls back to the 1×1 dummy when unset.
    VkImageView cookie_view_    = VK_NULL_HANDLE;
    VkSampler   cookie_sampler_ = VK_NULL_HANDLE;
    glm::vec4   cloud_params_ = glm::vec4(0.0f); // center.xz, half_extent, enabled

    // For the sky branch in the shader — view + projection of the camera.
    glm::mat4   view_matrix_ = glm::mat4(1.0f);
    glm::mat4   proj_matrix_ = glm::mat4(1.0f);

    // Helper functions
    void create_light_buffer();
    void create_pipeline();
    void create_descriptor_sets();
    void create_output_image(uint32_t width, uint32_t height);
    void create_render_pass();
    void create_framebuffer(uint32_t width, uint32_t height);
    void create_gbuffer_sampler();
    void create_shadow_sampler();
    void create_dummy_shadow_textures();
    void create_ltc_textures();
    void update_descriptor_set();
    void upload_lights();
    void cleanup();
};

} // namespace gws::renderer::gpu
