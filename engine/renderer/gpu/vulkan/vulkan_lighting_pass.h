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
                                   bool casts_shadow = true);

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
                            bool casts_shadow = false);

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

    /// Set the camera world-space position used for view-direction reconstruction.
    void set_camera_position(const glm::vec3& position) { camera_position_ = position; }

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
    void update_descriptor_set();
    void upload_lights();
    void cleanup();
};

} // namespace gws::renderer::gpu
