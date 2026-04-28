# Post-Processing System Documentation

## Overview

The post-processing system provides a flexible and extensible framework for applying visual effects to the final rendered image. It works seamlessly with the deferred rendering pipeline and supports effect stacking, tone mapping, bloom, anti-aliasing, color grading, and much more.

### Key Features

- **Modular effect architecture** - Add/remove effects independently
- **Flexible stacking** - Chain effects in custom orderings
- **Ping-pong rendering** - Efficient multi-pass effect composition
- **HDR support** - Works with 16-bit and 32-bit floating point textures
- **8 built-in effects** - Ready-to-use post-processing passes
- **Easy extensibility** - Create custom effects by inheriting from PostProcessingEffect
- **Per-effect control** - Enable/disable and adjust intensity per effect
- **Intermediate textures** - Access internal framebuffers for custom shaders

## Architecture

### Core Components

#### 1. PostProcessor (Main Manager)
Central orchestrator that manages all post-processing effects, framebuffers, and rendering.

```cpp
// Create post processor
auto post_processor = PostProcessor::Create(device, width, height);
post_processor->Initialize();

// Add effects
auto tonemap = std::make_unique<ToneMappingEffect>();
ToneMapConfig tm_cfg;
tm_cfg.exposure = 1.0f;
post_processor->AddEffect(std::move(tonemap), &tm_cfg);

// Process final render
post_processor->Process(final_hdr_texture, screen_framebuffer, delta_time);
```

#### 2. PostProcessingEffect (Base Class)
Abstract base class for all post-processing effects. All effects inherit from this.

**Key Methods:**
- `Initialize()` - Shader compilation and setup
- `Apply()` - Execute the effect
- `SetEnabled()` / `IsEnabled()` - Toggle effect on/off
- `SetIntensity()` / `GetIntensity()` - Control effect strength

#### 3. PostProcessingEffectConfig (Configuration)
Base configuration struct with inheritance hierarchy for effect-specific parameters.

```cpp
struct ToneMapConfig : public PostProcessingEffectConfig {
    TonemapOperator op = TonemapOperator::Uncharted2;
    float exposure = 1.0f;
    float gamma = 2.2f;
    float white_point = 11.2f;
};
```

### Rendering Pipeline

```
Input (HDR Texture)
    ↓
[Effect 1] → Intermediate FB 0
    ↓
[Effect 2] → Intermediate FB 1
    ↓
[Effect 3] → Intermediate FB 0
    ↓
[Effect N] → Screen Framebuffer
    ↓
Output (LDR Screen)
```

The post-processor uses ping-pong framebuffers to efficiently chain effects:
- Effect 1 reads from source, writes to FB0
- Effect 2 reads from FB0, writes to FB1
- Effect 3 reads from FB1, writes to FB0
- ... and so on

## Built-in Effects

### 1. Tone Mapping
Converts HDR (high dynamic range) colors to LDR (low dynamic range) for display.

**Configuration:**
```cpp
struct ToneMapConfig {
    TonemapOperator op;           // Tone mapping algorithm
    float exposure = 1.0f;        // Exposure adjustment
    float gamma = 2.2f;           // Gamma correction
    float white_point = 11.2f;    // White point (Uncharted2)
};
```

**Supported Operators:**
- `Reinhard` - Simple Reinhard: `c / (c + 1)`
- `ReinhardLuminance` - Luminance-weighted Reinhard
- `FilmicALU` - John Hable's Filmic ALU curve
- `Uncharted2` - Uncharted 2 filmic tone curve (highly recommended)
- `ACES` - Academy Color Encoding System (industry standard)
- `Linear` - No tone mapping, direct linear output

**Usage:**
```cpp
auto tonemap = std::make_unique<ToneMappingEffect>();
ToneMapConfig cfg;
cfg.op = TonemapOperator::Uncharted2;
cfg.exposure = 1.2f;
cfg.gamma = 2.2f;
post_processor->AddEffect(std::move(tonemap), &cfg);
```

### 2. Bloom
Creates a glow effect from bright areas of the image.

