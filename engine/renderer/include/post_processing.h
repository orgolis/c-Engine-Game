#pragma once

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>
#include "texture.h"
#include "framebuffer.h"
#include "shader.h"

namespace schizo::renderer {

// Forward declarations
class RenderDevice;
class Mesh;

// ============================================================================
// Post-Processing Effects
// ============================================================================

/**
 * @enum PostProcessingEffectType
 * @brief Available post-processing effect types
 */
enum class PostProcessingEffectType : uint8_t {
    ToneMapping,              // HDR to LDR conversion
    Bloom,                    // Bright area bloom effect
    MotionBlur,               // Temporal motion blur
    ChromaticAberration,      // Color channel separation
    ColorGrading,             // Color lookup table adjustment
    Vignette,                 // Edge darkening
    DepthOfField,             // Out-of-focus blur
    FXAA,                     // Fast approximate anti-aliasing
    SMAA,                     // Subpixel morphological anti-aliasing
    FilmGrain,                // Film grain effect
    SSAO,                     // Screen-space ambient occlusion
    SSR,                      // Screen-space reflections
    Sharpen,                  // Sharpening filter
    GaussianBlur,             // Gaussian blur
    Custom,                   // Custom effect
};

/**
 * @enum TonemapOperator
 * @brief HDR tone mapping operators
 */
enum class TonemapOperator : uint8_t {
    Reinhard,                 // Simple Reinhard
    ReinhardLuminance,        // Reinhard with luminance
    FilmicALU,                // John Hable's Filmic ALU
    Uncharted2,               // Uncharted 2 filmic curve
    ACES,                     // Academy Color Encoding System
    Linear,                   // Linear (no tone mapping)
};

/**
 * @struct PostProcessingEffectConfig
 * @brief Base configuration for post-processing effects
 */
struct PostProcessingEffectConfig {
    bool enabled = true;
    float intensity = 1.0f;
    uint32_t order = 0;  // Execution order
    
    PostProcessingEffectConfig() = default;
    virtual ~PostProcessingEffectConfig() = default;
};

/**
 * @struct ToneMapConfig
 * @brief Configuration for tone mapping effect
 */
struct ToneMapConfig : public PostProcessingEffectConfig {
    TonemapOperator op = TonemapOperator::Uncharted2;
    float exposure = 1.0f;
    float gamma = 2.2f;
    float white_point = 11.2f;  // For Uncharted2
};

/**
 * @struct BloomConfig
 * @brief Configuration for bloom effect
 */
struct BloomConfig : public PostProcessingEffectConfig {
    float threshold = 1.0f;       // Brightness threshold
    float knee = 0.1f;             // Soft threshold
    float radius = 1.0f;           // Bloom radius
    float strength = 1.0f;         // Blend strength
    uint32_t blur_passes = 3;      // Number of blur passes
    uint32_t blur_samples = 16;    // Samples per blur pass
};

/**
 * @struct MotionBlurConfig
 * @brief Configuration for motion blur effect
 */
struct MotionBlurConfig : public PostProcessingEffectConfig {
    float velocity_scale = 1.0f;   // Motion blur strength
    uint32_t samples = 16;         // Number of samples
    bool use_depth = true;         // Use depth-aware motion blur
};

/**
 * @struct ChromaticAberrationConfig
 * @brief Configuration for chromatic aberration effect
 */
struct ChromaticAberrationConfig : public PostProcessingEffectConfig {
    float amount = 0.005f;         // Aberration amount
    bool use_depth = false;        // Distance-based aberration
};

/**
 * @struct ColorGradingConfig
 * @brief Configuration for color grading effect
 */
struct ColorGradingConfig : public PostProcessingEffectConfig {
    std::shared_ptr<Texture> lut_texture;  // 3D LUT texture
    float saturation = 1.0f;
    float contrast = 1.0f;
    glm::vec3 color_filter = glm::vec3(1.0f);
};

/**
 * @struct VignetteConfig
 * @brief Configuration for vignette effect
 */
struct VignetteConfig : public PostProcessingEffectConfig {
    float radius = 1.2f;           // Vignette radius
    float softness = 0.5f;         // Edge softness
    float darkness = 0.5f;         // Darkness amount
};

/**
 * @struct DepthOfFieldConfig
 * @brief Configuration for depth of field effect
 */
struct DepthOfFieldConfig : public PostProcessingEffectConfig {
    float focal_distance = 10.0f;  // Focus distance
    float focal_range = 2.0f;      // Range of focus
    float blur_amount = 1.0f;      // Maximum blur
    uint32_t blur_samples = 16;    // Samples for blur
};

/**
 * @struct FXAAConfig
 * @brief Configuration for FXAA effect
 */
struct FXAAConfig : public PostProcessingEffectConfig {
    float edge_threshold = 0.125f;
    float edge_threshold_min = 0.0625f;
    bool show_edges = false;
};

/**
 * @struct SharpenConfig
 * @brief Configuration for sharpening effect
 */
struct SharpenConfig : public PostProcessingEffectConfig {
    float amount = 1.0f;           // Sharpening amount
    float radius = 1.0f;           // Sharpening radius
};

/**
 * @struct FilmGrainConfig
 * @brief Configuration for film grain effect
 */
struct FilmGrainConfig : public PostProcessingEffectConfig {
    float amount = 0.05f;          // Grain amount
    bool colored = true;           // Colored grain
    float scale = 1.0f;            // Grain scale
};

/**
 * @struct SSAOConfig
 * @brief Configuration for SSAO effect
 */
struct SSAOConfig : public PostProcessingEffectConfig {
    float radius = 1.0f;           // Sample radius
    float bias = 0.025f;           // Depth bias
    uint32_t samples = 16;         // Number of samples
    float power = 1.0f;            // Occlusion power
    bool blur = true;              // Apply blur
};

// ============================================================================
// Post-Processing Effect Base Class
// ============================================================================

/**
 * @class PostProcessingEffect
 * @brief Base class for post-processing effects
 * 
 * All post-processing effects inherit from this class and implement
 * the virtual methods to define effect-specific logic.
 */
class PostProcessingEffect {
public:
    virtual ~PostProcessingEffect() = default;
    
