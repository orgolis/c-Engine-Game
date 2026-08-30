# Performance audit — why simple scenes are slow on low-end hardware

**Audited:** 2026-08-29, against `main` at v0.8.1
**Method:** source reading, not measurement. No timings were captured on a slow machine — the profiler that
would produce them is itself finding **F2**.
**Companion research:** the general technique survey (graphics / physics / memory) is an artifact,
*The Frame Budget Handbook*; this file is the engine-specific half.

---

## The mechanism, in one paragraph

Two findings multiply. The renderer draws every frame at a **hardcoded 1920×1080** regardless of window or
viewport size (**F1**), and each of those pixels costs **28 bytes of G-buffer** written and read back
(**F5**), on top of SSAO, SSR, clouds and volumetric light that all default on (**F6**) and are each a
full-screen pass. None of that cost depends on what is in the scene, which is exactly why an empty room is
slow. And it has been hard to locate because the GPU profiler measures five render-graph stages and **none of
the expensive passes** (**F2**), so the overlay reports a comfortable total while the GPU is saturated.

The single distinction that organises everything below: **cost that scales with the scene** (draw calls,
triangles, physics bodies) versus **cost that scales with pixels** (every full-screen pass). This engine's
problem is entirely the second kind. LOD, culling, batching and instancing all attack the first kind, and all
of them already exist and work — which is why they have not helped.

---

## Findings

Ranked by expected win per unit of effort. Every claim carries the evidence that produced it.

### F1 — Render targets are pinned at 1920×1080 and never resized 🔴

`editor/src/main.cpp:5935` declares `constexpr uint32_t kW = 1920, kH = 1080;`. Those values are assigned to
the G-buffer config at `main.cpp:6079-6080` and are the **only** assignment to `g_cfg.width/height` in the
file. `VulkanRenderGraph::resize()` exists at `vulkan_render_graph.cpp:590` and correctly resizes the
G-buffer, lighting and post-processing — and **is never called from anywhere in the codebase**. It is dead
code.

The comment at `main.cpp:7466-7468` states:

> *"The 3D scene's offscreen targets are sized to the Viewport panel (not the window) so they don't need
> recreation here — ImGui::Image() rescales them."*

**This is false.** The targets are a fixed 1920×1080; `ImGui::Image()` rescales that result down into
whatever the docked Viewport panel happens to be.

**Consequence.** On a 1366×768 laptop the engine renders **1.98× more pixels than the entire display has**,
and the viewport panel showing the result is smaller again — a docked viewport around 950×600 means roughly
**3.6× the pixel work is done and thrown away**. Every resolution-dependent cost in the frame is multiplied by
that factor. This is the largest single waste found.

> **Correction (2026-08-30), from trying to fix it.** This entry originally said "the fix already has a
> written implementation waiting to be called." That was too optimistic and is worth recording rather than
> quietly editing away. `VulkanRenderGraph::resize()` covers only the **G-buffer, lighting and
> post-processing**. SSR, transparent, volumetric light, froxel fog, water, HZB and DDGI own full-screen
> targets and have **no resize method at all** — and resizing only some would leave those passes sampling a
> G-buffer of a different size, which is corruption rather than a slow frame. Live viewport-tracking resize
> therefore needs resize paths on seven more passes and is its own piece of work. What ships first is the
> half that is safe: **sizing the targets from the real framebuffer times a render scale**, which is where
> most of the win is for a small display anyway.

### F2 — The GPU profiler cannot see the expensive passes 🔴

The engine has a per-pass GPU timestamp overlay, sorted by cost, in the Performance panel under **GPU (N2)**
(`main.cpp:5519-5540`) — exactly the right tool. It records five stages: `Shadow, Geometry, Lighting,
Transparent, PostProcess` (`vulkan_render_graph.cpp:65-75`). The only two `vkCmdWriteTimestamp` calls in the
entire renderer are at `vulkan_render_graph.cpp:375` and `:407`, bracketing those stages.

SSAO, SSR, DDGI, clouds, volumetric light, froxel fog and water are dispatched from `main.cpp:9903-9989`,
**between** graph stages. The source says why:

> *"Outside the render graph because it's a compute pass and the graph currently only models render-pass
> stages."*

