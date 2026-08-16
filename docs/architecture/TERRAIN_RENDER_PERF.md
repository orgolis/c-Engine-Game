# Terrain rendering performance — what is actually slow, and what to do about it

> ## ⚠️ Correction (2026-08-16) — read this before the rest
>
> The headline claim below ("the dominant cost is PCIe vertex traffic") was **reasoned, not measured**,
> and measurement does **not** support it. P1 was implemented anyway — it is standard practice and it did
> help — but it is not the fix for the reported lag, and the numbers say so:
>
> | measurement (RTX 3060, `--frames`, `--stress-draw-all`) | result |
> |---|---|
> | GPU time, geometry pass, resolution 256 / 512 | **0.26 ms / 0.28 ms** |
> | GPU time, ALL stages summed | **under 1 ms** |
> | Frame time, scene with a resolution-1024 terrain | 12.09 ms |
> | Frame time, **same scene with the terrain removed entirely** | **12.88 ms** |
>
> Removing the terrain does not make the frame faster. In this harness the terrain is **not** the
> bottleneck, the frame is **not GPU-bound**, and the reported lag is **not reproduced**. The ~11 ms frame
> has under 1 ms of GPU and ~0.9 ms of tracked CPU zones; the remainder is unaccounted for.
>
> This is exactly the trap recorded as "measure before planning against a famous problem": a plausible
> bandwidth number (8.8 GB/s) was computed and treated as a diagnosis. The arithmetic below is still
> correct about *how much data moves*; what was never shown is that moving it was costing frame time.
>
> **Two instrument bugs found while checking, both fixed or logged:**
> - Per-stage GPU timings were resolved every frame and printed nowhere. Now logged under `--log-debug`.
> - At resolution 1024 those timings resolve as **0.00 ms for every stage** — a readback failure, not a
>   real zero. This blinds precisely the case under investigation and should be fixed before any further
>   performance work here.
>
> **What is still open:** reproducing the lag as the user sees it — interactively, in the viewport. See
> the end of this document.

**Question asked:** high resolution on large terrain generates a lot of lag, especially *graphics* lag.
What can be done **besides world streaming**? And is back-face / occlusion culling working at all?

**Short answer:** the dominant cost is not triangle count and not culling. It is that **every mesh vertex
and index buffer in this engine lives in system RAM**, so the GPU re-fetches the terrain across PCIe on
every pass of every frame. Terrain has **no LOD tiers at all**, which multiplies that traffic. Back-face
culling exists but is disabled on exactly the content in use. Occlusion culling *is* running, and is close
to useless for terrain by construction.

Measured on an RTX 3060, MinGW Debug build, v0.6.9.

---

## 1. The measurements

### Terrain geometry, by resolution

| resolution | chunks | vertices | vertex bytes | index bytes | triangles |
|---:|---:|---:|---:|---:|---:|
| 256  | 16  | 67 600 | 3.1 MB | 1.5 MB | 0.13 M |
| 512  | 64  | 270 400 | 12.4 MB | 6.0 MB | 0.52 M |
| 1024 | 256 | 1 081 600 | **49.5 MB** | **24.0 MB** | **2.10 M** |

`SceneVertex` is 48 bytes (position 12 + normal 12 + uv 8 + tangent 16).

### Where that memory lives — the core finding

`Mesh::create` allocates through `create_host_buffer`
([vulkan_scene_mesh.cpp](../../engine/renderer/gpu/vulkan/vulkan_scene_mesh.cpp)), which asks for
`HOST_VISIBLE | HOST_COHERENT` and takes the **first** matching memory type.

`vulkaninfo` on this machine:

```
memoryHeaps[0]  11.83 GiB  DEVICE_LOCAL          <- VRAM
memoryHeaps[1]  31.89 GiB  (no flags)            <- system RAM
memoryHeaps[2]   214 MiB   DEVICE_LOCAL          <- the small host-visible VRAM window

memoryTypes[3]  heapIndex = 1  HOST_VISIBLE | HOST_COHERENT   <- first match, chosen
```

So the chosen type is on **heap 1 — system RAM**. Every vertex and index buffer in the engine is there.
The file's own header already says so: *"for production-scale meshes a staging upload is the right move
(TODO)"*.

**Consequence at resolution 1024:** 73.5 MB of terrain geometry is pulled over PCIe by the G-Buffer pass,
then **again** by the shadow pass — roughly **147 MB per frame**. At 60 fps that is ~8.8 GB/s of vertex
traffic alone, which saturates a PCIe 3.0 x16 link before a single texture is touched. This is the
"graphics lag", and it scales exactly the way the report describes: with terrain size and resolution.