**Configuration:**
```cpp
struct BloomConfig {
    float threshold = 1.0f;       // Brightness threshold for bloom
    float knee = 0.1f;            // Soft threshold range
    float radius = 1.0f;          // Bloom blur radius
    float strength = 1.0f;        // Final blend strength
    uint32_t blur_passes = 3;     // Number of blur passes
    uint32_t blur_samples = 16;   // Samples per blur pass
};
```

**Process:**
1. Threshold pass - Extract bright areas with soft falloff
2. Blur passes - Apply Gaussian blur (configurable iteration count)
3. Composite - Blend bloom back with original

**Usage:**
```cpp
auto bloom = std::make_unique<BloomEffect>();
BloomConfig cfg;
cfg.threshold = 1.0f;
cfg.strength = 1.5f;
post_processor->AddEffect(std::move(bloom), &cfg);
```

### 3. FXAA (Fast Approximate Anti-Aliasing)
Real-time edge-based anti-aliasing with minimal performance cost.

**Configuration:**
```cpp
struct FXAAConfig {
    float edge_threshold = 0.125f;     // Edge detection threshold
    float edge_threshold_min = 0.0625f; // Minimum threshold
    bool show_edges = false;           // Debug visualization
};
```

**Features:**
- Detects edges based on luminance variance
- No history buffer required (no temporal artifacts)
- Works with forward and deferred rendering
- Can optionally visualize detected edges

**Usage:**
```cpp
auto fxaa = std::make_unique<FXAAEffect>();
FXAAConfig cfg;
cfg.edge_threshold = 0.125f;
post_processor->AddEffect(std::move(fxaa), &cfg);
```

### 4. Vignette
Darkens edges of the screen for cinematic effect and focus.

**Configuration:**
```cpp
struct VignetteConfig {
    float radius = 1.2f;          // Vignette radius (>1.0 is center)
    float softness = 0.5f;        // Edge softness/falloff
    float darkness = 0.5f;        // Maximum darkness amount
};
```

**Usage:**
```cpp
auto vignette = std::make_unique<VignetteEffect>();
VignetteConfig cfg;
cfg.radius = 1.3f;
cfg.darkness = 0.4f;
post_processor->AddEffect(std::move(vignette), &cfg);
```

### 5. Film Grain
Adds analog film grain noise for retro or photographic aesthetic.

**Configuration:**
```cpp
struct FilmGrainConfig {
    float amount = 0.05f;         // Grain intensity
    bool colored = true;          // RGB grain vs grayscale
    float scale = 1.0f;           // Grain pattern scale
};
```

**Features:**
- Temporal grain (changes per frame for animation)
- Colored or mono grain options
- Scalable pattern size

**Usage:**
```cpp
auto grain = std::make_unique<FilmGrainEffect>();
FilmGrainConfig cfg;
cfg.amount = 0.03f;
cfg.colored = true;
post_processor->AddEffect(std::move(grain), &cfg);
```

### 6. Sharpen
Unsharp mask-based sharpening for clarity enhancement.

**Configuration:**
```cpp
struct SharpenConfig {
    float amount = 1.0f;          // Sharpening strength
    float radius = 1.0f;          // Kernel radius
};
```

**Usage:**
```cpp
auto sharpen = std::make_unique<SharpenEffect>();
SharpenConfig cfg;
cfg.amount = 0.8f;
post_processor->AddEffect(std::move(sharpen), &cfg);
```

### 7. Chromatic Aberration
Separates RGB channels for lens distortion effect.

**Configuration:**
```cpp
struct ChromaticAberrationConfig {
    float amount = 0.005f;        // Aberration amount
    bool use_depth = false;       // Distance-based aberration
};
```

**Features:**
- Distance-based aberration option (stronger at screen edges)
- Real-time lens distortion effect

**Usage:**
```cpp
auto aberration = std::make_unique<ChromaticAberrationEffect>();
ChromaticAberrationConfig cfg;
cfg.amount = 0.008f;
post_processor->AddEffect(std::move(aberration), &cfg);
```

### 8. Color Grading
Applies color adjustments and LUT (Look-Up Table) transforms.

**Configuration:**
```cpp
struct ColorGradingConfig {
    std::shared_ptr<Texture> lut_texture;  // 3D LUT
    float saturation = 1.0f;
    float contrast = 1.0f;
    glm::vec3 color_filter = glm::vec3(1.0f);
};
```