`grep -c vkCmdWriteTimestamp` returns **0** for every one of `vulkan_ssao_pass`, `vulkan_ssr_pass`,
`vulkan_cloud_pass`, `vulkan_ddgi_pass`, `vulkan_froxel_fog_pass` and `vulkan_post_processing`.

Worse, `GPUProfiler::submit_frame` computes `total_gpu_time_ms_` as the **sum of the stages it was handed**
(`gpu_profiler.cpp`), so both the reported total and the derived "GPU load %" systematically exclude the
expensive work.

**This is the `ui_viewport` incident one layer up** — a diagnostic that reports confidently and omits the
costly path. It is why this problem has resisted diagnosis, and it must be fixed first: not because it is the
biggest win, but because without it no other fix can be shown to have worked.

### F3 — No quality preset system exists 🔴

No `QualityPreset`, `GraphicsPreset` or scalability type anywhere in `editor/` or `engine/`. Individual
toggles exist and work, scattered through the Post-Processing panel. A user on weak hardware must find each
one and already know which are expensive. There is no Low.

### F4 — The master off-switch is an environment variable 🔴

`main.cpp:915-921` defines `GWS_NO_FX` plus `GWS_NO_DDGI`, `GWS_NO_SSR`, `GWS_NO_WATER`, `GWS_NO_CLOUDS`,
`GWS_NO_VOLUMETRIC` and `GWS_NO_FROXEL`. `GWS_NO_FX=1` disables every expensive effect at once — the single
most effective performance control in the codebase.

It is unreachable for anyone launching through the Hub, which is how this engine is actually used. This is a
recorded prior failure repeating verbatim: the same shape as gating ray-traced reflections behind
`GWS_SSR_RT=1` and asking the user to test it.

### F5 — The G-buffer is 28 bytes per pixel, 8 of them redundant 🟠

`vulkan_g_buffer.cpp:250-280`:

| Attachment | Format | Bytes/px |
|---|---|---|
| Position | `R16G16B16A16_SFLOAT` | **8** |
| Normal | `R16G16B16A16_SFLOAT` | 8 |
| Albedo | `R8G8B8A8_UNORM` | 4 |
| Material | `R8G8B8A8_UNORM` | 4 |
| Depth | `D32_SFLOAT` | 4 |

The **world-position** target is reconstructible from the depth buffer bound beside it —
`tools/lighting_pass.frag` binds `positionTex` at binding 0 *and* `depthTex` at binding 4. Dropping it saves
8 B/px, about **29% of G-buffer bandwidth**.

At 1080p that is 58 MB written and 58 MB read back per frame for the G-buffer alone; SSAO, SSR and volumetric
light each re-read position+normal+depth (20 B/px ≈ 41 MB) on top. A conservative tally is
**250–350 MB/frame, or 15–21 GB/s at 60 fps** — against roughly 30–50 GB/s shared with the CPU on integrated
graphics.

**Cost of the fix:** 14 shaders sample a position G-buffer (`ddgi_composite`, `froxel_composite`,
`lighting_pass`, `volumetric_composite`, `water`, `gtao`, `hbao`, `hdao`, `ssao`, `ssao_rt`, `ssr`, `ssr_rt`,
`volumetric_light`, `vxao`). Real work — schedule after F1–F3.

### F6 — Expensive effects default to on 🟠

Read from each pass header; only `froxel_fog` is disabled at startup (`main.cpp:6338`, commented
*"opt-in (heavy)"*).

| Pass | Default | GPU-timed |
|---|---|---|
| SSAO / GTAO | **on** | no |
| Cloud shadow + raymarched clouds | **on** | no |
| SSR | **on** | no |
| Volumetric light | **on** | no |
| Froxel fog | off (forced at startup) | no |
| DDGI | off, and needs hardware RT | no |

Raymarched clouds default-on is the most surprising: it is typically among the most expensive passes in any
engine that has one. It does render at an internal `resolution_scale = 0.5`.

### F7 — No render-scale or dynamic resolution control 🟠

The only resolution scaling anywhere is the cloud pass's private `resolution_scale`
(`vulkan_cloud_pass.h:50`). There is no global render scale, no dynamic resolution, and — per F1 — not even a
"match the window" path. Because every resolution-dependent cost scales together, this is the one control
that could hold a frame rate under any load.