### Terrain has no LOD

`build_terrain_chunk_mesh` emits **one** LOD tier per chunk:

```cpp
sm.lods.push_back({0, static_cast<uint32_t>(idx.size()), 0.0f});
```

`Mesh::select_lod(submesh, distance)` and the shadow pass's `base_lod + 1` both work correctly — they
just have nothing to select from. **A chunk 4 km away draws all 8192 of its triangles.**

> The feature catalog claims "chunked LOD" for terrain. That is chunking, not LOD. The row is wrong and
> should be corrected.

---

## 2. Culling: what is actually on

| path | back-face culling | why |
|---|---|---|
| Primitives, glTF | **ON** (`VK_CULL_MODE_BACK_BIT`) | trusted CCW winding |
| OBJ, cooked `.pak` | **OFF** | `set_double_sided(true)` — OBJ winding is not standardised |
| **Terrain (G-Buffer)** | **OFF** | `VK_CULL_MODE_NONE`, *"terrain can be grazed from below"* |
| **Shadow pass (everything)** | **OFF** | `VK_CULL_MODE_NONE` for all casters |

So the impression that "the engine doesn't really have back-face culling" is **substantially correct for
the content in use**: terrain and imported OBJ models are both drawn two-sided, and *all* shadow casting
is two-sided. Only primitives and glTF get it.

**Occlusion culling is present and running** — the impression that it is missing is wrong, but the
impression that it does nothing for terrain is right:

- `VulkanHzbCuller` is wired and enabled (Stage 3.C), and its zone shows in the profiler (`hzb_cull ~0.24 ms`).
- It tests **one AABB per draw**, against a **CPU readback of the depth pyramid at mip 4**, **one frame late**.
- A terrain chunk's AABB is large, flat and spans much of the screen. It is almost never *fully* behind
  another surface, so it is almost never culled. Terrain is a good **occluder** and a terrible **occludee**,
  and this culler only does occludees.
- Per-draw occlusion *queries* (`VulkanOcclusionCuller`) are deliberately left unwired — the per-query
  design hangs the editor. That decision is correct; HZB is the better shape.

**Frustum culling works** and is the one doing real work today (observed 55 → 25 draws).

---

## 3. What to do, in priority order

### P1 — Device-local vertex/index buffers with a staging upload  ★ biggest win

Move mesh geometry from system RAM to VRAM. This is the fix for the actual bottleneck, and it helps
**every mesh in the engine**, not just terrain.

- Allocate `DEVICE_LOCAL` and upload through a staging buffer + one-time command buffer.
- Sub-allocate rather than one `vkAllocateMemory` per buffer. Today a resolution-1024 rebuild makes
  **512 raw device allocations**; drivers commonly cap `maxMemoryAllocationCount` at 4096, so a 2048
  terrain (2048 allocations) plus the rest of a scene is genuinely close to the wall.
- **VMA is already vendored and configured** ("Vulkan Memory Allocator (VMA) configured as header-only"),
  so the sub-allocator does not need writing.
- Watch out: the RT path takes buffer device addresses from these buffers, so keep
  `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` and the `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT` flag.

*Expected: the dominant per-frame cost disappears. Also removes most of the terrain rebuild cost measured
in `36f3c26` (~5.7 s at resolution 1024).*

### P2 — Real terrain LOD  ★ large win, and it compounds with P1

The grid structure makes this unusually easy — no `meshopt_simplify` needed. Build each chunk at
power-of-two vertex strides (every 2nd, 4th, 8th sample) and push them as additional `MeshLod` tiers.
The renderer already selects tiers by distance and already asks the shadow pass for one tier coarser.

- Cracks between adjacent chunks at different tiers are the only real work. The two standard fixes are
  **skirts** (a cheap vertical apron around each chunk — a few extra triangles, zero stitching logic) or
  index-level stitching of the coarse edge. Skirts are the pragmatic choice here.
- Holes (`HasHole`) must be honoured at every tier, or caves reappear at distance.

*Expected: distant chunks drop 4×/16×/64× in vertex and triangle count. Combined with P1, a
resolution-1024 terrain stops being a bandwidth problem at all.*

### P3 — Turn back-face culling on where it is currently off

1. **Shadow pass** — switch from `VK_CULL_MODE_NONE` to **front-face culling**. This is the standard
   shadow-map configuration: it halves shadow rasterisation *and* reduces acne/peter-panning. Almost free.
