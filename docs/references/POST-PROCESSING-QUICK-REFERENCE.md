# Post-Processing Quick Reference

## Quick Start

```cpp
#include "graphics.h"
using namespace schizo::renderer;

// Create and initialize
auto post_processor = PostProcessor::Create(device, 1920, 1080);
post_processor->Initialize();

// Use default stack (recommended)
post_processor->SetupDefaultStack();

// In render loop, after deferred rendering
post_processor->Process(final_hdr_texture, screen_framebuffer, delta_time);
```

## Effect Creation Patterns

### Tone Mapping (HDR → LDR conversion)
```cpp
auto tonemap = std::make_unique<ToneMappingEffect>();
ToneMapConfig cfg;
cfg.op = TonemapOperator::Uncharted2;  // Or ACES, Reinhard, etc.
cfg.exposure = 1.0f;
cfg.gamma = 2.2f;
post_processor->AddEffect(std::move(tonemap), &cfg);
```

### Bloom (Glow effect)
```cpp
auto bloom = std::make_unique<BloomEffect>();
BloomConfig cfg;
cfg.threshold = 1.0f;      // Brightness threshold
cfg.strength = 1.0f;       // Blend strength
cfg.blur_passes = 3;       // Number of blur iterations
post_processor->AddEffect(std::move(bloom), &cfg);
```

### FXAA (Anti-aliasing)
```cpp
auto fxaa = std::make_unique<FXAAEffect>();
FXAAConfig cfg;
cfg.edge_threshold = 0.125f;
post_processor->AddEffect(std::move(fxaa), &cfg);
```

### Vignette (Edge darkening)
```cpp
auto vignette = std::make_unique<VignetteEffect>();
VignetteConfig cfg;
cfg.radius = 1.3f;      // Radius
cfg.softness = 0.5f;    // Edge falloff
cfg.darkness = 0.4f;    // Darkness amount
post_processor->AddEffect(std::move(vignette), &cfg);
```

### Film Grain (Noise)
```cpp
auto grain = std::make_unique<FilmGrainEffect>();
FilmGrainConfig cfg;
cfg.amount = 0.05f;
cfg.colored = true;     // RGB vs luminance
post_processor->AddEffect(std::move(grain), &cfg);
```

### Sharpen (Clarity)
```cpp
auto sharpen = std::make_unique<SharpenEffect>();
SharpenConfig cfg;
cfg.amount = 1.0f;
post_processor->AddEffect(std::move(sharpen), &cfg);
```

### Chromatic Aberration (Lens distortion)
```cpp
auto aberration = std::make_unique<ChromaticAberrationEffect>();
ChromaticAberrationConfig cfg;
cfg.amount = 0.008f;
post_processor->AddEffect(std::move(aberration), &cfg);
```

### Color Grading (Color correction)
```cpp
auto grading = std::make_unique<ColorGradingEffect>();
ColorGradingConfig cfg;
cfg.saturation = 1.2f;
cfg.contrast = 1.1f;
cfg.color_filter = glm::vec3(1.0f, 0.95f, 0.9f);
post_processor->AddEffect(std::move(grading), &cfg);
```

## API Reference

### PostProcessor

| Method | Purpose |
|--------|---------|
| `Create(device, w, h)` | Factory method |
| `Initialize()` | Setup shaders and buffers |
| `AddEffect(effect, config)` | Register effect |
| `RemoveEffect(type)` | Unregister effect |
| `GetEffect(type)` | Get effect pointer |
| `SetEffectEnabled(type, bool)` | Toggle effect |
| `SetAllEffectsEnabled(bool)` | Toggle all effects |
| `GetActiveEffectCount()` | Count enabled effects |
| `Process(source, target, dt)` | Apply all effects |
| `Resize(width, height)` | Update resolutions |
| `SetupDefaultStack()` | Add tone map + bloom + FXAA |
| `GetDevice()` | Get render device |
| `GetIntermediateTexture(idx)` | Access internal texture |
| `GetPingPongBuffer(idx)` | Access internal framebuffer |

### PostProcessingEffect

| Method | Purpose |
|--------|---------|
| `Initialize(device, config)` | Setup (override) |
| `Apply(device, source, target)` | Render (override) |
| `GetType()` | Get effect type (override) |
| `GetName()` | Get name string (override) |
| `SetEnabled(bool)` | Toggle on/off |
| `IsEnabled()` | Check if active |
| `SetIntensity(float)` | Set strength (0-1+) |
| `GetIntensity()` | Get strength |

## Configuration Structs

### Common Fields (All Configs)
```cpp
struct PostProcessingEffectConfig {
    bool enabled = true;
    float intensity = 1.0f;
    uint32_t order = 0;  // Execution order
};
```

### ToneMapConfig
```cpp
TonemapOperator op;              // Algorithm (Uncharted2, ACES, etc.)
float exposure = 1.0f;           // Exposure adjustment
float gamma = 2.2f;              // Gamma correction
float white_point = 11.2f;       // White point (Uncharted2)
```

### BloomConfig
```cpp
float threshold = 1.0f;          // Brightness threshold
float knee = 0.1f;               // Soft threshold
float radius = 1.0f;             // Blur radius
float strength = 1.0f;           // Blend strength
uint32_t blur_passes = 3;        // Blur iterations
uint32_t blur_samples = 16;      // Samples per pass
```

### FXAAConfig
```cpp
float edge_threshold = 0.125f;   // Edge detection
float edge_threshold_min = 0.0625f;  // Minimum threshold
bool show_edges = false;         // Debug visualization
```

### VignetteConfig
```cpp
float radius = 1.2f;             // Vignette radius
float softness = 0.5f;           // Edge softness
float darkness = 0.5f;           // Darkness amount
```

