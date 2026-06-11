# Project Schizo — AAA Engine Master Implementation Plan

> **Purpose:** A single dependency-ordered, technically detailed roadmap that maps the 15-pillar AAA
> engine plan (Pillars A–O) onto the *actual current state* of the codebase, ordered by what must exist
> before what, with implementation steps and the working theory of each subsystem.
>
> **Target game:** *Project Schizo* — open-world multiplayer action-RPG / looter-shooter
> (Wuthering Waves movement · Destiny 2 builds · Elden Ring frame-data combat · Cyberpunk world density).
> See [`docs/game-design/project-schizo.md`](../game-design/project-schizo.md).
>
> **Engine namespaces in tree today:** `schizo::` (gameplay/scene/core), `gws::renderer::gpu::` (Vulkan).
>
> **Status:** Living document. **Version 1.0 — 2026-06-12.**
> Companion docs: [`EngineTechnicalRework/EXECUTION_CHECKLIST.md`](../EngineTechnicalRework/EXECUTION_CHECKLIST.md)
> (renderer rework, Phases 0–7), [`phase-6-planning/IMPLEMENTATION-ROADMAP.md`](../phase-6-planning/IMPLEMENTATION-ROADMAP.md)
> (gameplay systems).

---

## 0. How to read this document

The original AAA plan groups work into **pillars A–O**. Pillars are *categories*, not an execution order.
You cannot build them A→O because, e.g., almost everything assumes a **Job System** (A2) that does not yet
exist, and the **ECS** (D) is the data backbone that rendering, physics, animation and networking all read
from. This document re-orders the pillars into **16 stages** by hard dependency, and inside each stage each
subsystem gets:

- **Pillar tag** — which A–O item it satisfies.
- **Current state** — what is actually in the repo today (verified against source, not docs).
- **How it works** — the technical theory: data structures, algorithms, memory layout, GPU passes.
- **Steps** — ordered, buildable increments.
- **Depends on / Unblocks** — edges in the dependency graph.
- **Acceptance** — how you know the stage is done.

> **Honesty note on "done":** the renderer rework checklist marks many boxes ✅ that reflect *intent*.
> Where this plan says "present," it means code exists and compiles; where it says "specialized/partial,"
> the feature exists but not at the breadth the pillar plan implies. Verify `*.cpp` before trusting a ✅.

---

## 1. Current-state snapshot (the honest baseline)

| Pillar | State | What exists in tree |
|---|---|---|
| A1 Memory | 🟡 Partial | `engine/core/memory/`: `allocator_base`, `stack_allocator`, `pool_allocator`, `general_allocator`. No arena/frame allocator, no tracking/leak/frag tooling. |
| A2 Job System | 🔴 Absent | No thread pool / scheduler / fibers anywhere in `core`. **Top-priority gap.** |
| A3 Reflection | 🟡 Minimal | `renderer/gpu/vulkan/reflection.h` — editor-inspector only, not RTTI/serialization-grade. |
| A4 Serialization | 🟡 Partial | `editor/.../scene_serializer`, `core/file_io`. No versioning/binary+text duality. |
| A5 Logging | 🟢 Present | `engine/core/logging/logging.h`. |
| B1 RHI | 🟢 Good | `engine/renderer/gpu/render_device.h` + Vulkan backend. Vulkan only. |
| B2 Renderer | 🟢 Strong | Deferred + render graph + WBOIT transparency + thousands-of-lights. |
| B3 PBR | 🟢 Strong | `pbr.h`, `ibl.h`. No material layering. |
| B4 Lighting | 🟡 Partial | IBL + RT shadows/AO/reflections. No realtime GI, no lightmaps, no path tracer. |
| B5 Shadows | 🟢 Good | Cascaded + cube + spot. No virtual/contact shadows. |
| B6 Advanced | 🟡 Partial | Sky/atmosphere + SSR. No volumetrics/clouds/water/hair/skin. |
| B7 GPU-driven | 🟡 Partial | Meshlet + frustum cull + HZB. Indirect/mesh-shaders incomplete. |
| C1 Import | 🟡 Partial | glTF + stb_image. No FBX/USD/OBJ. |
| C2 Build pipeline | 🔴 Absent | No offline cook/compress/dependency-tracking. |
| C3 Virtual assets | 🔴 Absent | No virtual textures / mesh streaming. |
| D ECS | 🔴 Mismatch | `scene/entity.h` is OOP virtual-`Component` GameObject model — **not** archetype ECS. |
| E Physics | 🟡 Partial | Custom rigid bodies, constraints, particles, octree broad-phase. No Jolt, cloth, destruction. |
| F Animation | 🟡 Partial | Skeleton, animator, blend trees, skeletal mesh. No state machine asset/IK/motion-matching. |
| G Audio | 🔴 Absent | No audio subsystem. |
| H Networking | 🟡 Specialized | Rollback + deterministic sim + input sync (fighting-game style). No replication/interest-mgmt/dedi clusters. |
| I World | 🔴 Mostly absent | Octree + mesh LOD only. No streaming/world-partition/procedural/large-world coords. |
| J AI | 🔴 Absent | No navmesh/BT/utility/GOAP. |
| K Editor | 🟢 Good | Scene/asset-browser/material/gizmo/inspector/hierarchy/undo-redo/profiler panels (ImGui). No anim/terrain editor, no visual scripting. |
| L Scripting | 🔴 Native-only | C++ only, no managed/Lua, no hot-reload. |
| M Platform | 🔴 Windows-only | `build-msvc`; Vulkan portable in principle. No Linux/console/mobile targets. |
| N Profiling | 🟡 Partial | GPU profiler + per-pass timing + `profiler.h`. No mem/net profiler, no frame capture. |
| O Modern AAA | 🔴 Absent | No Nanite/Lumen/crowd/world-sim/destruction. |

**Overall:** ~20–25% of the pillar plan, very unevenly — a mature renderer + editor + action-game gameplay
core, on top of foundations (jobs, ECS, asset cook) that are missing and must come first.

---

## 2. The dependency graph (the "best order")

