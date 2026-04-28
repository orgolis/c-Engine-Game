# Deferred Rendering Pipeline

## Overview

The Deferred Rendering Pipeline provides a scalable, efficient approach to rendering scenes with many dynamic lights. Instead of calculating lighting once per light per pixel (forward rendering), deferred rendering calculates all lighting in a single pass using data stored in the G-Buffer.

## Architecture

### Rendering Pipeline Stages

```
Input (3D Scene Objects)
    ↓
[GEOMETRY PASS] → G-Buffer (4 MRT)
    ↓
[LIGHT CULLING PASS] → Determine visible lights per tile
    ↓
[LIGHTING PASS] → Full-screen lighting calculation
    ↓
[FORWARD PASS] → Transparent objects (hybrid mode)
    ↓
[COMPOSITE PASS] → Final output to screen
```

### G-Buffer (Geometry Buffer)

The G-Buffer stores essential surface properties for deferred lighting calculations:

| Attachment | Format | Content | Usage |
|---|---|---|---|
| Position | RGBA16F | World-space position (XYZ) + padding | Lighting calculations, depth reconstruction |
| Normal | RGBA16F | World-space normal (XYZ) + roughness (W) | Lighting calculations, normal-based effects |
| Albedo | RGBA8 | Surface color (RGB) + metallic (W) | Diffuse/specular calculations |
| Material | RGBA8 | Material ID (R) + AO (G) + Emission (B) + padding (A) | Material selection and effects |
| Depth | R32F | Linear depth or standard depth | Depth-based reconstruction |

### Rendering Modes

```cpp
enum class RenderingMode {
    Forward,    // Traditional forward rendering (all lights per object)
    Deferred,   // Pure deferred (good for many lights)
    Hybrid,     // Deferred opaque + Forward transparent (best quality)
};
```

## Core Components

### 1. GBuffer Class

Manages the multi-render-target framebuffer and its attachments:

```cpp
class GBuffer {
    // Creation
    static std::unique_ptr<GBuffer> Create(RenderDevice* device, const GBufferConfig& config);
    
    // Lifecycle
    void BeginGeometryPass(RenderDevice* device);
    void EndGeometryPass(RenderDevice* device);
    void BeginLightingPass(RenderDevice* device);
    void EndLightingPass(RenderDevice* device);
    
    // Access
    std::shared_ptr<Texture2D> GetPositionTexture() const;
    std::shared_ptr<Texture2D> GetNormalTexture() const;
    std::shared_ptr<Texture2D> GetAlbedoTexture() const;
    std::shared_ptr<Texture2D> GetMaterialTexture() const;
    std::shared_ptr<Texture2D> GetDepthTexture() const;
};
```

### 2. DeferredRenderer Class

Main deferred rendering pipeline orchestrator:

```cpp
class DeferredRenderer {
    // Creation
    static std::unique_ptr<DeferredRenderer> Create(RenderDevice* device, const DeferredConfig& config);
    
    // Rendering passes
    void GeometryPass(RenderDevice* device, const Objects& objects);
    void LightCullingPass(RenderDevice* device, LightManager* lights);
    void LightingPass(RenderDevice* device, LightManager* lights, ...);
    void ForwardPass(RenderDevice* device, const TransparentObjects& objects);
    void CompositePass(RenderDevice* device);
    
    // Configuration
    void SetRenderingMode(RenderingMode mode);
    void SetLightCullingEnabled(bool enabled);
    void SetTileBasedDeferredEnabled(bool enabled);
};
```

### 3. TileLightBinner

Spatially bins lights for efficient culling:

```cpp
class TileLightBinner {
    TileLightBinner(uint32_t width, uint32_t height, uint32_t tile_size);
    
    void BinLights(const std::vector<std::shared_ptr<Light>>& lights, 
                   const glm::mat4& proj_matrix);
    
    const Tile& GetTile(uint32_t x, uint32_t y) const;
    uint32_t GetMaxTileLightCount() const;
};
```