**Features:**
- 3D LUT support for cinematic color correction
- Saturation adjustment
- Contrast control
- Color filter overlay

**Usage:**
```cpp
auto grading = std::make_unique<ColorGradingEffect>();
ColorGradingConfig cfg;
cfg.saturation = 1.2f;
cfg.contrast = 1.1f;
cfg.color_filter = glm::vec3(1.0f, 0.95f, 0.9f); // Slight warm tint
post_processor->AddEffect(std::move(grading), &cfg);
```

## Usage Patterns

### Basic Usage

```cpp
// Create post processor
auto post_processor = PostProcessor::Create(device, 1920, 1080);
post_processor->Initialize();

// Use default stack (tone map + bloom + FXAA)
post_processor->SetupDefaultStack();

// In render loop:
// After deferred rendering creates final_hdr_texture
post_processor->Process(final_hdr_texture, screen_framebuffer, delta_time);
```

### Custom Effect Stack

```cpp
// Tone mapping
auto tonemap = std::make_unique<ToneMappingEffect>();
ToneMapConfig tm_cfg;
tm_cfg.op = TonemapOperator::ACES;
tm_cfg.exposure = 1.1f;
post_processor->AddEffect(std::move(tonemap), &tm_cfg);

// Bloom
auto bloom = std::make_unique<BloomEffect>();
BloomConfig bloom_cfg;
bloom_cfg.threshold = 0.8f;
bloom_cfg.strength = 1.2f;
post_processor->AddEffect(std::move(bloom), &bloom_cfg);

// Vignette
auto vignette = std::make_unique<VignetteEffect>();
VignetteConfig vig_cfg;
vig_cfg.darkness = 0.3f;
post_processor->AddEffect(std::move(vignette), &vig_cfg);

// Color grading
auto grading = std::make_unique<ColorGradingEffect>();
ColorGradingConfig grade_cfg;
grade_cfg.saturation = 1.15f;
post_processor->AddEffect(std::move(grading), &grade_cfg);

// FXAA
auto fxaa = std::make_unique<FXAAEffect>();
FXAAConfig fxaa_cfg;
post_processor->AddEffect(std::move(fxaa), &fxaa_cfg);
```

### Dynamic Effect Control

```cpp
// Enable/disable effects on demand
post_processor->SetEffectEnabled(PostProcessingEffectType::Bloom, false);
post_processor->SetEffectEnabled(PostProcessingEffectType::FilmGrain, true);

// Adjust effect intensity
auto bloom = post_processor->GetEffect(PostProcessingEffectType::Bloom);
if (bloom) {
    bloom->SetIntensity(0.8f);
}

// Query active effects
uint32_t active_count = post_processor->GetActiveEffectCount();
```

### Resolution Changes

```cpp
// Handle window resize
void OnWindowResize(uint32_t new_width, uint32_t new_height) {
    post_processor->Resize(new_width, new_height);
}
```

## Creating Custom Effects

To create a custom post-processing effect:

```cpp
class MyCustomEffect : public PostProcessingEffect {
public:
    bool Initialize(RenderDevice* device, PostProcessingEffectConfig* config) override {
        // Compile shader
        std::string vs = "...vertex shader source...";
        std::string fs = "...fragment shader source...";
        shader_ = std::make_shared<Shader>();
        return shader_->Compile(device, vs, fs);
    }
    
    void Apply(RenderDevice* device, 
              std::shared_ptr<Texture> source_texture,
              Framebuffer* target_framebuffer) override {
        target_framebuffer->Bind(device);
        device->Clear(true, true, glm::vec4(0.0f));
        
        shader_->Use(device);
        shader_->SetInt(device, "u_texture", 0);
        shader_->SetFloat(device, "u_param", param_value_);
        
        source_texture->Bind(device, 0);
        // Draw fullscreen quad here
    }
    
    PostProcessingEffectType GetType() const override {
        return PostProcessingEffectType::Custom; // or your own enum
    }
    
    std::string GetName() const override { return "MyCustomEffect"; }
    
private:
    std::shared_ptr<Shader> shader_;
    float param_value_ = 1.0f;
};
```

## Integration with Deferred Renderer

