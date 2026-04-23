/**
 * @file vulkan_post_processing.h
 * @brief Vulkan implementation of post-processing effects
 * 
 * Supports bloom, tone mapping, temporal anti-aliasing, and other screen-space effects.
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
 * @enum PostProcessEffect
 * @brief Post-processing effect types
 */
enum class PostProcessEffect {
    Bloom,           // Bloom/glow effect
    ToneMapping,     // Tone mapping (ACES)
    TAA,             // Temporal anti-aliasing
    FXAA,            // Fast approximate anti-aliasing
    Chromatic,       // Chromatic aberration
    Vignette,        // Vignette effect
    FilmGrain,       // Film grain
};

/**
 * @struct BloomConfig
 * @brief Configuration for bloom effect
 */
struct BloomConfig {
    bool enabled = true;
    float threshold = 1.0f;        // Luminance threshold for bloom
    float intensity = 1.0f;        // Bloom contribution strength
    uint32_t mip_levels = 5;       // Number of downsampled mip levels
    float blur_sigma = 1.0f;       // Gaussian blur sigma
};

/**
 * @struct ToneMappingConfig
 * @brief Configuration for tone mapping
 */
struct ToneMappingConfig {
    bool enabled = true;
    float exposure = 1.0f;         // Overall exposure
    float gamma = 2.2f;            // Gamma correction
    // ACES tone mapping parameters
    float contrast = 1.0f;
    float saturation = 1.0f;
};

/**
 * @struct TAAConfig
 * @brief Configuration for temporal anti-aliasing
 */
struct TAAConfig {
    bool enabled = true;
    float blend_factor = 0.05f;    // Blend between current and history
    uint32_t max_samples = 8;      // Number of samples for jitter
};

/**
 * @struct PostProcessingConfig
 * @brief Overall post-processing configuration
 */
struct PostProcessingConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    BloomConfig bloom;
    ToneMappingConfig tone_mapping;
    TAAConfig taa;
    bool enable_chromatic = false;
    bool enable_vignette = false;
    bool enable_film_grain = false;
};

/**
 * @class VulkanPostProcessing
 * @brief Vulkan post-processing system
 */
class VulkanPostProcessing {
public:
    VulkanPostProcessing() = default;
    ~VulkanPostProcessing();
    
    // Deleted copy operations
    VulkanPostProcessing(const VulkanPostProcessing&) = delete;
    VulkanPostProcessing& operator=(const VulkanPostProcessing&) = delete;
    
    // Allow move operations
    VulkanPostProcessing(VulkanPostProcessing&&) = default;
    VulkanPostProcessing& operator=(VulkanPostProcessing&&) = default;
    
    /**
     * @brief Create post-processing system
     * @param device Vulkan device
     * @param config Post-processing configuration
     * @return Unique pointer to post-processing system
     */
    static std::unique_ptr<VulkanPostProcessing> create(VulkanDevice* device,
                                                        const PostProcessingConfig& config);
    
    /**
     * @brief Set input image (before post-processing)
     */
    void set_input_image(VkImageView image_view);
    
    /**
     * @brief Get output image (after post-processing)
     */
    VkImageView get_output_image() const { return output_view_; }
    
    /**
     * @brief Execute bloom effect
     */
    void apply_bloom(VkCommandBuffer cmd);
    
    /**
     * @brief Execute tone mapping
     */
    void apply_tone_mapping(VkCommandBuffer cmd);
    
    /**
     * @brief Execute TAA
     */
    void apply_taa(VkCommandBuffer cmd);
    
    /**
     * @brief Execute chromatic aberration
     */
    void apply_chromatic(VkCommandBuffer cmd);
    
    /**
     * @brief Execute vignette effect
     */
    void apply_vignette(VkCommandBuffer cmd);
    
    /**
     * @brief Execute film grain
     */
    void apply_film_grain(VkCommandBuffer cmd);
    
    /**
     * @brief Execute all enabled post-processing effects
     */
    void render(VkCommandBuffer cmd);
    
    /**
     * @brief Enable/disable effect
     */
    void set_effect_enabled(PostProcessEffect effect, bool enabled);
    
    /**
     * @brief Check if effect is enabled
     */
    bool is_effect_enabled(PostProcessEffect effect) const;
    
    /**
     * @brief Update configuration
     */
    void update_config(const PostProcessingConfig& config);
    
    /**
     * @brief Resize render targets
     */
    void resize(uint32_t width, uint32_t height);

private:
    VulkanDevice* device_ = nullptr;
    PostProcessingConfig config_;
    
    // Input/output images
    VkImageView input_view_ = VK_NULL_HANDLE;
    VkImage output_image_ = VK_NULL_HANDLE;
    VkImageView output_view_ = VK_NULL_HANDLE;
    VkDeviceMemory output_memory_ = VK_NULL_HANDLE;
    
    // Bloom resources
    std::vector<VkImage> bloom_mips_;
    std::vector<VkImageView> bloom_mip_views_;
    std::vector<VkDeviceMemory> bloom_mip_memories_;
    VkImage bloom_output_ = VK_NULL_HANDLE;
    VkImageView bloom_output_view_ = VK_NULL_HANDLE;
    VkDeviceMemory bloom_output_memory_ = VK_NULL_HANDLE;
    
    // TAA resources
    VkImage taa_history_ = VK_NULL_HANDLE;
    VkImageView taa_history_view_ = VK_NULL_HANDLE;
    VkDeviceMemory taa_history_memory_ = VK_NULL_HANDLE;
    
    // Pipeline resources
    VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets_;
    
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    
    std::vector<VkPipeline> pipelines_;
    std::vector<VkPipelineLayout> pipeline_layouts_;
    
    // Effect state
    bool bloom_enabled_ = true;
    bool tone_mapping_enabled_ = true;
    bool taa_enabled_ = true;
    bool chromatic_enabled_ = false;
    bool vignette_enabled_ = false;
    bool film_grain_enabled_ = false;
    
    // Helper functions
    void create_output_image();
    void create_bloom_resources();
    void create_taa_resources();
    void create_descriptor_sets();
    void create_pipelines();
    void cleanup();
};

} // namespace gws::renderer::gpu
