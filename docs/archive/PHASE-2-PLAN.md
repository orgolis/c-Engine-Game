# Phase 2: Graphics & Rendering — Plan

**Duration:** Estimated 4-6 weeks  
**Status:** Just Started  
**Goal:** Complete graphics pipeline foundation with PBR and deferred shading

---

## Overview

Phase 2 builds the rendering system that will display all the content created by the engine. The focus is on establishing a solid graphics abstraction layer that supports multiple backends (OpenGL, Vulkan) while implementing modern rendering techniques (PBR, deferred shading).

---

## Deliverables

### 1. Graphics Abstraction Layer
- **RenderDevice** - Encapsulates GPU API (OpenGL/Vulkan selector)
- **Shader System** - Compile, link, and manage shaders
- **Buffer Management** - VBO, IBO, UBO abstractions
- **Texture System** - Load, bind, and manage textures
- **Framebuffer Support** - Offscreen rendering for deferred shading
- **Sampler Objects** - Texture filtering and wrapping

### 2. Window & Context Management
- **Window Class** - GLFW integration or custom
- **Graphics Context** - OpenGL/Vulkan context creation
- **Input Polling** - Keyboard, mouse, gamepad input
- **Event System** - Window resize, close, focus events
- **Multi-Monitor Support** - Getters for display info

### 3. Material & PBR System
- **Material Definition** - Base and derived material types
- **Texture Atlasing** - Combine textures for efficiency
- **PBR Properties** - Albedo, Normal, Metallic, Roughness, AO
- **Standard PBR Shader** - Full PBR implementation
- **Material Instancing** - Reuse materials with variant parameters

### 4. Mesh & Geometry
- **Mesh Definition** - Vertices, indices, tangents, UVs
- **Mesh Loading** - Support OBJ, GLTF/GLB formats
- **Vertex Formats** - Different layouts (static, skinned, particle)
- **Bounding Volumes** - AABB, Sphere for culling
- **Tangent Space** - Auto-calculate for normal mapping

### 5. Basic Lighting
- **Light Types** - Directional, Point, Spot
- **Light Attenuation** - Distance falloff
- **Shadow Mapping** - Basic directional shadows
- **Light Culling** - Frustum culling for many lights

### 6. Deferred Rendering Setup
- **G-Buffer Layout** - Position, Normal, Albedo, Material properties
- **Deferred Pass** - Light accumulation from G-Buffer
- **Forward+ Lighting** - Hybrid approach for transparency
- **MSAA Integration** - Anti-aliasing support

### 7. Scene Management
- **Render Queue** - Sort and batch rendering calls
- **Frustum Culling** - Don't render what's not visible
- **Level of Detail (LOD)** - Use simplified meshes at distance
- **Spatial Partitioning** - Octree or BVH for queries
- **Static Batching** - Combine static meshes into single draw call

### 8. Post-Processing
- **PostProcess Stack** - Chain effects (Bloom, FXAA, etc)
- **Bloom** - Simulate HDR glow
- **Tone Mapping** - Convert HDR to SDR for display
- **FXAA** - Fast approximate anti-aliasing (backup for MSAA)
- **Color Grading** - LUT-based color adjustments

---

## Technical Architecture

### Renderer Abstraction Layers

```
Application Layer (Game)
    ↓
SceneRenderer (high-level API)
    ├─→ ForwardRenderer (simple, transparent objects)
    └─→ DeferredRenderer (complex, many lights)
    ↓
RenderDevice (GPU API abstraction)
    ├─→ OpenGLDevice
    ├─→ VulkanDevice
    └─→ (Future: DirectX12)
    ↓
Hardware (GPU)
```

### Data Flow

```
Mesh + Material → RenderQueue → Culling → Sorting → 
GBuffer Pass → LightPass → PostProcess → Display
```

---

## Implementation Order

### Week 1-2: Foundation
1. Window creation and event handling
2. Basic OpenGL context setup
3. Simple triangle rendering
4. Shader compilation and linking
5. Input system integration

### Week 2-3: Mesh & Material
1. Mesh loading (OBJ support)
2. Vertex buffer management
3. Material definition and storage
4. Texture loading and binding
5. Simple diffuse rendering test

### Week 3-4: PBR & Lighting
1. PBR shader implementation
2. Light definition and uniform passing
3. Directional light with shadow mapping
4. Basic point light support
5. Viewing and lighting test scene

### Week 4-5: Deferred Rendering
1. G-Buffer implementation
2. Deferred light pass shader
3. Deferred + Forward hybrid
4. Many-light handling
5. Performance benchmarking

### Week 5-6: Polish & Optimization
1. Post-processing pipeline
2. Frustum culling
3. Static batching
4. LOD system basics
5. Performance optimization

---

## File Structure

