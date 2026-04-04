# Phase 2: Graphics & Rendering — Status Report

**Duration:** April 2 – May 13, 2026 (6 weeks estimated)  
**Status:** 🔄 In Progress  
**Starting Tests Passing:** 27/27 (from Phase 1)

---

## Overview

Phase 2 establishes the graphics pipeline: window management, mesh rendering, PBR materials, and deferred shading. The foundation must be rock-solid because every visual feature and performance optimization depends on it.

---

## Implementation Progress

### Week 1-2: Window & Graphics Foundation

#### ✅ Complete
- [ ] GLFW window creation and event handling
- [ ] OpenGL 4.5+ context initialization  
- [ ] Basic rendering loop (clear → draw → swap)
- [ ] Input system integration (keyboard, mouse, gamepad)
- [ ] Window event callbacks (resize, focus, close)

#### 🔄 In Progress
- [ ] Graphics abstraction layer design

#### ⏳ Upcoming
- [ ] Cross-platform input mapping

---

### Week 2-3: Mesh & Material System

#### ⏳ Upcoming
- [ ] Vertex layout definitions (position, normal, tangent, UV)
- [ ] Mesh class and geometry storage
- [ ] OBJ file loader
- [ ] Vertex buffer binding and rendering
- [ ] Material property storage

---

### Week 3-4: PBR & Basic Lighting

#### ⏳ Upcoming
- [ ] PBR shader implementation (metallic-roughness)
- [ ] Light structures (directional, point, spot)
- [ ] Directional light shadow mapping
- [ ] Basic point light support
- [ ] Lighting test scene

---

### Week 4-5: Deferred Rendering Pipeline

#### ⏳ Upcoming
- [ ] G-Buffer (position, normal, albedo, material ID)
- [ ] Deferred geometry pass
- [ ] Deferred lighting pass shader
- [ ] Hybrid forward + deferred rendering
- [ ] Many-light handling (100+ lights)

---

### Week 5-6: Polish & Optimization

#### ⏳ Upcoming
- [ ] Post-processing pipeline (bloom, tone mapping, FXAA)
- [ ] Frustum culling implementation
- [ ] Static batching
- [ ] Basic LOD system
- [ ] Performance profiling and optimization

---

## Architecture Decisions

### Graphics API Strategy
- **Primary:** OpenGL 4.5+ (quick iteration, cross-platform)
- **Future:** Vulkan/DirectX12 abstraction layer designed from start
- **Why OpenGL first:** 2-3 week faster initial learning curve, easier debugging, solid foundation for abstraction

### Rendering Architecture

```
Application (Game code)
      ↓
RenderQueue (batching, sorting)
      ↓
   Renderer (SceneRenderer)
  /        \
Forward   Deferred
Renderer   Renderer
  |          |
  └─→ RenderDevice (GPU abstraction)
        |
      OpenGL
      (+ future: Vulkan)
```

### Buffer Management
- **VAO-centric:** Encapsulate vertex layout, VBO, IBO in single object
- **Persistent mapping:** Use GPU persistent memory for streaming updates
- **Ring buffers:** Avoid GPU stalls during dynamic vertex data updates

---

## Testing Strategy

### Unit Tests (Target: +15 new tests)
```cpp
// Shader compilation and caching
TEST_CASE("Shader caches compiled bytecode") { }
TEST_CASE("Shader detects circular includes") { }

// Material system
TEST_CASE("Material stacks uniform values") { }
TEST_CASE("Material inheritance works") { }

// Frustum culling
TEST_CASE("AABB vs frustum culling") { }
TEST_CASE("Sphere vs frustum culling") { }

// Vertex formats
TEST_CASE("Vertex stride calculation") { }
TEST_CASE("VAO binding validates format") { }
```

### Integration Tests
```cpp
// Test mesh loading
auto mesh = LoadOBJ("assets/models/cube.obj");
REQUIRE(mesh->vertex_count() == 24);

// Test PBR rendering
RenderScene(scene_with_pbr_material);
// Validate rendered pixels against reference
```

### Visual Regression Tests
- Screenshot comparison for lighting correctness
- PBR reference renders vs implementation
- Deferred vs forward rendering visual parity

---

## Known Challenges

### Challenge 1: Shader Hot-Reloading in Debug
- **Problem:** Modify .glsl file → need to see changes without restart
- **Solution:** Shader cache with file stat time tracking, reload on change
- **Implementation:** FileWatcher system (Phase x feature)

### Challenge 2: GPU Synchronization
- **Problem:** GPU lag from CPU → need to avoid CPU-GPU pipeline stalls
- **Solution:** Ring buffers for dynamic data, async texture upload, command buffer recording

### Challenge 3: Deferred Rendering Alpha Blending
- **Problem:** Deferred doesn't handle transparency well
- **Solution:** Hybrid approach — deferred for opaque, forward for transparent
- **Implementation:** Separate render queues by material blend mode

### Challenge 4: Memory Pressure from Textures
- **Problem:** High-res PBR textures eat VRAM quickly
- **Solution:** Implement texture streaming (Phase 4), compression (DXT5, BC4/5)
- **For now:** Limit to reasonable resolution (2K max textures)

---

## Dependencies

### From Phase 1 (already available)
- Math library (Vec3, Mat4, Quaternion, frustum culling functions)
- Memory allocators
- Logging system
- File I/O

