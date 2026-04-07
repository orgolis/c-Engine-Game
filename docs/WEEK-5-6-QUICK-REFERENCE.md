# Week 5-6 Quick Reference

## Post-Processing Pipeline

```cpp
// Setup
auto post_processor = PostProcessor::Create(device, width, height);
post_processor->Initialize();

// Add effects
ToneMapConfig tm_config;
tm_config.op = TonemapOperator::Uncharted2;
tm_config.exposure = 2.0f;
post_processor->AddEffect(std::make_unique<ToneMappingEffect>(), &tm_config);

BloomConfig bloom_config;
bloom_config.threshold = 1.0f;
bloom_config.strength = 0.5f;
post_processor->AddEffect(std::make_unique<BloomEffect>(), &bloom_config);

// Apply
post_processor->Process(hdr_texture, output_framebuffer, delta_time);
```

---

## Frustum Culling

```cpp
// Extract from camera
Frustum frustum;
frustum.ExtractFromCamera(camera);

// Test geometry
if (frustum.IntersectsAABB(mesh_min, mesh_max)) {
    mesh->Draw(device);
}

if (frustum.IntersectsSphere(light_pos, light_radius)) {
    // Light affects this region
}

// Get corner positions for debugging
auto corners = frustum.GetCorners();
```

---

## Static Batching

```cpp
// Create and initialize
auto batch = StaticBatch::Create(device);
batch->Initialize();

// Add instances
uint32_t id1 = batch->AddInstance(tree_mesh, tree_material, transform1);
uint32_t id2 = batch->AddInstance(tree_mesh, tree_material, transform2);
uint32_t id3 = batch->AddInstance(tree_mesh, tree_material, transform3);

// Update
batch->UpdateInstanceTransform(id1, new_transform);
batch->SetInstanceVisible(id2, false);

// Draw (optimized)
batch->Draw(device);

// Stats
auto stats = batch->GetStats();
std::cout << "Draw calls: " << stats.draw_calls << "\n";
std::cout << "Triangles: " << stats.triangles_rendered << "\n";
```

---

## LOD System

```cpp
// Create mesh
auto sphere = Mesh::CreateSphere(device, 1.0f, 32, 16);

// Add LOD levels
std::vector<Vertex> lod0_verts, lod1_verts, lod2_verts;
std::vector<uint32_t> lod0_idx, lod1_idx, lod2_idx;
// ... populate with different detail levels ...

sphere->AddLODLevel(0, lod0_verts, lod0_idx, 5.0f);   // < 5m away
sphere->AddLODLevel(1, lod1_verts, lod1_idx, 15.0f);  // 5-15m
sphere->AddLODLevel(2, lod2_verts, lod2_idx, 50.0f);  // > 15m

// Auto-select and draw
float distance = glm::distance(camera.position, sphere_pos);
sphere->DrawAuto(device, distance);

// Manual control
sphere->DrawLOD(device, 1);  // Force LOD 1
```

---

## Performance Profiling

### CPU Profiling

```cpp
auto profiler = PerformanceProfiler::Create();

// Manual scopes
profiler->BeginScope("RenderPass");
  // ... rendering code ...
profiler->EndScope();

// Or with RAII helper
{
    ProfileScope scope(profiler.get(), "UpdateScene");
    // ... update code ...
}

// Get results
auto results = profiler->GetAllResults();
for (const auto& r : results) {
    std::cout << r.name << ": " << r.avg_ms << "ms\n";
}

// Print report
profiler->PrintReport(true);  // Sort by time
```

### Frame Time Tracking

```cpp
auto frame_tracker = FrameTimeTracker::Create();

while (running) {
    frame_tracker->BeginFrame();
    
    // ... render frame ...
    
    frame_tracker->EndFrame();
    
    if (frame_count % 60 == 0) {
        std::cout << "FPS: " << frame_tracker->GetFPS() << "\n";
        std::cout << "Frame time: " << frame_tracker->GetAverageFrameTime() << "ms\n";
    }
}
```

### Memory Profiling

```cpp
auto mem_profiler = MemoryProfiler::Create();

// Track allocations
mem_profiler->TrackBufferAllocation(vbo_handle, 1024*1024, "VBO");
mem_profiler->TrackBufferAllocation(texture_handle, 4*1024*1024, "Texture");

// Query
size_t total = mem_profiler->GetTotalMemoryUsed();
size_t vbo_mem = mem_profiler->GetMemoryUsedByType("VBO");

mem_profiler->PrintReport();
```

---

## Scene-Renderer Integration (MeshRenderer)

```cpp
// Create entity with renderer
auto entity = scene->CreateEntity("MyObject");
auto renderer = entity->AddComponent<MeshRenderer>();

// Setup geometry
renderer->SetMesh(my_mesh);
renderer->SetMaterial(my_material);

// Control rendering
renderer->SetVisible(true);
renderer->SetCastShadows(true);
renderer->SetReceiveShadows(true);

// LOD control
renderer->SetLODBias(0.5f);      // Prefer higher detail
renderer->SetForceLOD(0);        // Force specific LOD
renderer->UnsetForceLOD();       // Back to automatic

// Query bounds (automatically updated)
glm::vec3 center = renderer->GetBoundsCenter();
glm::vec3 extents = renderer->GetBoundsExtents();
float radius = renderer->GetBoundsRadius();

// Rendering (called by scene/renderer system)
renderer->Render(device, view_matrix, projection_matrix);
```

### LineRenderer (Debug Visualization)

```cpp
auto entity = scene->CreateEntity("DebugViz");
auto lines = entity->AddComponent<LineRenderer>();

// Add debug geometry
lines->AddLine(glm::vec3(0, 0, 0), glm::vec3(10, 0, 0), glm::vec4(1, 0, 0, 1));
lines->AddBox(mesh_min, mesh_max, glm::vec4(1, 1, 0, 1));
lines->AddSphere(light_pos, light_radius, glm::vec4(0, 1, 0, 1));

// Clear
lines->Clear();

// Render
lines->Render(device, view_matrix, projection_matrix);
```

---

## Including Everything

```cpp
#include "graphics.h"  // Includes all rendering systems

// Now available:
// - Frustum (frustum.h)
// - StaticBatch, DynamicBatch (batch.h)
// - PerformanceProfiler, FrameTimeTracker, etc. (profiler.h)
// - MeshRenderer, SkinnedMeshRenderer, LineRenderer (scene module)
// - Mesh::AddLODLevel, etc. (mesh.h)
// - PostProcessor (post_processing.h)
```

---

## Performance Tips

1. **Batching** - Combine static meshes with same material
2. **LOD** - Use for physics debug objects, distant scenery
3. **Frustum Culling** - Always cull before expensive operations
4. **Profiling** - Profile before optimizing (identify real bottleneck)
5. **Post-Processing** - Apply selectively (bloom is expensive)
6. **LOD Bias** - Adjust per-object for quality/perf tradeoff

---

## Common Issues

**Issue:** LOD doesn't switch automatically
- **Solution:** Ensure distance thresholds are set correctly with `AddLODLevel`

**Issue:** Post-processor not blending correctly
- **Solution:** Check that source texture format matches (RGBA16F recommended)

**Issue:** Frustum culling clips visible geometry
- **Solution:** Verify bounds calculation with `UpdateBounds()` call

**Issue:** MeshRenderer component doesn't render
- **Solution:** Ensure entity has a Transform and is active with `SetActive(true)`

---

## See Also

- `WEEK-5-6-COMPLETION.md` - Detailed feature documentation
- `phase-2-status.md` - Overall Phase 2 progress
- Renderer headers for full API documentation
- Scene headers for component lifecycle details