    /**
     * Initialize the effect
     * @param device Graphics device
     * @param config Effect configuration
     * @return True if successful
     */
    virtual bool Initialize(RenderDevice* device, PostProcessingEffectConfig* config) = 0;
    
    /**
     * Apply the effect to source texture
     * @param device Graphics device
     * @param source_texture Input texture
     * @param target_framebuffer Output framebuffer
     */
    virtual void Apply(RenderDevice* device, 
                      std::shared_ptr<Texture> source_texture,
                      Framebuffer* target_framebuffer) = 0;
    
    /**
     * Get the effect type
     */
    virtual PostProcessingEffectType GetType() const = 0;
    
    /**
     * Get the effect name
     */
    virtual std::string GetName() const = 0;
    
    /**
     * Enable/disable the effect
     */
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }
    
    /**
     * Set effect intensity
     */
    void SetIntensity(float intensity) { intensity_ = intensity; }
    float GetIntensity() const { return intensity_; }
    
protected:
    bool enabled_ = true;
    float intensity_ = 1.0f;
};

// ============================================================================
// Post-Processing Pass
// ============================================================================

/**
 * @struct PostProcessPass
 * @brief Represents a single post-processing pass
 */
struct PostProcessPass {
    std::string name;
    std::shared_ptr<ShaderProgram> shader;
    std::shared_ptr<Framebuffer> target;
    uint32_t order = 0;
};

// ============================================================================
// Post Processor
// ============================================================================

/**
 * @class PostProcessor
 * @brief Main post-processing pipeline manager
 * 
 * Manages the application of post-processing effects to the final render.
 * Supports effect stacking, tone mapping, bloom, AA, color grading, and more.
 * 
 * Usage pattern:
 * - Create post processor
 * - Add effects in desired order
 * - Call Process() after main rendering is complete
 */
class PostProcessor {
public:
    /**
     * Create a post processor instance
     * @param device Graphics device
     * @param width Frame buffer width
     * @param height Frame buffer height
     * @return Unique pointer to post processor
     */
    static std::unique_ptr<PostProcessor> Create(RenderDevice* device,
                                                  uint32_t width, uint32_t height);
    
    ~PostProcessor();
    
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;
    PostProcessor(PostProcessor&&) = default;
    PostProcessor& operator=(PostProcessor&&) = default;
    
    /**
     * Initialize the post processor
     * @return True if successful
     */
    bool Initialize();
    
    /**
     * Shut down the post processor
     */
    void Shutdown();
    
