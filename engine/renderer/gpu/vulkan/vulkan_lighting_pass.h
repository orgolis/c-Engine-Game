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
    
    // Helper functions
    void create_light_buffer();
    void create_pipeline();
    void create_descriptor_sets();
    void create_output_image(uint32_t width, uint32_t height);
    void cleanup();
};

} // namespace gws::renderer::gpu