### F8 — The 3D pipeline runs even when the Viewport panel is hidden 🟡

`main.cpp:4439` returns early from *drawing* the panel when `show_viewport` is false, but the render graph and
every effect pass still execute unconditionally (`main.cpp:~9890-10017`). Closing the viewport should cost
nothing and currently costs a full frame. Minor next to F1, and nearly free to fix.

### F9 — No suballocator: one device allocation per texture and per mesh 🟠

No VMA and no custom suballocator. **62 raw `vkAllocateMemory` call sites**, including 4 in
`vulkan_texture.cpp` and 3 in `vulkan_scene_mesh.cpp` — the per-asset paths.
`maxMemoryAllocationCount` is never queried anywhere in the engine.

This is not a bug today; it is an **asset-count ceiling the engine does not know it has**. Drivers commonly
cap allocations at 4096, and a project with a few thousand textures and meshes will meet it — as an
allocation failure on a user's machine, not during development. Each allocation also carries alignment
padding, so real footprint consistently exceeds the sum of the resources.

### F10 — Render targets are never aliased, and all are sized for 1920×1080 🟠

No `LAZILY_ALLOCATED`, no `TRANSIENT_ATTACHMENT_BIT`, no memory aliasing anywhere. Roughly **59
render-target images** are created across the passes (10 in the G-buffer, 12 in post-processing, 8 in clouds,
10 in the environment map, …), each holding a permanent allocation for the whole run, and most live for a
single pass.

This is **F1 seen as a memory cost rather than a bandwidth one** — every target is sized for the hardcoded
1920×1080 regardless of the window. On integrated graphics that footprint is shared system RAM taken from the
CPU. A dozen full-screen `RGBA16F` targets at 1080p is ~200&nbsp;MB summed, and perhaps ~40&nbsp;MB if aliased
down to the peak concurrent set. The render graph already knows each resource's first and last use, which is
exactly what aliasing needs: **the structure to exploit this exists and is unused.**

---

## Checked, and NOT the problem

Recorded so they are not re-investigated. Several are actively good.

| Area | State |
|---|---|
| **Barrier hygiene** | Good — 3 uses of `ALL_COMMANDS` across 51 `vkCmdPipelineBarrier` calls, two of them in a helper's fallback branch |
| **G-buffer load/store ops** | Correct — `CLEAR` then `STORE` on targets that are all written and all later sampled |
| **Disabled passes** | Early-out cleanly (`if (!enabled_ ...) return;`); no wasted work |
| **Shadow maps** | Cheap. `cascade_count` is forced to **1** at `main.cpp:6107` and only cascade 0 renders, at 2048² |
| **LOD** | Exists and is used in the real draw path (`vulkan_g_buffer.cpp:1325` → `Mesh::select_lod`) |
| **Instancing / indirect** | Present, including the `multiDrawIndirect` capability check |
| **Mipmaps** | Generated via `vkCmdBlitImage` chain |
| **Block compression** | BC1/BC3/BC5/BC7 supported by the texture loader |
| **Pipeline cache** | Created at device init and persisted to disk (`vulkan_device.cpp:91,119`) |
| **Swapchain** | `minImageCount + 1`, `frames_in_flight = 2`, MAILBOX preferred with FIFO fallback — all sane |
| **Frame allocator (CPU)** | Exists (`engine/core/memory/frame_allocator.h`); no significant per-frame heap allocation in the render path. **GPU** allocation is a separate story — see F9/F10 |
| **Physics** | Not implicated in a simple scene. The mesh-collider memoisation from the v0.7.x fix still stands |

---

## Not yet examined

The audit was scoped to the frame's resolution-dependent cost. These remain open and could each hide
something:

- **Actual draw-call counts** in a real project scene — submission cost was never measured
- **Shader occupancy and register pressure** — needs a vendor offline compiler; the lighting shader is large
- **Whether project textures are actually cooked to BC**, as opposed to the loader merely supporting it.
  Note the `New ▸ Texture` generator writes uncompressed 24-bit BMP
- **Terrain's 14-binding pipeline** cost specifically
- **ImGui / editor UI GPU cost** — the editor draws a lot of panels every frame
- **Vertex format width** — whether meshes carry fat `float32` attributes
- **Overdraw and transparency** in practice, including particles
- **CPU-side frame breakdown** — this audit was almost entirely GPU-side