```
                    ┌─────────────────────────────────────────────┐
 STAGE 0  FOUNDATION │ Memory → Logging → Job System → Reflection → │
                    │ Serialization → Core Profiler hooks          │
                    └───────────────┬─────────────────────────────┘
                                    │ (everything below runs on jobs + reflection)
                    ┌───────────────▼──────────────┐
 STAGE 1  DATA       │ ECS: entities, archetype     │
                    │ storage, system scheduler     │
                    └───────────────┬──────────────┘
            ┌───────────────────────┼───────────────────────┐
 STAGE 2    ▼                       ▼                        ▼
 ASSETS   Import → Cook pipeline → Virtual assets (design)
            │
 STAGE 3    ▼   RENDERING COMPLETION (already strong; finish GPU-driven,
            │   GI, volumetrics/water, virtual geometry, virtual shadows)
            │
 STAGE 4-5  ▼   Physics (Jolt) ───► Animation (skeleton/IK/state machine/motion-match)
            │        │                     │
 STAGE 6    ▼        ▼                     ▼  Audio (mixer/spatial/DSP/streaming)
 STAGE 7    ▼   Networking (replication + prediction over existing rollback core)
 STAGE 8    ▼   World systems (streaming, large-world coords, procedural)
 STAGE 9    ▼   AI (navmesh, BT, utility, GOAP, crowd)
 STAGE 10   ▼   Gameplay integration (frame-data combat, abilities, loot, vehicles)
 STAGE 11   ▼   Editor & tooling expansion
 STAGE 12   ▼   Scripting + hot reload
 STAGE 13   ▼   Platform layer (Linux/console/mobile)
 STAGE 14   ═══ Performance infrastructure (CONTINUOUS, all stages)
 STAGE 15   ▼   Modern AAA (Nanite-like geometry, Lumen-like GI, crowd/world sim, destruction)
```

**Three rules that drive the order:**

1. **Jobs and ECS are retrofit-hostile.** Adding multithreaded job scheduling and a data-oriented ECS *after*
   gameplay is written means rewriting gameplay. Do them first (Stages 0–1).
2. **Networking is retrofit-hostile** (the design doc says so explicitly). The deterministic core already
   exists; extend it into replication *before* gameplay systems proliferate state (Stage 7, but its
   determinism constraints inform Stages 4–5).
3. **Performance infrastructure is continuous** (Stage 14), not a phase — instrument as you build.

---

# STAGE 0 — Foundation Hardening (Pillar A + N baseline)

Goal: a memory, threading, reflection and serialization substrate that every later system is built on.
Nothing in Stages 1–15 should call raw `new`/`delete` on the hot path or block a thread by hand.

## 0.1 Memory System — `engine/core/memory` (Pillar A1)

**Current state:** base/stack/pool/general allocators exist. Missing: arena, frame (double-buffered),
tracking, leak detection, defragmentation, and a global allocator policy.

**How it works (theory):**
- An **allocator** is an object that owns a region of memory and hands out sub-regions with a known
  lifetime policy. The engine standard is: *no system calls the OS allocator directly; it requests an
  allocator appropriate to the lifetime of the data.*
- **Arena (linear/bump) allocator:** a pointer into a block; `alloc(n)` returns the pointer and advances it
  by `n` (aligned). No per-object free; you `reset()` the whole arena. O(1) alloc, zero fragmentation.
  Used for per-level or per-load-scope data.
- **Frame allocator:** an arena that is `reset()` once per frame, *double-buffered* so frame N's transient
  data survives into frame N+1's first reads (job results, command-encoding scratch). Two arenas, ping-pong.
- **Stack allocator:** arena + a `marker` stack so you can `free_to_marker()` for nested scopes (LIFO).
- **Pool allocator:** fixed-size free-list. `alloc` pops a node, `free` pushes it. O(1), no fragmentation,
  for same-size objects (components, particles, events).
- **General allocator:** size-segregated free-lists + coalescing for variable-size long-lived allocations
  (the fallback). Wrap an existing TLSF/dlmalloc-style design.
- **Tracking:** every allocation routes through a thin header (or side table) recording `{size, tag,
  call-site, frame}`. Leak detection = "live allocations at shutdown grouped by tag." Fragmentation metric =
  `largest_free_block / total_free`.

**Steps:**
1. Define `IAllocator` policy interface (`allocate(size, align, tag)`, `deallocate(ptr)`, `reset()` where
   valid). Make `general_allocator` implement it; keep existing pool/stack as implementations.
2. Add `ArenaAllocator` and `FrameAllocator` (double-buffered). Wire a global `g_frame_allocator` reset at
   the top of the main loop.
3. Add an allocation-tag enum (`Render`, `Physics`, `Audio`, `Scene`, `Net`, `Temp`, …) threaded through
   `allocate`.
4. Add `MemoryTracker` (debug builds): side table keyed by pointer, dumps live-by-tag on shutdown and on
   demand to the profiler panel.
5. Replace hot-path `new`/`std::vector` churn in renderer draw-list building and ECS command buffers with
   the frame allocator.

**Depends on:** nothing. **Unblocks:** everything (especially Job System scratch, ECS storage).
**Acceptance:** frame allocator measurable in profiler; shutdown reports zero untagged leaks; a stress test
allocating/freeing 1M pool nodes shows O(1) and no fragmentation growth.

## 0.2 Logging — `engine/core/logging` (Pillar A5)

**Current state:** present. Harden, don't rebuild.

**How it works:** a logging front-end macro (`LOG_INFO(cat, fmt, …)`) formats lazily and pushes a record
into a **lock-free MPSC ring buffer**; a dedicated consumer thread drains it to sinks (console, file,
in-editor panel, optional network). Categories are compile-time tags with runtime level filters. Crash
handler flushes the ring + writes a minidump.

**Steps:** (1) confirm/add category + per-category level filter; (2) move sink I/O off the calling thread via
the ring buffer (critical once Job System lands — many threads log); (3) add crash-dump flush hook;
(4) reserve a remote sink interface for later.

**Depends on:** A1 (ring buffer memory). **Unblocks:** debuggability of all later stages.

## 0.3 Job System — `engine/core/jobs` (Pillar A2) — **CRITICAL, NEW**

**Current state:** none. This is the single highest-leverage missing foundation.

**How it works (theory):**
- A **work-stealing thread pool**: one worker thread per hardware core minus reserved threads (render
  submission, audio). Each worker owns a **double-ended queue (deque)** of jobs. A worker pushes/pops its
  own jobs from one end (LIFO, cache-hot); idle workers **steal** from the *other* end of a random victim's
  deque (FIFO, oldest = largest remaining subtree). This balances load with minimal contention.
- A **job** is `{function pointer, payload pointer, atomic unfinished-count, parent}`. Completing a job
  decrements its parent's counter; the parent is "done" when its counter hits zero. This gives a **dependency
  graph / fork-join** for free: spawn children, then `wait(job)` spins-help (runs other jobs) until done.
- **`parallel_for(range, fn)`** splits a range into chunks (≈ items / (cores·k)) and spawns a job per chunk,
  with recursive subdivision so large ranges don't serialize on one push.
