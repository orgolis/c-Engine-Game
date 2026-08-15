# Terrain rendering performance — what is actually slow, and what to do about it

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