### New Third-Party Libraries (add to third_party/)
1. **GLEW or glad** — OpenGL function loader
2. **stb_image.h** — Single-header image loader
3. **Optionally:** assimp for complex model formats (but OBJ first)

### Already in Workspace
- **GLM** — backup/learning reference
- **spdlog** — extend for GPU state logging
- **Catch2** — extend with visual tests

---

## File Structure Plan

```
engine/
├── renderer/                    # NEW
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── render_device.h      # GPU abstraction interface
│   │   ├── shader.h             # Shader compilation & management
│   │   ├── mesh.h               # Mesh geometry
│   │   ├── material.h           # Material definition & PBR
│   │   ├── texture.h            # Texture loading & binding
│   │   ├── light.h              # Light definitions
│   │   ├── camera.h             # Camera & frustum structures
│   │   ├── renderer.h           # High-level renderer API
│   │   ├── render_queue.h       # Batching & sorting
│   │   ├── post_process.h       # Post-processing effects
│   │   └── vulkan/              # Future Vulkan backend
│   │       ├── vulkan_device.h
│   │       └── vk_swapchain.h
│   └── src/
│       ├── opengl/              # OpenGL-specific
│       │   ├── opengl_device.cpp
│       │   ├── opengl_shader.cpp
│       │   └── opengl_buffer.cpp
│       ├── renderer.cpp
│       ├── render_queue.cpp
│       ├── mesh.cpp
│       ├── material.cpp
│       ├── texture.cpp
│       ├── light.cpp
│       ├── post_process.cpp
│       └── tests/
│           ├── test_shader_caching.cpp
│           ├── test_frustum_culling.cpp
│           └── test_material_stack.cpp
│
├── window/                      # NEW (or extend input/)
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── window.h
│   │   └── events.h
│   └── src/
│       ├── window.cpp
│       ├── events.cpp
│       └── tests/
│           └── test_window_events.cpp
│
├── core/                        # FROM PHASE 1 (unchanged)
│   ├── containers/
│   ├── file_io/
│   ├── logging/
│   ├── math/
│   ├── memory/
│   └── physics/
│
└── third_party/
    ├── glfw/                    # Exists
    ├── glm/                     # Exists
    ├── spdlog/                  # Exists
    ├── catch2/                  # Exists
    ├── opengl-headers/          # NEW (or subset of GLEW)
    └── stb/                     # NEW (stb_image.h)
```

---

## Success Metrics

### Performance Targets
- Window creation & frame loop: < 16ms (60 FPS target)
- Shader compilation: < 500ms per shader (cached)
- Mesh rendering (1000 static meshes): < 10ms
- Deferred shading with 100 lights: < 5ms light pass

### Quality Targets
- All unit tests passing (27 + 15 new)
- Integration test suite 100% pass rate
- Visual regression tests pixel-perfect
- Code coverage: > 85% for core renderer paths

### Code Quality
- All code passes clang-format
- No compiler warnings
- Static analysis clean (clang-tidy)
- Documentation for all public interfaces

---

## Blockers & Risks

### Risk 1: OpenGL Debug Performance
- **Severity:** Medium
- **Mitigation:** Use release builds for performance tests, KHR_debug for validation
- **Timeline:** Week 1-2

### Risk 2: Windows-Specific OpenGL Issues
- **Severity:** High (nvidia driver quirks common)
- **Mitigation:** Test on multiple GPU vendors early, use ANGLE if needed
- **Timeline:** Week 1

### Risk 3: Shader Compilation Memory Spikes
- **Severity:** Low
- **Mitigation:** Stream shader compilation, compile off-thread
- **Timeline:** Week 3 (deferred concern)

---

## Phase 2 → Phase 3 Handoff

Once Phase 2 completes:
- **Renderer API** stable and documented
- **Mesh + Material** loading pipeline complete
- **PBR shading** verified against reference
- **Deferred pipeline** functional with 100+ lights
- **Performance baseline** established (frame time budgets)

Phase 3 builds on this:
- Animation system uses mesh & material APIs
- Physics system renders debug geometry
- Editor integrates renderer for viewport
- Particles use renderer for GPU effects

---

## Notes & Observations

### What Went Well in Phase 1
- Clear module boundaries made testing easy
- Math library was over-engineered but pays off now
- CMake + presets setup saved weeks of integration hell

### Phase 2 Lessons to Take Forward
- Early abstraction layers (RenderDevice) prevent expensive refactors later
- Hot-reload debugging features (for shaders) increase iteration speed dramatically
- GPU profiling (RenderDoc integration) catches performance issues early

### Architecture Flexibility
- The RenderDevice abstraction is designed for:
  - Direct3D 12 backend in future
  - Headless rendering (CI/testing)
  - Remote GPU execution (streaming)
- Shader language abstraction with shader transpilation (GLSL → SPIR-V → HLM)

---

## Timeline & Checkpoints

```
Week 1-2 (Apr 2-15)   → Window + basic rendering ✓
Week 2-3 (Apr 16-29)  → Mesh loading + materials
Week 3-4 (Apr 30-13)  → PBR + lighting
Week 4-5 (May 6-19)   → Deferred pipeline
Week 5-6 (May 14-27)  → Polish + optimization

Checkpoint Tests:
✓ Window opens, triangle renders
✓ Mesh loads and displays
✓ Lit sphere matches reference render
✓ Deferred outperforms forward (many lights)
✓ 60 FPS sustained with complex scene
```

---

**Last Updated:** April 2, 2026  
**Next Review:** April 8, 2026 (Week 1 checkpoint)