- **Fibers (optional, later):** instead of blocking a worker on `wait`, switch to another fiber so the OS
  thread never stalls. Powerful but invasive; defer until profiling shows wait-stalls matter. Start with the
  spin-help model.
- **Determinism hook (for netcode):** the *simulation* job graph must produce identical results regardless
  of steal order. Achieve this by making simulation jobs side-effect-free into per-entity scratch, then
  merging in a fixed order — never by relying on float-accumulation order across threads.

**Steps:**
1. `JobSystem::init(num_workers)`, worker threads, per-worker Chase-Lev work-stealing deque.
2. `Job* create_job(fn, payload, parent=nullptr)`, `run(job)`, `wait(job)` with spin-help.
3. `parallel_for` with recursive splitting.
4. Counters/labels per job for the profiler (Stage 14) — color spans by tag.
5. Integrate: convert frustum culling, LOD selection, animation sampling, particle update to `parallel_for`.
6. (Deferred) fiber backend behind the same API if wait-stalls dominate.

**Depends on:** A1 (job allocator: pool of `Job` nodes per worker). **Unblocks:** ECS system scheduler (D3),
parallel physics/animation/culling, async asset loading.
**Acceptance:** `parallel_for` over 1M elements scales near-linearly to core count; a fork-join tree of depth
10 completes with no deadlock under TSan; sim job graph passes the existing determinism replay test.

## 0.4 Reflection — `engine/core/reflection` (Pillar A3) — **promote out of editor**

**Current state:** `renderer/gpu/vulkan/reflection.h` is editor-only. Promote to a core, code-generated or
macro-driven system because **Serialization, ECS, Editor inspector, Networking, and Scripting all need it.**

**How it works (theory):**
- For each reflected type we need, at runtime: its name, size, alignment, and a list of **fields**
  `{name, type, offset, attributes}`, plus optional methods. This is **RTTI++** — enough metadata to walk an
  object generically: read/write any field by name, enumerate fields for an inspector, serialize by iterating
  fields, and build network deltas by diffing fields.
- Two viable mechanisms: (a) **macro registration** — `REFLECT_BEGIN(T) REFLECT_FIELD(x) REFLECT_END()` that
  populates a static `TypeInfo` at startup; (b) **a libclang-based code generator** that parses annotated
  headers (`[[reflect]]`) and emits the registration. Start with (a) (no build-system complexity); graduate
  to (b) when the type count makes manual registration painful.
- **Attributes** (`Range(0,1)`, `NotNetworked`, `EditorHidden`, `Version(3)`) drive editor widgets,
  serialization rules and replication filters from one source of truth.

**Steps:**
1. `TypeInfo`/`FieldInfo` structs + global type registry keyed by stable type-id (FNV hash of name).
2. `REFLECT_*` macros; register core math types, `Transform`, and component types as they appear.
3. Generic visitor API: `for_each_field(void* obj, TypeInfo&, visitor)`.
4. Refactor the existing editor inspector to consume core reflection (delete the editor-local copy).
5. (Later) libclang generator under `tools/reflectgen` once manual registration exceeds ~40 types.

**Depends on:** A1. **Unblocks:** A4, D (component registration), K (inspector), H (delta replication),
L (script bindings). **Acceptance:** an inspector renders any reflected struct with no per-type UI code;
round-trip "object → field walk → struct" preserves data.

## 0.5 Serialization — `engine/core/serialization` (Pillar A4)

**Current state:** scene serializer in editor + file_io. Generalize on top of reflection.

**How it works (theory):**
- **Reflection-driven:** to serialize, walk fields via `TypeInfo` and emit each by type; to deserialize,
  read fields by name/id into offsets. No hand-written per-type read/write.
- **Two back-ends, one front-end:** a `text` writer (JSON/TOML, for source-controlled assets and editor
  saves) and a `binary` writer (tagged, little-endian, for cooked runtime assets and saves). Same visitor,
  different sink.
- **Versioning / backward compatibility:** every type carries a `version` attribute; on load, missing fields
  take defaults and renamed/removed fields are handled by per-type **upgrade functions** keyed by
  `(type, from_version)`. Binary uses field **ids** (stable hashes) not order, so adding a field never breaks
  old data.
- **Save games** are just serialization of the live ECS world (or a designated subset) plus a header
  `{game version, world tick, schema hash}`.

**Steps:**
1. `Archive` interface (`serialize(field, value)` both directions) with `TextArchive` and `BinaryArchive`.
2. Reflection-driven `serialize(Archive&, void*, TypeInfo&)`.
3. Versioning + upgrade-function registry.
4. Port editor scene save/load onto it; add binary path for cooked assets (used by Stage 2).
5. Save-game = ECS world snapshot serialize (depends on Stage 1).

**Depends on:** A3, A1. **Unblocks:** C2 (cooked binary assets), K (scene save), H (snapshot/state sync),
save games. **Acceptance:** load a v1 asset with the v3 schema and have upgrade functions fill the gap;
text and binary round-trip identical objects.

## 0.6 Core profiler hooks (Pillar N1–N3 baseline)

