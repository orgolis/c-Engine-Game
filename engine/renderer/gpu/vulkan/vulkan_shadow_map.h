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

/**
 * @enum ShadowMapType
 * @brief Type of shadow mapping
 */
enum class ShadowMapType {
    Cascaded2D,    // Cascaded shadow maps for directional lights
    Cubemap,       // Cubemap for point lights
    Standard2D,    // Standard 2D shadow map for spot lights
};

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
    
    // Helper functions
    void create_shadow_image();
    void create_shadow_sampler();
    void create_render_pass();
    void create_framebuffer();
    void create_cascade_matrices();
    void cleanup();
};

} // namespace gws::renderer::gpu
