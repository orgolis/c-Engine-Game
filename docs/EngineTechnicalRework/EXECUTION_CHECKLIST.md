# Engine Rework Quick Reference & Execution Checklist
> One-page summary and action items for Phase 0 through Phase 7
> **Date:** April 20, 2026

---

## EXECUTIVE BRIEF

**Problem:** Engine renderer tightly coupled to OpenGL/GLAD. No abstraction layer prevents multi-API support, hardware ray tracing, or future optimization.

**Solution:** 7-phase Vulkan migration (28 weeks) with abstraction layer. All game systems remain unchanged. Existing tests (27/27) will continue to pass.

**Outcome:** Production-ready Vulkan renderer with multi-API support, ray tracing capability, 15-30% performance improvement expected.

---

## PHASE OVERVIEW

| Phase | Duration | Status | Key Deliverable |
|-------|----------|--------|-----------------|
| **0** | Week 1 | 📋 Ready | Clean codebase, Vulkan SDK integrated |
| **1** | Weeks 2-4 | 🎯 Next | Blue window, Vulkan device initialized |
| **2** | Weeks 5-8 | ⏳ Planned | Triangle rendering, shader compilation |
| **3** | Weeks 9-12 | ⏳ Planned | Mesh loading, materials, batch rendering |
| **4** | Weeks 13-16 | ⏳ Planned | Full deferred pipeline (G-Buffer, lighting, shadows) |
| **5** | Weeks 17-20 | ⏳ Planned | Ray tracing, LOD system, occlusion culling |
| **6** | Weeks 21-24 | ⏳ Planned | ImGui integration, debug tools |
| **7** | Weeks 25-28 | ⏳ Planned | Final optimization, documentation complete |

---

## FILES TO READ FIRST

1. **REWORK_MASTER_PLAN.md** — Complete redesign strategy (this should be your master reference)
2. **CLEANUP_GUIDE.md** — Step-by-step file removal/archival instructions
3. **DEPENDENCIES_AND_LIBRARIES.md** — Installation guide for Vulkan SDK and libraries

---

## PHASE 0 CHECKLIST (Week 1) — START HERE

- [ ] **Read Documentation**
  - [ ] Read `REWORK_MASTER_PLAN.md` (1 hour)
  - [ ] Read `CLEANUP_GUIDE.md` (30 min)
  - [ ] Read `DEPENDENCIES_AND_LIBRARIES.md` (30 min)

- [ ] **Install Vulkan SDK**
  - [ ] Download from https://www.lunarg.com/vulkan-sdk/
  - [ ] Install to recommended path
    - Windows: `C:\VulkanSDK\1.3.280`
    - Linux: `/usr/local/vulkan`
  - [ ] Verify: `vulkaninfo` works
  - [ ] Verify: `glslangValidator --version` works

- [ ] **Cleanup Codebase (Execute in order)**
  - [ ] Remove backup files (`.restored`)
  - [ ] Remove stub implementations (`deferred_renderer_stub.cpp`)
  - [ ] Consolidate versioned files (`*_new.cpp`)
  - [ ] Merge physics constraints implementations
  - [ ] Archive outdated documentation (→ `docs/archive/`)
  - [ ] Clean build artifacts (build logs)
  - [ ] Update `.gitignore`
  - [ ] Commit: "cleanup: remove redundant files"

- [ ] **Update Build System**
  - [ ] Create `engine/renderer/gpu/` directory
  - [ ] Move `render_device.h` abstraction here
  - [ ] Update `CMakeLists.txt` with Vulkan detection
  - [ ] Test build: `cmake -B build && cmake --build build`
  - [ ] Verify tests still pass: `./build/bin/tests`
  - [ ] Commit: "build: integrate Vulkan SDK, add abstraction layer"

- [ ] **Git Management**
  - [ ] Ensure all changes committed
  - [ ] Tag cleanup milestone: `git tag cleanup-phase-0`
  - [ ] Push to remote: `git push origin cleanup-phase-0`