## Usage Examples

### Basic Deferred Renderer Setup

```cpp
using namespace schizo::graphics;

RenderDevice* device = /* get device */;

DeferredConfig config;
config.width = 1920;
config.height = 1080;
config.mode = RenderingMode::Hybrid;
config.enable_light_culling = true;
config.max_lights_per_tile = 32;

auto deferred_renderer = DeferredRenderer::Create(device, config);
```

### Full Frame Rendering

```cpp
// Prepare scene data
std::vector<std::pair<std::shared_ptr<Mesh>, std::shared_ptr<Material>>> opaque_objects;
std::vector<std::pair<std::shared_ptr<Mesh>, std::shared_ptr<Material>>> transparent_objects;

LightManager* light_manager = /* get light manager */;

// Render frame
deferred_renderer->BeginFrame(device);

// Stage 1: Render all opaque objects to G-Buffer
deferred_renderer->GeometryPass(device, opaque_objects);

// Stage 2: Determine which lights affect which tiles
deferred_renderer->LightCullingPass(device, light_manager);

// Stage 3: Calculate lighting using G-Buffer
deferred_renderer->LightingPass(device, light_manager, view_matrix, proj_matrix);

// Stage 4: Render transparent objects (hybrid mode only)
if (deferred_renderer->GetRenderingMode() != RenderingMode::Deferred) {
    deferred_renderer->ForwardPass(device, transparent_objects);
}

// Stage 5: Composite to screen
deferred_renderer->CompositePass(device);

deferred_renderer->EndFrame(device);
```

### G-Buffer Visualization

```cpp
// Debug: Visualize a G-Buffer attachment
GBuffer* gbuffer = deferred_renderer->GetGBuffer();

// View position
DeferredDebugger::VisualizeGBuffer(device, gbuffer, 0);  // Position

// View normals
DeferredDebugger::VisualizeGBuffer(device, gbuffer, 1);  // Normal

// View albedo
DeferredDebugger::VisualizeGBuffer(device, gbuffer, 2);  // Albedo

// View material ID
DeferredDebugger::VisualizeGBuffer(device, gbuffer, 3);  // Material
```

### Handling Window Resize

```cpp
void OnWindowResize(uint32_t new_width, uint32_t new_height) {
    deferred_renderer->Resize(device, new_width, new_height);
}
```

### Tile-Based Light Binning

```cpp
// Enable tile-based optimization
deferred_renderer->SetTileBasedDeferredEnabled(true);

// Perform light culling per tile
deferred_renderer->LightCullingPass(device, light_manager);

// Later, visualize which tiles have how many lights
uint32_t max_lights = deferred_renderer->GetGBuffer()->GetAttachmentCount();
```

## Performance Optimization Techniques

### 1. Light Culling

Only calculate lighting from lights that actually affect a pixel:

```cpp
DeferredConfig config;
config.enable_light_culling = true;
config.max_lights_per_tile = 32;
```

Without culling, all lights are evaluated for every pixel. With culling:
- **Desktop/Console:** Cull per light sphere projection
- **Mobile:** Cull per tile (simplified bounding checks)

### 2. Tile-Based Deferred Rendering

Instead of per-pixel culling, bin lights into screen-space tiles:

```cpp
config.enable_tile_based_deferred = true;
config.tile_size = 16;  // 16x16 pixel tiles
```

Benefits:
- Reduced branching in shader
- Better GPU cache locality
- Fixed work per tile regardless of light count

### 3. Half-Resolution Deferred

For bandwidth-constrained devices, render deferred at half resolution:

```cpp
DeferredConfig deferred_config;
deferred_config.width = 1920 / 2;
deferred_config.height = 1080 / 2;
```

With upsampling/reconstruction pass for final output.

### 4. Hybrid Rendering

Deferred for opaque geometry (many lights), forward for transparency:

```cpp
config.mode = RenderingMode::Hybrid;
// Deferred for opaque objects → many lights efficient
// Forward for transparent → complex blending modes work
```