    /**
     * Add a post-processing effect
     * @param effect The effect to add
     * @param config Effect-specific configuration
     * @return True if successfully added
     */
    bool AddEffect(std::unique_ptr<PostProcessingEffect> effect,
                   PostProcessingEffectConfig* config);
    
    /**
     * Remove an effect by type
     * @param type Effect type to remove
     */
    void RemoveEffect(PostProcessingEffectType type);
    
    /**
     * Get an effect by type (const)
     * @param type Effect type to retrieve
     * @return Pointer to effect, or nullptr if not found
     */
    PostProcessingEffect* GetEffect(PostProcessingEffectType type);
    const PostProcessingEffect* GetEffect(PostProcessingEffectType type) const;
    
    /**
     * Enable/disable all effects
     */
    void SetAllEffectsEnabled(bool enabled);
    
    /**
     * Enable/disable a specific effect
     */
    void SetEffectEnabled(PostProcessingEffectType type, bool enabled);
    
    /**
     * Get number of active effects
     */
    uint32_t GetActiveEffectCount() const;
    
    /**
     * Process (apply all effects to input)
     * @param source_texture Input texture from deferred renderer
     * @param target_framebuffer Output framebuffer (usually screen)
     * @param time_delta Time since last frame
     */
    void Process(std::shared_ptr<Texture> source_texture,
                Framebuffer* target_framebuffer,
                float time_delta);
    
    /**
     * Resize post-processing buffers
     * @param width New width
     * @param height New height
     */
    void Resize(uint32_t width, uint32_t height);
    
    /**
     * Add common post-processing stack (tone mapping + bloom + FXAA)
     */
    void SetupDefaultStack(const ToneMapConfig* tm_config = nullptr,
                          const BloomConfig* bloom_config = nullptr,
                          const FXAAConfig* fxaa_config = nullptr);
    
    /**
     * Get render device
     */
    RenderDevice* GetDevice() const { return device_; }
    
    /**
     * Get current width
     */
    uint32_t GetWidth() const { return width_; }
    
    /**
     * GetCurrentHeight
     */
    uint32_t GetHeight() const { return height_; }
    
    /**
     * Get current frame time for temporal effects
     */
    float GetFrameTime() const { return frame_time_; }
    
    /**
     * Get internal intermediate texture (for custom shaders)
     */
    std::shared_ptr<Texture> GetIntermediateTexture(uint32_t index = 0);
    
    /**
     * Get ping-pong framebuffer
     */
    Framebuffer* GetPingPongBuffer(uint32_t index);
    
private:
    PostProcessor(RenderDevice* device, uint32_t width, uint32_t height);
    
    // Internal methods
    void CreateFramebuffers();
    void CreateBlitQuad();
    void SetupDefaultEffects();
    PostProcessingEffect* FindEffect(PostProcessingEffectType type);
    
    // Member variables
    RenderDevice* device_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    float frame_time_ = 0.0f;
    
    // Effects
    std::vector<std::unique_ptr<PostProcessingEffect>> effects_;
    std::unordered_map<int, PostProcessingEffect*> effect_map_;
    
    // Framebuffers for ping-pong rendering
    std::shared_ptr<Framebuffer> pingpong_fb_[2];
    std::shared_ptr<Texture> pingpong_texture_[2];
    
    // Intermediate textures
    std::shared_ptr<Texture> intermediate_textures_[4];
    std::shared_ptr<Framebuffer> intermediate_fbs_[4];
    
    // Fullscreen quad for rendering effects
    std::shared_ptr<Mesh> fullscreen_quad_;
    
    // Blit shader for simple texture copy
    std::shared_ptr<ShaderProgram> blit_shader_;
};

// ============================================================================
// Built-in Effect Creators (Factories)
// ============================================================================

/**
 * @class ToneMappingEffect
 * @brief HDR tone mapping post-processing effect
 */
class ToneMappingEffect : public PostProcessingEffect {
public:
    bool Initialize(RenderDevice* device, PostProcessingEffectConfig* config) override;
    void Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
              Framebuffer* target_framebuffer) override;
    PostProcessingEffectType GetType() const override { return PostProcessingEffectType::ToneMapping; }
    std::string GetName() const override { return "ToneMapping"; }
    
    void SetOperator(TonemapOperator op);
    void SetExposure(float exposure);
    void SetGamma(float gamma);
    
private:
    std::shared_ptr<ShaderProgram> shader_;
    ToneMapConfig* config_ = nullptr;
};

/**
 * @class BloomEffect
 * @brief Bloom post-processing effect
 */
