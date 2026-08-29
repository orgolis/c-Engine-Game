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
that factor. This is the largest single waste found, and the fix is the one that already has a written
implementation waiting to be called.

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
| **Frame allocator** | Exists (`engine/core/memory/frame_allocator.h`); no significant per-frame allocation in the render path |
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

Items 1–4 are days rather than weeks. **1 must come first** — not because it is the biggest win, but because
without it none of the others can be shown to have worked, and this project has twice been burned by a
measurement that was confidently wrong.