```
engine/
├── renderer/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── render_device.h        # GPU API abstraction
│   │   ├── shader.h               # Shader compilation
│   │   ├── mesh.h                 # Mesh definition
│   │   ├── material.h             # Material system
│   │   ├── texture.h              # Texture loading
│   │   ├── light.h                # Light definitions
│   │   ├── camera.h               # Camera classes
│   │   ├── renderer.h             # High-level renderer API
│   │   ├── scene_renderer.h       # SceneRenderer class
│   │   └── post_process.h         # Post-processing effects
│   └── src/
│       ├── render_device.cpp
│       ├── shader.cpp
│       ├── mesh.cpp
│       ├── material.cpp
│       ├── texture.cpp
│       ├── light.cpp
│       ├── camera.cpp
│       ├── scene_renderer.cpp
│       ├── opengl/
│       │   ├── opengl_device.h
│       │   └── opengl_device.cpp
│       └── post_process.cpp
├── window/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── window.h
│   │   └── input.h
│   └── src/
│       ├── window.cpp (GLFW wrapper)
│       └── input.cpp
└── core/
    └── ... (existing from Phase 1)
```

---

## Dependencies

### Existing (already in third_party/)
- **glm** - Math library (used in Phase 1, reused here)
- **spdlog** - Logging

### New Third-Party Libraries
- **GLFW3** - Window and input management
- **OpenGL (via GLEW or glad)** - Graphics API
- **stb_image** - Image loading (add to third_party/)
- **assimp** (optional) - Complex model loading
- **KTX** (optional) - Texture compression format

### Decision: Which renderer to start with?
- **OpenGL 4.5+** - Simpler to start, cross-platform, good for learning
- Use OpenGL first, design abstraction for future Vulkan migration

---

## Testing Strategy

### Unit Tests
- Shader compilation caching
- Vertex format validation
- Material property stacking
- Culling algorithms

### Integration Tests  
- Load and render sample mesh
- PBR rendering matches reference
- Deferred vs Forward comparison
- Performance benchmarks

### Visual Tests
- Render a simple scene
- Verify lighting looks correct
- Shadows render properly
- Post-processing applies correctly

---

## Known Challenges & Solutions

### Challenge 1: OpenGL State Management
- **Problem:** OpenGL is a state machine, easy to accidentally break state
- **Solution:** Use VAO (Vertex Array Objects) encapsulation, avoid global state

### Challenge 2: Shader Synchronization with Physics
- **Problem:** Mesh vertex positions might be animated/deformed
- **Solution:** Use separate vertex buffer updates in render tick, don't modify during physics

### Challenge 3: Many Lights Performance
- **Problem:** Rendering 100+ dynamic lights is slow with forward rendering
- **Solution:** Use deferred rendering or light culling (compute shaders in Phase 3)

### Challenge 4: Texture Memory Pressure
- **Problem:** Large open world has tons of textures
- **Solution:** Streaming system, texture atlasing, virtual texturing (Phase 4)

---

## Success Criteria

✅ **Phase 2 Complete When:**
1. Window creation and rendering loop working
2. Sample mesh loads and displays with proper geometry
3. PBR shader renders correctly with multiple lights
4. Deferred rendering pipeline implemented and tested
5. 60 FPS maintained with reasonable scene complexity (1000+ meshes visible)
6. Post-processing effects apply without breaking rendering
7. Unit tests passing for core systems

---

## Dependencies on Previous Phases

**From Phase 1:**
- Math library (Vec3, Mat4, Quaternion) for transforms and lighting calculations
- Logging system for debugging shader compilation
- Memory allocators for efficient buffer management

**Phase 2 → Phase 3 Handoff:**
- Renderer will be used by physics system to visualize
- Animation system will use mesh and material foundations
- Editor will need access to renderer for viewport

---

## Notes for Implementation

### Shader Organization
- Store shaders as plain text files (not compiled)
- Hot-reload shaders during development (debug feature)
- Pre-compile shaders for shipping builds
- Use shader macros for variant generation

### Material Edition
- Design materials as composable: Base + Overlay + Effects
- Store material presets as JSON/YAML
- Support material hot-reloading for iteration speed

### Performance Profiling
- Use GPU timing queries to profile each pass
- Track draw calls per frame (keep under 5000 for 60 FPS)
- Monitor VRAM usage (target < 2GB for mid-range GPUs)

---

## Next Phase Preview (Phase 3)

Once Phase 2 is complete, Phase 3 will add:
- **Animation System** - Skeletal animation, blending, state machines
- **Physics Simulation** - Integrate Jolt physics with visual feedback
- **Particles** - GPU-driven particle effects for combat/environment
- **VFX System** - Shader-based visual effects chains

---

**Phase 2 Start Date:** April 2, 2026  
**Status:** Planning Complete → Ready to Implement