Stand up the *instrumentation API* now (the full UI is Stage 14) so every later stage is born instrumented:
a scoped CPU timer macro that emits `{tag, thread, start, end}` into a per-thread ring (drained by the job
system's idle workers), plus the existing GPU timestamp queries and the A1 memory tracker feeding one panel.

**Acceptance:** a flame strip in the editor shows main-loop + job spans this stage onward.

---

# STAGE 1 — Data Backbone: ECS (Pillar D)

This is the most consequential architectural decision in the plan. The repo today uses an **OOP
GameObject/Component** model (`scene/entity.h`: `class Component { virtual OnUpdate(dt); … }` with
heap-allocated components behind pointers). The pillar plan and game design call for a **data-oriented,
archetype ECS** capable of thousands–millions of entities updated multithreaded. The Phase-6 roadmap even
lists **EnTT** as "integrated" — reconcile this: either adopt EnTT (sparse-set ECS) or build an archetype
ECS. Recommendation below.

## 1.0 Decision: archetype ECS vs sparse-set (EnTT)

**How the two differ:**
- **Sparse-set (EnTT):** each component type has its own packed array + a sparse index `entity→slot`.
  Iterating one component is cache-perfect; iterating *combinations* (a "view") hops between arrays. Cheap
  add/remove. Excellent default, battle-tested, header-only.
- **Archetype (Unity DOTS / Unreal Mass style):** entities with the *same set* of component types live
  together in **chunks** (16 KB blocks) as **struct-of-arrays**. Iterating a query over an archetype is a
  linear sweep over contiguous SoA memory — maximal SIMD/cache throughput. Add/remove a component **moves**
  the entity to another archetype (more expensive structural change).

**Recommendation:** Adopt **EnTT** as the registry now (it's already referenced, it's the lower-risk path,
and it gives 90% of the data-oriented win), and **reserve an archetype-chunk allocator behind the same query
API** for the hottest systems (transforms, animation, particles) if profiling demands it later. This avoids a
from-scratch ECS while keeping the door open for O-tier scale (crowds of 100k+).

## 1.1 Entity system (Pillar D1)

**How it works:** an **entity is a 64-bit handle** = `{index, generation}`. Generation invalidates stale
handles after an entity is destroyed and its index recycled (`is_alive(e) == generations[e.index] ==
e.generation`). The registry maps handles to component storage; entities themselves hold no data.

**Steps:** wrap EnTT `registry` in `schizo::ecs::World`; expose `create()`, `destroy()`, `valid()`,
`add<T>()`, `get<T>()`, `remove<T>()`, `view<Ts...>()`. Keep a stable `Name`/`Id` component for tooling.

## 1.2 Component storage / archetype option (Pillar D4)

**How it works:** components are **plain structs** (no virtuals) registered with reflection (Stage 0.4) so
the editor/serializer see them automatically. Storage is EnTT pools by default. For the archetype option,
chunks store SoA arrays sized to fill 16 KB; a query caches the matching chunk list and invalidates on
structural change.

**Steps:** define the core component set as POD: `Transform`, `LocalToWorld`, `MeshRenderer`, `Material`,
`RigidBody`, `Collider`, `LightComponent`, `Skeleton`, `AnimationState`, `AudioSource`, `NetId`,
`AbilityState`, `Health`. Register each with reflection. (Archetype-chunk backend is deferred until a hot
system needs it.)

## 1.3 System scheduler (Pillar D3) — runs on the Job System

**How it works (theory):** a **system** is a function over a query. The scheduler builds a **dependency graph
from data access declarations**: each system declares the component types it reads and writes; two systems
conflict iff one writes a type the other reads/writes. Non-conflicting systems run in parallel on the job
system; conflicting ones are serialized by an edge. Structural changes (add/remove/create/destroy) are not
applied immediately — they're recorded into a per-thread **command buffer** and **played back at a sync
point** between system batches, so iteration is never invalidated mid-sweep and the order is deterministic.

**Steps:**
1. `System` registration with `reads<…>()/writes<…>()` access sets.
2. Build the conflict graph; topologically batch into parallel waves.
3. Per-thread `EntityCommandBuffer`; deterministic playback at sync points.
4. Convert one real system end-to-end first (transform → `LocalToWorld` propagation), measure, then migrate.

## 1.4 Migrating the existing OOP scene

**How it works:** the current `Scene`/`Entity`/`Component` (with `OnUpdate`) becomes a thin **authoring/editor
layer** that compiles down to ECS components, *or* is replaced outright. Lowest-risk path: keep editor-facing
`Entity` as a handle wrapper over the ECS world; move per-frame logic out of `Component::OnUpdate` virtuals
into ECS systems. Gameplay systems already in `core/` (character controller, ability, network) are ported to
operate on components rather than `GetComponent<T>()` pointer chasing.

**Depends on:** Stage 0 (jobs, reflection). **Unblocks:** all gameplay, rendering submission, physics,
animation, networking read their state from here. **Acceptance:** 100k entities with `Transform +
MeshRenderer` iterate and submit draws in a single parallel pass under budget; structural changes are
deterministic across runs (netcode replay still passes).

---

# STAGE 2 — Asset Pipeline (Pillar C)

You cannot fill an open world without an offline pipeline that turns artist files into fast-loading runtime
assets. The renderer already consumes glTF at runtime; that becomes the *import* front-end of a real cook
pipeline.

## 2.1 Import system (Pillar C1)

**Current state:** glTF + stb_image at runtime. Need FBX, OBJ, USD coverage and a unified import API.

**How it works:** an `Importer` per source format parses into a **neutral intermediate** (`ImportedScene`:
meshes with typed vertex streams, materials, textures, skeletons, animations, node hierarchy). The runtime
never sees FBX/USD — only the cooked output. Use Assimp for FBX/OBJ breadth (fast to integrate) and keep the
in-tree glTF loader for the common path; add `tinyusdz` for USD when USD-authored content appears.

**Steps:** (1) define `ImportedScene` intermediate; (2) wrap existing glTF loader to emit it; (3) add Assimp
importer (FBX/OBJ) behind the same interface; (4) USD later.

## 2.2 Build / cook pipeline (Pillar C2) — **NEW**

**How it works (theory):**
- A **cooker** transforms `ImportedScene` → platform-optimized binary assets the engine `mmap`s and uses with
  zero parsing: meshes become GPU-ready vertex/index blobs with precomputed LOD chains (meshoptimizer is
  already vendored) and meshlets; textures become **block-compressed** (BC7/BC5/BC6H) mip chains; materials
  become parameter blocks referencing texture GUIDs.
- **Content addressing & dependency tracking:** every source asset has a GUID; the cooker records a
  dependency graph (mesh → materials → textures) and a content hash. A build is incremental: re-cook only
  assets whose source or dependencies changed (hash mismatch). Output goes to a **pak/bundle** with a TOC
  `{guid → offset, size, compression}`.
- **Compression:** per-asset codec choice — LZ4 for fast-load mesh/anim, BC* for textures (GPU-native, no CPU
  decode), Oodle/Zstd for cold bulk.

**Steps:**
1. Asset GUID + manifest; `tools/assetcook` CLI driving importers → `Archive` binary (Stage 0.5).
2. Texture cooking: mip-gen + BC compression (use `bc7enc`/`ispc_texcomp`).
3. Mesh cooking: optimize vertex cache, generate LODs + meshlets, pack vertex streams.
4. Dependency graph + content hashing → incremental cook.
5. Pak/bundle writer + runtime `PakFile` reader (memory-mapped).

## 2.3 Virtual assets / streaming (Pillar C3) — design now, implement with Stage 8

**How it works:** **virtual textures** split textures into 128×128 tiles; the GPU samples a page-table
indirection texture, a feedback pass reports which tiles are needed, and a streamer pages tiles into a
physical atlas on demand — constant VRAM regardless of world texel count. **Streaming meshes** stream LODs /
geometry pages by distance. These integrate with world streaming (Stage 8); here, only the **asset format**
(tiled, page-aligned) is fixed so cooked data is stream-ready.

**Depends on:** Stage 0.5 (binary serialization), meshoptimizer (present). **Unblocks:** content production,
Stage 8 streaming, Stage 15 virtual geometry. **Acceptance:** changing one texture re-cooks only that texture
and its dependents; cooked scene loads via mmap with no runtime parse; cooked LODs match runtime-generated.

---

# STAGE 3 — Rendering Completion (Pillar B remaining + O1/O2 entry)

The renderer is the strongest pillar. This stage finishes the *breadth* the AAA plan implies. Each item slots
into the existing render graph (`engine/renderer/gpu/vulkan/vulkan_render_graph`).

## 3.1 GPU-driven rendering completion (Pillar B7)

**Current state:** meshlets + frustum cull + HZB exist; indirect/mesh-shader path incomplete.

**How it works:** the CPU stops issuing per-object draws. Instead, all instances live in GPU buffers; a
**compute culling pass** tests each instance against the frustum and HZB, writes survivors' draw args into an
indirect buffer, and the GPU draws everything with `vkCmdDrawIndexedIndirectCount` (or mesh-shader dispatch).
This is what lets the open world push hundreds of thousands of instances. Meshlet (cluster) culling does the
same at sub-mesh granularity, plus per-cluster backface-cone rejection.

**Steps:** move instance/material/transform data into GPU SSBOs; compute cull → indirect-count draw; enable
the meshlet path through mesh shaders where supported (fallback to the existing meshlet-as-index path);
re-enable HZB occlusion now that draws are GPU-side. (Respect the known winding/cull traps — keep per-mesh
`double_sided` flags.)

## 3.2 Realtime Global Illumination (Pillar B4 / toward O2 "Lumen-like")

**How it works (theory):** start with **DDGI (dynamic diffuse GI via irradiance probes)**: a grid of probes,
each storing irradiance in an octahedral map; every frame, trace a handful of rays per probe (hardware RT is
already wired — reuse the TLAS), update probe irradiance with a moving average, and at shading time sample the
8 surrounding probes (trilinear + visibility weighting via per-probe depth/Chebyshev to avoid light leak).
This gives dynamic indirect diffuse. **Specular** reuses the existing SSR + RT reflections. The full
"Lumen-like" surface-cache/screen-probe system is Stage 15.

**Steps:** probe grid + octahedral irradiance/depth atlases; per-frame RT probe update (reuse TLAS);
shading-time probe sampling with visibility weights; relight on light/geometry change. **Depends on:** RT
scene (present), Stage 1 (light components).

## 3.3 Volumetrics, atmosphere, clouds, water (Pillar B6)

**How it works:**
- **Volumetric fog:** a **froxel** grid (camera-frustum-aligned 3D texture). Pass 1 writes per-froxel
  scattering/absorption from fog density + lights (with shadow sampling). Pass 2 **ray-marches/integrates**
  along view rays front-to-back into a final scattering texture, applied during composite. Light shafts fall
  out for free.
- **Atmosphere:** physically-based sky (Bruneton-style precomputed transmittance + multiscatter LUTs) — the
  repo already has a sky/IBL system; extend it to feed aerial perspective into the froxel volume. (Note the
  known **skybox-gray-by-camera-position bug** — fix during this work; it's in the sky shader.)
- **Volumetric clouds:** ray-march a 3D noise (Perlin-Worley) density field with two detail octaves, Henyey-
  Greenstein phase, and cheap multiscatter approximation; temporally amortize across frames.
- **Water:** FFT ocean (Tessendorf) or Gerstner waves for the surface; screen-space refraction + the existing
  SSR/planar reflection for reflection; depth-based absorption/scattering for underwater (the game has
  underwater sections + swimming).

**Steps (independent, parallelizable):** froxel fog first (biggest visual/perf win and reuses shadow data);
then aerial perspective from atmosphere; then clouds; then water. **Depends on:** lighting + shadows
(present), depth (present).

## 3.4 Virtual shadow maps + contact shadows (Pillar B5 completion)

**How it works:** **VSMs** replace cascades with a single huge virtual shadow texture, sparsely backed by
physical pages allocated only where the camera actually samples shadows (driven by a feedback pass, same idea
as virtual textures). Result: stable, high-res shadows without cascade seams. **Contact shadows** are a short
screen-space ray-march in the depth buffer to add fine contact occlusion cheaply.

**Steps:** add screen-space contact shadows first (cheap, immediate quality); VSM is a larger effort, pair it
with virtual-texture infra from Stage 2.3.

## 3.5 Material layering, hair, skin (Pillar B3/B6 completion)

**How it works:** **layered materials** blend N PBR layers by mask (e.g., base + wear + decals) in the
shader, parameters fed by the cooked material block. **Skin** = subsurface scattering (screen-space
diffusion / Burley) + dual-specular. **Hair** = Kajiya-Kay or Marschner shading on strand or card geometry
with order-independent transparency (the WBOIT path already exists).

**Depends on:** material system (present), WBOIT (present). **Acceptance for Stage 3:** open test scene with
100k+ GPU-driven instances, DDGI indirect, froxel fog, and water renders at budget with validation clean.

---

# STAGE 4 — Physics (Pillar E) — adopt Jolt

**Current state:** custom rigid bodies + constraints + octree broad-phase + particles. The game design doc
explicitly targets **Jolt**. Custom physics will not reach AAA stability/performance for a networked action
RPG; migrate to Jolt while keeping the custom particle/PDE solvers for VFX.

**How it works (theory):**
- **Broad phase** (find *possible* collisions cheaply): an AABB tree / SAP over body bounds → candidate
  pairs. (Replaces the custom octree for dynamics.)
- **Narrow phase** (exact contact): GJK/EPA for convex shapes → contact manifolds (points, normal,
  penetration).
- **Solver:** a sequential-impulse / TGS constraint solver resolves contacts + joints over iterations to
  satisfy non-penetration and friction; integrates velocities → positions at a fixed timestep.
- **Determinism:** Jolt is deterministic given identical inputs/order — essential because networking (Stage
  7) rolls back and re-simulates physics. Run physics inside the deterministic fixed-step (existing
  `DeterministicSimulation`, 66 Hz).
- **Character physics:** a **kinematic capsule character controller** (not a dynamic body) doing collide-and-
  slide against the world, with ground/slope/step detection — feeds the existing 8-state movement machine
  (`core/character`). Swimming/climbing/wall-run are movement states that switch the controller's constraint
  mode.

**Steps:**
1. Vendor Jolt; wrap in `schizo::physics::PhysicsWorld` behind a thin engine interface (so gameplay doesn't
   bind to Jolt types).
2. ECS components: `RigidBody`, `Collider` (box/sphere/capsule/convex/mesh), `CharacterController`.
3. Sync: ECS `Transform` ↔ Jolt body each fixed step; run inside the deterministic sim.
4. Port the existing capsule character controller + ground detector onto Jolt's character.
5. Hitbox system (combat): **separate** collision capsules per bone, decoupled from render mesh, activated
   per animation frame (frame-data combat) — queried against, not simulated.
6. Advanced (Pillar E4): cloth (position-based dynamics), ropes (PBD chains), destruction (pre-fractured
   chunks + Jolt). Defer until core is stable.

**Depends on:** Stage 1 (components), deterministic sim (present). **Unblocks:** character movement,
combat hit detection, networking re-sim. **Acceptance:** 1k dynamic bodies stable at 66 Hz; identical results
across two runs with same inputs (determinism); character collide-and-slide on slopes/steps.

---

# STAGE 5 — Animation (Pillar F)

**Current state:** skeleton, animator, blend trees, skeletal mesh components exist. Missing: state-machine
*asset*, IK, motion matching, animation streaming. The game needs root-motion, parkour, and frame-data
combat poses.

**How it works (theory):**
- **F1 Skeleton:** a hierarchy of bones (local TRS); a pose is an array of local transforms; the **skinning
  matrices** = `globalBone * inverseBind`, uploaded to a GPU buffer; the vertex shader blends by bone
  weights. (Skeletal mesh path exists.)
- **F2 Blend trees:** a DAG of nodes that each output a pose; **blend nodes** interpolate child poses by a
  parameter (1D speed blend: idle→walk→sprint; 2D directional). Evaluated bottom-up per frame.
- **F3 State machines:** states reference blend trees; **transitions** have conditions + blend durations;
  cross-fade between the outgoing and incoming pose during the transition window. This drives the locomotion
  + combat movesets; integrate with the **priority-based movement state machine** in the design doc.
- **F4 IK:** **two-bone IK** (analytic) for foot/hand placement; **FABRIK** or CCD for chains; foot IK
  raycasts to ground and adjusts ankle + pelvis so feet plant on uneven terrain. Applied as a *post-pass*
  after the pose is sampled.
- **Root motion:** the animation's root-bone displacement drives character translation (weighted feel),
  reconciled with the physics character controller.
- **F5 Motion matching:** instead of hand-authored transitions, search a database of animation poses each
  frame for the clip+frame whose pose & trajectory best match the desired trajectory; blend to it. High
  quality, data-heavy — adopt after locomotion is solid.
- **F6 Streaming:** stream animation clips by need (large movesets) using the Stage 2 cook/stream format.

**Steps:**
1. Make pose sampling + blend-tree eval a **job-parallel** ECS system (`parallel_for` over animated
   entities), writing skinning matrices to a GPU buffer.
2. State-machine **asset** (data-driven, reflected/serialized) + editor (Stage 11) to author it.
3. Foot IK (two-bone) + ground raycast; pelvis adjust.
4. Root-motion integration with the Stage-4 character controller.
5. Frame-data hook: expose per-frame events (hitbox on/off, cancel windows) to the combat system (Stage 10).
6. (Later) motion matching; animation streaming.

**Depends on:** Stage 1, Stage 4 (root motion ↔ controller), Stage 0.3 (parallel sampling). **Acceptance:**
1k skinned characters sampled+blended in parallel under budget; foot IK plants on slopes; root motion matches
controller without sliding.

---

# STAGE 6 — Audio (Pillar G) — **NEW, entirely missing**

**How it works (theory):**
- **G1 Mixer:** audio runs on its own real-time thread pulling from a **bus graph** (voices → group buses →
  master). Each block, active **voices** are decoded/resampled, summed into their bus, processed by bus DSP,
  and written to the device ring buffer. Never block this thread (no locks, no allocation — use lock-free
  command queues from the game thread).
- **G2 Spatial audio:** per-voice 3D position → distance attenuation + **HRTF or panning** relative to the
  listener (the camera/player). Doppler from relative velocity. Occlusion/obstruction via physics raycasts
  (reuse Stage 4) lowpass-filtering occluded sources.
- **G3 Reverb:** send buses to reverb zones (Schroeder/FDN reverb or convolution); the player's current zone
  sets parameters. **G4 DSP:** EQ, compressor, lowpass/highpass, distortion as bus inserts.
- **G5 Streaming:** music/ambience stream from disk (decode in chunks via job system); SFX are resident.
- **G6 Procedural:** parameter-driven synthesis for things like ability/impact layers.

**Steps:**
1. Pick a backend: **integrate FMOD/Wwise** (fast, AAA-grade, but licensing) *or* build on **miniaudio**
   (device + decode) with a custom mixer. Recommendation: miniaudio + custom bus graph for control and no
   licensing, FMOD/Wwise if budget allows.
2. `AudioSource` / `AudioListener` ECS components; lock-free command queue game→audio thread.
3. Bus graph + voice mixer on the audio thread.
4. Spatialization (distance/pan/Doppler) + physics-driven occlusion.
5. Reverb zones + DSP inserts; music streaming via job system.

**Depends on:** Stage 0 (jobs/memory), Stage 1 (components), Stage 4 (occlusion rays). **Acceptance:** 128
concurrent spatial voices with zero audio-thread stalls; occlusion audibly filters blocked sources.

---

# STAGE 7 — Networking (Pillar H)

**Current state:** strong **deterministic + rollback + input-sync** core (`core/network`) — built for
fighting-game-style lockstep. The MMO-looter-shooter design needs this *extended* into
**replication + client prediction + server authority + interest management**. Do this before gameplay state
proliferates (the design doc: "never bolt networking on later").

**How it works (theory):**
- **H1 Replication:** the server is authoritative. Replicated entities/components are **serialized as deltas**
  (diff vs last acked state, via reflection field-walk from Stage 0.4/0.5) and sent at a tick rate.
  **Snapshot interpolation** smooths remote entities between received snapshots. Reliable-ordered for events,
  unreliable for state.
- **H2 Prediction & reconciliation:** the local player **predicts** movement/abilities immediately from
  input; the server simulates authoritatively; when the server state for tick T arrives, the client
  **rewinds to T, replays buffered inputs T..now** (the existing rollback machinery), and corrects. Physics
  determinism (Stage 4) makes this exact.
- **H3 Dedicated servers:** the simulation must run headless (no renderer/audio) on Linux + Windows. This
  forces a clean engine/render separation (also helps Stage 13). Server runs the ECS world + physics + AI at
  fixed tick.
- **H4 Large-scale:** **interest management** — a server only sends a client entities within its
  **relevancy** (grid/AOI around the player). **Entity prioritization** ranks updates by distance/importance
  under a bandwidth budget. Server clusters / zone servers hand off players across world regions (couples
  with Stage 8 world partition).

**Steps:**
1. Transport: integrate a reliable-UDP library (ENet / GameNetworkingSockets / asio per roadmap) behind a
   `Transport` interface.
2. Replication: mark components `Networked` (reflection attribute); delta-serialize changed fields per tick;
   snapshot buffer + interpolation for remotes.
3. Client prediction + server reconciliation built on the existing rollback manager + Jolt determinism.
4. Headless server build target (no renderer/audio) — Linux + Windows.
5. Interest management (AOI grid) + per-client bandwidth-budgeted prioritization.
6. Server-authoritative damage/loot (anti-cheat) per design doc.

**Depends on:** Stage 0.4/0.5 (delta serialize), Stage 1 (ECS world snapshot), Stage 4 (deterministic
physics). **Unblocks:** all multiplayer content. **Acceptance:** 8 players in one zone stay in sync with
prediction; forced packet loss/latency reconciles smoothly; headless server runs the sim on Linux.

---

# STAGE 8 — World Systems (Pillar I)

The open world needs streaming, planet-scale coordinates, and procedural content.

**How it works (theory):**
- **I1 Streaming & world partition:** the world is a grid of **cells**; cells stream **in/out by distance**
  from the camera/players on the job system (async load → GPU upload → activate). World state for unloaded
  cells persists (Stage 0.5 serialization + Stage 7 server authority). Couples with virtual textures/meshes
  (Stage 2.3) so texel/geometry budget stays bounded.
- **I2 LOD:** mesh LOD exists (meshoptimizer). Add **material LOD** (cheaper shaders far away),
  **animation LOD** (lower sample rate / no IK for distant characters), and **HLOD** (merged proxy meshes for
  whole distant cells).
- **I3 Procedural generation:** terrain (heightfield or signed-distance + marching cubes for caves/overhangs
  the design wants), biome assignment (noise + rules), and scatter (vegetation/rocks via Poisson-disk +
  density maps), all cookable and streamable.
- **I4 Large-world coordinates:** floating-point precision breaks past a few km from origin. Use a
  **camera-relative / floating-origin** scheme (rebase the world so the camera is near origin; render in
  camera-relative space) and/or **double-precision world positions** rounded to float per-cell. Critical for
  a *large seamless* world + space/dimension travel in the lore.

**Steps:** (1) cell grid + async streaming on jobs; (2) floating-origin rebasing; (3) material/anim LOD +
HLOD; (4) virtual textures/meshes wired (Stage 2.3); (5) procedural terrain + biomes + scatter; (6) Throne
World instanced dimensions (design) as separate streamed worlds.

**Depends on:** Stage 1, 2, 3 (GPU-driven), 7 (server-side persistence). **Acceptance:** fly across the world
with no hitches or precision jitter; cells stream within budget; one Throne World instance loads as a
separate dimension.

---

# STAGE 9 — AI (Pillar J)

**How it works (theory):**
- **J1 Navigation:** bake a **navmesh** (Recast-style: voxelize collision, build walkable regions, extract a
  polygon mesh); pathfind with **A\*** over nav polys + **string-pulling/funnel** for smooth paths; local
  avoidance via **RVO/ORCA**. **Dynamic updates** re-bake affected tiles when the world changes (destruction,
  streaming).
- **J2 Behavior trees:** a tree of composites (sequence/selector/parallel) + decorators + leaf tasks ticked
  each frame, with a **blackboard** for shared state. Good for scripted enemy/boss logic + the design's
  multi-phase boss movesets.
- **J3 Utility AI:** score candidate actions by weighted considerations (curves over game state); pick the
  highest. Good for emergent NPC behavior / Cyberpunk-style schedules.
- **J4 GOAP:** actions with preconditions/effects + a goal; plan a sequence via backward search. For NPCs
  that solve problems flexibly.
- **J5 Massive simulation (toward O4):** thousands of agents via ECS systems (Stage 1) + LOD'd AI (full BT
  near players, cheap state machines far away) on the job system. Crowd flow fields for 100k+.

**Steps:** (1) Recast/Detour navmesh + A\* + funnel; (2) ECS-driven BT runner + blackboard; (3) utility
scorer; (4) GOAP planner; (5) combat AI (target select → ability select → execute) over the ability system
(Stage 10); (6) crowd/flow-field scaling. **Depends on:** Stage 1, 4 (queries), 5 (anim), 8 (world).
**Acceptance:** enemies path around dynamic obstacles; a boss runs a multi-phase BT; 1k agents update within
budget.

---

# STAGE 10 — Gameplay Systems Integration (game-specific)

Most of these *exist in skeleton form* (`core/character`, `core/ability`, `core/gameplay`, `core/network`)
per the Phase-6 roadmap. This stage hardens them on top of the new foundations and wires them to combat
feel. (Full specs: [`phase-6-planning/`](../phase-6-planning/).)

- **Movement** (Wuthering-Waves feel): the priority state machine (knockback > ability > attack > dodge >
  grapple > air-dash > wall-run > sprint > walk) on the Stage-4 controller + Stage-5 root motion, momentum
  conserved across transitions.
- **Frame-data combat** (Elden Ring): every attack = startup/active/recovery frames; hitboxes (Stage 4.5)
  activate only on active frames; parry/guard/dodge i-frame windows; poise/stagger; multi-hit dedupe. Driven
  by Stage-5 animation events; server-authoritative hit validation (Stage 7).
- **Ability + modifier pipeline** (Destiny 2): data-driven abilities (reflected/serialized assets), the
  Add→Multiply→Set modifier stack, cooldowns/charges, skill trees, aspects/fragments. Server-authoritative.
- **Loot/itemization, stats, progression, vehicles:** data-driven items with rolled perks; stat aggregation
  through the modifier pipeline; vehicles as physics bodies (Stage 4) with their own control states.

**Depends on:** Stages 1, 4, 5, 7. **Acceptance:** a class with 3 abilities + ultimate, frame-data melee,
and a modifier-driven build plays correctly in multiplayer with server authority.

---

# STAGE 11 — Editor & Tooling (Pillar K)

**Current state:** strong (scene/asset-browser/material/gizmo/inspector/hierarchy/undo-redo/profiler).
Expand to cover the new systems.

**How it works:** the editor is an ImGui app over the live ECS world; the inspector is **reflection-driven**
(Stage 0.4) so new components appear automatically. Add: **animation state-machine editor** (graph UI over
the Stage-5 asset), **terrain editor** (sculpt heightfield + paint biomes, Stage 8), **visual scripting**
graph (Stage 12 backend), and richer **debug visualization** (navmesh, AI blackboards, physics, lights,
froxels). Undo/redo already exists — route new edits through it.

**Steps:** anim graph editor → terrain editor → visual-scripting graph → expanded debug viz.
**Depends on:** Stages 0.4, 5, 8, 12, 14. **Acceptance:** author a state machine, sculpt terrain, and inspect
AI/physics entirely in-editor.

---

# STAGE 12 — Scripting (Pillar L)

**How it works:** native C++ (L1) is the engine language. Add a **managed/embedded language** (L2) for
gameplay iteration — recommend **Lua (sol2)** for hot-reloadable gameplay/quests, or **C#** via .NET hosting
if the team prefers (heavier). Bindings are generated from **reflection** (Stage 0.4) so scripts see
components/systems without hand-written glue. **L3 Hot reload:** watch script files, reload the VM module,
re-bind — state lives in ECS (data), not the script, so reload is safe. (Native hot-reload via DLL swap is a
larger, optional effort.)

**Steps:** embed Lua; reflection-driven bindings; per-entity script components; file-watch hot reload; expose
ability/quest/AI hooks to script. **Depends on:** Stage 0.4, 1. **Acceptance:** edit an ability's script and
see it live without restart; scripted quest drives world-state flags.

---

# STAGE 13 — Platform Layer (Pillar M)

**Current state:** Windows-only (`build-msvc`); Vulkan + GLFW are portable. The headless server (Stage 7)
already forces a clean platform split.

**How it works:** a thin **platform abstraction** (window, input, filesystem, threads, time, sockets) with
per-OS backends; the rest of the engine is platform-agnostic. **M1 Windows** (done), **M2 Linux** (server +
desktop, Vulkan native), then **consoles** (M3–M5: NDA SDKs, separate RHI backends behind the existing
`render_device` interface) and **M6 mobile** (Vulkan + reduced feature set) as reach goals.

**Steps:** (1) factor a `Platform` interface; (2) Linux backend (validates the abstraction + serves dedi
servers); (3) console/mobile backends behind RHI later. **Depends on:** Stage 7 (headless). **Acceptance:**
identical sim builds and runs on Windows + Linux; renderer runs on Linux Vulkan.

---

# STAGE 14 — Performance Infrastructure (Pillar N) — **CONTINUOUS**

Not a phase — instrument from Stage 0 onward; this stage *formalizes* the tools.

**How it works:** **N1 CPU profiler** — scoped timers → per-thread ring → flame graph (job spans colored by
tag). **N2 GPU profiler** — timestamp queries per pass (exists) → timeline. **N3 Memory profiler** — the A1
tracker → live-by-tag + fragmentation graph. **N4 Network profiler** — per-channel bandwidth, RTT, packet
loss, rollback depth. **N5 Frame capture** — snapshot one frame's draw/dispatch list + resources for offline
inspection (RenderDoc-style hooks + an in-engine lightweight capture).

**Acceptance:** every stage ships with its hot path visible in one profiler overlay; regressions caught by
captured baselines.

---

# STAGE 15 — Modern AAA Features (Pillar O)

The reach tier, built only on top of everything above.

- **O1 Nanite-like virtual geometry:** cluster meshes into a BVH of meshlet groups with precomputed LODs;
  GPU selects per-cluster LOD by screen-error and software-rasterizes tiny triangles into a visibility
  buffer; material pass shades from the vis-buffer. Builds on Stage 2 (cook) + Stage 3.1 (GPU-driven) +
  Stage 2.3 (streaming geometry pages).
- **O2 Lumen-like GI:** upgrade Stage 3.2 DDGI to a **surface-cache + screen-/world-space radiance probes**
  with RT (or SDF) tracing for fully dynamic diffuse+specular GI with infinite bounces (temporally
  accumulated).
- **O3 Procedural animation:** physics-driven secondary motion, procedural locomotion adaptation (builds on
  Stage 5 IK + Stage 4).
- **O4 Crowd simulation (100k+):** flow fields + LOD'd agents on ECS/jobs (extends Stage 9.5).
- **O5 World simulation:** persistent ecosystems / NPC schedules / world-state evolution (extends Stage 7
  persistence + Stage 9 utility AI).
- **O6 Procedural destruction:** runtime Voronoi fracture + Jolt (extends Stage 4.6).

**Depends on:** essentially all prior stages. **Acceptance:** per-feature, measured against a representative
Project-Schizo zone.

---

## 3. Cross-cutting principles (apply at every stage)

1. **Data-oriented by default** — components are POD, logic is systems over queries, hot loops are SoA and
   job-parallel.
2. **Determinism for anything the netcode touches** — fixed timestep, fixed merge order, no thread-order-
   dependent float accumulation in the sim.
3. **Reflection is the single source of truth** — editor UI, serialization, and network replication all
   derive from it; never hand-maintain three copies of a field list.
4. **No raw allocation on the hot path** — frame/pool/arena allocators (Stage 0.1) only.
5. **Instrument as you build** (Stage 14) — a feature without a profiler span is unfinished.
6. **RHI/engine/render separation** — gameplay never calls Vulkan; the headless server proves the seam.
7. **Cook, don't parse at runtime** — runtime loads cooked binary (Stage 2); source formats live in the
   editor/cooker only.

## 4. Suggested sequencing & parallelism

- **Critical path (serial):** 0.1 → 0.3 (jobs) → 0.4 (reflection) → 0.5 (serialization) → Stage 1 (ECS) →
  Stage 4 (physics) → Stage 7 (networking).
- **Parallel tracks once Stage 1 lands:** Rendering (Stage 3) and Asset pipeline (Stage 2) can proceed
  independently of physics/animation. Audio (Stage 6) is largely independent after Stage 1. Editor (Stage 11)
  and Profiler (Stage 14) grow continuously.
- **Do not start** gameplay hardening (Stage 10), world streaming (Stage 8), or AI (Stage 9) before
  ECS + physics + networking are stable — they all consume that state.

---

*Document version 1.0 — 2026-06-12. Maps Pillars A–O onto the live codebase in dependency order.*
*Update the current-state table (§1) as stages land; keep the dependency graph (§2) authoritative for order.*
