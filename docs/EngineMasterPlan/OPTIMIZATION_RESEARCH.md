# Optimization research: where the bytes and the milliseconds are

**Date:** 2026-11-10. **Engine:** v0.8.13 (`298b536`), Windows release build.
**Scope:** an inventory of what the engine stores and moves, measured down to
struct offsets and bytes per pixel, plus what published work says about each
finding. It is research: nothing in the engine was changed. It follows
[PERFORMANCE_AUDIT.md](PERFORMANCE_AUDIT.md), which records the work up to
v0.8.13.

Every number below is one of three kinds, and says which:

- **measured**: taken on this machine, with the tools in
  [tools/perf_research/](../../tools/perf_research/README.md).
- **arithmetic**: follows exactly from measured sizes.
- **estimate**: taken from someone else's published figures. It is a guess
  about this engine until measured here.

## The short version

1. **Memory is where the large, cheap wins are.** The editor holds 507 MiB of
   GPU memory for the MyGame project. 47% of it is the terrain, and the
   terrain's ray-tracing structures alone are 150 MiB. The same structures cost
   the process another 290 MB of private memory. On the CPU side, 192 MiB of
   the editor's 206 MiB heap belongs to a frame allocator that is used for half
   a megabyte.
2. **The CPU frame is bound by memory, not by instructions.** At 20,000
   entities the engine's scene work is 4.4 ms of CPU per frame. Rebuilding the
   whole engine with AVX2 changed none of it. Walking the same data in a dense
   array is 10 to 15 times faster than walking it through entity pointers.
   About 2.0 ms of the 4.4 ms re-examines a scene in which nothing changed. A sampling profiler
   found more of the same that no profiler zone covers: 0.7 ms a frame in edit
   mode and 2.9 ms in play mode, 2.0 ms of it the script system walking 20,000
   entities that have no scripts.
3. **On the GPU, ambient occlusion costs a quarter of a fullscreen frame**,
   about 2.4 times the published cost of the reference implementation of the
   same technique.
4. **An idle editor is not idle.** With nothing moving it uses 41% of a CPU
   core and keeps the GPU about 40% busy.
5. **Two correctness problems turned up.** The shipped binary mixes AVX2 code
   (Jolt) with SSE2 code (everything else), so by its flags it needs an AVX2
   processor the moment physics starts, and about 5% of Steam machines have
   none. And the
   depth buffer resolves 75 mm at 500 m, which every pass that rebuilds
   positions from depth now inherits.

### Ranked candidates

"Effort" is S (hours), M (days) or L (a project). Section numbers point to the
evidence.

| # | Change | Gain | Evidence | Effort |
|---|--------|------|----------|--------|
| 1 | Size the frame allocator to what is used (§3.1) | −190 MB of committed memory | measured | S |
| 2 | One O(1) "nothing changed" check before the per-entity passes, including the terrain prunes and the particle scan, which have no zone (§4.1, §4.6) | −2.0 ms of zoned work and −0.7 ms of unzoned walks at 20k entities, static scene | measured (zones, sampler) | S–M |
| 3 | The script system keeps a list of scripted entities instead of copying and walking the scene every play frame (§4.6) | −2.0 ms per play-mode frame at 20k entities | measured (sampler) | S |
| 4 | Throttle the editor when idle or unfocused (§4.5) | −41% of a core, −30 points of GPU load while idle | measured; estimate for the remedy | S–M |
| 5 | Terrain chunks share one index buffer (§2.2) | −34.6 MiB VRAM | arithmetic | S–M |
| 6 | Compact static BLASes and sub-allocate them (§2.2, §2.4) | −66 to −82 MiB VRAM, 261 fewer device allocations, part of the 290 MB process cost | estimate (NVIDIA: 44–55%) | M |
| 7 | Decide the instruction-set policy: Jolt down to the engine's baseline, or a start-up check (§7.1) | no crash on Play for ~5% of machines | measured (flags), survey | S |
| 8 | Pass the device's pipeline cache to every pipeline (§6.2) | about −2.2 s on a start with a cold driver cache | measured | S |
| 9 | Halve the cost of ambient occlusion (§5.2) | about −0.8 ms of 5.1 ms at 1920×1032 | measured cost; estimate for the target | M |
| 10 | Reversed, zero-to-one depth (§5.6) | depth step at 500 m: 75 mm → 0.02 mm; near clip back to 0.1 m | arithmetic | M |
| 11 | Slimmer render-target formats (§2.3) | −37 MiB at 1920×1032, less bandwidth in every pass that reads them | arithmetic | M |
| 12 | Spread the first frame's terrain and BLAS builds; cache imported models (§6.3, §6.4) | −325 ms first-frame hitch; no re-parse of every source model on every start | measured | M |
| 13 | Terrain vertices at 16 bytes instead of 48 (§2.2) | −35 MiB VRAM | arithmetic | M–L |
| 14 | Quantized vertex formats and 16-bit indices for all meshes (§2.5) | about half of mesh memory | estimate | L |
| 15 | Draw lists as compact handles; hot entity data in dense arrays (§3.4, §4.2) | 10–15× on the walks that remain after #2 | measured (microbenchmark) | L |
| 16 | Clustered light culling, cached shadow maps, temporal upscaling (§5.3, §5.4, §5.7) | scale with lights, static scenes and low-end GPUs | estimate | L each |
| 17 | A GPU-instanced foliage pass: instances in a buffer, culled on the GPU, drawn into the G-buffer and the shadow map without a draw item per tree (added v0.8.28; see ROAD_TO_TEST_GAME.md, B3) | 100,000 trees placed: today the nearest 40,000 cost ~11 ms CPU (foliage 2.4, HZB cull 3.6, sort 2.7, shadow); all 96,000 visible were 50 ms CPU / 39 ms GPU | measured | L |

Things that were checked and are **not** worth doing are in §8, so that nobody
spends a day re-checking them.

## 1. How it was measured

| Question | Method |
|----------|--------|
| What GPU memory exists, byte for byte | The Vulkan SDK's `VK_LAYER_LUNARG_api_dump` layer logs every call with its arguments. `vk_inventory.py` turns the log into an inventory of images, buffers, device allocations, acceleration structures and pipelines, and a histogram of one steady frame. |
| Where start-up time goes | The same layer with timestamps on every call. |
| Who allocates heap memory | The editor's own object files, relinked with one extra object and the linker's `--wrap`, so every `malloc`/`free` passes through a probe. One probe counts allocations per frame and records call stacks; the other tracks every live block back to the stack that made it. |
| Struct sizes and holes | A probe file compiled with the editor's exact flags, then `gdb ptype /o`. |
| What a layout or a flag is worth | Two microbenchmarks, and a second full build of the editor with `-march=x86-64-v3`, compared in interleaved runs. |
| Idle cost | Per-thread CPU time over ten seconds, `nvidia-smi`, and a debugger attach to name the threads. |
| What the engine's own zones miss | `stack_sampler`: from outside, with no debugger, it suspends the main thread every few milliseconds and walks its stack. |

Runs used the MyGame project at a 1920×1032 window, 300 frames, fixed time step.
"20k" means the same project with `--stress-entities 20000`. Timing runs were
interleaved A/B and repeated three times. One zone still varies by about ±15%
between identical runs on this machine, so differences smaller than that are
reported as "no difference".