class BloomEffect : public PostProcessingEffect {
public:
    bool Initialize(RenderDevice* device, PostProcessingEffectConfig* config) override;
    void Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
              Framebuffer* target_framebuffer) override;
    PostProcessingEffectType GetType() const override { return PostProcessingEffectType::Bloom; }
    std::string GetName() const override { return "Bloom"; }
    
    void SetThreshold(float threshold);
    void SetStrength(float strength);
    
private:
    std::shared_ptr<ShaderProgram> threshold_shader_;
    std::shared_ptr<ShaderProgram> blur_shader_;
    std::shared_ptr<ShaderProgram> composite_shader_;
    BloomConfig* config_ = nullptr;
};

/**
 * @class FXAAEffect
 * @brief FXAA anti-aliasing post-processing effect
 */
class FXAAEffect : public PostProcessingEffect {
public:
    bool Initialize(RenderDevice* device, PostProcessingEffectConfig* config) override;
    void Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
              Framebuffer* target_framebuffer) override;
    PostProcessingEffectType GetType() const override { return PostProcessingEffectType::FXAA; }
    std::string GetName() const override { return "FXAA"; }
    
private:
    std::shared_ptr<ShaderProgram> shader_;
    FXAAConfig* config_ = nullptr;
};

/**
 * @class VignetteEffect
 * @brief Vignette post-processing effect
 */
class VignetteEffect : public PostProcessingEffect {
public:
    bool Initialize(RenderDevice* device, PostProcessingEffectConfig* config) override;
    void Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
              Framebuffer* target_framebuffer) override;
    PostProcessingEffectType GetType() const override { return PostProcessingEffectType::Vignette; }
    std::string GetName() const override { return "Vignette"; }
    
private:
    std::shared_ptr<ShaderProgram> shader_;
    VignetteConfig* config_ = nullptr;
};

/**
 * @class SharpenEffect
 * @brief Sharpening post-processing effect
 */
class SharpenEffect : public PostProcessingEffect {
public:
    bool Initialize(RenderDevice* device, PostProcessingEffectConfig* config) override;
    void Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
              Framebuffer* target_framebuffer) override;
    PostProcessingEffectType GetType() const override { return PostProcessingEffectType::Sharpen; }
    std::string GetName() const override { return "Sharpen"; }
    
private:
    std::shared_ptr<ShaderProgram> shader_;
    SharpenConfig* config_ = nullptr;
};

/**
 * @class ChromaticAberrationEffect
 * @brief Chromatic aberration post-processing effect
 */
class ChromaticAberrationEffect : public PostProcessingEffect {
public:
    bool Initialize(RenderDevice* device, PostProcessingEffectConfig* config) override;
    void Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
              Framebuffer* target_framebuffer) override;
    PostProcessingEffectType GetType() const override { return PostProcessingEffectType::ChromaticAberration; }
    std::string GetName() const override { return "ChromaticAberration"; }
    
private:
    std::shared_ptr<ShaderProgram> shader_;
    ChromaticAberrationConfig* config_ = nullptr;
};

/**
 * @class FilmGrainEffect
 * @brief Film grain post-processing effect
 */
class FilmGrainEffect : public PostProcessingEffect {
public:
    bool Initialize(RenderDevice* device, PostProcessingEffectConfig* config) override;
    void Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
              Framebuffer* target_framebuffer) override;
    PostProcessingEffectType GetType() const override { return PostProcessingEffectType::FilmGrain; }
    std::string GetName() const override { return "FilmGrain"; }
    
private:
    std::shared_ptr<ShaderProgram> shader_;
    FilmGrainConfig* config_ = nullptr;
};

/**
 * @class ColorGradingEffect
 * @brief Color grading using LUT post-processing effect
 */
class ColorGradingEffect : public PostProcessingEffect {
public:
    bool Initialize(RenderDevice* device, PostProcessingEffectConfig* config) override;
    void Apply(RenderDevice* device, std::shared_ptr<Texture> source_texture,
              Framebuffer* target_framebuffer) override;
    PostProcessingEffectType GetType() const override { return PostProcessingEffectType::ColorGrading; }
    std::string GetName() const override { return "ColorGrading"; }
    
    void SetLUT(std::shared_ptr<Texture> lut);
    
private:
    std::shared_ptr<ShaderProgram> shader_;
    ColorGradingConfig* config_ = nullptr;
};

} // namespace schizo::renderer