**Estimated Time:** 4-5 hours  
**Success Criteria:** All tests pass (27/27), build succeeds, no redundant files

---

## PHASE 1 CHECKLIST (Weeks 2-4) — Vulkan Foundation

- [ ] **Implement Vulkan Device Layer**
  - [ ] Create `VulkanDevice` class (GPU selection, initialization)
  - [ ] Implement swapchain management (triple-buffering)
  - [ ] Setup command buffer ring-buffer allocator
  - [ ] Implement descriptor set allocator (bindless)
  - [ ] Integrate Vulkan Memory Allocator (VMA)

- [ ] **Window System Refactoring**
  - [ ] Create `WindowSurface` abstraction
  - [ ] Separate GLFW (windowing) from graphics context
  - [ ] Implement Vulkan surface creation

- [ ] **Validation & Error Handling**
  - [ ] Enable validation layers
  - [ ] Implement error callback logging
  - [ ] Test: Render blank window with clear color

- [ ] **Tests**
  - [ ] All existing tests still pass (27/27)
  - [ ] New test: VulkanDevice initialization succeeds
  - [ ] New test: Swapchain presents without crashes

**Success Criteria:** Blue window renders without crashes or validation errors

---

## PHASE 2 CHECKLIST (Weeks 5-8) — Graphics Pipeline

- [ ] **Shader System**
  - [ ] Integrate glslang shader compiler
  - [ ] Implement GLSL → SPIR-V compilation
  - [ ] Setup shader caching (pre-compiled .spv files)

- [ ] **Graphics Pipeline**
  - [ ] Implement pipeline creation
  - [ ] Setup render pass definitions
  - [ ] Implement barrier system

- [ ] **Basic Rendering**
  - [ ] Render triangle to screen
  - [ ] Render fullscreen quad
  - [ ] Test G-Buffer visualization (one RT at a time)

- [ ] **Tests**
  - [ ] Triangle renders correctly
  - [ ] Quad renders correctly
  - [ ] Shader compilation works
  - [ ] All original tests still pass (27/27)

**Success Criteria:** Triangle and quad rendering working, shader compilation automated

---

## PHASE 3 CHECKLIST (Weeks 9-12) — Resource Management

- [ ] **Vulkan Resources**
  - [ ] Implement `VulkanBuffer` (vertex, index, staging)
  - [ ] Implement `VulkanImage` (textures, attachments)
  - [ ] Implement texture loading & GPU upload
  - [ ] Setup mip generation via compute

- [ ] **Mesh & Material System**
  - [ ] Port mesh loading (glTF via tinygltf)
  - [ ] Implement material parameter binding
  - [ ] Batch rendering optimization

- [ ] **Render Graph**
  - [ ] Implement declarative render pass graph
  - [ ] Auto-barrier insertion
  - [ ] Resource aliasing support

- [ ] **Tests**
  - [ ] Load & render glTF mesh
  - [ ] Textures apply correctly
  - [ ] Material parameters update
  - [ ] All original tests pass (27/27)

**Success Criteria:** Complex meshes render with textures and materials

---

## PHASE 4 CHECKLIST (Weeks 13-16) — Deferred Rendering

- [ ] **Deferred Pipeline**
  - [ ] Port G-Buffer pass (4 render targets)
  - [ ] Implement HZB (hierarchical Z-buffer)
  - [ ] Port lighting pass (directional, point, spot)
  - [ ] Port shadow mapping (cascaded)
  - [ ] Implement clustered light assignment

- [ ] **Post-Processing**
  - [ ] Bloom implementation
  - [ ] Tone mapping (ACES)
  - [ ] TAA (temporal anti-aliasing)

- [ ] **Performance**
  - [ ] Profile GPU time per pass
  - [ ] Compare to OpenGL baseline
  - [ ] Optimize memory layout