2. **Terrain G-Buffer** — a heightfield cannot have overhangs, so from any camera above the surface every
   visible triangle is front-facing. The `cull-none` justification ("can be grazed from below") only
   applies to terrains with **holes** (caves). Gate it: cull back faces unless `AnyHoles()`.
3. **OBJ** — the editor now imports OBJ through the asset-pipeline importer, which knows whether the file
   supplied `vn` normals. Flag double-sided only when winding is genuinely ambiguous instead of always.

*Expected: modest but broad. The shadow-pass change is the clear one.*

### P4 — Fix the indirect-draw upload (CPU, but severe at high chunk counts)

`VulkanIndirectDrawBuffer::upload()` memcpy's the **entire accumulated command list**, and
`draw_items` calls it **once per submesh**. With N chunks each contributing M meshlets the per-frame copy
is `M·N(N+1)/2` commands — **quadratic in chunk count**. At 256 chunks that is tens of MB of pointless
memcpy every frame.

Fix: track how many commands are already uploaded and copy only the new tail (resetting on `grow()`,
which reallocates the buffer). Small, contained change.

### P5 — Make occlusion culling matter for terrain (only after P1–P4)

Today's per-draw, one-frame-late, CPU-readback HZB will never help terrain. If occlusion culling is wanted
for real, the shape is **GPU-driven**: test per-meshlet bounds against the depth pyramid in a compute pass
and write `instanceCount = 0` into the indirect commands. The buffer is already created with
`STORAGE` usage precisely so a compute shader can edit draw arguments — the intent is in the code.

This is the largest piece of work here and the smallest win for terrain specifically. Do it for dense
interior scenes, not for open ground.

### Deliberately not recommended

- **World streaming** — excluded by the question, and correctly so: it addresses *world size*, not the
  per-frame cost of the terrain that is already resident.
- **Per-draw occlusion queries** — known to hang the editor; HZB/GPU-driven is the right direction.
- **Smaller chunks** — more draws, more indirect commands, worse P4. Chunk size is not the problem.

---

## 4. Suggested order

1. **P1** (device-local + VMA sub-allocation) — fixes the measured bottleneck, engine-wide.
2. **P2** (terrain LOD tiers + skirts) — the terrain-specific multiplier.
3. **P3** (shadow front-face culling, terrain back-face when hole-free) — cheap, immediate.
4. **P4** (indirect upload tail copy) — cheap, removes a quadratic.
5. **P5** (GPU-driven occlusion) — only if dense scenes demand it.

P1 and P2 together are expected to remove the reported symptom. P3 and P4 are small enough to fold in
alongside. P5 is a separate project.

---

## 5. Status after P1, and what the measurements changed

**P1 is done** (`d7c153c`): geometry is now DEVICE_LOCAL, uploaded through a batched staging copy
(`MeshUploadBatch` — one staging buffer and one submit for a whole terrain rebuild, instead of one queue
stall per chunk).

Measured, `--stress-draw-all` (frustum culling off, so every chunk draws — reproducible worst case):

| resolution | before | after | change |
|---:|---:|---:|---:|
| 256  | 9.36 ms | 9.71 ms | ~noise |
| 512  | 13.31 ms | 11.03 ms | **−17%** |
| 1024 | 11.95 ms | 12.23 ms | ~noise |

Full-terrain rebuilds are **28–34% faster**. Frame time is otherwise unchanged, which is consistent with
the control above: terrain was never the frame-time cost in this harness.

**So P1 is kept on its merits** — device-local geometry is the correct architecture, it removes a real
PCIe transfer, and it measurably speeds up rebuilds — but **it is not the answer to the reported lag.**

### What this invalidates in the plan above

- **P1's stated justification was wrong.** It is still worth having; the reasoning was not.
- **P2 (terrain LOD) is unaffected and still worth doing** — terrain genuinely has no LOD tiers, which is
  a real defect regardless of what it costs today. But it should now be justified by a measurement, not by
  the bandwidth argument.
- **P3 and P4 are unaffected** — both are correctness/efficiency fixes that stand on their own. P4's
  quadratic (`upload()` copying the whole command list once per submesh) is CPU cost that grows with chunk
  count, and CPU is where the unaccounted frame time is, so **P4 is now a better candidate than P1 was**.

### The real next step: reproduce the lag