---

## Recommended order

1. **Make the profiler tell the truth (F2).** Timestamps around every pass, not just the five graph stages,
   and a total that includes them. Cheapest item here; everything after it is otherwise a guess.
2. **Size the render targets to the viewport (F1).** `VulkanRenderGraph::resize()` already does the work and
   is never called — wire it to the panel size and delete the false comment.
3. **A quality preset with a real Low, in the UI (F3, F4, F6).** Expose what `GWS_NO_FX` already does, and
   pick sane defaults; a preset that turns whole passes *off* beats one that lowers their sample counts,
   because a pass that does not run costs nothing.
4. **A render-scale slider, then dynamic scaling (F7).**
5. **Drop the position attachment (F5).** ~29% of G-buffer bandwidth; 14 shaders to touch.
6. **Skip the 3D pipeline when the viewport is hidden (F8).**
7. **Auto-detect the preset** from `VkPhysicalDeviceType` so an integrated GPU gets a usable first launch
   without finding the settings menu.
8. **Alias render targets and add a suballocator (F9, F10).** Footprint work rather than frame-time work; the
   right time is when a real project starts to strain memory, or when F1 is fixed and the targets need to be
   recreated on resize anyway — the two changes touch the same code.

Items 1–4 are days rather than weeks. **1 must come first** — not because it is the biggest win, but because
without it none of the others can be shown to have worked, and this project has twice been burned by a
measurement that was confidently wrong.

---

## Work plan

Three releases. Each has a stated **done** criterion that can be checked, because the recurring failure in
this project is a fix that was believed rather than verified.

### v0.9.0 — "see it, then afford it"

The whole release is diagnosis plus control. Nothing here changes how the engine looks at default quality;
it changes what a person can measure and what they can turn off.

| # | Item | Done when |
|---|---|---|
| 1 | **F2 — profiler covers every pass** | Every pass dispatched in a frame appears in the GPU (N2) overlay with its own timestamped duration, and the reported total equals the sum of what actually ran. A pass that did not run reads 0, not a stale value. Verified by a headless check on the accounting, and by reading the overlay in the editor. |
| 2 | **F1 — targets sized to the viewport** | Resizing the Viewport panel changes the render resolution; `VulkanRenderGraph::resize()` is called and no longer dead; the false comment is gone. Verified by the overlay's pass times falling when the panel shrinks. |
| 3 | **F8 — skip the pipeline when the viewport is hidden** | Closing the Viewport panel drops GPU time to approximately the ImGui cost. |
| 4 | **F7 — render-scale control** | A slider in the UI scales render resolution independently of panel size; 0.5 quarters the pixel work, visible in the overlay. |
| 5 | **F3 / F4 / F6 — quality presets** | A preset control with a real **Low** that disables SSAO, SSR, clouds and volumetric light; defaults chosen per device type; every `GWS_NO_*` env var has a UI equivalent. Reachable from the Hub-launched editor with no environment variables. |

**Release gate:** on the low-end machine, the overlay accounts for the whole frame, and Low + 0.5 render scale
produces a measured, reported improvement. Not "feels faster".

### v0.9.1 — bandwidth

| # | Item | Done when |
|---|---|---|
| 6 | **F5 — drop the G-buffer position attachment** | Position is reconstructed from depth in all 14 consumers; the G-buffer is 20 B/px; output is visually unchanged (compared against captured reference images), and the overlay shows the Geometry and Lighting passes cheaper. |

### v0.9.2 — footprint

| # | Item | Done when |
|---|---|---|
| 7 | **F9 — suballocator** | Texture and mesh uploads draw from pooled blocks; `maxMemoryAllocationCount` is queried and the headroom logged. |
| 8 | **F10 — alias transient render targets** | Peak render-target footprint measurably below the naive sum; reported in the Memory (N3) panel. |

### Deliberately not in the plan yet

The unexamined list stands: draw-call counts, shader occupancy, whether project textures are actually cooked
to BC, terrain's pipeline, editor UI cost, vertex formats, overdraw, and the CPU side. **Item 1 exists partly
to answer these** — once the overlay accounts for the whole frame, the next thing to optimise stops being a
matter of opinion.