- [ ] **Tests**
  - [ ] Complex scene renders correctly
  - [ ] Shadows cast properly
  - [ ] Performance ≥ OpenGL version
  - [ ] Visual regression tests pass

**Success Criteria:** Full lighting pipeline matching OpenGL output, performance competitive

---

## PHASE 5 CHECKLIST (Weeks 17-20) — Advanced Features

- [ ] **Ray Tracing (Optional for Phase 7)**
  - [ ] Implement acceleration structures (BLAS/TLAS)
  - [ ] Shadow rays + reflections
  - [ ] Software RT fallback

- [ ] **Culling & LOD**
  - [ ] HZB occlusion culling
  - [ ] Portal culling (interior areas)
  - [ ] LOD system (meshoptimizer)

- [ ] **Performance Profiling**
  - [ ] Per-pass GPU timing
  - [ ] Memory bandwidth analysis
  - [ ] Draw call reduction verification

**Success Criteria:** Advanced features working, culling reduces draw calls 50%+

---

## PHASE 6 CHECKLIST (Weeks 21-24) — Editor & Debugging

- [ ] **ImGui Integration**
  - [ ] Vulkan backend for ImGui
  - [ ] Scene hierarchy panel
  - [ ] Inspector (reflection-driven)
  - [ ] 3D viewport with gizmos

- [ ] **Debug Tools**
  - [ ] GPU profiler (per-pass timing)
  - [ ] Physics visualization
  - [ ] Light volume visualization
  - [ ] Memory bandwidth gauge

- [ ] **Tests**
  - [ ] Editor UI responsive
  - [ ] Profiler accurate
  - [ ] All original tests pass (27/27)

**Success Criteria:** Full editor functionality, debugging tools working

---

## PHASE 7 CHECKLIST (Weeks 25-28) — Polish & Finalization

- [ ] **Integration Testing**
  - [ ] All 27 unit tests pass ✅
  - [ ] Graphics integration tests pass ✅
  - [ ] Visual regression tests pass ✅
  - [ ] No validation layer errors ✅

- [ ] **Performance Optimization**
  - [ ] GPU bottleneck identification
  - [ ] Command buffer overhead minimized
  - [ ] Memory bandwidth optimized

- [ ] **Documentation**
  - [ ] Update architecture docs
  - [ ] Write Vulkan API reference
  - [ ] Document shader compilation
  - [ ] Performance benchmarks published

- [ ] **Code Cleanup**
  - [ ] Remove debug code
  - [ ] Code style consistency check
  - [ ] Final code review

- [ ] **Deployment**
  - [ ] Tag release: `vulkan-v1.0`
  - [ ] Build release binaries
  - [ ] Final validation

**Success Criteria:** All systems integrated, optimized, fully tested, documented

---

## QUICK START COMMANDS

### Clone & Setup
```bash
cd c:\dev\ProjectSchizo - Copy\c-Engine-Game

# Ensure Vulkan SDK installed
echo %VULKAN_SDK%
# Should output: C:\VulkanSDK\1.3.280

# Clone submodules
git submodule add https://github.com/zeux/volk.git third_party/volk
git submodule add https://github.com/charles-lunarg/vk-bootstrap.git third_party/vk-bootstrap
git submodule update --init --recursive
```

### Build (Phase 0 Complete)
```bash
mkdir build && cd build

# Windows
cmake .. -DCMAKE_BUILD_TYPE=Debug -DGWS_GRAPHICS_API=vulkan
cmake --build . --config Debug

# Linux
cmake .. -DCMAKE_BUILD_TYPE=Debug -DGWS_GRAPHICS_API=vulkan
cmake --build . -j $(nproc)
```

### Test
```bash
cd build
./bin/tests
# Expected: 27/27 tests passing
```

---

## KEY FILES REFERENCE