### BloomConfig, FilmGrainConfig, etc.
See POST-PROCESSING-SYSTEM.md for full details.

## Tone Mapping Operators

| Operator | Use Case | Notes |
|----------|----------|-------|
| `Linear` | Debug | No tone mapping |
| `Reinhard` | Fallback | Simple, fast |
| `ReinhardLuminance` | Balanced | Luminance-weighted |
| `FilmicALU` | Filmic | Fast filmic curve |
| `Uncharted2` | **Recommended** | Industry standard |
| `ACES` | Professional | Color accurate |

## Common Recipes

### Cinematic Look
```cpp
post_processor->SetupDefaultStack();

auto vignette = std::make_unique<VignetteEffect>();
VignetteConfig vig_cfg;
vig_cfg.darkness = 0.3f;
post_processor->AddEffect(std::move(vignette), &vig_cfg);

auto grain = std::make_unique<FilmGrainEffect>();
FilmGrainConfig grain_cfg;
grain_cfg.amount = 0.02f;
post_processor->AddEffect(std::move(grain), &grain_cfg);
```

### Bright, Vibrant Look
```cpp
auto tonemap = std::make_unique<ToneMappingEffect>();
ToneMapConfig tm_cfg;
tm_cfg.exposure = 1.2f;
post_processor->AddEffect(std::move(tonemap), &tm_cfg);

auto grading = std::make_unique<ColorGradingEffect>();
ColorGradingConfig grade_cfg;
grade_cfg.saturation = 1.3f;
grade_cfg.contrast = 1.15f;
post_processor->AddEffect(std::move(grading), &grade_cfg);

auto bloom = std::make_unique<BloomEffect>();
BloomConfig bloom_cfg;
bloom_cfg.strength = 1.5f;
post_processor->AddEffect(std::move(bloom), &bloom_cfg);
```

### High-Quality Photo Mode
```cpp
// High-end tone mapping
auto tonemap = std::make_unique<ToneMappingEffect>();
ToneMapConfig tm_cfg;
tm_cfg.op = TonemapOperator::ACES;
tm_cfg.exposure = 1.0f;
post_processor->AddEffect(std::move(tonemap), &tm_cfg);

// Gentle bloom
auto bloom = std::make_unique<BloomEffect>();
BloomConfig bloom_cfg;
bloom_cfg.threshold = 0.9f;
bloom_cfg.strength = 0.8f;
post_processor->AddEffect(std::move(bloom), &bloom_cfg);

// Professional color grading
auto grading = std::make_unique<ColorGradingEffect>();
ColorGradingConfig grade_cfg;
grade_cfg.saturation = 1.0f;   // Natural saturation
grade_cfg.contrast = 1.05f;    // Subtle contrast
post_processor->AddEffect(std::move(grading), &grade_cfg);

// High-quality AA
auto fxaa = std::make_unique<FXAAEffect>();
FXAAConfig fxaa_cfg;
fxaa_cfg.edge_threshold = 0.063f; // Higher precision
post_processor->AddEffect(std::move(fxaa), &fxaa_cfg);
```

### Retro Game Look
```cpp
auto grain = std::make_unique<FilmGrainEffect>();
FilmGrainConfig grain_cfg;
grain_cfg.amount = 0.15f;      // Visible grain
grain_cfg.scale = 2.0f;        // Larger grain
post_processor->AddEffect(std::move(grain), &grain_cfg);

auto tonemap = std::make_unique<ToneMappingEffect>();
ToneMapConfig tm_cfg;
tm_cfg.op = TonemapOperator::Reinhard;  // Simple curve
tm_cfg.gamma = 2.0f;
post_processor->AddEffect(std::move(tonemap), &tm_cfg);

auto grading = std::make_unique<ColorGradingEffect>();
ColorGradingConfig grade_cfg;
grade_cfg.color_filter = glm::vec3(1.0f, 0.9f, 0.8f);  // Warm tint
post_processor->AddEffect(std::move(grading), &grade_cfg);
```

## Performance Quick Guide

| Hardware Level | Recommended Effects | Estimate |
|---------------|--------------------|----------|
| **Mobile/Low** | Tone Map only | 0.2 ms |
| **Mid-range** | ToneMap + FXAA | 0.8 ms |
| **High-end** | ToneMap + Bloom + FXAA + Grading | 2.5 ms |
| **Ultra** | All effects + TAA | 4.0 ms |

## Integration Checklist

- [ ] Create PostProcessor with device and resolution
- [ ] Call Initialize()
- [ ] Add effects (or use SetupDefaultStack())
- [ ] Call Process() each frame after deferred rendering
- [ ] Handle window resize with Resize()
- [ ] Monitor frame time for performance

## Troubleshooting

**Color looks wrong after tone mapping**
→ Check exposure value, try different operators (ACES most neutral)

**Bloom is too strong/weak**
→ Adjust threshold and strength values

**FXAA removes too much detail**
→ Increase edge_threshold_min, or use fewer passes per blur_passes

**Performance is slow**
→ Disable bloom, reduce blur_passes, profile with GetActiveEffectCount()

**Artifacts at screen edges**
→ Using vignette? Check radius value. Using blur? Check edge handling.

## Memory Footprint

```
Resolution: 1920×1080
16-bit RGBA per texture

Per effect:
- Ping-pong FB 0: 8.3 MB
- Ping-pong FB 1: 8.3 MB
- Intermediate buffers (4×): 26.4 MB

Total base: ~43 MB
Per additional effect: +8 MB (if unique buffers)

Total typical: 43-60 MB
```

## Next Steps

1. Read POST-PROCESSING-SYSTEM.md for detailed documentation
2. Review shader implementations in post_processing.cpp
3. Experiment with effect combinations
4. Profile on target hardware
5. Create custom effects as needed

