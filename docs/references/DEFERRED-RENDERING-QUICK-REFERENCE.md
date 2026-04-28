# Deferred Rendering Quick Reference

## Creating Deferred Renderer

```cpp
DeferredConfig config;
config.width = 1920;
config.height = 1080;
config.mode = RenderingMode::Hybrid;
config.enable_light_culling = true;

auto deferred = DeferredRenderer::Create(device, config);
```

## Rendering a Frame

```cpp
// Begin frame
deferred->BeginFrame(device);

// Render opaque objects to G-Buffer
deferred->GeometryPass(device, opaque_objects);

// Determine visible lights
deferred->LightCullingPass(device, light_manager);

// Calculate lighting
deferred->LightingPass(device, light_manager, view_matrix, proj_matrix);

// Render transparent objects (on top)
if (deferred->GetRenderingMode() != RenderingMode::Deferred) {
    deferred->ForwardPass(device, transparent_objects);
}

// Output to screen
deferred->CompositePass(device);

// End frame
deferred->EndFrame(device);
```

## Configuration

```cpp
struct DeferredConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    RenderingMode mode = RenderingMode::Hybrid;
    bool enable_light_culling = true;
    bool enable_tile_based_deferred = false;
    uint32_t tile_size = 16;
    uint32_t max_lights_per_tile = 32;
    uint32_t max_shadow_casting_lights = 8;
    bool debug_gbuffer = false;
    bool debug_light_count = false;
    bool debug_tile_heatmap = false;
};
```

## G-Buffer Access

```cpp
GBuffer* gbuffer = deferred->GetGBuffer();

auto position = gbuffer->GetPositionTexture();
auto normal = gbuffer->GetNormalTexture();
auto albedo = gbuffer->GetAlbedoTexture();
auto material = gbuffer->GetMaterialTexture();
auto depth = gbuffer->GetDepthTexture();

// Resize
gbuffer->Resize(device, new_width, new_height);

// Clear
gbuffer->Clear(device, glm::vec4(0.0f));
```

## Rendering Modes

```cpp
// Forward rendering (traditional per-light calculation)
deferred->SetRenderingMode(RenderingMode::Forward);

// Deferred (all lights in single pass - best for many lights)
deferred->SetRenderingMode(RenderingMode::Deferred);

// Hybrid (deferred opaque + forward transparent - best quality)
deferred->SetRenderingMode(RenderingMode::Hybrid);
```

## Optimization Settings

```cpp
// Enable spatial light culling
deferred->SetLightCullingEnabled(true);

// Enable tile-based light binning
deferred->SetTileBasedDeferredEnabled(true);

// Set configuration after creation
config.tile_size = 16;
config.max_lights_per_tile = 32;
```

## Debug Visualization

```cpp
// Show all debug visualizations
deferred->SetDebugMode(true, true, true);

// Show only G-Buffer
deferred->SetDebugMode(true, false, false);

// Show light count heatmap
deferred->SetDebugMode(false, true, false);

// Show tile distribution
deferred->SetDebugMode(false, false, true);

// Visualize specific G-Buffer attachment
DeferredDebugger::VisualizeGBuffer(device, gbuffer, 0);  // Position
DeferredDebugger::VisualizeGBuffer(device, gbuffer, 1);  // Normal
DeferredDebugger::VisualizeGBuffer(device, gbuffer, 2);  // Albedo
DeferredDebugger::VisualizeGBuffer(device, gbuffer, 3);  // Material
```

## Performance Profiling

```cpp
float geom_time = deferred->GetGeometryPassTime();
float cull_time = deferred->GetLightCullingTime();
float lighting_time = deferred->GetLightingPassTime();
float total = deferred->GetTotalFrameTime();

uint32_t visible_lights = deferred->GetVisibleLightCount();
uint32_t max_tile_lights = deferred->GetMaxLightsPerTile();
```

## G-Buffer Layout

| Index | Name | Format | Contains |
|---|---|---|---|
| 0 | Position | RGBA16F | World position (XYZ) |
| 1 | Normal | RGBA16F | World normal (XYZ) + roughness (W) |
| 2 | Albedo | RGBA8 | Surface color (RGB) + metallic (W) |
| 3 | Material | RGBA8 | Material ID (R) + AO (G) + Emission (B) |
| Depth | Depth | R32F | Linear depth |

## Tile Light Binning

```cpp
TileLightBinner binner(width, height, 16);  // 16x16 tiles
binner.BinLights(lights, proj_matrix);

auto tile = binner.GetTile(pixel_x, pixel_y);
uint32_t light_count = tile.light_count;
for (uint32_t idx : tile.light_indices) {
    // Process light
}
```

## Memory Footprint (1920x1080)

```
Position:  8 MB  (RGBA16F)
Normal:    8 MB  (RGBA16F)
Albedo:    4 MB  (RGBA8)
Material:  4 MB  (RGBA8)
Depth:     8 MB  (R32F)
────────────────━
Total:    32 MB
```

## Window Resize

```cpp
void OnWindowResize(uint32_t width, uint32_t height) {
    deferred->Resize(device, width, height);
}
```

## Light Manager Integration

```cpp
// Get light manager
LightManager* lights = deferred->GetLightManager();

// Or set light manager
deferred->SetLightManager(light_manager);

// Query visible lights after culling
uint32_t visible = deferred->GetVisibleLightCount();
```

## Typical Frame Setup

```cpp
// Create deferred renderer
auto deferred = DeferredRenderer::Create(device, {
    .width = 1920,
    .height = 1080,
    .mode = RenderingMode::Hybrid,
    .enable_light_culling = true,
    .enable_tile_based_deferred = true,
});

// Set light manager
deferred->SetLightManager(your_light_manager);

// Each frame:
deferred->BeginFrame(device);
deferred->GeometryPass(device, opaque_objects);
deferred->LightCullingPass(device, light_manager);
deferred->LightingPass(device, light_manager, view, proj);
deferred->ForwardPass(device, transparent_objects);
deferred->CompositePass(device);
deferred->EndFrame(device);
```

## Tips & Tricks

1. **Hybrid mode** is safest — deferred for opaque, forward for transparent
2. **Enable light culling** — huge performance improvement with many lights
3. **Use tile-based** deferred for 100+ lights efficiency
4. **16x16 tiles** is good default; adjust based on light count/density
5. **Debug visualization** helps verify G-Buffer data correctness
6. **Profile separately** — geometry pass vs lighting pass vs forward pass
7. **Half-res deferred** on mobile — reduce bandwidth requirements
8. **Limit shadow lights** — shadows are expensive even in deferred

## Header Location

Main headers:
- `engine/renderer/include/g_buffer.h`
- `engine/renderer/include/deferred_renderer.h`

Implementation:
- `engine/renderer/src/deferred_renderer.cpp`

Central interface:
- `engine/renderer/include/graphics.h` (includes both)