The headless harness cannot see the problem: an empty-of-terrain scene costs the same as a
resolution-1024 one. Before more work, the reported case needs to be reproduced with the instruments now
available (`--log-debug` for per-frame CPU zones and per-stage GPU time, `--stress-draw-all` for a
reproducible worst case). Specifically worth pinning down:

1. **What is the other ~11 ms?** Under 1 ms of GPU and ~0.9 ms of tracked CPU zones leaves most of the
   frame unexplained. The geometry *recording* path — where the per-meshlet cull loop and the quadratic
   indirect upload live — is not inside a profiler zone. It should be.
2. **Fix the GPU-timestamp readback at high draw counts.** Reporting 0.00 ms for every stage at
   resolution 1024 makes the instrument useless exactly where it is needed.
3. **Does the lag depend on what the user is doing?** Idle, camera movement, and sculpting are three very
   different code paths; the slider fix in `36f3c26` already removed one of them.

---

## 6. Where the frame time actually goes — the question that closes this out

Instrumenting the two blocking points at the top of the frame answers the "unaccounted ~9 ms" above, and
it changes what is worth doing next. Resolution 512, all chunks drawn:

```
frame 9.96 ms
  acquire_swapchain_image   3.3 – 5.3 ms
  wait_prev_frame_fence     ~3.0 ms
  hzb_cull                  0.19 ms
  ecs_sync                  0.16 ms
  draw_sort                 0.05 ms
  build_draw_items          0.04 ms
  ── all GPU stages summed  under 1 ms
```

**Six to eight milliseconds of a ten-millisecond frame is the editor waiting for the presentation engine.**
The swapchain prefers `MAILBOX`, so this is display-refresh pacing, not tearing avoidance and not
rendering. GPU work is roughly a tenth of the frame; CPU work about a twentieth.

The renderer is not the bottleneck in this scene. It has a large amount of headroom, and "120 fps" was
the display's number, not the engine's.

## 7. Status: P2 done, P5 deliberately not built

**P2 — terrain LOD: done.** A `MeshLod` is an index range into the mesh's existing buffers, and a terrain
chunk is a regular grid, so a half-density tier is just an index list visiting every second vertex of the
same vertex buffer. Coarser tiers cost index data and nothing else — no decimation, no meshoptimizer, no
extra vertices. Measured on a 64-cell chunk:

| tier | triangles | used beyond |
|---:|---:|---:|
| 0 | 8 704 | 0 m |
| 1 | 2 304 | 100 m |
| 2 | 640 | 200 m |
| 3 | 192 | 400 m |

45× fewer triangles at the coarsest tier. Cracks between neighbouring tiers are covered by skirts — a
vertical apron under each chunk rim, dropped by the chunk's own height range so it cannot be shorter than
the worst disagreement possible. Holes are honoured at every tier (a merged quad is dropped if any cell
under it is carved), so caves never seal over with distance.

Two things had to be fixed for it to do anything: LOD distance was measured to the model's **origin**, and
every chunk of a terrain shares the terrain's transform — so all chunks reported the same distance and
would have picked the same tier forever. Distance now uses the mesh bounding-box centre, in both the
G-Buffer and the shadow pass. Cost: a full rebuild is ~50% slower (more index generation) and a sculpt
rebuild went 11 ms → 15 ms. That is the trade — tiers are built once per edit and paid back on every frame
of every subsequent draw.

**P5 — GPU-driven occlusion culling: investigated, not built.** Two independent reasons, either sufficient:

1. **It is structurally blocked.** A compute pass can only zero `instanceCount` for commands that already
   exist, and it cannot run inside a render pass. Today `draw_items` *appends* a submesh's indirect
   commands and *draws* them in the same loop iteration, inside the render pass. Restructuring to
   record-everything-then-cull-then-draw means one indirect call covering many meshes, and one indirect
   call can only read one bound vertex/index buffer — so it needs **pooled geometry buffers**.
   `vulkan_indirect_draws.h` predicted exactly this: *"Pooling is what a single whole-scene call would
   need, and that is a later, larger change."*

2. **There is nothing to win right now.** GPU work is under 1 ms of a ~10 ms frame that is 6–8 ms
   presentation wait. Halving GPU time would change the frame by nothing measurable. The standing
   directive to move CPU work to the GPU carries its own qualifier — *"if it makes more sense and would
   result in a performance gain"* — and here it would not.

If GPU-driven culling is still wanted, the honest sequence is **pooled vertex/index buffers first**, then
a whole-scene indirect pass, then compute culling on top. That is a project, not a task, and it should be
justified by a scene where GPU time actually dominates — which this one does not.