**The machine.** Ryzen 5 4500 (Zen 2, 6 cores, 12 threads, 8 MB L3), RTX 3060,
1920×1080 at 75 Hz, GCC 13.2 (MinGW-w64, UCRT). 64 GB of DDR4 in four Corsair
modules **rated for 3600 MHz and running at 2133 MHz**: XMP/DOCP is off in the
BIOS. That is outside the engine, but it matters for these numbers. Almost all
of the engine's CPU time is spent waiting on memory, and TechPowerUp's Zen 2
scaling test measured about 10% at 720p between DDR4-2133 and DDR4-3200
([source](https://techpowerup.com/review/amd-zen-2-memory-performance-scaling-benchmark/5.html)).
Turning XMP on is free performance for this machine, and until then the CPU
numbers here are pessimistic compared with a typical user's.

## 2. GPU memory, byte by byte

### 2.1 Totals

Measured at the end of start-up, before the scene targets shrink to the
Viewport panel (F1): 1920×1032, ray tracing on, default effects.

| What | Count | MiB | Share |
|------|------:|----:|------:|
| Images (render targets, volumes, lookup tables) | 49 | 228.7 | 45% |
| Acceleration structures | 261 | 149.8 | 30% |
| Vertex buffers | 262 | 52.6 | 10% |
| Index buffers | 263 | 34.7 | 7% |
| All other buffers | 37 | 0.7 | 0% |
| Reserved but unused (block slack, alignment) | | 40.3 | 8% |
| **Device memory allocated** | **342 allocations** | **506.8** | |

`nvidia-smi` agrees from the outside: the GPU's used memory rises by about
560 MiB when the editor starts.

### 2.2 The terrain is 236.7 MiB of it

MyGame has one terrain of 1024×1024 cells. The engine cuts it into 16×16 = 256
chunks, and each chunk owns three GPU objects of exactly the same size:

| Per chunk | Bytes | ×256 |
|-----------|------:|-----:|
| Vertex buffer: 4,481 vertices × 48 bytes | 215,088 | 52.5 MiB |
| Index buffer: 35,520 indices × 4 bytes (four LOD tiers) | 142,080 | 34.7 MiB |
| BLAS over LOD 0: 8,704 triangles, 70.4 bytes per triangle | 612,480 | 149.5 MiB |
| **Total** | **969,648** | **236.7 MiB** |

Four changes, in order of effort:

- **Share the index buffer (−34.6 MiB, arithmetic).** `build_terrain_chunk_mesh`
  generates indices by looping over the grid, with no dependence on heights.
  Every chunk of the same cell count without holes therefore gets the same
  142 KB, byte for byte, and one shared buffer can serve them all. Chunks with
  holes keep their own. This also removes an index upload from every sculpt
  rebuild. Regular-grid terrain
  techniques such as geometry clipmaps rest on the same property: one grid
  topology serves every block ([Hoppe](https://hhoppe.com/proj/geomclipmap/)).
- **Compact the BLASes (−66 to −82 MiB, estimate).** The engine builds with
  `PREFER_FAST_TRACE` and never compacts. NVIDIA reports that compaction cuts
  BLAS memory "by at least 50%" in some games, measures 44% to 55% on three
  test scenes, and an average of 52% on RTX 3000-series cards
  ([tips](https://developer.nvidia.com/blog/tips-acceleration-structure-compaction/),
  [RTXMU](https://developer.nvidia.com/blog/reducing-acceleration-structure-memory-with-nvidia-rtxmu/)).
  The procedure: build with `ALLOW_COMPACTION`, query the compacted size, copy
  with the compact mode a frame or two later, then destroy the original once no
  frame in flight uses it. NVIDIA's advice is to budget a few compactions per
  frame and to skip BLASes that are rebuilt often. For terrain that means
  compacting a chunk only after it has stopped being sculpted.
- **Shrink the terrain vertex (−35 MiB, arithmetic).** A terrain vertex carries
  position, normal, UV and a four-float tangent, 48 bytes. On a height field
  the UV and the tangent follow from the position. Position plus a packed
  normal is 16 bytes. This needs its own vertex layout through the terrain
  G-buffer pipeline, the shadow pass, the voxelizer and the ray-tracing hit
  code, which is why it is ranked below the first two.
- **Allocate BLAS storage through VMA (§2.4).**

Together the first three take the terrain from 236.7 MiB to about 89 MiB, if
compaction reaches NVIDIA's 52% average.

### 2.3 Render targets: 78 bytes per pixel

The 49 images, grouped by owner. "Scales" means the size follows the scene
resolution, so since v0.8.12 these shrink with the Viewport panel.

| Owner | Images | Format | MiB at 1920×1032 | Scales |
|-------|--------|--------|-----------------:|:------:|
| G-buffer: normal + roughness | 1 | RGBA16F | 16.9 | yes |
| G-buffer: albedo + metallic, emissive + AO | 2 | RGBA8 | 16.9 | yes |
| G-buffer: depth | 1 | D32F | 8.4 | yes |
| Lighting output (HDR) | 1 | RGBA16F | 16.9 | yes |
| Post-processing output | 1 | RGBA16F | 16.9 | yes |
| TAA history | 1 | RGBA16F | 16.9 | yes |
| Transparent accumulation + reveal | 2 | RGBA16F, R16F | 21.1 | yes |
| SSR reflection | 1 | RGBA16F | 16.9 | yes |
| AO (GTAO working images, VXAO output) | 4 | R8 | 8.4 | yes |
| Bloom chain, half size down | 5 | RGBA16F | 6.6 | yes |
| Volumetric scatter, half size | 1 | RGBA16F | 4.7 | yes |
| Clouds, half size | 3 | RGBA16F | 14.1 | yes |
| HZB pyramid, half size, 10 mips | 1 | R32F | 3.3 | yes |
| Shadow map 2048² | 1 | D32F | 16.0 | no |
| Froxel fog 160×90×64 | 2 | RGBA16F | 15.0 | no |
| Environment cube 512² + irradiance + prefiltered + BRDF | 4 | RGBA16F, RG16F | 16.3 | no |
| Cloud noise 128³ + 32³, cloud shadow, sky panorama | 4 | RGBA8, R16F, RGBA16F | 8.9 | no |
| VXAO voxel grid 128³ | 1 | R8 | 2.0 | no |
| DDGI atlases | 2 | RGBA16F, RG16F | 1.9 | no |
| LTC tables, font atlas, fallbacks | 11 | various | 0.6 | no |
| **Total** | **49** | | **228.7** | |

RGBA16F is 158.7 MiB of the 228.7. Counting the targets that scale, the engine
keeps about **78 bytes for every output pixel**: 66 at full resolution and 12
more in half-resolution targets and the HZB.

Candidates, all arithmetic, each to be checked for the one thing that could
block it:

| Target | Today | Proposed | Saves | Check first |
|--------|-------|----------|------:|-------------|
| Lighting output | RGBA16F | B10G11R11 | 8.4 MiB | The shader writes alpha = 1. Confirm no later pass reads destination alpha. |
| Post-processing output | RGBA16F | RGBA8 or A2B10G10R10 | 8.4 MiB | It holds the tone-mapped image, which is already display-range. |
| TAA history | RGBA16F | B10G11R11 | 8.4 MiB | Banding in dark blue gradients. |
| G-buffer normal + roughness | RGBA16F | A2B10G10R10: octahedral normal in 10+10 bits, roughness in 10 | 8.4 MiB | Specular highlights on smooth surfaces. |
| Bloom chain | RGBA16F | B10G11R11 | 3.3 MiB | None expected. |

That is −37 MiB at 1920×1032, and the same halving for the TAA output, FXAA
and colour-effects intermediates when those are enabled (16.9 MiB each today).
What the literature says about the two format changes:

- B10G11R11 is half the size of RGBA16F and is the usual choice for HDR colour
  without alpha. Its blue channel has 5 mantissa bits against 6 for red and
  green, and Unity documents "a subtle blue/yellow banding in some rare cases"
  ([Wronski](https://bartwronski.com/2017/04/02/small-float-formats-r11g11b10f-precision/),
  [Unity](https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@14.0/api/UnityEngine.Rendering.Universal.HDRColorBufferPrecision.html)).
  It is on Vulkan's required-format list as a blendable colour attachment. As
  a storage image (compute-written targets) it depends on the
  `shaderStorageImageExtendedFormats` feature, which should be checked at
  device creation.
- Octahedral encoding stores a unit vector in two components
  ([Cigolle et al., JCGT 2014](https://jcgt.org/published/0003/02/01/paper.pdf)).
  One practitioner's sweep found the error visible at 16 bits in total, hard to
  see at 18 and not noticeable from 20
  ([analysis](https://liamtyler.github.io/posts/octahedral_analysis)), which is
  what 10+10 gives.

The transparent accumulation target, the froxel grids, the volumetric and cloud
targets all use their alpha channel and stay as they are. Aliasing transient
targets (F10 in the audit) is a separate saving on top of this.

### 2.4 342 device allocations, 261 of them one per BLAS

Meshes and textures go through VMA: three blocks, 128 MiB reserved. Everything
else calls `vkAllocateMemory` directly: each render target, each of the 261
acceleration-structure buffers, each material's uniform buffer. The histogram
of live allocations has 257 between 512 KiB and 1 MiB (the BLASes) and 42 under
64 KiB, 23 of them 256 bytes or less (uniform buffers).

NVIDIA's guidance is to sub-allocate, because `vkAllocateMemory` is expensive
and drivers limit the allocation count
([dos and don'ts](https://developer.nvidia.com/blog/vulkan-dos-donts)). For
acceleration structures specifically, a dedicated buffer is aligned to at least
64 KB where the structure itself needs 256 B; their RTXMU library saved 8 to
71 MB on test scenes by sub-allocating, mostly from small BLASes.

For this engine the memory saving is small while BLASes are 612 KB each. The
point is the count: a terrain four times this size would make 1,024 raw
allocations, and start-up already spends 0.36 s in 840 `vkCreateBuffer` and
bind calls (§6.1).

### 2.5 Mesh formats

- `SceneVertex` is 48 bytes: position 12, normal 12, UV 8, tangent 16, all
  32-bit floats. `SkinnedVertex` is 80 bytes: the same plus four 32-bit joint
  indices and four float weights.
- Every index is 32 bits. A terrain chunk has 4,481 vertices, and every mesh
  under 65,536 vertices could use 16-bit indices, including in its BLAS.
- The cook step already runs meshoptimizer's vertex-cache and vertex-fetch
  passes and builds LOD tiers. It does not quantize: "vertices are opaque
  stride-byte records, indices are u32".

meshoptimizer's own guidance: half floats or 16-bit normalized integers for
positions and UVs, and 8 or 10 bits per component for normals, "to optimize
memory bandwidth when fetching the vertex data even further, and to reduce the
amount of memory required to store the mesh"
([README](https://github.com/zeux/meshoptimizer)). A conservative layout is
24 bytes (float position, 10-10-10-2 normal and tangent, half-float UV), and 32
bytes for skinned meshes with 8-bit joints and weights. That is half of today's
mesh memory. It touches every pipeline's vertex input and the cooked format's
version, so it is a project, not a patch.

### 2.6 What ray tracing costs the process

| Default scene | Working set | Private bytes |
|---------------|------------:|--------------:|
| Ray tracing on | 0.33 GB | 0.87 GB |
| `GWS_FORCE_NO_RT=1` | 0.19 GB | 0.58 GB |

Measured. The 261 acceleration structures cost 150 MiB on the GPU and a
further 290 MB of private memory (140 MB of it resident) in the process,
inside the driver. Fewer and smaller BLASes help both.

## 3. CPU memory

### 3.1 The heap: 206 MiB, of which 192 MiB is one allocator

Measured at frame 150 of the default scene: 206 MiB live in 15,075 blocks.

| Owner | Live MiB |
|-------|---------:|
| `FrameAllocator` | 192.0 |
| Scene contents, mostly the terrain's 1025² height samples and hole flags | 5.3 |
| ImGui (fonts, windows) | 2.0 |
| Job system thread-local job rings | 1.5 |
| Python extension host (pocketpy) | about 3 |
| Everything else | about 2 |

`main.cpp` creates `FrameAllocator(8 MiB, 12 workers)`, and the allocator keeps
two arenas per worker: 24 arenas × 8 MiB. Its only user is two arrays of
`vec3` bounds per frame, on the main thread, which is 480 KB at 20,000 draws.
The other 22 arenas are never touched.

The arenas are allocated without being written, so Windows commits the pages
but does not back them with RAM. The cost is 192 MB of commit charge (it is a
fifth of the editor's private bytes), not 192 MB of physical memory. It still
counts against the system's commit limit. Sizing it to need, or reserving
address space and committing on demand, removes it.

### 3.2 What one entity costs: 2.7 KB in 6.7 heap blocks

Measured as the difference between the default scene and 20,000 stress
entities (a cube or sphere with a mesh renderer): +54.3 MiB live, +134,454
blocks, +361,385 allocations made.

| Owner | Bytes per entity |
|-------|-----------------:|
| The `Entity` object | 632 |
| ECS mirror slot (entity id, stamps, two matrices, hash node) | about 450 |
| Mesh-renderer component, its control block and strings | about 340 |
| Draw records (`EntityDraw`, `DrawItem`) | about 260 |
| Culling output, render-graph copy, ray-tracing instance list | about 380 |
| Scene registration, EnTT storage, other | about 650 |

At 100,000 entities that is 270 MB, which is acceptable. The reason to care is
not the total but the next two sections: where the bytes sit decides how fast
they can be walked.

### 3.3 Layouts

| Type | Bytes | Notes |
|------|------:|-------|
| `Entity` | 616 | See below. |
| `Transform` | 168 | 11 bytes of holes. |
| `MeshRendererComponent` | 184 | Two 32-byte strings; 10 bytes of holes. |
| `Material` | 224 | 96 bytes of uniforms, 8 texture pointers. |
| `Mesh` | 104 | |
| `DrawItem` | 96 | 64 of them the model matrix. |
| `EntityDraw` | 96 | 64 of them the model matrix. |
| `Light` (CPU and GPU) | 144 | 64 bytes of shadow matrix in every light, shadowed or not. |
| Instance data (GPU) | 128 | Two matrices: MVP and model. |
| `Job` | 128 | Two cache lines, by design. |

`Entity` in detail (offsets from `gdb ptype /o`):

| Offset | Bytes | Member |
|-------:|------:|--------|
| 0 | 16 | `enable_shared_from_this` |
| 16 | 64 | `name_`, `tag_` |
| 80 | 8 | `id_` and a 4-byte hole |
| 88 | 16 | `parent_` |
| 104 | 8 | `is_active_` and a 7-byte hole |
| 112 | 8 | `content_stamp_` |
| 120 | 24 | `children_` |
| 144 | 168 | `transform_` (its stamps are at 264 and 272) |
| 312 | 56 | `mesh_component_` |
| 368 | 64 | `skinned_mesh_component_` |
| 432 | 88 | `particle_emitter_component_` |
| 520 | 64 | `npc_agent_component_` |
| 584 | 24 | `components_` |
| 608 | 8 | flags and 6 bytes of padding |

Two facts follow. First, 272 bytes (44%) are four optional components that
every entity carries whether it uses them or not, and 62 bytes (10%) are
padding. Second, the fields that the per-frame passes read (active flag,
content stamp, transform stamps) sit at offsets 104 to 119 and 264 to 279: two
different cache lines per entity, 632 bytes apart from the next entity's.

### 3.4 What that layout costs

`layout_bench` walks 20,000 objects of exactly this shape and reads exactly
those four fields. "Cold" evicts the data between passes, which is the engine's
situation: a whole frame of other work runs between two walks. Measured:

| Layout | Warm | Cold |
|--------|-----:|-----:|
| 632-byte heap objects through pointers | 0.20–0.60 ms | 0.28–0.33 ms |
| One dense 24-byte record per entity | 0.011 ms | 0.030 ms |
| Struct of arrays | 0.010 ms | 0.020 ms |

A dense array is 10 to 15 times faster cold, which is the engine's case, and
more when warm. For comparison, the engine's own parallel gather over 20,000
entities (`sync.gather`) takes 0.35 to 0.47 ms: twelve threads walking
pointers are slower than one thread walking an array. 20,000 entities are
12 MiB of objects, more than this processor's 8 MB of L3.

This matches what is published. Splitting hot fields from cold ones gave a
20% speedup and 60% fewer last-level cache misses on a SPEC benchmark
([DINAMITE](https://arxiv.org/pdf/1606.00396)), and DICE rebuilt Battlefield
3's culling around flat arrays for this reason
([Culling the Battlefield](https://www.ea.com/frostbite/news/culling-the-battlefield-data-oriented-design-in-practice)).

## 4. The CPU frame

### 4.1 4.4 ms at 20,000 entities, 2.0 ms of it to learn that nothing changed

Median of three runs per zone, static scene, camera still:

| Zone | ms | Depends on |
|------|---:|------------|
| `hzb_cull` | 0.94 | camera |
| `ecs_sync` | 0.82 | scene changes only |
| `build_draw_items` | 0.76 | scene changes only |
| `draw_sort` | 0.70 | camera |
| `stage_geometry` | 0.46 | camera |
| `rt_scene_update` | 0.24 | scene changes only |
| `skinning_scan` | 0.17 | scene changes only |
| `shadow_light_cull` | 0.15 | lights and casters |
| `stage_shadow` | 0.12 | lights and casters |
| **Sum of the medians** | **4.37** | |

Since v0.8.9 and v0.8.10 the four "scene changes only" passes do almost no
work for an entity or draw that has not changed. They still visit every one to
find that out, and §3.4 is what a visit costs.

The engine already has what an O(1) answer needs: `Transform::ChangeStampCounter()`
is a single global counter that every transform and content change advances,
and `hierarchy_generation()` and `component_set_generation()` cover structure.
Today the sync and the draw build use the counter only as a "since when"
reference. If the three counters and the entity count are what they were last
frame, nothing an entity owns can have changed, and all four passes can reuse
last frame's result without touching an entity. That removes **1.99 ms of the
4.37 ms** in the frame that matters most in an editor: the one where the user
is looking, not editing.

The guard has to cover everything those passes read that is not an entity:
an asset finishing its background load, a material edit, a terrain rebuild.
Each needs to advance a counter the guard checks.

Three more walks of the same kind have no zone at all and so are missing from
the table; §4.6 measures them. The same guard covers two of them.

The risk is a mutation that does not stamp. The existing verifiers
(`GWS_ECS_VERIFY_ALL`, `GWS_DRAW_VERIFY`) are the test: run them with the guard
enabled and they recompute everything and compare.

A dirty list (each change appends the entity to a list) would also make frames
with a few changes proportional to the change count. Bevy's maintainers
declined that design for their general API because a list forgets history that
a tick remembers ([discussion](https://github.com/bevyengine/bevy/issues/1443)).
This engine has the stamps already, so a list would be an accelerator on top of
them, not a replacement, and the guard above is the cheaper first step.

### 4.2 The draw list is copied whole several times a frame

A draw is a 96-byte `DrawItem`, two thirds of it a matrix. In one frame at
20,000 draws the list exists as: the build's records (5.0 MiB live), the
frustum-culled copy (3.7 MiB), the render graph's copy (1.9 MiB), the
ray-tracing instance list (1.7 MiB) and the instance buffer (128 bytes per
instance). `sort.gather` (0.17–0.20 ms) is a permutation of 96-byte structs.

Passing an index or a 16-byte handle (mesh, material, transform index, flags)
and leaving the matrices in one array would shrink each of those copies three
to six times. The GPU instance record could be 48 bytes (a 3×4 model matrix,
with the view-projection applied in the vertex shader) instead of 128. This is
ranked low because it is a wide refactor for a few tenths of a millisecond.

### 4.3 Instruction sets: measured, no gain

A second full build with `-march=x86-64-v3` (AVX2, FMA, BMI2) and
`-ffp-contract=off`, compared with the shipping build in three interleaved
pairs at 20,000 entities:

| Sum of the nine zones above | Shipping build | AVX2 build |
|-----------------------------|---------------:|-----------:|
| Pair 1 | 4.57 ms | 5.01 ms |
| Pair 2 | 4.25 ms | 3.70 ms |
| Pair 3 | 4.24 ms | 4.71 ms |

The AVX2 build produced both the fastest and the slowest of the six runs. No
difference outside the noise. This agrees with the mixed results reported for
Linux distributions: one forum test measured a geometric mean of about 15% for
Haswell-level code, while a CachyOS comparison found one or two percent
([forum test](https://www.phoronix.com/forums/forum/software/desktop-linux/1244867-x86_64-microarchitecture),
[comparison](https://sunnyflunk.github.io/2023/01/15/x86-64-v3-Mixed-Bag-of-Performance.html)).
Code that waits on memory does not get faster with wider registers.

The kernels themselves do respond, in both directions. `mat_bench`, per
operation:

| Kernel | Shipping flags | `-march=x86-64-v3` |
|--------|---------------:|-------------------:|
| GLM `mat4 * mat4` | 5.4–7.0 ns | 2.2 ns in one program, **11.5 ns** in another |
| Hand-written SSE `mat4 * mat4`, bit-identical result | 4.5 ns | 3.1 ns |
| GLM frustum test, one box | 6.3–7.3 ns | 6.7–7.4 ns |
| SSE frustum test, four boxes at once, same answers | 2.5 ns | 2.2 ns |

The same GLM multiply got 2.4 times faster in one benchmark and 1.6 times
slower in another under the same flags. The probable cause, not confirmed in
the disassembly, is a store-forwarding stall: GLM returns the matrix by value,
and the compiler copies it with wide moves right after writing it with narrow
ones. Whether that happens depends on the code around the multiply. A
written-out SSE kernel was stable in both programs and gives the same bits.

**Do not turn on GLM's own SIMD.** `GLM_FORCE_INTRINSICS` with
`GLM_FORCE_DEFAULT_ALIGNED_GENTYPES` makes `mat4 * mat4` faster (3.6–4.9 ns)
but the plane test **three to four times slower** (20–28 ns), and it changes
`sizeof(glm::vec3)` from 12 to 16. A struct laid out like `SceneVertex` grows
from 48 to 64 bytes, which would silently break every vertex buffer and
GPU-facing struct. Measured.

Conclusion: no flag change. If a matrix or culling loop ever shows up in a
profile, write that one kernel by hand.

### 4.4 Heap allocations per frame: 117, none in the renderer's core

Measured over 100 steady frames, default scene. The count is the same at
20,000 entities.

| Per frame | Owner |
|----------:|-------|
| 58 | `Profiler::end_frame`: rebuilds a string-keyed map every frame |
| 20 | GPU profiler: vectors of name and time pairs |
| 6 | Three `*_to_text` calls: the material graph, animation graph and sequence are serialized every frame to compare with the saved text |
| 10 | `std::filesystem::path` built every frame in `main` and the menu bar |
| 4 | Hierarchy panel label strings |
| 3 | `VulkanGBuffer::draw_items` |
| about 16 | Everything else |

With `--log-debug` the count is 246, because the console panel re-copies its
entries whenever a line is logged and the profiler logs one every frame.

This is small: on the order of ten microseconds a frame. It is recorded so
that it stays small. The serialization is deliberate (the
code explains that a comparison cannot miss an edit the way a dirty flag can),
but its cost grows with the document, so a large material graph will make it
visible.

### 4.5 The idle editor

Measured with the editor open on the default scene, nothing moving, the window
in the background:

| | Idle editor | No editor |
|-|------------:|----------:|
| CPU | 41% of one core | |
| GPU utilization | 39–42% | 6–10% |

Over ten seconds: the main thread used 14–17% of a core, a thread inside the
NVIDIA driver 12–14%, and about a dozen others (job workers and helpers)
roughly 1% each. All of it is the same frame rendered 75 times a second.

The editor already stops rendering when minimized and skips the 3D frame when
the Viewport is hidden (F8). It does not slow down when it loses focus or when
nothing has changed. The other engines do. Godot's documentation says "the
Godot editor only redraws when necessary", and of its "Update Continuously"
setting: "this will increase power usage and heat/noise emissions since the
editor will now be rendering constantly, even if nothing has changed on
screen"
([Godot docs](https://docs.godotengine.org/en/stable/tutorials/troubleshooting.html)).
Unreal has had a "Use Less CPU when in background" editor preference since
UE4 ([forum thread](https://forums.unrealengine.com/t/processor-usage/56722)).
Unity's Interaction Mode throttles the editor's idle loop
([Unity issue](https://issuetracker.unity3d.com/issues/linux-high-cpu-usage-when-editor-interaction-mode-is-set-to-no-throttling)).

Two levels are possible. Dropping to a low rate when the window is unfocused is
simple. Redrawing only on change needs a rule for the effects that animate by
themselves (clouds, water, TAA convergence): for example, render at full rate
while anything changed in the last second, then fall to a few frames a second.
On a laptop this is the difference between the fan running and not.

### 4.6 What the zones do not see: 0.7 ms in edit mode, 2.9 ms in play mode

The profiler's zones account for all but 1.4 ms of an edit-mode frame at
20,000 entities. In play mode the unaccounted part is 4.1 ms. A sampling
profiler that takes the main thread's call stack from outside (3,250 samples
in play mode, 770 in edit mode) shows where it goes. Shares are of wall time
in a 13.3 ms frame:

| Function | What it does every frame | Edit mode | Play mode |
|----------|--------------------------|----------:|----------:|
| `ScriptSystem::update` | Copies the whole entity list (20,000 `shared_ptr` copies), then asks each entity whether it is active in its hierarchy and whether it has a script | not running | 14.8% = 1.96 ms |
| `TerrainMeshCache::prune` and `TerrainGpuCache::prune` | Each walks every entity, looks its id up in a map of terrains and rebuilds the map | 2.4% = 0.32 ms | 5.2% = 0.69 ms |
| `EditorParticleEmitters::update` | Walks every entity to find the enabled emitters | 2.7% = 0.36 ms | 2.2% = 0.29 ms |
| **Total** | | **0.68 ms** | **2.94 ms** |

None of the 20,000 stress entities has a script or an emitter, and the scene
has one terrain. All of this is the cost of finding that out again, 75 times a
second. At these sample counts each share is good to about half a percentage
point in play mode and one point in edit mode.

Three remedies, smallest first:

- Give all three a profiler zone. The profiler reported a frame it had not
  seen, which is finding F12 of the audit again.
- The two prunes depend only on which entities exist. The particle pass has
  to find the emitters only when something changed; the emitters it found last
  time still need simulating every frame. Both searches can sit behind the
  guard of §4.1.
- The script system needs a list of the entities that have a script, rebuilt
  when the component set changes. It snapshots the entity list because a
  script may spawn entities while it runs; a snapshot of the scripted entities
  serves the same purpose and is as long as the number of scripts.

Two side observations from the same runs. The walks the zones do cover are 20%
to 35% slower in play mode than in edit mode (`ecs_sync` 0.64 → 0.86 ms,
`sync.gather` 0.31 → 0.41 ms) on the same scene; the cause was not traced.
And sampling by attaching a debugger gives the wrong answer here: the first
attempt, 40 attaches, put 30% of the frame in `Profiler::end_frame`, which is
simply the first code to run after the present call returns while the debugger
keeps stopping and restarting the process.

## 5. The GPU frame

### 5.1 Pass times

Measured on the RTX 3060. The fullscreen column is the 20k scene at 1920×1032;
the docked column is the default scene at the Viewport panel's 1169×454.

| Pass | 1920×1032 | Share | 1169×454 |
|------|----------:|------:|---------:|
| Lighting | 1.61 ms | 31% | 0.46 ms |
| AO: GTAO + blur | 0.59 + 0.77 ms | 26% | 0.19 + 0.25 ms |
| Volumetric light | 0.54 ms | 11% | 0.20 ms |
| Geometry (G-buffer) | 0.43 ms | 8% | 0.19 ms |
| Shadow | 0.29 ms | 6% | 0.33 ms |
| Post-processing | 0.27 ms | 5% | 0.09 ms |
| SSR | 0.25 ms | 5% | 0.11 ms |
| Froxel fog | 0.15 ms | 3% | 0.13 ms |
| Transparent, water, HZB | 0.25 ms | 5% | 0.12 ms |
| **Total** | **5.14 ms** | | **2.09 ms** |

On this card the frame is small either way. The table matters for the cards
the audit is about: an integrated GPU runs the same passes several times to
more than ten times slower.

### 5.2 Ambient occlusion: 1.36 ms, a quarter of the frame

GTAO takes 0.59 ms and its blur 0.77 ms at 1920×1032. Intel's XeGTAO, an open
implementation of the same technique, lists its High preset at about 0.56 ms
at 1920×1080 on an RTX 2060, a slower card (figure from the project README as
mirrored in a [fork](https://github.com/kevinypcao/XeGTAO); the original
repository is archived). The engine's AO costs about 2.4 times that. The
scenes differ, so this is a rough comparison, but the gap is large.

The blur costs more than the effect it cleans, which the audit already noted
under F11. Options, in the order to try them: a cheaper denoise (XeGTAO's
README shows one 5×5 spatial pass followed by TAA); computing AO at half
resolution with a depth-aware upsample, which one commercial AO package
documents as a two to four times saving
([HTrace AO](https://ipgames.gitbook.io/htrace-ao/v2/settings-and-properties/gtao-settings/gtao-quality));
folding the three R8 working images into fewer. A target of 0.5 ms would take
0.8 ms (16%) off the fullscreen frame.

### 5.3 Lighting: every pixel visits every light

The lighting shader ends in `for (i = 0; i < pc.lightCount; ++i)` over a buffer
of up to 128 lights. There is no per-tile or per-cluster list, so the cost is
pixels × lights whatever the lights' ranges. With the handful of lights in the
test scenes this does not show. With dozens it will be the frame.

Clustered shading sorts lights into a 3D grid of view-space cells and has each
pixel read only its cell's list. The original paper reports it "much faster for
some cases that are challenging for tiled shading" and scaling to very large
light counts
([Olsson, Billeter, Assarsson 2012](https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf)).
The froxel fog pass already defines a 160×90×64 view-space grid, which is the
same kind of structure; a light grid would be much coarser.

### 5.4 Shadows: a fixed cost that is the second largest pass when docked

The shadow pass draws into a fixed 2048² map, so its 0.29–0.33 ms does not
shrink with the viewport: at the docked size it is 16% of the frame. It runs
every frame even when no light and no caster moved.

Caching is standard: Unity HDRP redraws static casters only on request and
dynamic casters every frame, Flax reports that copying a cached static map
"drastically improves both GPU and CPU performance", and Insomniac scrolled a
cached cascade as the camera moved
([HDRP](https://github.com/Unity-Technologies/Graphics/pull/1559),
[Flax](https://forum.flaxengine.com/t/new-shadow-map-atlas/1675),
[CSM scrolling](https://advances.realtimerendering.com/s2012/insomniac/Acton-CSM_Scrolling(Siggraph2012).pdf)).
The simplest form fits the stamps the engine already has: if the light and no
shadow caster changed, keep last frame's map. The map is also D32F (16 MiB);
D16 would halve it but needs a quality check on large scenes.

### 5.5 Bandwidth

Every pass reads and writes the targets in §2.3. Halving the HDR targets and
the normal target halves the bytes those passes move. On the RTX 3060 at these
sizes that will be hard to see. On an integrated GPU, which shares the
system's memory bandwidth, it is one of the few changes that helps every pass
at once.

### 5.6 Depth precision: 75 mm at 500 m, and a near plane at 0.2 m

The projection comes from `glm::perspective` with only the Y axis flipped. GLM
builds an OpenGL projection, where clip-space depth runs from −w to w, and
Vulkan keeps only 0 to w. Two consequences, both arithmetic for near = 0.1 m
and far = 1000 m:

- Depth reaches 0 at 0.2 m, so geometry between the nominal near plane (0.1 m)
  and 0.2 m is clipped.
- The 32-bit float depth buffer resolves these steps:

| Distance | Today | Reversed, zero-to-one |
|---------:|------:|----------------------:|
| 10 m | 0.03 mm | 0.001 mm |
| 100 m | 3.0 mm | 0.006 mm |
| 250 m | 18.6 mm | 0.018 mm |
| 500 m | 74.5 mm | 0.018 mm |
| 900 m | 241 mm | 0.007 mm |

Since F5, the lighting, AO, SSR, fog, water and DDGI passes all rebuild world
positions from this buffer, so its step is their position error. At 500 m a
shadow ray starts up to 7 cm off the surface.

Reversing the range (near plane at 1, far at 0, compare `GREATER`, clear to 0)
puts the float's dense values where the projection is sparse
([Reed, Depth Precision Visualized](https://www.reedbeta.com/blog/depth-precision-visualized/)).
Reed notes that the trick does nothing under OpenGL's −1 to 1 convention, so
the projection has to become zero-to-one first (`GLM_FORCE_DEPTH_ZERO_TO_ONE`
or a hand-built matrix). The cost is nothing at run time. The work is finding
every place that assumes depth 1 is far: the sky test in the lighting shader,
the HZB (max becomes min), depth biases, and each `inverse(view_proj)` user.

### 5.7 The lever for low-end hardware

For a GPU that is five to ten times slower, no per-pass saving above is
enough. The frame has to be rendered at fewer pixels. The engine has a render
scale and, since v0.8.12, resizable targets. What it lacks is a reconstruction
filter better than bilinear. FSR 2's Quality mode renders 67% of the output
resolution per axis, 2.25 times fewer pixels, and costs under 0.6 ms at 1080p
on a Radeon RX 5700 XT; it is open source and needs depth, colour and motion
vectors, which the engine has ([AMD](https://gpuopen.com/fsr2/)). It replaces
TAA instead of adding to it.

### 5.8 The command stream is not a problem

One steady frame is 750 Vulkan calls: 104 indexed draws, 29 compute
dispatches, 13 render passes, 57 barriers, 29 pipeline binds, one submit.
GPU-driven techniques exist to cut this count
([Haar and Aaltonen](https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf),
[multi-draw indirect](https://docs.vulkan.org/samples/latest/samples/performance/multi_draw_indirect/README.html)),
and here there is nothing to cut.

## 6. Start-up and hitches

### 6.1 A warm start reaches the first frame in 1.4 s

Measured from `vkCreateInstance` to the first present, with the logging layer
attached:

| Step | Time |
|------|-----:|
| `vkCreateDevice` | 0.30 s |
| 840 `vkCreateBuffer` + bind calls (terrain chunks and their BLAS buffers) | 0.36 s |
| Waiting on fences and queues during uploads | 0.16 s |
| Instance creation, device enumeration | 0.08 s |
| One gap of CPU work before an image creation (not traced) | 0.08 s |
| Everything else | 0.43 s |

### 6.2 A cold driver cache adds 2.2 s, and the engine's own cache is bypassed

The engine creates a `VkPipelineCache`, loads it from disk and saves it on
exit. **Only 2 of the 43 pipeline creation calls pass it.** The other 41 pass
a null handle.

On this machine the NVIDIA driver's own disk cache hides that: the lighting
pass is created in 1 to 2 ms. The driver appears to key that cache on the
executable's name. Four differently named copies of the editor were run during
this research, and each one's first run took **1.24 to 1.40 s** to create the
lighting pass, 0.37 to 0.41 s for DDGI and 0.43 to 0.52 s for the cloud pass.
Every later run of the same name took milliseconds. That cold case is the
first start after an install or a driver update on every machine, and every
start on a driver without such a cache.

Khronos's own sample measures pipeline creation more than doubling without a
cache and recommends saving the data between runs
([sample](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html));
Arseny Kapoulkine describes, from Roblox's experience, validating the saved
file against the driver before trusting it
([robust cache](https://zeux.io/2019/07/17/serializing-pipeline-cache/)). The
fix here is to pass `device->get_pipeline_cache()` at the 41 call sites.

### 6.3 The first frame takes 325 ms

The log's slow-frame line: 138 ms in the draw build, 110.6 ms of it building
the 256 terrain chunk meshes, and 131 ms building 260 BLASes, all inside the
first rendered frame. Building a bounded number per frame, or before the
window is shown, removes the hitch. The terrain build also allocates and frees
247 MiB in 72,000 blocks along the way.

### 6.4 Source models are parsed on every start

Start-up makes 1.66 million heap allocations, and 1.51 million of them (91%)
are the OBJ importer parsing the scene's four models: 5.2 MiB of text, one
allocation for every 3.6 bytes of input. It runs on a loader thread, so it
does not block the window, but the last model appears 1.5 s after start-up
begins and the work repeats on every launch. The project also holds a 39 MiB
OBJ that this scene does not use; a scene that did would pay about seven times
as much.

The cooked mesh format already exists (`mesh_cook.h`). An import cache, a
cooked blob keyed by the source file's path, size and time, turns the second
and later starts into a file read. Unity's Library folder and Unreal's derived
data cache are this idea.

## 7. Toolchain and binary

### 7.1 Two instruction sets in one binary

| Code | Translation units | Flags |
|------|------------------:|-------|
| Engine, editor, tools | 410 | `-O3` (baseline x86-64: SSE2) |
| Jolt Physics | 143 | `-O3 -g -mavx2 -mbmi -mf16c -mlzcnt -mpopcnt -ffp-contract=off` |
| `jolt_physics.cpp` (the engine's wrapper) | 1 | as Jolt |
| Other third-party code | about 270 | `-O3` |

Jolt's CMake defaults enable AVX2 and the engine inherits them. Jolt is
initialized lazily, when the first physics world is created. So on a processor
without AVX2 the editor would start, render and edit, and then die with an
illegal instruction when physics first runs, on Play at the latest. This is
what the flags imply. It was not reproduced, because this machine has AVX2.

About 95% of machines in Valve's survey report AVX2: 95.17% in the October
2026 figures
([report](https://shattered.io/steam-hardware-survey-october-2026-windows-11-72-51-linux-3-95-2026/),
[survey](https://store.steampowered.com/hwsurvey/Steam-Hardware-Software-Survey)).
The other 5% lean toward the low-end hardware the audit is written for: Intel
before Haswell, and the Pentium, Celeron and Atom lines for years after.

The choice is between two consistent states:

- **Baseline everywhere.** Jolt's CMake options `USE_AVX2`, `USE_AVX`,
  `USE_F16C`, `USE_LZCNT` and `USE_TZCNT` are ON by default and can be turned
  off, down to SSE2 if `USE_SSE4_1` and `USE_SSE4_2` go too. Physics gets
  somewhat slower. Nothing crashes.
- **AVX2 as the stated minimum.** Keep Jolt as it is and check the processor
  at start-up with a clear message. §4.3 shows there is no speed to gain by
  also compiling the engine that way.

Either is a small change. Leaving it mixed is the only wrong option.

### 7.2 Link-time and profile-guided optimization

Link-time optimization is off, and the CMake file records why: "no LTO (breaks
MinGW)". Published results for GCC are 8% for LTO, 8% for PGO and 12% for both
on SPEC, and one game engine's developers reported 1% to 3% from PGO
([GCC paper](https://arxiv.org/pdf/1403.6997),
[survey](https://arxiv.org/pdf/2507.16649),
[Spring RTS](https://springrts.com/phpbb/viewtopic.php?p=362034)). Given
§4.3, this engine is more likely to sit at the low end, and the toolchain risk
is real. Not recommended now.

### 7.3 The binary is small

The stripped editor is 11.3 MiB and the Windows package is 14.2 MB.

| Code section, 9.1 MiB | MiB |
|-----------------------|----:|
| Engine and editor | 3.3 |
| Jolt | 1.6 |
| ImGui and stb | 1.0 |
| zstd and LZ4 | 0.8 |
| pocketpy | 0.7 |
| spdlog and fmt | 0.4 |
| Everything else | 1.3 |

Two things to tidy, neither urgent. `TINYGLTF_IMPLEMENTATION` is a global
compile definition, so the glTF loader's implementation is compiled into four
translation units (`LoadFromJson` appears four times, 86 KiB of duplicates in
all). Duplicated functions across the binary total 0.3 MiB. And the 33.8 MiB of
debug information in the local release build comes from Jolt's `-g`; packaging
strips it.

## 8. Checked, and not worth doing

| Idea | Finding |
|------|---------|
| Compile the engine for AVX2 | No measurable gain (§4.3). |
| GLM's SIMD types | Slower plane tests and broken struct sizes (§4.3). |
| A faster general allocator (mimalloc) | The steady frame makes 117 allocations, all on the main thread. Allocator swaps pay when many threads allocate heavily ([LLVM](https://discourse.llvm.org/t/rfc-replacing-the-default-crt-allocator-on-windows/55795)), which is the opposite case. |
| Fewer draw calls, GPU-driven submission | 750 Vulkan calls a frame (§5.8). |
| Binary size | 11.3 MiB (§7.3). |
| Thread-local storage on MinGW | It is emulated and slower than native, but its 246 call sites are per job and per profiler zone, not per entity. Keep `thread_local` out of per-entity loops on this toolchain and it stays irrelevant. |
| Start-up on a warm machine | 1.4 s (§6.1). |
| Texture formats | The cook step already produces mipped BC1/BC3/BC5/BC7. BC7 is a quarter of RGBA8's memory ([Willems](https://saschawillems.de/files/Vulkanised_2021_texture_compression_in_vulkan.pdf)). Not measured here: the test project has no textures. |

## 9. Not examined

- Play mode beyond the frame profile of §4.6: physics, audio and networking
  under load, and scenes that do have scripts.
- A project with real textures, where texture memory would dominate and the
  uncooked RGBA8 path the editor uses would matter.
- Any GPU other than the RTX 3060, in particular an integrated one, where the
  ranking of §5 could change.
- The Linux build.
- The compacted size of this engine's BLASes on this driver. The 44–55% is
  NVIDIA's figure for other content.
- Shader-level optimization inside the lighting and volumetric passes.

## 10. Suggested order

Cheapest and safest first. Each line says how to know it worked, with the
tools in `tools/perf_research`.

1. **Frame allocator size.** `resolve_live.py` shows the owner's live bytes;
   private bytes in the health line drop by about 0.19 GB.
2. **Pipeline cache at every call site.** `vk_inventory.py pipelines` shows
   the cache handle on all 43 calls. To see the gain, delete the driver's cache
   or rename the executable and time the lighting pass's creation.
3. **Instruction-set policy.** `compile_commands.json` shows one flag set, or
   the start-up check exists.
4. **The unchanged-scene guard, and zones for the three unzoned walks.** Zones
   at 20k: `ecs_sync`, `build_draw_items`, `rt_scene_update` and
   `skinning_scan` go to about zero on a static scene, and the verifiers stay
   silent while editing. `stack_sampler` shows the terrain prunes and the
   particle scan gone, and `ScriptSystem::update` near zero in play mode once
   it walks a list of scripted entities.
5. **Idle and unfocused throttle.** `thread_cpu.ps1` and `nvidia-smi` with the
   editor idle.
6. **Terrain: shared index buffer, then BLAS compaction and sub-allocation.**
   `vk_inventory.py buffers accel memory`: index buffers 34.7 → about 0.2 MiB,
   acceleration structures 149.8 → about 72 MiB, device allocations 342 → under
   100. Log the compacted size per BLAS so the real ratio replaces the estimate.
7. **Ambient occlusion.** GPU zones `AO.GTAO` and `AO.Blur` at 1920×1032, with
   screenshot comparison.
8. **Reversed, zero-to-one depth.** A scene with geometry at 500 m and
   ray-traced shadows, before and after.
9. **Render-target formats**, together with F10.
10. **First-frame hitch and the import cache.** The slow-frame line and the
    start-up allocation count from `resolve_live.py ... count`.
11. **Vertex formats and 16-bit indices**, with the next change to the cooked
    format's version.
12. **The larger projects** (§3.4 dense data, §5.3 clustered lights, §5.4
    shadow caching, §5.7 upscaling), each when a real project needs it.

### Found by the test game (v0.8.38, docs/systems/TEST_GAME.md)

13. **Navmesh bake from full-resolution terrain.** Starting Play on the test
    game bakes 2.2 million navmesh triangles from the terrain and stalls one
    frame for about 4.7 s (slow-frame line at Play start). Bake from a coarser
    grid (every 2nd or 4th vertex), cache the bake with the scene, or bake on a
    worker. Measure: the slow-frame line at Play start, and agent paths still
    found (`npcagent_check`).
14. **Agents look up their target by name and scan the scene every frame**
    (`GetEntityByName` per agent per frame; the play loop maps gameplay
    entities to scene entities by scanning). Fine at 9 agents; measure at 100.
15. **The test game's frame, as a baseline.** Running towards the beacon at
    1920×1032: about 3 ms CPU, 3–5 ms GPU, 40–300 draws, 0.8–1.4 M triangles
    (75 fps, display-limited). Re-measure after each change above with
    `GWS_TEST_GAME_WALK=1` and `zones.py`.

## Sources

Hardware and adoption

- [Steam Hardware & Software Survey](https://store.steampowered.com/hwsurvey/Steam-Hardware-Software-Survey), and a [report on the October 2026 figures](https://shattered.io/steam-hardware-survey-october-2026-windows-11-72-51-linux-3-95-2026/)
- [TechPowerUp: Zen 2 memory performance scaling](https://techpowerup.com/review/amd-zen-2-memory-performance-scaling-benchmark/5.html)

Ray tracing and Vulkan

- [NVIDIA: Tips: Acceleration Structure Compaction](https://developer.nvidia.com/blog/tips-acceleration-structure-compaction/)
- [NVIDIA: Reducing Acceleration Structure Memory with RTXMU](https://developer.nvidia.com/blog/reducing-acceleration-structure-memory-with-nvidia-rtxmu/)
- [NVIDIA: Vulkan Dos and Don'ts](https://developer.nvidia.com/blog/vulkan-dos-donts)
- [Khronos Vulkan samples: pipeline cache](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html)
- [Kapoulkine: Robust pipeline cache serialization](https://zeux.io/2019/07/17/serializing-pipeline-cache/)
- [Khronos Vulkan samples: multi-draw indirect](https://docs.vulkan.org/samples/latest/samples/performance/multi_draw_indirect/README.html)
- [Haar and Aaltonen: GPU-Driven Rendering Pipelines, SIGGRAPH 2015](https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf)

Formats and geometry

- [meshoptimizer](https://github.com/zeux/meshoptimizer)
- [Cigolle et al.: A Survey of Efficient Representations for Independent Unit Vectors, JCGT 2014](https://jcgt.org/published/0003/02/01/paper.pdf)
- [Tyler: octahedral encoding analysis](https://liamtyler.github.io/posts/octahedral_analysis)
- [Wronski: Small float formats, R11G11B10F precision](https://bartwronski.com/2017/04/02/small-float-formats-r11g11b10f-precision/)
- [Unity: HDR colour buffer precision](https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@14.0/api/UnityEngine.Rendering.Universal.HDRColorBufferPrecision.html)
- [Willems: Texture compression in Vulkan, Vulkanised 2021](https://saschawillems.de/files/Vulkanised_2021_texture_compression_in_vulkan.pdf)
- [Hoppe: Geometry clipmaps](https://hhoppe.com/proj/geomclipmap/)
- [Reed: Depth Precision Visualized](https://www.reedbeta.com/blog/depth-precision-visualized/)

Rendering techniques

- [Olsson, Billeter, Assarsson: Clustered Deferred and Forward Shading, HPG 2012](https://www.cse.chalmers.se/~uffe/clustered_shading_preprint.pdf)
- [XeGTAO README, as mirrored in a fork](https://github.com/kevinypcao/XeGTAO)
- [HTrace AO documentation: GTAO quality settings](https://ipgames.gitbook.io/htrace-ao/v2/settings-and-properties/gtao-settings/gtao-quality)
- [Unity HDRP: cached shadow maps](https://github.com/Unity-Technologies/Graphics/pull/1559)
- [Flax Engine forum: new shadow map atlas](https://forum.flaxengine.com/t/new-shadow-map-atlas/1675)
- [Acton: CSM Scrolling, SIGGRAPH 2012](https://advances.realtimerendering.com/s2012/insomniac/Acton-CSM_Scrolling(Siggraph2012).pdf)
- [AMD FidelityFX Super Resolution 2](https://gpuopen.com/fsr2/)

Data layout and compilers

- [DINAMITE: A modern approach to memory performance profiling](https://arxiv.org/pdf/1606.00396)
- [DICE: Culling the Battlefield](https://www.ea.com/frostbite/news/culling-the-battlefield-data-oriented-design-in-practice)
- [Bevy: change detection design discussion](https://github.com/bevyengine/bevy/issues/1443)
- [Phoronix forums: x86-64 microarchitecture levels, a user's benchmark](https://www.phoronix.com/forums/forum/software/desktop-linux/1244867-x86_64-microarchitecture)
- [x86-64-v3: Mixed Bag of Performance](https://sunnyflunk.github.io/2023/01/15/x86-64-v3-Mixed-Bag-of-Performance.html)
- [Optimizing large applications (GCC LTO and PGO)](https://arxiv.org/pdf/1403.6997)
- [From Profiling to Optimization: a survey of PGO](https://arxiv.org/pdf/2507.16649)
- [Spring RTS forum: PGO results](https://springrts.com/phpbb/viewtopic.php?p=362034)
- [LLVM RFC: replacing the default CRT allocator on Windows](https://discourse.llvm.org/t/rfc-replacing-the-default-crt-allocator-on-windows/55795)

Editors

- [Godot documentation: troubleshooting (editor redraw and power use)](https://docs.godotengine.org/en/stable/tutorials/troubleshooting.html)
- [Unreal Engine forum: processor usage and the background CPU preference](https://forums.unrealengine.com/t/processor-usage/56722)
- [Unity issue tracker: editor Interaction Mode throttling](https://issuetracker.unity3d.com/issues/linux-high-cpu-usage-when-editor-interaction-mode-is-set-to-no-throttling)
