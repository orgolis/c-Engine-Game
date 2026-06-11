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
    // Effect strengths (used when the matching enable is on).
    float chromatic_intensity = 0.005f; // radial UV offset scale
    float vignette_intensity  = 0.5f;   // 0 = none, 1 = strong
    float vignette_radius     = 0.75f;  // distance where darkening starts
    float film_grain_intensity = 0.05f; // additive noise amplitude
    // Sharpen (unsharp mask).
    bool  enable_sharpen      = false;
    float sharpen_intensity   = 0.5f;
    // Lens distortion (barrel +, pincushion -).
    bool  enable_lens_distortion = false;
    float lens_distortion     = 0.15f;
    // Color grading.
    bool  enable_color_grade  = false;
    float cg_temperature      = 0.0f;   // warm(+)/cool(-)
    float cg_tint             = 0.0f;   // magenta(+)/green(-)
    float cg_saturation       = 1.0f;
    float cg_contrast         = 1.0f;
    float cg_brightness       = 1.0f;
    // Stylized effects.
    bool  enable_posterize    = false;
    float posterize_levels    = 8.0f;
    bool  enable_pixelate     = false;
    float pixelate_size       = 4.0f;   // block size in pixels
    bool  enable_scanlines    = false;
    float scanline_intensity  = 0.3f;
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
     * @brief Bind the G-Buffer depth view so auto-exposure can exclude sky
     *        pixels from metering. Call before `set_input_image` (which
     *        lazily inits auto-exposure). A sampleable depth view (the same
     *        one the lighting pass uses) is expected.
     */
    void set_scene_depth(VkImageView depth_view) { scene_depth_view_ = depth_view; }
    
    /**
     * @brief Get output image view (after post-processing) — for sampling.
     */
    VkImageView get_output_image() const { return output_view_; }

    /**
     * @brief Get the underlying output VkImage — for blits / transfers.
     */
    VkImage get_output_vk_image() const { return output_image_; }
    
    /**
     * @brief Execute bloom effect
     */
    void apply_bloom(VkCommandBuffer cmd);
    
    /**
     * @brief Execute tone mapping
     */
    void apply_tone_mapping(VkCommandBuffer cmd);

    /**
     * @brief Dispatch the auto-exposure compute pass. Reads the lit HDR,
     *        smooths the persistent exposure value toward a target based
     *        on average scene luminance. Must run before tone mapping.
     * @param cmd      Command buffer to record into
     * @param delta_s  Seconds since last frame, for frame-rate independent smoothing
     */
    void apply_auto_exposure(VkCommandBuffer cmd, float delta_s);

    /**
     * @brief Turn auto-exposure on/off. When off, tone mapping falls back
     *        to the static `config.tone_mapping.exposure` value.
     */
    void set_auto_exposure_enabled(bool enabled) { auto_exposure_enabled_ = enabled; }
    bool is_auto_exposure_enabled() const { return auto_exposure_enabled_; }

    /**
     * @brief Seconds since previous frame, used by auto-exposure for
     *        frame-rate-independent smoothing. Caller updates this each
     *        frame (typically from the editor loop's `dt`).
     */
    void set_delta_time(float dt) { delta_time_ = dt; }

    /**
     * @brief Apply FXAA-Lite to the post-process output target. Blits the
     *        current output to a private intermediate, then runs the FXAA
     *        fragment shader writing back to the output. Must run after
     *        tone mapping (FXAA works on LDR).
     */
    void apply_fxaa(VkCommandBuffer cmd);

    /// Turn FXAA on/off. Default: on.
    void set_fxaa_enabled(bool enabled) { fxaa_enabled_ = enabled; }
    bool is_fxaa_enabled() const { return fxaa_enabled_; }

    /**
     * @brief Combined colour-FX pass: chromatic aberration + vignette +
     *        film grain, each gated by its enable flag. Runs after FXAA
     *        on the LDR output (blit to intermediate → effects → output).
     */
    void apply_color_fx(VkCommandBuffer cmd);

    // Runtime toggles + tuning for the three colour effects.
    void set_chromatic(bool on, float intensity)  { chromatic_enabled_ = on; config_.chromatic_intensity = intensity; }
    void set_vignette(bool on, float intensity, float radius) { vignette_enabled_ = on; config_.vignette_intensity = intensity; config_.vignette_radius = radius; }
    void set_film_grain(bool on, float intensity)  { film_grain_enabled_ = on; config_.film_grain_intensity = intensity; }
    bool is_chromatic_enabled() const  { return chromatic_enabled_; }
    bool is_vignette_enabled() const   { return vignette_enabled_; }
    bool is_film_grain_enabled() const { return film_grain_enabled_; }
    // Mutable access to live-tune intensities from an editor panel.
    float& chromatic_intensity_ref()  { return config_.chromatic_intensity; }
    float& vignette_intensity_ref()   { return config_.vignette_intensity; }
    float& vignette_radius_ref()      { return config_.vignette_radius; }
    float& film_grain_intensity_ref() { return config_.film_grain_intensity; }

    // Sharpen / lens distortion / color grade.
    void set_sharpen(bool on)          { sharpen_enabled_ = on; }
    void set_lens_distortion(bool on)  { lens_distortion_enabled_ = on; }
    void set_color_grade(bool on)      { color_grade_enabled_ = on; }
    bool is_sharpen_enabled() const         { return sharpen_enabled_; }
    bool is_lens_distortion_enabled() const { return lens_distortion_enabled_; }
    bool is_color_grade_enabled() const     { return color_grade_enabled_; }
    float& sharpen_intensity_ref()    { return config_.sharpen_intensity; }
    float& lens_distortion_ref()      { return config_.lens_distortion; }
    float& cg_temperature_ref()       { return config_.cg_temperature; }
    float& cg_tint_ref()              { return config_.cg_tint; }
    float& cg_saturation_ref()        { return config_.cg_saturation; }
    float& cg_contrast_ref()          { return config_.cg_contrast; }
    float& cg_brightness_ref()        { return config_.cg_brightness; }

    // Stylized: posterize / pixelate / scanlines.
    void set_posterize(bool on)  { posterize_enabled_ = on; }
    void set_pixelate(bool on)   { pixelate_enabled_ = on; }
    void set_scanlines(bool on)  { scanlines_enabled_ = on; }
    bool is_posterize_enabled() const { return posterize_enabled_; }
    bool is_pixelate_enabled() const  { return pixelate_enabled_; }
    bool is_scanlines_enabled() const { return scanlines_enabled_; }
    float& posterize_levels_ref()    { return config_.posterize_levels; }
    float& pixelate_size_ref()       { return config_.pixelate_size; }
    float& scanline_intensity_ref()  { return config_.scanline_intensity; }

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

    // Shader registry (owned). Compiles GLSL to SPIR-V at runtime.
    std::unique_ptr<class VulkanShaderRegistry> shader_registry_;

    // Sampler used for reading the input HDR texture.
    VkSampler input_sampler_ = VK_NULL_HANDLE;

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

    // TAA: blends current input with `taa_history_` (initially zero) and
    // writes to `taa_output_image_`. This is a *temporal smear*, not real
    // TAA — without motion vectors there's no reprojection, so moving
    // pixels just ghost. The pass exists to validate the GPU machinery
    // and slots into the post-process chain so a future motion-vector
    // implementation can swap the shader without re-plumbing.
    VkRenderPass     taa_render_pass_         = VK_NULL_HANDLE;
    VkFramebuffer    taa_framebuffer_         = VK_NULL_HANDLE;
    VkDescriptorSetLayout taa_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool taa_descriptor_pool_     = VK_NULL_HANDLE;
    VkDescriptorSet  taa_descriptor_set_      = VK_NULL_HANDLE;
    VkPipelineLayout taa_pipeline_layout_     = VK_NULL_HANDLE;
    VkPipeline       taa_pipeline_            = VK_NULL_HANDLE;
    VkImage          taa_output_image_        = VK_NULL_HANDLE;
    VkImageView      taa_output_view_         = VK_NULL_HANDLE;
    VkDeviceMemory   taa_output_memory_       = VK_NULL_HANDLE;

    // Bloom: bright-pass + Gaussian blur into bloom_mips_[0] (1/2 res).
    // Tone mapping samples this with linear filtering for automatic upsample.
    VkRenderPass bloom_render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer bloom_framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout bloom_descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool bloom_descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet bloom_descriptor_set_ = VK_NULL_HANDLE;
    VkPipelineLayout bloom_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline bloom_pipeline_ = VK_NULL_HANDLE;
    uint32_t bloom_width_ = 0;
    uint32_t bloom_height_ = 0;
    
    // Effect state
    bool bloom_enabled_ = true;
    bool tone_mapping_enabled_ = true;
    bool taa_enabled_ = true;
    bool chromatic_enabled_ = false;
    bool vignette_enabled_ = false;
    bool film_grain_enabled_ = false;
    bool sharpen_enabled_ = false;
    bool lens_distortion_enabled_ = false;
    bool color_grade_enabled_ = false;
    bool posterize_enabled_ = false;
    bool pixelate_enabled_ = false;
    bool scanlines_enabled_ = false;
    bool auto_exposure_enabled_ = true;

    // Auto-exposure resources. Buffer persists across frames so the
    // smoothed exposure value carries over without CPU readback.
    VkBuffer              auto_exposure_buffer_  = VK_NULL_HANDLE;
    VkDeviceMemory        auto_exposure_memory_  = VK_NULL_HANDLE;
    VkDescriptorSetLayout auto_exposure_dsl_     = VK_NULL_HANDLE;
    VkDescriptorPool      auto_exposure_pool_    = VK_NULL_HANDLE;
    VkDescriptorSet       auto_exposure_set_     = VK_NULL_HANDLE;
    VkPipelineLayout      auto_exposure_layout_  = VK_NULL_HANDLE;
    VkPipeline            auto_exposure_pipeline_ = VK_NULL_HANDLE;
    VkSampler             auto_exposure_sampler_ = VK_NULL_HANDLE;
    VkImageView           scene_depth_view_      = VK_NULL_HANDLE; // for sky exclusion
    bool                  auto_exposure_initialized_ = false;
    bool                  auto_exposure_needs_seed_  = true;
    float                 delta_time_ = 1.0f / 60.0f; // sensible default before first set

    bool init_auto_exposure_resources();

    // FXAA resources: a private intermediate image (copy of output_image_
    // before FXAA runs), render pass + framebuffer writing back to the
    // main output_view_, plus its own pipeline + descriptor set.
    bool                  fxaa_enabled_       = true;
    bool                  fxaa_initialized_   = false;
    VkImage               fxaa_intermediate_image_  = VK_NULL_HANDLE;
    VkDeviceMemory        fxaa_intermediate_memory_ = VK_NULL_HANDLE;
    VkImageView           fxaa_intermediate_view_   = VK_NULL_HANDLE;
    VkSampler             fxaa_sampler_       = VK_NULL_HANDLE;
    VkRenderPass          fxaa_render_pass_   = VK_NULL_HANDLE;
    VkFramebuffer         fxaa_framebuffer_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout fxaa_dsl_           = VK_NULL_HANDLE;
    VkDescriptorPool      fxaa_pool_          = VK_NULL_HANDLE;
    VkDescriptorSet       fxaa_set_           = VK_NULL_HANDLE;
    VkPipelineLayout      fxaa_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline            fxaa_pipeline_      = VK_NULL_HANDLE;

    bool init_fxaa_resources();

    // Colour-FX resources (chromatic + vignette + grain), same private-
    // intermediate pattern as FXAA.
    bool                  colorfx_initialized_ = false;
    float                 colorfx_time_        = 0.0f; // accumulates dt for grain
    VkImage               colorfx_intermediate_image_  = VK_NULL_HANDLE;
    VkDeviceMemory        colorfx_intermediate_memory_ = VK_NULL_HANDLE;
    VkImageView           colorfx_intermediate_view_   = VK_NULL_HANDLE;
    VkSampler             colorfx_sampler_     = VK_NULL_HANDLE;
    VkRenderPass          colorfx_render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer         colorfx_framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout colorfx_dsl_         = VK_NULL_HANDLE;
    VkDescriptorPool      colorfx_pool_        = VK_NULL_HANDLE;
    VkDescriptorSet       colorfx_set_         = VK_NULL_HANDLE;
    VkPipelineLayout      colorfx_pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline            colorfx_pipeline_    = VK_NULL_HANDLE;

    bool init_colorfx_resources();
    
    // Helper functions
    void create_output_image();
    void create_bloom_resources();
    void create_taa_resources();
    void create_descriptor_sets();
    void create_pipelines();
    void create_tonemap_render_pass();
    void create_tonemap_framebuffer();
    void create_tonemap_sampler();
    void create_tonemap_pipeline();
    void update_input_descriptor();
    void create_bloom_render_pass();
    void create_bloom_framebuffer();
    void create_bloom_descriptor();
    void create_bloom_pipeline();
    void update_bloom_descriptor();
    void create_taa_output_image();
    void create_taa_render_pass();
    void create_taa_framebuffer();
    void create_taa_descriptor();
    void create_taa_pipeline();
    void update_taa_descriptor();
    void prime_taa_history();
    void cleanup();
};

} // namespace gws::renderer::gpu
