# Phase 4: Deferred Rendering — Architecture

> **Status:** End-to-end frame works (2026-04-27). `engine_window_test.exe`
> renders a demo triangle through the full deferred pipeline and presents
> it on a GLFW window. See [Status](#status) for the list of what's real
> vs. placeholder.

---

## Pipeline overview

A frame in Phase 4 walks four ordered stages, recorded into a single command
buffer by `VulkanRenderGraph::execute_stage`:

```
┌─────────┐   ┌──────────┐   ┌──────────┐   ┌──────────────┐
│ Shadow  │ → │ Geometry │ → │ Lighting │ → │ PostProcess  │ → swapchain
└─────────┘   └──────────┘   └──────────┘   └──────────────┘
   D32F          G-Buffer       HDR R16F     bloom→TAA→tone
                 (4 colors      output       map composite
                 + D32F depth)  (R16G16B16A16_SFLOAT)
```

Each stage owns its render pass, framebuffer, descriptor set, and pipeline.
The render graph inserts a coarse `vkCmdPipelineBarrier` between consecutive
stages and writes timestamp queries around each one for per-stage µs
profiling. Stages with a null component pointer in `RenderGraphConfig` are
silently skipped (e.g. headless tests can omit shadows).

## Components

### `VulkanGBuffer` — `engine/renderer/gpu/vulkan/vulkan_g_buffer.h`

Owns 4 color attachments (`R16G16B16A16_SFLOAT × 2`, `R8G8B8A8 × 2`) plus
one `D32_SFLOAT` depth attachment, a render pass, and a framebuffer. The
geometry pass clears all attachments at `LOAD_OP_CLEAR` and finalizes the
attachments to `SHADER_READ_ONLY_OPTIMAL` so the lighting pass can sample
them without a manual barrier.

Standard layout (matches the lighting shader's binding indices 0-4):
- 0: world-space position (RGBA16F; alpha unused)
- 1: world-space normal (RGB) + roughness (A)
- 2: albedo (RGB) + metallic (A)
- 3: material id (R) + AO (G) + emission (B) + reserved (A)
- 4: depth (D32F)

**Two pipelines.** As of Phase 5 Week 1 the G-Buffer owns *two* graphics
pipelines targeting the same render pass:

1. **Demo pipeline** — interleaved 3-vertex hardcoded triangle, no
   descriptor sets, push constants `{mat4 mvp; vec4 albedoMetallic}`.
   `draw_demo_triangle(cmd, view, proj)` binds and draws it. Used by the
   smoke test when no draw list is supplied.
2. **Scene pipeline** — built only when `GBufferConfig::material_set_layout`
   is non-null. Vertex format is `SceneVertex {pos, normal, uv, tangent}`
   matching the new `Mesh` class. Pipeline layout has set=0 empty + set=1
   bound to a per-material descriptor (UBO + 5 combined image samplers).
   Push constants `{mat4 mvp; mat4 model}` (80 bytes). The fragment shader
   samples baseColor / normal / metallicRoughness / AO / emissive from
   set=1 and writes the standard G-Buffer attachment layout.

`draw_items(cmd, view, proj, draws, count)` iterates a `DrawItem` list,
binding the scene pipeline once, then per item: `Material::bind` at set=1
(skipping rebinds against the previous draw), `Mesh::bind` (skipping
rebinds), push MVP+model, `vkCmdDrawIndexed`. Caller-supplied draws
replace the demo triangle when present; the demo path stays for
empty-list smoke tests.

### `VulkanShadowMap` — `engine/renderer/gpu/vulkan/vulkan_shadow_map.h`

Owns a `D32_SFLOAT` array image (one layer per cascade for `Cascaded2D`,
6 layers for `Cubemap`, 1 for `Standard2D`), a depth-only render pass,
and a framebuffer. **The framebuffer keeps a live reference to its
`shadow_view_` for its lifetime** — destroying the view immediately after
framebuffer creation (the original code's behaviour) leaves a dangling
internal pointer that segfaults on the first `vkCmdBeginRenderPass`.

**Caster pipeline (Phase 5 Week 1).** A depth-only graphics pipeline now
lives on `VulkanShadowMap`, sharing the `SceneVertex` vertex layout with
the G-Buffer's scene pipeline so a single draw list can be rasterised by
both stages. Single push constant: `mat4 light_mvp` (vertex-only). Front-
face culling + (1.25 const + 1.75 slope) depth bias for shadow-acne
avoidance. `draw_items(cmd, view, proj, draws, count)` iterates the same
`DrawItem` list the geometry stage uses. Cascade matrices are still
camera-derived; per-cascade orthographic projections are the next step.

### `VulkanLightingPass` — `engine/renderer/gpu/vulkan/vulkan_lighting_pass.h`

Owns an `R16G16B16A16_SFLOAT` HDR output image, render pass, framebuffer,
and a graphics pipeline that runs a PBR fragment shader over a fullscreen
triangle (3 vertices from `gl_VertexIndex`).

Descriptor set layout (matches the shader's set=0 bindings):
| Binding | Type                     | Source                                  |
|---------|--------------------------|-----------------------------------------|
| 0-4     | combined image sampler   | G-Buffer position/normal/albedo/material/depth |
| 5       | sampler2DArray           | shadow map (or 1×1 dummy if unbound)    |
| 6       | samplerCube              | point shadow (or 1×1 dummy)             |
| 7       | storage buffer           | `Light[128]` SSBO, host-coherent        |

Push constants (32 bytes, well under the 128-byte spec guarantee):
```c
struct {
    vec3 cameraPos;     float ambientIntensity;
    vec3 ambientColor;  uint  lightCount;
};
```

The shipped shader's `viewInv` / `projInv` push constants were dropped
because world position is read directly from the G-Buffer rather than
reconstructed from depth. The shader uses GGX + Schlick for specular and
PCF for cascaded shadows.

### `VulkanPostProcessing` — `engine/renderer/gpu/vulkan/vulkan_post_processing.h`

Owns a chain of three pipelines, each with its own render pass and
descriptor set, executed in order from `render()`:

1. **Bloom** — bright-extract (`luminance > threshold`) + 9-tap radial
   Gaussian, written to `bloom_mips_[0]` at half source resolution.
2. **TAA** — `mix(history, current, blendFactor)` written to
   `taa_output_image_`. **Placeholder**: no motion vectors, so this is
   a temporal smear, not real TAA. The pass exists so the GPU machinery
   is real and a future motion-vector implementation can swap the shader
   without re-plumbing.
3. **Tone mapping** — ACES + gamma, with optional additive bloom
   composite (sampled with linear filter for implicit upsample). Writes
   to `output_image_`.

### `VulkanRenderGraph` — `engine/renderer/gpu/vulkan/vulkan_render_graph.h`

Fixed-stage orchestrator. Not a generalised frame graph (Frostbite-style
resource aliasing + auto-barrier inference is Phase 5+). What it does:

- Records each stage's `begin_pass → recorder → end_pass` between paired
  `vkCmdWriteTimestamp` calls (TOP_OF_PIPE / BOTTOM_OF_PIPE).
- Inserts a coarse execution-only `vkCmdPipelineBarrier` between
  consecutive stages — most of the actual hazard tracking is done by the
  per-stage render passes' `initialLayout`/`finalLayout` transitions and
  subpass dependencies.
- Resolves timestamp queries on demand via `resolve_timings()` — call
  after waiting on the submission fence. Per-stage queries use
  `WAIT_BIT` so unused-stage slots don't poison the batch.
- Holds a per-frame `CameraData{view, proj, position}` set via
  `set_camera()`. Forwarded to the lighting pass automatically (camera
  position) and consumed by the default geometry recorder (mvp).
- Holds a per-frame `std::vector<DrawItem>` set via `set_draw_items()`.
  The default geometry and shadow recorders both iterate this list; an
  empty list falls back to `draw_demo_triangle` for the legacy smoke
  path. `DrawItem = { const Mesh*, const Material*, mat4 model, uint32 submesh_index }`.

## Data flow per frame

1. **CPU side** before submit:
   - `VulkanLightingPass::add_*_light(...)` populates `lights_`.
   - `VulkanPostProcessing::set_input_image(lighting->get_output_view())`
     wires the post-process input to the lighting HDR output.
   - `VulkanRenderGraph::set_camera({view, proj, position})` stores the
     per-frame camera state and forwards `position` into the lighting pass.
2. **`begin_frame(cmd)`** — resets timestamp pool and frame stats.
3. **Shadow** — `VulkanShadowMap::begin_directional_pass` opens depth-only
   render pass; recorder draws shadow casters; `end_pass` closes.
4. **Geometry** — `VulkanGBuffer::begin_geometry_pass` opens the MRT
   render pass; the default recorder calls `draw_demo_triangle(view, proj)`
   when no caller-supplied recorder is provided; `end_geometry_pass`
   transitions all 4 color attachments to `SHADER_READ_ONLY_OPTIMAL`.
5. **Lighting** — `VulkanLightingPass::begin_pass` opens the HDR render
   pass and `upload_lights()` mmap-copies the CPU light list to the
   light SSBO; default recorder `render()` binds pipeline +
   descriptors, pushes constants (cameraPos + ambient + lightCount),
   draws the fullscreen triangle into `output_image_`.
6. **PostProcess** — `VulkanPostProcessing::render()` runs bloom → TAA →
   tone mapping. Tone mapping's `output_image_` is the visible "final"
   output.
7. **`end_frame(cmd)`** — closes the frame.
8. **(Caller)** — for windowed presentation: blit
   `post->get_output_vk_image()` → swapchain image (with
   `SHADER_READ_ONLY → TRANSFER_SRC` and `UNDEFINED → TRANSFER_DST →
   PRESENT_SRC` layout transitions), submit with acquire/render
   semaphores, then `swapchain->present_image(idx, render_sem)`.
9. **(Caller, optional)** — wait on submission fence, then
   `graph->resolve_timings()` to populate per-stage µs in `RenderGraphStats`.

## Phase-4 known limitations and follow-ups <a id="status"></a>

Implemented (real GPU work, validation clean):
- G-Buffer (geometry pass with built-in demo-triangle pipeline)
- Lighting (PBR shader, GGX specular, PCF shadows, push-constant camera)
- Shadow map (render pass + framebuffer)
- Bloom (single bright-pass + 9-tap blur, tone-mapping composite)
- Tone mapping (ACES + gamma)
- TAA (placeholder — temporal smear, no motion vectors)
- Render graph (4 stages + barriers + timestamp profiling + camera state)
- `VulkanDevice::attach_surface` + `create_window_swapchain`
- End-to-end frame: `engine_window_test.exe` blits the post-processing
  output onto a swapchain image and presents.

Closed by Phase 5 Week 1 (2026-04-27):
- ✅ **Caller-supplied draw lists** — `DrawItem` + `RenderGraph::set_draw_items` + `VulkanGBuffer::draw_items` + `VulkanShadowMap::draw_items`.
- ✅ **Material / glTF integration** — new `gws::renderer::gpu::{Texture, Mesh, Material, GltfLoader}` in the deferred namespace, with stb_image / extended mini-tinygltf backing.
- ✅ **Shadow caster submission** — depth-only pipeline on the shadow stage, iterating the same draw list.

Still not implemented (carried into Phase 5+ remaining weeks / Phase 7):
- **Resource aliasing** in the render graph (deferred from Week 3).
- **Real TAA** with motion-vector reprojection — needs a velocity
  G-Buffer attachment and a jittered camera matrix sequence.
- **Multi-mip Kawase / dual-filter bloom** — current bloom is a single
  half-res bright-pass + 9-tap blur, not the textbook quality bloom.
- **Clustered light culling** — all lights are evaluated per pixel.
- **OpenGL baseline benchmark** — the legacy OpenGL backend is
  source-gated but not actively built or measured.
- **Inline-shader vs disk-shader deduplication** — the inline shader
  strings in `vulkan_lighting_pass.cpp` / `vulkan_post_processing.cpp` /
  `vulkan_g_buffer.cpp` are copies of files in
  `engine/renderer/gpu/vulkan/shaders/` and can drift.
- **Hierarchical glTF node transforms** — `GltfLoader` currently uses each
  node's own TRS without recursing through parents; for flat glTFs (most
  Blender exports) this is fine, for complex hierarchies it's incomplete.
- **Per-cascade orthographic light projection** for shadow stage — caster
  pipeline currently uses the camera's view/proj.

## Validation status

`tests/deferred_smoke_test.cpp` records all four stages and submits to a
real graphics queue with `VK_LAYER_KHRONOS_validation` enabled. Latest
representative run (320×240, lighting → post wiring through the demo
triangle, all post-fx enabled):

```
[OK] Frame submitted: shadow=1 geometry=1 lighting=1 post=1
[OK] GPU timings (us): shadow=6.2  geometry=9.2  lighting=11.3  post=18.4
=== SMOKE TEST PASSED ===
```

`tests/engine_window_test.cpp` runs the full pipeline end-to-end at
640×480, blits the post-processing output onto an acquired swapchain
image, and presents to a GLFW window. Sample run:

```
[OK] Swapchain created: 640x480 (3 images)
[OK] Acquired swapchain image 0
[OK] Frame submitted + presented
[OK] GPU timings (us): shadow=6.4  geometry=15.4  lighting=19.5  post=41.0
=== ENGINE WINDOW TEST PASSED ===
```

`gws_tests [deferred]` Catch2 suite (configuration / wiring tests, no GPU
work): 42 assertions across 9 cases, all passing.

`tests/deferred_scene_test.cpp` (Phase 5 Week 1) exercises the new
draw-list path: builds a per-material descriptor pool, programmatic
textured cube, single `DrawItem`, runs through the full deferred stack
with both shadow and geometry stages iterating the draw list. Sample run:

```
[OK] Frame submitted: shadow=1 geometry=1 lighting=1 post=1
[OK] GPU timings (us): shadow=10.7  geometry=8.2  lighting=14.3  post=22.5
=== DEFERRED SCENE TEST PASSED ===
```

If `GLTF_TEST_FILE` is set in the environment, the test additionally
exercises `GltfLoader::load(...)` against that file and appends its draws.