## Advantages of Deferred Rendering

| Advantage | Details |
|---|---|
| **Scalability** | Performance scales with geometry, not lights |
| **Many Lights** | 100+ dynamic lights without major overhead |
| **Predictable** | Frame time proportional to screen area, not geometry |
| **Post-Processing** | Easy to add effects (bloom, SSAO) using G-Buffer data |
| **Light Culling** | Natural fit for spatial light binning |

## Disadvantages

| Disadvantage | Mitigation |
|---|---|
| **Transparency** | Use Forward Shader on top (Hybrid mode) |
| **MSAA** | G-Buffer MSAA is expensive; use post-process AA instead |
| **Complex Materials** | Limited to G-Buffer slots; use material IDs + lookup tables |
| **Bandwidth** | G-Buffer reads are texture-heavy; use compression |

## Memory Usage

Example for 1920x1080 resolution:

```
Position (RGBA16F):  4 × 2 bytes = 8 MB
Normal   (RGBA16F):  4 × 2 bytes = 8 MB
Albedo   (RGBA8):    4 × 1 bytes = 4 MB
Material (RGBA8):    4 × 1 bytes = 4 MB
Depth    (R32F):     1 × 4 bytes = 8 MB
─────────────────────────────
Total:                          32 MB
```

Compare to forward rendering with 100 lights: ~400+ MB for light buffers.

## Debug Visualization

```cpp
// Enable debug modes
deferred_renderer->SetDebugMode(
    true,   // visualize G-Buffer
    true,   // visualize light count heatmap  
    true    // visualize tile heatmap
);

// Now each frame shows overlay indicating:
// - Which attachments contain what data
// - Per-pixel light count (red = many lights, blue = few)
// - Per-tile light distribution
```

## Performance Metrics

```cpp
float geom_time = deferred_renderer->GetGeometryPassTime();
float cull_time = deferred_renderer->GetLightCullingTime();
float lighting_time = deferred_renderer->GetLightingPassTime();
float frame_time = deferred_renderer->GetTotalFrameTime();

spdlog::info("Frame: {:.2f}ms (Geom: {:.2f}ms, Cull: {:.2f}ms, Lighting: {:.2f}ms)",
             frame_time, geom_time, cull_time, lighting_time);
```

## Best Practices

1. **Use Hybrid Mode by Default** — Best visual quality and flexibility
2. **Enable Light Culling** — Essential for performance with many lights
3. **Reasonable Tile Size** — 16x16 gives good balance between culling efficiency and cache
4. **Limit Lights Per Tile** — 32-64 is typically enough; more has diminishing returns
5. **Avoid High Shadow Count** — Max 4-8 shadow-casting lights even with deferred
6. **Use Half-Res Deferred on Mobile** — If bandwidth is limiting
7. **Visualize G-Buffer During Development** — Catch data corruption early
8. **Profile Shadow Pass Separately** — Shadow rendering is separate from lighting

## Shader Integration

The deferred renderer expects the following shader layout:

### Geometry Pass Input
```glsl
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 tangent;
```

### Geometry Pass Output (to G-Buffer)
```glsl
layout(location = 0) out vec4 out_position;    // World position
layout(location = 1) out vec4 out_normal;      // World normal + roughness
layout(location = 2) out vec4 out_albedo;      // Albedo + metallic
layout(location = 3) out vec4 out_material;    // Material ID + AO + emission
```

### Lighting Pass Input (reads from G-Buffer)
```glsl
uniform sampler2D u_Position;
uniform sampler2D u_Normal;
uniform sampler2D u_Albedo;
uniform sampler2D u_Material;
uniform sampler2D u_Depth;
```

## Future Enhancements

- Clustered deferred rendering (3D light grid)
- Compute shader light culling
- Variable rate deferred (VRS integration)
- MSAA support via resolve passes
- Stochastic rendering for transparency
- Deep G-Buffer for complex materials