```cpp
// Full frame rendering pipeline
void RenderFrame() {
    // Deferred rendering pass
    deferred_renderer->BeginFrame(camera);
    deferred_renderer->GeometryPass(scene);
    deferred_renderer->LightingPass(lights);
    deferred_renderer->ForwardPass(transparent_objects);
    auto hdr_composite = deferred_renderer->CompositePass();
    
    // Post-processing
    post_processor->Process(hdr_composite, screen_framebuffer, delta_time);
}
```

## Performance Considerations

### Memory Usage
For 1920x1080 resolution:
- Ping-pong textures: 2 × 1920 × 1080 × 16-bit RGBA ≈ 13 MB
- Intermediate textures: 4 × 1920 × 1080 × 16-bit RGBA ≈ 26 MB
- Total: ~39 MB

### Performance per Effect (approximate, 1920x1080, GTX 1080)
- Tone Mapping: 0.1-0.2 ms
- FXAA: 0.5-0.8 ms
- Bloom (3 passes): 1.0-2.0 ms
- Vignette: 0.05 ms
- Film Grain: 0.2 ms
- Sharpen: 0.3 ms
- Color Grading: 0.1 ms

### Optimization Tips

1. **Selective Effect Application**
   - Disable expensive effects like bloom on lower-end hardware
   - Use fewer blur passes for bloom (2 instead of 3)

2. **Effect Ordering**
   - Place cheap effects first (vignette, grain)
   - Place expensive effects later if possible (bloom)

3. **Resolution Scaling**
   - Consider downsampling for bloom calculation
   - Can improve performance while maintaining quality

4. **Temporal Effects**
   - Film grain and motion blur can be TAA-compatible
   - Helps hide aliasing and temporal artifacts

## Shader Integration

Effects access the following standard uniforms:

```glsl
// All effects receive:
uniform sampler2D u_texture;      // Source color texture
uniform float u_time;             // Frame time for temporal effects
uniform vec2 u_texel_size;       // 1.0 / texture_dimensions

// Effect-specific uniforms set via shader->SetFloat(), etc.
```

## Debug Features

FXAA has built-in edge visualization:
```cpp
auto fxaa = post_processor->GetEffect(PostProcessingEffectType::FXAA);
FXAAEffect* fxaa_effect = dynamic_cast<FXAAEffect*>(fxaa);
if (fxaa_effect && fxaa_effect->config_) {
    fxaa_effect->config_->show_edges = true;  // Visualize edges
}
```

## Future Enhancement Ideas

1. **Motion Blur** - Temporal blur based on velocity
2. **Depth of Field** - Focus-based blur
3. **SMAA** - Subpixel morphological anti-aliasing
4. **Screen-Space Ambient Occlusion (SSAO)** - AO from depth
5. **Screen-Space Reflections (SSR)** - Real-time reflections
6. **Adaptive Exposure** - Histogram-based eye adaptation
7. **Temporal Anti-Aliasing (TAA)** - Temporal frame reprojection
8. **Variable Rate Shading** - VRS-aware effects
9. **Ray-Traced Effects** - RT reflections, shadows, GI
10. **Machine Learning** - AI-based upscaling (DLSS, FSR)

## Best Practices

1. **Always use tone mapping** - Essential for HDR to LDR conversion
2. **Test on target hardware** - Effects have different costs per GPU
3. **Profile your stack** - Use performance metrics to identify bottlenecks
4. **Order effects by complexity** - Place trivial effects first
5. **Document custom effects** - Make configuration clear
6. **Handle dynamic resolution** - Call Resize() when window changes
7. **Consider artistic direction** - Effects should support your visual style
8. **Test with various exposures** - Effects should work across exposure range

## Troubleshooting

### Color banding after tone mapping
- Increase bit depth of intermediate textures to 32-bit float
- Use dithering in final output

### Bloom appearing too bright
- Reduce bloom threshold value
- Decrease blur radius and passes
- Lower intensity multiplier

### FXAA removing important details
- Increase edge_threshold_min
- Combine with MSAA for better results
- Consider SMAA instead

### Performance issues
- Reduce blur passes for bloom (3 → 2 or 1)
- Disable expensive effects on lower-end hardware
- Consider downsampling for bloom