| File | Purpose | Phase |
|------|---------|-------|
| `REWORK_MASTER_PLAN.md` | Complete design document | All |
| `CLEANUP_GUIDE.md` | File removal instructions | 0 |
| `DEPENDENCIES_AND_LIBRARIES.md` | Installation guide | All |
| `engine/renderer/gpu/render_device.h` | Abstraction interface | 1 |
| `engine/renderer/gpu/vulkan/vulkan_device.h` | Vulkan implementation | 1 |
| `CMakeLists.txt` | Build configuration | 0 |
| `.gitignore` | Git ignore patterns | 0 |

---

## CRITICAL SUCCESS FACTORS

| Factor | Why Important | Verification |
|--------|---------------|--------------|
| **Abstraction Layer** | Multi-API support, future-proof | Has `RenderDevice` interface ✅ |
| **Backward Compatibility** | Game code unchanged | Game-facing API stable ✅ |
| **Test Passing** | Regression detection | 27+/27 tests passing ✅ |
| **Performance Parity** | Justifies migration | ≥ OpenGL performance ✅ |
| **Documentation Sync** | Onboarding new devs | Docs match code ✅ |
| **Validation Layers** | GPU error catching | Enabled in debug builds ✅ |

---

## TEAM COMMUNICATION

### Announcement Template
```
📢 ENGINE REWORK INITIATIVE

We're migrating the engine from OpenGL (GLAD) to Vulkan over the next 7 months.

✅ What stays the same:
   - All game systems (character, ability, network, physics, animation)
   - All game code (no changes required)
   - All 27 passing unit tests

🔧 What changes:
   - Graphics API: OpenGL → Vulkan
   - Renderer abstraction layer added
   - Build system updated with Vulkan SDK

📅 Timeline:
   - Phase 0 (Week 1): Cleanup & setup
   - Phase 1-7 (Weeks 2-28): Staged implementation
   - Each phase has clear milestones

🎯 Benefits:
   - 15-30% performance improvement expected
   - Ray tracing support
   - Multi-GPU capable
   - Future console support easier

📚 Documentation:
   - REWORK_MASTER_PLAN.md (main reference)
   - CLEANUP_GUIDE.md (immediate actions)
   - DEPENDENCIES_AND_LIBRARIES.md (setup)

Questions? Contact [Lead Engineer]
```

---

## TROUBLESHOOTING MATRIX

| Issue | Cause | Solution |
|-------|-------|----------|
| Build fails: "vulkan.h not found" | VULKAN_SDK not set | Set `%VULKAN_SDK%` or `$VULKAN_SDK` |
| CMake error: "glslang not found" | Vulkan SDK not installed | Install from https://www.lunarg.com/vulkan-sdk/ |
| Tests fail after cleanup | Files deleted incorrectly | Restore from git (files in history) |
| Compilation slow | Shader compilation per-build | Pre-compile shaders, cache .spv files |
| GPU validation errors | Vulkan API misuse | Check validation layer output |
| Performance worse | Incorrect resource layout | Profile GPU to find bottleneck |

---

## SUCCESS METRICS

**Phase 0:** ✅ No redundant files, Vulkan SDK integrated, tests passing
**Phase 1:** ✅ Blue window renders, no validation errors
**Phase 2:** ✅ Triangle/quad rendering, shaders compile
**Phase 3:** ✅ Meshes render with textures
**Phase 4:** ✅ Full lighting pipeline, shadows working
**Phase 5:** ✅ Culling effective, LOD system operational
**Phase 6:** ✅ Editor fully integrated
**Phase 7:** ✅ 27+/27 tests passing, performance benchmarked, documented

---

## NEXT STEPS

1. **NOW:** Read `REWORK_MASTER_PLAN.md` and `CLEANUP_GUIDE.md`
2. **Week 1:** Follow Phase 0 checklist (cleanup & setup)
3. **Week 2:** Start Phase 1 (Vulkan device initialization)
4. **Weekly:** Update this checklist as progress is made

---

**Version:** 1.0  
**Last Updated:** April 20, 2026  
**Status:** Ready for Execution  
**Estimated Total Duration:** 28 weeks (7 months)

