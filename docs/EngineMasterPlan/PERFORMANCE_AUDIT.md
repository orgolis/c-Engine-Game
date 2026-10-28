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

## F14 — the core systems' cost per entity, measured at scale 🔴 (in progress)

**Measured 2026-10-28, `MyGame` plus `--stress-entities N`** (N primitives added in memory, never saved;
a grid in front of the camera, one in four parented, varied rotation and scale).

A 30-draw test scene cannot answer how the CPU side scales. Every system that walks the scene costs
microseconds there, which earlier read as "not worth optimising". At 20k entities the frame is
**103 ms and entirely CPU-bound** — the GPU fence wait drops to 0.009 ms:

| Zone | 1k | 5k | 20k |
|---|---|---|---|
| `ecs_sync` | 1.76 | 7.5 | **32.6 ms** |
| `build_draw_items` | 0.74 | 3.2 | **13.1 ms** |
| `rt_scene_update` | 0.76 | 2.3 | **8.9 ms** |
| `ui_hierarchy` | 0.41 | 1.9 | **8.2 ms** |
| `stage_geometry` | 0.17 | 0.8 | 5.4 ms |
| `draw_sort` | 0.10 | 0.4 | 2.7 ms |
| *(between zones)* | | | ~31 ms |

All linear, all with large per-entity constants. The order of work is: remove the waste in each
system, then spread what remains across the job system. Parallelising waste only distributes it.

**Correction to an earlier claim.** The job system is not "almost entirely idle": ECS render
collection, the system scheduler, transform propagation, frustum culling (above 2,048 items) and the
HZB AABB build all use it. The earlier count looked only at `editor/src/main.cpp`.

### (a) `GetComponent<T>()` was an RTTI scan — fixed

Every component lookup did `dynamic_pointer_cast` over the entity's component list. Under MinGW
GCC 13 a failed `dynamic_cast` costs ~25 ns (the type comparison can fall back to comparing mangled
names), and the ECS mirror alone asks every entity for ~10 kinds per frame, mostly ones it lacks:
**15.3 ms per frame at 20k entities**, before draw building asked the same questions again.

`AddComponent<T>` now records the exact type (a name-based hash plus `type_info`), and for a `final`
T `GetComponent` compares that key instead — for a final type, "dynamic_cast succeeds" and "is
exactly a T" are the same statement. Non-final T keeps the original scan, unchanged. All 18 leaf
components are now `final`; a future subclass means removing `final`, which falls back to correct and
slower rather than silently missing components.

| | Before | After |
|---|---|---|
| one miss, 2-component entity | 82–104 ns | **5–9 ns** |
| `sync_scene_components` @ 20k | 15.3 ms | **6.3 ms** |
| `ecs_sync` @ 20k | 33.3 ms | **21.0 ms** |

`componentlookup_check` asserts agreement with the dynamic_cast scan for every kind (first-found,
duplicates, removal, the non-final path) and that every leaf component is final. Mutation-tested:
dropping one `final` fails 1 assertion; recording the wrong type key fails 12.

*Benchmark trap worth recording:* inlined into a timing loop, the scan measured 0.2 ns and briefly
suggested `final` alone made dynamic_cast cheap. It does not under GCC 13 (~49 ns final or not,
measured out of line).

### (b) The ECS mirror rebuilt its bookkeeping every frame — fixed

`sync_and_run` kept three maps keyed by `Transform*`: entity identity (persistent), an index map
rebuilt from scratch every sync, and the world-matrix map rendering reads, cleared and refilled every
sync — 40k node allocations and frees per frame at 20k entities to describe an unchanged scene. The
hierarchy resolve allocated three scene-sized vectors per sync plus a fresh vector *per entity*. And
a verification pass recomputed every entity's world matrix from the scene, every frame, in release.

Now one persistent slot per transform holds identity, this sync's index, a last-seen stamp and the
world matrix. A steady-state sync does one lookup per entity and allocates nothing; the table is only
walked for removals when there are more slots than active transforms. Scratch arrays persist.
Verification runs in full whenever the set changes (so the editor's one-time "N/N match" log still
means all of them) and otherwise on a rotating 1/32 slice — every entity still checked every 32
frames.

| Pass @ 20k | Before | After |
|---|---|---|
| `sync.verify` | 2.53 ms | **0.08 ms** |
| `sync.publish` | 1.95 ms | **0.81 ms** |
| `sync.hierarchy` | 1.93 ms | **0.53 ms** |
| `sync.clear` | 1.06 ms | **0 ms** |
| `sync.entities` | 10.09 ms | **7.92 ms** |
| `sync.parents` | 0.85 ms | 1.55 ms — *worse*: the long-lived table's nodes are larger and scattered |
| **`ecs_sync`** | **21.0 ms** | **12.7 ms** (33.3 ms before (a)) |

`ecssync_check` covers what a persistent table can get quietly wrong: a removed or deactivated entity
still answered for, a remove-and-add in one frame (counts equal), identity across syncs, a moved
parent, and sampled verification that still covers everything. Mutation-tested: disabling the stale
sweep fails 6, always sampling fails 2.

**What remains in the mirror** is ~6 ms of `sync_scene_components`: every component of every entity
copied every frame whether or not it changed. Removing that needs change tracking on the scene side
(a version counter per component), which is a design change rather than a cleanup.

### (c) Edit mode updated every entity up to six times a frame, to do nothing — fixed

Found by a new `[frame-cp]` debug line that prints every gap between the frame's checkpoints, next to
the `[profiler]` zones: ~6 ms at 20k entities sat between `pre_input` and `pre_scripts` with no zone
at all. It was edit mode's `scene->Update()`.

`Scene::Update` walked every entity once per fixed step (3–4 at that frame rate, more the slower the
frame), then for Update, then LateUpdate, then a destroyed-entity sweep. Each visit was a cache miss on
a large `Entity` (it embeds four component structs by value), a recursive parent-chain walk in
`IsActiveInHierarchy`, and a virtual call per component — into an empty default almost every time.
Only six component classes override any hook; **none overrides `OnFixedUpdate`**.

`AddComponent<T>` now records which hooks T overrides, decided at compile time (`&T::OnUpdate` has
type `void (Component::*)(float)` exactly when T runs the empty default). The scene keeps a list of
entities per hook, rebuilt only when the entity list or any component set changes — tracked by a
version and a process-wide generation counter, since an entity does not know its scene — and each
pass visits only its list. The destroyed-entity sweep runs only after a `Destroy()`.

| @ 20k entities | Before | After |
|---|---|---|
| `pre_input → pre_scripts` | 5.7–7.2 ms | **0.007 ms** |
| `scene_update` (new zone) | — | **0.002 ms** |
| lists rebuilt during the run | — | **1** (2 entities have OnUpdate) |
| frame | 64.4 ms | 58.2 ms |

`scenehooks_check` asserts the compile-time detection, that each hook runs where and only where it did,
that additions, removals, `Destroy()` and active state are honoured, and that an unchanged scene does
not rebuild its lists. Mutation-tested: a never-refreshing cache fails 3, a rebuild-every-time cache
fails 1, detection that marks everything fails to compile.

### (d) The Scene Hierarchy panel built every row, every frame — fixed

Each row was a label string assembled from name and id, a tree node, a drag source, a drop target and a
context-menu check — for all 20k entities, with a couple of dozen on screen. `ImGuiListClipper` needs a
flat list and this is a tree whose open state lives in ImGui, so it is clipped by hand: an off-screen
row only adds its height to a pending skip, emitted as one spacer when the next visible row comes or
the list ends, so the scrollbar still describes the whole tree. An off-screen *open* node still pushes
its id, so its children keep the same ids and stored open/closed state.

`ui_hierarchy` @ 20k: **8.5 → 1.36 ms**. Screenshot A/B against the previous build: **0 of 252,540
pixels differ** in the panel, scrollbar thumb included. The remaining ~1.4 ms is walking 20k entities
to keep ids and open state right; a cached flattened list would remove it. No check: the panel needs
a live ImGui frame and the editor's state; the evidence is the pixel diff. Verified at the top of the
list only — the skip arithmetic is the same at any scroll offset, but that was not screenshotted.

### (e) Draw building: allocation churn fixed; the rest is memory-bound

**Fixed: the draw lists were rebuilt from zero capacity every frame.** `opaque_draws` and
`transparent_draws` were locals of the frame loop — at 20k entities, 2 MB of ~104-byte items regrown
through ~15 reallocations per frame. `draw_sort` then allocated three more scene-sized vectors and
swapped a brand-new 2 MB buffer into the list, so a persistent list alone would have saved nothing.
Both lists and the sort scratch now persist. `build_draw_items` 9.24 → 7.99 ms, `draw_sort` 2.03 →
1.90 ms.

**Made faster, effect in-frame not resolved: the material content hash.** Every primitive hashes
its `MaterialDesc` every frame to find the GPU material it found last frame. The hash was byte-at-a-
time FNV-1a; it is now word-at-a-time with a final avalanche. Out-of-line microbenchmark: **82 → 32 ns**
(no maps), **115 → 37 ns** (one map). In the editor at 20k an interleaved A/B could not separate it
from run-to-run noise (two valid rounds: 8.05 / 8.98 ms vs 8.08 / 7.38 ms; a third round was lost to
a minimised window). The likely reason is the finding below: the loop waits on memory, and the CPU
overlaps hash arithmetic with those waits. `material_check` now probes every float component
individually and a swapped pair, since the hash packs floats two to a word — mutation-tested:
dropping one half of a word is caught only by the new assertions.

**The finding that sets the next step: draw building is memory-bound.** A split with in-loop timers
put ~6 ms of the ~8 ms before material resolution even starts — `IsActiveInHierarchy`, the
world-matrix lookup, two component lookups. That is not computation; it is cache misses on large
`Entity` objects (four component structs embedded by value), on each component the lookup inspects,
and on the matrix table. Per-entity work that waits on memory scales with the cores issuing the
requests, which is the job system's case. (In-loop timers at this granularity add more than they
measure — six clock reads per entity — so these splits are directional, not exact.)

### (f) Draw building on the job system — done

`build_draw_items` is now two phases. **Phase one runs on the job system** (above 1,024 entities) and
only reads: active check, the ECS world matrix, component lookups, primitive mesh, and the inline
material's content hash. **Phase two runs on the main thread in scene order** and does everything
that touches a cache — materials (resolved by the key phase one computed, with the last hit
remembered), mesh assets, terrain — none of which is thread-safe. Visiting entities in the same order
as the old single loop keeps the output identical.

One race had to be designed out rather than locked around: `Transform::GetWorldMatrix()` recomputes
into a `mutable` cache when dirty, so two workers computing a shared dirty parent would race. Phase
one never calls it — it takes only the matrix the ECS mirror already computed, and an entity created
after this frame's sync gets its OOP matrix in phase two.

Interleaved A/B, two rounds, 20k entities on a 6-core / 12-thread Ryzen 5 4500:

| | Serial | Two-phase |
|---|---|---|
| `build_draw_items` | 7.28 / 6.93 ms | **1.90 / 1.80 ms** (3.8×) |
| frame | 44.4 / 45.2 ms | **38.5 / 38.4 ms** |
| draws / triangles | 14,851 / 5,365,539 | identical |
| viewport pixels vs serial | — | **0 changed** (noise floor between serial runs: 0) |

No check accompanies it: the function needs a live device for its material cache. The evidence is
the identical counts and pixels, and phase one being read-only by construction.

### (g) The TLAS instance loop read from GPU-mapped memory — fixed

`rt_scene_update` was ~9 ms at 20k entities. Zones inside it put 8.06 ms in `rebuild_tlas`, 0.33 ms
in BLAS checks and 0.18 ms in the per-instance shading data — so not the two
`vkGetBufferDeviceAddress` calls per instance that looked like the obvious suspect.

`rebuild_tlas` assigned each `VkAccelerationStructureInstanceKHR` field straight into the mapped,
host-coherent instance buffer, which is uncached or write-combined memory. Four of those fields are
**bitfields**, and assigning a bitfield is a read-modify-write — so every instance **read from
GPU-mapped memory** several times, each read a round trip write-combining cannot hide. The shading-
data loop next to it builds a local struct and stores it whole, which is why it took 0.18 ms for the
same count. Now the instance is built locally and stored with one `memcpy`: stores only.

| @ 20k entities | Before | After |
|---|---|---|
| `rt.rebuild_tlas` | 8.06 ms | **0.41 ms** (20×) |
| `rt_scene_update` | 8.57 ms | **0.90 ms** |
| frame | 41.4 ms | **31.4 ms** |

Screenshot A/B against the previous build: 1 viewport pixel differs by 1 level (frame times differ,
and auto-exposure steps by frame time). A sweep found no other instance writer and no in-place
modification of a mapped pointer. **No check:** a mutable reference into a mapped buffer is not
syntactically distinguishable from one into a CPU array (the one candidate pattern matched only a CPU
array). The robust guard is a write-only wrapper type for mapped pointers — a broader change, noted
as a recommendation.

**Open question raised while reading this code (not fixed):** the TLAS, its instance buffer and the
instance-data buffer are each single-buffered and rewritten every frame, with two frames in flight. If
the GPU is still executing the previous frame's TLAS build or ray queries when the CPU overwrites
them, that is a CPU/GPU race. It has not been observed; it needs synchronization validation before
anyone claims either way.

### (h) Whole-scene scans to find a few entities — lights and water indexed, a quadratic removed

With the named systems fixed, five new checkpoints (`pre_ecs`, `pre_build`, `pre_record`,
`pre_stages`, `pre_post`) located ~8.6 ms at 20k entities that no zone covered. One block,
`pre_skinning → pre_ecs`, was 2.1 ms with no zone at all. The frame walks the whole scene **six times**
to find a handful of entities: three times for skinned meshes, once for lights (~1.8 ms — two lights
in this scene), once for water, and once **per skinned actor** to check that actor's entity still
exists (actors × entities per frame — a quadratic waiting for the first scene with several characters).

- `Scene::EntitiesWithAny<Ts...>()`: entities having any of the given component types, in scene
  order, cached per type set and rebuilt only when the entity list or a component set changes — the
  same signals as the per-hook lists. Lights use it; water uses the union `<Water, Terrain>`, which
  keeps the original per-entity order.
- The actor prune uses the scene's existing id map: one lookup per actor.
- The three skinned-mesh scans remain (~0.2 ms each): `SkinnedMeshComponent` is a struct embedded in
  `Entity`, not a `Component`, so no change signal exists to cache against.

`pre_skinning → pre_ecs` @ 20k: **2.1 → 0.68 ms**. Screenshot A/B, two clean reruns per binary:
0–1 pixel difference (noise floor). A first run differed across the whole viewport — its draw count
was different too (15,627 vs 14,851): the editor camera had been moved by outside input while the
window was up, not by this change, as the reruns showed.

`scenehooks_check` gains the index: correct set in scene order, a union listed once and in order,
additions and removals of components and entities, and no rebuild when unchanged. Mutation-tested:
a never-refreshing index fails 4, a rebuild-every-query index fails 1.

### (i) More per-frame copies, two audio scans

`VulkanRenderGraph::set_draw_items` took its vector **by value** — an allocation, a 1.5 MB copy and a
free per frame. `geometry_draws` and `shadow_draws` were fresh locals every frame. `EditorAudioDriver`
walked the whole scene twice a frame (listener, sources). Now: a const-reference copy into retained
capacity, persistent lists, and `EntitiesWithAny` for audio (scene order kept, so the first active
listener is unchanged). `pre_build → pre_record` @ 20k: 7.6 / 7.0 → 6.5 / 5.0 ms.

### (j) The ECS mirror on the job system — done

The mirror's per-entity pass (`sync.entities`, ~6.5 ms @ 20k) is memory-bound like draw building, but
it *writes* into the ECS, and adding components is not thread-safe. The observation that made it
parallel: in a scene that is not changing shape, every entity already has every ECS component it
mirrors, and overwriting an existing component is a write to that entity's slot and nothing else.

- The pools are fetched once on the calling thread (`registry::storage<T>()` creates a missing pool,
  which is a mutation); workers only call `contains()` and `get()`.
- A worker builds each component's fresh value — the same thing `World::add` assigns for an existing
  component; no update listeners exist anywhere to skip — and writes it in place.
- Anything else — an `EcsAuthoritative` entity, a component the ECS does not have yet, a
  MeshRenderer to remove — falls back to the original serial code for that entity.
- Each kind's scene→ECS conversion is one `to_ecs()` function used by both paths, so they cannot
  disagree about a field.

Interleaved A/B, two clean rounds (identical draw counts):

| @ 20k | Before | After |
|---|---|---|
| `sync.entities` | 6.54 / 6.31 ms | **1.43 / 1.50 ms** (4.4×) |
| `ecs_sync` | 10.8 / 11.0 ms | **4.8 / 5.5 ms** |
| frame | 25.7 / 26.4 ms | **20.1 / 19.7 ms** |

`ecssync_check` gains a 3,000-entity scene with the job system running — the only scene in any check
large enough to take the new path: steady-state edits to transforms, lights and colliders all arrive;
no entity gains a component it lacks; a component added to an existing entity and a MeshRenderer
removal both take the fallback; an authoritative entity's ECS write survives. Mutation-tested: the
in-place path skipping lights fails 2, a missing component treated as handled fails 1, ignoring the
authoritative tag fails 1.

Screenshots: two serial runs 0 px apart, two parallel runs 13 px apart, serial vs parallel ~470 px —
all 1 level, mixed sign, confined to the upper viewport. This was first written up as "likely a
time-based effect at a different frame rate — an inference". **Since measured (see (k)): with
`GWS_FIXED_DT`, serial and parallel mirror are 0 px apart.** The difference was frame timing.

### (k) `draw_sort` as a radix sort; the geometry stage's list copy; exact screenshot A/Bs

**`draw_sort`.** `std::sort` compared float depths through an index indirection — ~1 ms of the sort's
~1.8 ms at 15k draws. `front_to_back_order` (`draw_order.h`, Vulkan-free) maps each depth to a
32-bit key whose unsigned order is its float order (negatives inverted, `-0.0` normalised to tie
with `+0.0`), runs a four-pass LSD radix sort, then sorts only runs of exactly equal depth by
material — the original tie-break, exactly. Depths are computed on the job system above 2,048 draws.
`draworder_check` compares the order element by element with the old `std::sort` on negatives, signed
zeros, mass ties and 20k random depths; mutation-tested (no sign inversion fails 4, no tie pass fails
4, no `-0.0` normalisation fails 2). Out of line: radix 0.19 ms vs `std::sort` 1.06 ms.

**Geometry stage.** `record_geometry` copied the whole draw list into a fresh local before culling:
`items_to_draw = draw_items_`, 1.5 MB allocated per frame. Now a persistent scratch member. Zones
showed the stage as 0.71 ms copy+cull and 2.85 ms recording (4,150 material and 4,174 mesh rebinds
across 14,859 draws).

| @ 20k, interleaved, two clean rounds | Before | After |
|---|---|---|
| `draw_sort` | 1.60 / 1.94 ms | **1.07 / 1.05 ms** |
| `stage_geometry` | 3.99 / 3.89 ms | **3.32 / 3.38 ms** |
| frame | 20.8 / 22.1 ms | **19.5 / 20.0 ms** |

**`GWS_FIXED_DT=<seconds>` — exact screenshot A/Bs.** Every performance change here produced the same
pattern: same-binary runs 0–20 px apart, before-vs-after a few hundred px, all 1 level. Two builds at
different speeds reach frame N at different simulated times, so anything animated differs. With
`GWS_FIXED_DT`, every frame advances simulation time by a fixed step, and frame N is the same instant
in both builds. Measured: this change, **0 px**; the parallel ECS mirror (j), **0 px**. Rendering
passes read no clocks of their own (checked), so the fixed step covers them. `[health]` fps is
meaningless while it is set, and the editor says so at startup.

**Still the big one in this stage: 2.85 ms of draw recording.** The fix is instancing — one draw per
(mesh, material, LOD) with per-instance matrices from a buffer — which touches shaders, the scene
pipeline layout and per-frame instance buffers, and pairs with a state-aware sort. Not done yet.

### (l) The ECS mirror's bookkeeping, skipped when nothing changed shape

After (b) and (j), `ecs_sync` was still ~6 ms @ 20k, spread over passes that each touch every entity:
`reconcile` (a slot lookup per entity), `parents` (re-reading each transform's parent pointer and
looking up the parent's slot), `publish` (two scattered writes per entity).

- **`reconcile`:** if this sync gathered exactly the same transforms in the same order as the last,
  every slot pointer, index and entity from the last sync is still right — slots are erased only by
  the sweep, which never erases a slot the last sync used — so the pass is skipped. Stamps go stale
  while nothing changes, which is safe: the next sync that changes something stamps every active
  slot, so anything stale still sweeps.
- **`parents`:** the parent pointer is read during the gather (the entity is in cache then). With the
  same list, an entity whose parent pointer is unchanged keeps last sync's parent index and
  `ShadowParent`; only a re-parented entity is resolved again.
- **`publish`:** on the job system above 1,024 entities (each write is to that entity's own element and
  slot; the pool is fetched once).

| @ 20k (fixed dt; one baseline lost to a minimised window) | Before | After |
|---|---|---|
| `sync.parents` | 1.44 ms | **0.03 ms** |
| `sync.reconcile` | 0.75 ms | **0.015 ms** |
| `sync.publish` | 0.74 ms | **0.26 ms** |
| `sync.gather` | 0.40 ms | 0.49–0.58 ms (now also reads parents) |
| **`ecs_sync`** | **6.03 ms** | **3.09 / 3.45 ms** |

Screenshots with `GWS_FIXED_DT`: **0 px**. `ecssync_check` gains a re-parent under an unchanged list,
the child then following its new parent, a same-set reorder (no create/destroy, identities and
matrices right), and world-matrix agreement for all 3,000 entities in the large scene (the parallel
publish). Mutation-tested: reusing parents without comparing fails 2; dropping the slot write in
publish fails 8; treating equal list sizes as the same list fails 4.

**A hang found by that last mutation.** It initially did not fail — it hung: stale indices formed a
parent cycle, and the hierarchy resolve walked up parent chains with no bound. A real cycle would
hang the editor the same way. The walk is now bounded by the entity count; a cycle is resolved as a
root with a one-time warning, so a corrupted hierarchy shows up as wrong matrices and a log line,
not a frozen editor.

### (m) The asset browser asked the file system for its breadcrumbs every frame — fixed

On plain MyGame, `ui_assetbrowser` was the single largest CPU zone: **~0.65 ms every frame, in every
scene** — with the panel's tab not even visible (it ignores `ImGui::Begin`'s return). Zones inside it
put the whole cost in the toolbar/breadcrumb row, where `render_breadcrumbs` called `fs::relative()`
every frame. That is not string arithmetic: it canonicalises both paths through the file system.

The breadcrumbs (`compute_breadcrumbs`, `editor/include/asset_browser_paths.h`) are now computed
once per folder change, lexically — both sides `lexically_normal()`ed, which removes `.` and `..`
as `fs::relative` did, without asking the disk. `ui_assetbrowser`: **0.652 → 0.010 ms**.
`assetbrowserpaths_check` compares against the old `fs::relative` algorithm on real directories,
including `.` and `..` paths and a folder no root contains; mutation-tested (dropping the
normalisation fails 2).

My first guess was the folder tree, which also re-listed directories from disk every frame for every
open node. It is now cached too (cleared with the file listing and every 2 s, so a folder made in
Explorer still appears), but it was not where this time went — the measurement after that change
was unchanged. Recorded because the guess was plausible and wrong.

**Measurement runs no longer appear on the desktop.** `--frames` windows open without focus and just
left of the primary monitor: still a real, restored window with its full framebuffer, rendering and
screenshotting as before. They used to open over whatever the person at the machine was doing; a
minimised one stalls the run and a clicked one moves the camera, and both happened repeatedly.

### (n) HZB hysteresis without per-frame allocation; the filtered list handed over, not copied

`hzb_cull` split into AABB build 0.24, pyramid test 0.21 and **filter 0.88 ms** @ 20k. The filter keeps
a 2-frame occlusion hysteresis keyed per draw, in an `unordered_map` rebuilt every frame — a node
allocation per occluded draw (3,683 here), freed again by the swap. It is now two persistent sorted
vectors with binary-search lookups; duplicates of a key always carry the same streak, so dropping them
reproduces the map's overwrite exactly. And the filtered list was copied twice — built into
`geometry_draws`, then copied by `set_draw_items` — though nothing reads it afterwards: it is now
swapped into the graph (`swap_draw_items`), O(1), both vectors keeping their capacity.

| @ 20k, fixed dt, two clean rounds | Before | After |
|---|---|---|
| `hzb_cull` | 1.41 / 1.53 ms | **1.18 / 1.15 ms** |
| frame | 20.6 / 20.2 ms | **18.7 / 19.1 ms** |

Screenshots **0 px** between every pair, draw counts identical.

**Measurement runs can no longer receive input.** The first attempt at this A/B was unusable: draw
counts differed even between two baseline runs, and one screenshot showed a different dock tab. The
off-screen window from (m) still got keyboard focus whenever Windows handed it on — when another
window closed — and typing then drove the editor camera. `--frames` windows are now `WS_EX_NOACTIVATE`
on Windows: never the foreground window, never anyone's keystrokes. Every run since: identical draw
counts. This very likely explains several "camera moved by outside input" runs earlier in F14 as
well — and those runs also saved MyGame's `editor.ini`, so its dock layout may have changed.

### (o) Instanced G-buffer draws — 14,851 draw calls → 45

Draws sharing a mesh, material, submesh, LOD and cull mode now go out as one `vkCmdDrawIndexed` with
`instanceCount = N`. Each instance's `{mvp, model}` — the same 128 bytes the push constants carry,
computed on the CPU exactly as before — sits in a storage buffer at set 2;
`tools/gbuffer_scene_instanced.vert` reads it at `gl_InstanceIndex` and does exactly the math of
`gbuffer_scene.vert`. A ring of three buffers (one per frame; the slot written was last read three
frames ago, which is also what makes growing it in place safe). The instanced layout matches the
scene layout for sets 0–1 and the push range, so bound sets survive a pipeline switch.

Exactness rules: terrain, material-graph and occlusion-query draws keep the per-object path. The
per-meshlet path is kept only where it would cull something: LOD 0's indices are its meshlets in
order, so when every meshlet passes the meshlet path's own frustum test, a whole-submesh draw is the
same triangles in the same order. Groups are formed with a stable counting sort, so instances stay
front to back. Instance data is stored whole into mapped memory (no reads — see (g)).

| @ 20k, fixed dt, interleaved | Per object | Instanced |
|---|---|---|
| draw calls | 14,851 | **45** (triangles identical: 5,365,539) |
| `stage_geometry` (CPU) | 3.63 / 4.41 ms | **1.97 / 1.84 ms** |
| GPU geometry pass | 4.84 / 2.74 ms | **3.08 / 2.46 ms** (clock refs matched per round) |

**Image:** an instanced frame and a per-object frame are **pixel-identical (0 px)** — measured on
the 20k scene and on plain MyGame. Validation layers: the instanced path adds **no** VUID (the set is
identical with `GWS_NO_INSTANCING=1`). `GWS_NO_INSTANCING=1` switches it off at runtime: an A/B in one
binary, and a way out if a driver disagrees. `shadersource_check` pins the new shader's SPIR-V.

**No automated check.** The path needs a device; a GPU check that renders the G-buffer both ways and
compares readbacks is possible with the existing headless-device harness, but is new harness code —
not done. The evidence is the cross-mode 0-px match and the validation diff.

### Open findings recorded while doing (o)

- **Rendering is not deterministic when the GPU is the bottleneck.** With `GWS_FIXED_DT`, same-binary
  runs matched exactly while frames were CPU-bound; in a GPU-bound state the same build gives
  ~400–2,000 px of difference between runs (stochastic water/reflection noise, a few object edges),
  in both instancing modes. Something reads GPU results with timing-dependent latency. Suspects: the
  single-buffered TLAS / instance data (see (g)), HZB readback timing, occlusion queries.
- **The validation layers already report 12 distinct VUIDs** before any of this work — including the
  known semaphore reuse (`VUID-vkQueueSubmit-pSignalSemaphores-00067`), image-layout and view-type
  mismatches on draws/dispatches/copies, a fragment-stage storage write without
  `fragmentStoresAndAtomics`, and a colour-blend attachment-count mismatch at pipeline creation.
- **`VulkanGBuffer::resize()` would double-destroy every handle**: the defaulted move copies raw
  handles without clearing the source, and the moved-from object's destructor runs `cleanup()`.
  Nothing in the editor appears to call it today.

### (p) The curved primitives get LOD tiers — 5.37M triangles → 1.43M

With CPU work down to ~17 ms the 20k frame is GPU-bound, and the geometry pass was drawing **5.37M
triangles, 5.1M of them spheres**: a sphere was 1,024 triangles at any distance, and nearly all of
the ~5,000 covered a few dozen pixels. Primitives had exactly one LOD.

Each curved primitive now has two lower tiers from **its own generator** (so every tier is exactly
round, with exact normals and UVs — decimating LOD 0 would only approximate that): sphere 16×32 →
8×16 → 4×8, cylinder 24 → 12 → 6, capsule 24×8 → 12×4 → 6×2. A tier switches in where its deviation
from LOD 0 projects to **under half a pixel** at a conservative view (1440 px tall at 45° FOV, 1,738
px per unit at 1 m). The thresholds are in units of the mesh's **own size**: these meshes are
`lod_error_bounded`, so the camera distance is divided by the draw's largest axis scale and a sphere
scaled ×10 keeps its detail ten times further out. glTF meshes keep world-unit thresholds, unchanged.
Switch distances: sphere 85.5 / 314.9 sizes, cylinder 76.2 / 283.7, capsule 119.7 / 457.4.

Two things the first version got wrong, both caught before commit:

1. **The error formula.** It took the larger of the two directions' sagittas. A UV quad's deepest
   point is near its centre, where both curvatures add: `r (1 − cos(π/a) cos(π/m))`. The first
   version underestimated that by up to 2×, so tiers would have switched in at about half the safe
   distance. Found by measuring every tier's real deviation (sampled triangles).
2. **Ray tracing would have made coarse spheres shadow themselves.** The BLAS is built from LOD 0
   only (unchanged), but shadow and AO rays start at the *rasterized* surface. An inscribed coarse
   tier's face centres are up to **0.068 × size inside the BLAS**, beyond the shadow ray's 0.02-unit
   offset, so the ray hits the object's own inner wall. A CPU model of the lighting pass's rays (same
   origin offsets, same epsilon, no culling) found **873 of 3,692 lit shadow rays (24%)** from an
   inscribed 4×8 sphere hitting its own LOD 0 at scale 1. AO is worse: 5,376 self-hits in the same
   setup. The 20k test scene could not show this: it is lit by the sky under cloud shadow, with no
   sharp sun shadows anywhere. A temporary build that forced tier 2 everywhere looked clean for that
   reason. A sunny scene would have shown it.

   **Fix: the lower tiers enclose the surface.** They are pushed out by `1 / (cos(π/a) cos(π/m))`
   until their flattest face touches it. Every ray start is then on or outside the ideal surface and
   LOD 0 lies inside it, so a ray leaving on the normal side only gets further away and **cannot hit
   its own object** — by construction, at any scale. Normals stay exactly radial (all of a tier's
   vertices are equidistant from its centre or axis). LOD 0 is the mesh it always was.

| @ 20k, fixed dt, interleaved | LOD 0 only (`GWS_NO_PRIMITIVE_LOD=1`) | LOD tiers |
|---|---|---|
| triangles | 5,365,539 | **1,427,011** (−73%) |
| draw calls | 45 | 39 |
| GPU geometry pass, median of 18 reports, two rounds | 2.65 / 2.54 ms | **1.40 / 1.23 ms** |
| `stage_geometry` (CPU), three rounds | 1.70 / 2.03 / 1.84 ms | 1.11 / 1.40 / 1.46 ms |

**The GPU total could not be resolved on this machine.** Per-run totals follow the GPU clock state:
the GTAO clock reference differed by −13% to +6% between paired runs, and the two long rounds moved
the total in opposite directions (18.52 → 19.98 ms, 19.60 → 17.12 ms). The geometry pass is lower in
every paired report, about 70 in all, including after clock normalisation. The render settings had
VSync on, which lets the driver downclock into the vblank slack, so I re-measured with `VSYNC=0` (two
more 1,200-frame rounds). Same picture: the geometry pass is lower in 35 of 36 samples paired by frame
index, and the total moved −0.3 ms in one round and −2.9 ms in the other. Lighting's ±1.5 ms swings do
not correlate between runs at the same simulated instant (r = −0.5 … +0.06), so they are run state
(clocks, other GPU clients preempting the longest pass), not scene content.

**Image:** cross-mode differences (≥8 levels: 122–428 px) are at this session's same-mode run-to-run
noise (100–304 px; see the GPU-bound nondeterminism below). They sit on distant silhouettes along the
horizon band, where sub-pixel geometry changes are expected.

`primitivelod_check` (98 assertions) checks the cache's real tier table:
- every tier's measured deviation (signed distance over sampled triangles) is bounded by its formula,
  and the formula is within 1.5× of it;
- LOD 0 is inscribed and the lower tiers enclose the surface; normals are radial;
- triangle counts descend, switch distances ascend, and each switch is ≤ 0.5 px;
- the lighting pass's shadow and AO rays from every tier, at scales 1/4/16, never hit their own LOD 0,
  and an inscribed tier does (the bug above);
- the six `lighting_pass.frag` lines the ray model mirrors are pinned;
- `lod_distance` is scale-aware, and moving a `Mesh` keeps it so.

Mutation-tested: all 16 header mutations are caught (inscribed tiers, a dropped meridian term, a
wrong capsule factor, a mis-centred cylinder, an under-reported error, tilted normals, the old
formula, …), and so is dropping the flag from the move constructor.

Also fixed on the way: `Mesh`'s move constructor and move assignment dropped `double_sided_` and
`bounding_box_` (and would have dropped the new flag). Nothing moves a `Mesh` by value today. Mesh
bounds are computed from all vertices, so these three primitives' culling bounds now include the
enclosing tiers — up to 25% larger for the capsule. That is conservative: it can never cull wrongly.

**The shadow-map pass keeps the primitives' G-buffer tier.** It normally draws one tier coarser
(`shadow_lod_for`), and terrain is already exempt. The same story applies here: the bias was harmless
while primitives had one tier and became a bug once they gained more. These tiers switch in exactly
at the half-pixel limit, so one tier coarser is past it. With RT off, a near sphere scaled ×2 would
cast a 16-gon shadow whose facets exceed a 2.9 cm shadow texel. It never costs more than before:
primitives used to cast LOD 0 at every distance. Both behaviours hang off one flag,
`Mesh::lod_error_bounded` ("each threshold is where this tier's error reaches half a pixel, in the
mesh's own size"). The shadow call site is GPU code with no automated check; the check asserts that
the flag survives moves.

**Open:** glTF meshes with meshoptimizer LOD tiers have the same raster-vs-BLAS mismatch, since a
decimated tier lies partly inside LOD 0. The self-shadowing risk applies to them. The general fix is
per-instance LOD selection in the TLAS (one BLAS per tier); not done.

---

## F13 — soft shadows traced the whole cone for pixels that did not need it 🔴 → fixed

**Measured 2026-10-28, RTX 3060, `MyGame`, 1920×1032, ray tracing on, 8 shadow rays (the High preset).**
Interleaved A/B of the old and new binary, three rounds each; `AO.GTAO` stayed at ~0.92 ms in both as a
GPU clock reference, so the comparison is within one clock state.

| | Lighting | GPU total |
|---|---|---|
| Before | 8.8–11.2 ms (median ~10.1) | 12.3–16.3 ms — over the 13.33 ms refresh budget |
| After | **4.5–5.8 ms (median ~5.45)** | **8.1–10.2 ms** — back under it |

**~4.6 ms saved, 46% of the lighting pass**, against a theoretical ceiling of 50%.

**The defect.** `rayQueryShadow()` cast all N cone rays for every shadowed pixel. Only the penumbra needs
them. A pixel fully in light or fully in shadow spent eight rays to learn what the first few already
said, and on most screens that is most pixels. F0 found that these rays dominate the frame; the response
so far was to ration them through presets (High = 8, first launch = Medium with RT off). That treats
the count as the problem when the design is the problem.

**The fix.** Trace the even-indexed half of the existing golden-angle spiral first. Those rays span the
disc (radii 0.25–0.90 of the cone at N=8, innermost included, so a thin occluder over the centre is
still seen). If they agree, return that. If they disagree, trace the odd half and average all N.
Same directions and count as before, so the penumbra estimate is unchanged. Below four rays the probe
half is too small to call a pixel solid, so every ray is traced as before.

**What it costs in image.** A pixel can only be misread where the odd rays would have disagreed with
unanimous even ones: the faint outer edge of a penumbra, worth one ray's step (1/N). The per-pixel
rotation already dithers by that much. Screenshot diff, before vs after, same frame:

- **0.047% of viewport pixels** (249) moved by 4+ levels, max 39, all on penumbra edges (sphere rim,
  the shaded side of a block, a slab edge). Indistinguishable at 8× zoom.
- 19.5% moved by exactly **1 level**, in contour bands that cover the **sky** too, which casts no
  shadow rays. Auto-exposure: the misread edge pixels nudge average luminance, exposure shifts by a
  fraction of a level, and smooth gradients re-quantize. Not a local shadow change.
- Noise floor (old binary vs itself): 0.2% of pixels, max 1 level.

**No new check.** The behaviour lives in GLSL, which no check here can execute without a GPU, and a
C++ model of it would test a copy, which is the trap fixed in `2b22630`. `shadersource_check` does pin
the shipped SPIR-V to this source. The evidence for the image claim is the diff above, and it should
be repeated if the cone sampling changes.

**Still open.** Divergence: a warp that straddles a penumbra edge runs both halves for all its lanes,
which is why the saving is 46% and not 50%. Temporal accumulation (one or two rays per pixel per frame,
reprojected) would cut much deeper, and is the larger project F0 actually points at.

---

## F12 — the tools lied about the frame, in two directions 🟠

**Measured 2026-10-26, RTX 3060, `MyGame` (30 draws, 169,427 triangles), 1920×1032.**

F11 closed by naming "the frame is paced at ~13.3 ms by something that is not the GPU" as the biggest
remaining mystery. It was not a mystery. **The display is 75 Hz, and 1/75 s is 13.333 ms.** The engine
does its work and waits for the monitor. Two instrumentation defects are what made that look like a
puzzle rather than a fact, and both are worth fixing on their own account.

### (a) A profiler zone that is not in its own block measures the rest of the frame

`GWS_PROFILE_ZONE` declares a local whose *lifetime* is the measurement. Four of the editor's 28 zones
were written as bare statements at the top level of the frame-loop body, so each one ran to the end of
the frame:

| Zone | Reported | Correctly scoped |
|---|---|---|
| `acquire_swapchain_image` | 11.637 ms | — (trivial) |
| `stage_shadow` | 10.515 ms | **0.363 ms** |
| `stage_transparent` | 9.145 ms | **0.125 ms** |
| `queue_submit` | 8.778 ms | **0.184 ms** |

Nothing errored. The zones were silently nested and all ended at the same point, so they sorted into a
tidy descending sequence that reads exactly like a real profile — 29× wrong, and convincing.

With them scoped, the frame resolves with no mystery in it: **~3.8 ms of work, ~7.4 ms blocked in
`present` waiting for the 75 Hz refresh.** Largest real items are `stage_geometry` 0.73 ms,
`ui_assetbrowser` 0.73 ms and `ecs_sync` 0.71 ms.

Guarded by `profilescope_check`, a source-text check — the defect is lexical scope, so there is nothing
left to inspect at runtime.

### (b) "VSync off" selected a mode that is still capped

`choose_present_mode` preferred MAILBOX when vsync was off and the log called it "uncapped". MAILBOX
neither blocks the CPU nor tears, and neither of those makes it uncapped: the presentation engine still
releases one image per vertical blank. Measured consequence — 75.1 fps vsync on, 75.3 fps vsync off, on a
75 Hz panel, while the GPU did 2.6 ms of work.

**That is why the F11 AO saving was invisible.** A real 0.75 ms improvement cannot move a frame rate
pinned to the refresh interval. Now prefers IMMEDIATE, falling back to MAILBOX then FIFO, and the log
describes what the chosen mode actually does. `presentmode_check` asserts on *capping*, not on mode
names. `present_mode_choice.h` keeps the decision free of Vulkan so it is checkable without a device.

> Switching to IMMEDIATE did **not** raise the frame rate here, because a windowed app on Windows is
> composited by DWM at the refresh rate regardless of present mode. The fix is still right — the log was
> false and "off" should mean off — but uncapped measurement needs exclusive fullscreen.

### (c) Correction, 2026-10-28 — the "honest" GPU total still excluded the render-graph stages

F11 says both log lines "now report `GPUProfiler`'s whole table". Structurally they did. But
`GpuZones::end_frame()` reported through `submit_frame()`, which zeroes every pass absent from its
argument — so each frame it erased the five render-graph stages (shadow, geometry, **lighting**,
transparent, post) that the graph had reported moments earlier. F2 moved the graph onto the additive
`submit_partial()` so it would stop clobbering the compute zones; nobody moved GpuZones, so the
clobbering simply ran the other way. **This is F2's bug a third time**, and it was in the fix for the
second.

Consequences for the numbers above:

- Every **GPU total** in F11 and F12 excludes the render-graph stages. The F11 *A/B difference* is
  unaffected (`AO.Blur` is a compute zone and was measured correctly), but its "GPU total" column
  understates by roughly 3.8 ms with ray tracing off (geometry 1.1, lighting 1.3, shadow 0.6, post 0.5,
  transparent 0.3 — measured today).
- **The verdict below holds only with ray-traced shadows off or at 1 ray.** Interleaved A/B on MyGame,
  same session, 75 Hz, VSync on:

| Config | GPU total | Lighting | Frame |
|---|---|---|---|
| RT on, 8 shadow rays (High) | 12.8–17.3 ms | 8.8–11.9 ms | 13.7–17.1 ms — **over budget, misses vblank** |
| RT off | 5.1–6.4 ms | 0.8–1.8 ms | 13.28 ms (refresh-paced) |
| RT on, 1 shadow ray | 4.8–6.7 ms | 0.7–1.8 ms | ~13 ms (refresh-paced) |

  Earlier the same day, in a slower GPU clock state, 8 rays read **20–23 ms** of lighting and the editor
  ran at **31 fps**. Ray tracing as such costs almost nothing here; the eight shadow rays cost about
  1.1 ms each. This is **F0**, found in August, hidden again by the truncated total.

Fixed by deleting `submit_frame()` outright — the header already documented the one-producer hazard,
and the documentation did not stop it. `gpuzones_check` now asserts the clobbering API stays gone
(mutation-tested: restoring it fails the check) and replays the real producer order.

**The fence wait was also a composite.** The zone named `wait_prev_frame_fence` wrapped the fence wait
*and* the occlusion resolve, the timestamp resolve and the HZB readback — three GPU reads, exactly the
kind of thing that blocks. Split into four zones. Measured: fence **31.3 ms**, the other three
**0.08 ms combined**. The wait really is the GPU finishing, and its books now balance against the
GPU table.

### What this means for how this document is read

> **Read with (c) above.** The paragraphs below were written from the truncated total. They are correct
> for ray-traced shadows off or at 1 ray, and wrong for the High preset's 8.

**Frame rate is not a usable metric on this machine.** At 75 Hz with ~4 ms of work there is roughly 9 ms
of idle headroom, so almost any optimization will measure as "no change in fps" while being perfectly
real. Judge work by the GPU breakdown and the CPU zone report, not by fps — and see the F11 retraction
for why absolute millisecond figures must come from an A/B inside one session.

The engine is **not currently frame-rate limited on this hardware.** That does not make the accumulated
costs acceptable — it means they are being paid out of headroom instead of out of frame rate, and a
heavier scene or a slower machine spends that headroom immediately.

---

## F11 — the AO denoise was sized for ray tracing and ran on everything 🟠

**Measured 2026-10-26 on an RTX 3060**, `MyGame` (30 draws, 169,427 triangles), 1920×1032, VSync and ray
tracing off, A/B in one session:

| Blur kernel | `AO.Blur` | GPU total |
|---|---|---|
| 7×7 (before) | 1.54 ms | 3.33 ms |
| 5×5 (after) | **0.79 ms** | **2.59 ms** |

**0.75 ms of GPU saved, 49% of the blur's cost** — matching the 49→25 tap ratio exactly. Frame rate did
not move (75.3 → 75.1 fps), because the frame is not GPU-bound right now. This is a real saving in a
budget that is not currently the constraint.

**The defect.** The bilateral blur went from 5×5 to 7×7 with looser sigmas, for the stated and correct
reason that RT AO is stochastic and tight sigmas preserved its grain. That tuning was then applied to
every technique. The kernel is O(r²) with three texture fetches per tap, so the analytic default paid 49
taps where 25 would do, to remove noise GTAO does not produce. Fixed by `ao_blur_params_for()` in
`engine/renderer/gpu/vulkan/ao_blur_params.h`, pinned by `aoblur_check` (15 assertions, mutation-tested
by reintroducing the bug: 7 failures).

### ⚠️ Retraction: the 2026-10-19 figures in this section were not representative

An earlier version of F11 reported a 44 ms frame, AO at 22–48 ms, and called it the dominant cost in the
engine. **Those absolute numbers are withdrawn.** Re-measuring the same project at the same resolution
with the same settings a week later gave GPU work of 2.6 ms where it had been 26–31 ms.

The giveaway is that the difference is near-uniform across unrelated passes:

| Pass | 2026-10-19 | 2026-10-26 | ratio |
|---|---|---|---|
| `AO.GTAO` | 8.9 ms | 0.93 ms | 9.6× |
| `VolumetricLight` | 4.4 ms | 0.55 ms | 8.0× |
| `SSR` | 4.17 ms | 0.23 ms | 18× |
| `HZB` | 0.05 ms | 0.05 ms | 1× (already at its floor) |

A single multiplier across passes that share no code points at GPU clock or power state during that
session, not at the shaders. **Lesson for this document: a GPU measurement is only trustworthy as an A/B
taken inside one session.** Absolute millisecond figures carried across sessions are not comparable, and
several numbers recorded here over the project's life were taken that way.

What survives from 2026-10-19 is everything differential or arithmetic: the 49-vs-25 tap argument, the
ratio between blur and AO, and both measurement defects below — which were real, and are fixed.

### The measurement defects (both fixed)

**The log omitted every compute pass.** The `[gpu]` and `[health]` lines printed the five render-graph
stages and nothing else — SSAO, SSR, HZB, DDGI, Water, Clouds, VolumetricLight and FroxelFog were all
absent, so a headless run, a CI run, or anyone reading `editor.log` saw a fraction of the frame presented
as the total. **This is F2 recurring one layer out**: F2 fixed the overlay and left the log reporting the
old five. Both lines now report `GPUProfiler`'s whole table, built by iteration rather than a fixed
format string, so a pass added later cannot go unprinted the same way.

**The health line's breakdown contradicted its own total.** The total came from `GPUProfiler` and the
five stages from `g_gpu_*_ms` resolved earlier in the frame, so the parts could exceed the whole, and did
— a 9.60 ms "light" inside a 2.94 ms total. Both now come from one snapshot.

**AO reported as one opaque number.** `SSAO` covered both the technique and the denoise behind it, which
cannot distinguish two bugs with different fixes. Now `AO.<technique>` and `AO.Blur`.

### Still open under this finding

- **The AO technique is hardcoded.** `VulkanSsaoPass::create()` sets GTAO, overriding the class default
  of `SSAO`. No quality preset selects it — Low/Medium/High only flip AO on and off — and nothing
  persists it, so the editor's dropdown forgets every launch.
- **A ~4 ms phantom** attached to roughly one pass per frame in the 2026-10-19 session and moved between
  them (HZB reading 0.05 ms one frame and 4.01 ms the next). Not observed on 2026-10-26. It may be the
  same underlying condition as the retraction above.
- ~~**The real current bottleneck is not the GPU.**~~ Resolved in F12: the ~13.3 ms was the 75 Hz
  refresh. And per F12 (c), the ~2.6 ms "GPU work" figure here excluded the render-graph stages.

---

## F0 — MEASURED: the lighting pass is 94% of the frame 🔴

**The first real numbers, from the user's own testing on 2026-08-30 (v0.8.2, RTX 3060).** Everything else in
this document was inference from reading source; this is measurement, and it outranks the lot.

Scene: **18 draws, 96,704 triangles** — trivially small. 1920×1080. Steady 60 fps.

| Pass | Time |
|---|---|
| shadow | 0.20 ms |
| geometry | 0.33 ms |
| **lighting** | **10.0 – 11.8 ms** |
| transparent | 0.12 ms |
| post | 0.26 ms |

The log also says: `VulkanLightingPass: using the ray-query fragment shader`. `tools/lighting_pass.frag`
casts **ray-query shadow rays per pixel** (`rayQueryShadowOne`), looped for soft shadows
(`lit += rayQueryShadowOne(...)`). That is the 11.8 ms.

**What this confirms and what it changes.** It confirms the audit's central thesis outright — an
18-draw scene costs 16.5 ms, so the frame is scene-independent. It *changes* the ranking: the single most
expensive thing in this engine is not the G-buffer's width or any post-process, it is per-pixel ray-traced
shadows inside the deferred lighting pass. On a fast discrete card. Every effect in §06 combined is smaller.

And it was reachable only through **`GWS_FORCE_NO_RT=1`** — an environment variable. That is the *third*
instance of the same failure in this audit (F4 was the first two). Now a preset flag: **Low and Medium
disable ray tracing**, with a checkbox beside them.

*Caveat worth stating:* on a machine **without** `VK_KHR_ray_query` the lighting pass already compiles its
non-RT variant, so the low-end device's profile is different from this one and may not be dominated by
lighting at all. This measurement is from the fast machine. It is still the largest single number anyone has
produced here.

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

**Phase 1 shipped in v0.8.6:** the lighting pass (94% of frame per F0, and the only one of the 14
consumers that already had both a depth binding and an invViewProj available) no longer reads the
position attachment — reconstructs from depth instead. `gbuffer_reconstruct_check` proves the formula.
The attachment itself is not yet removed: the other 13 consumers still read it, and 12 of them have no
camera matrix bound at all today (only `water.frag` does), so converting them needs new per-pass
UBO/push-constant plumbing, not just a shader math swap — a materially bigger task, scoped separately.
A useful side-finding while proving the formula: reconstruction from a float32 depth buffer is measurably
*more* precise than the original RGBA16F position texture at typical scene distances (one float32 depth
value beats three float16 position components) — this is not a precision trade-off, just a bandwidth one.

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

**Versioning continues in the 0.8 series.** Each item states what *done* means in
terms that can be checked, because the recurring failure in this project is a fix
that was believed rather than verified.

### v0.8.2 — the profiler tells the truth ✅ SHIPPED

Deliberately small and shipped on its own, ahead of any actual optimization.
Half of F2's done criterion is *"read the overlay on the machine that is slow"*,
and that cannot be done from here — so the diagnostic ships first and the rest of
the work gets aimed at real numbers instead of inference.

| Item | State |
|---|---|
| **F2 — every pass is timed** | ✅ One name-keyed timestamp system (`gpu_zones`) replaces the five-stage enum-keyed one. SSAO, SSR, DDGI, clouds, volumetric light, froxel fog, water and HZB are timed for the first time. `GPUProfiler` gained an additive API so the graph and the zones both report into one frame instead of one clobbering the other. `gpuzones_check` (25), mutation-verified. |
| **Copy breakdown** | ✅ A button in the GPU (N2) panel puts the exact sorted per-pass numbers on the clipboard and in the log, so a report does not have to be retyped off a live overlay. |
| **Render-settings model** | ✅ Presets + render scale, persisted, `rendersettings_check` (31). **Inert in this release** — no UI is wired to it yet. |

**Still unverified after this release:** the overlay's numbers on real hardware.
That is the point of shipping it.

### v0.8.3 — resolution and control ✅ SHIPPED

Aimed using whatever v0.8.2 reports.

| # | Item | Done when |
|---|---|---|
| 1 | **F1 — stop rendering at a hardcoded 1920×1080** | Targets are sized from the real framebuffer times the render scale. (Live viewport-tracking resize needs resize paths on seven more passes — see the correction under F1 — and is tracked separately.) |
| 2 | **F7 — render-scale control** | A slider in the UI; 0.5 quarters the pixel work, visible in the overlay. |
| 3 | **F3 / F4 / F6 — quality presets in the UI** | A preset control with a real Low; defaults chosen per device type; every `GWS_NO_*` env var has a UI equivalent, reachable from a Hub-launched editor. |
| 4 | **F0 — ray tracing becomes a setting** | ✅ Per-pixel ray-query shadows in the lighting pass, measured at 10–12 ms, are off at Low and Medium and have a checkbox. Applies at startup. |
| 5 | ~~F8 — skip the pipeline when the viewport is hidden~~ | **Deferred to v0.8.4.** The smallest win of the set, and gating the whole scene block interacts with play mode and the swapchain image the ImGui pass still needs. Not worth rushing into a release whose point is letting someone test on a second machine. |

**Release gate:** on the low-end machine, Low + 0.5 render scale produces a
*measured, reported* improvement. Not "feels faster".

### ~~v0.8.4~~ — bandwidth (renumbered: v0.8.4 shipped as a profiler-regression fix + VSync toggle instead)

| # | Item | Done when | Actually shipped |
|---|---|---|---|
| 5 | **F5 — drop the G-buffer position attachment** | Reconstructed from depth in all 14 consumers; G-buffer is 20 B/px; output visually unchanged against captured references; Geometry and Lighting measurably cheaper. | **1 of 14 in v0.8.6** (the lighting pass — see F5 above). The other 13 are a separate, bigger task: unlike the lighting pass, 12 of them have no camera matrix bound today. |

### ~~v0.8.5~~ v0.8.7+ — footprint (renumbered: v0.8.5 shipped as the Linux-support release instead)

| # | Item | Done when |
|---|---|---|
| 6 | **F9 — suballocator** | Texture and mesh uploads draw from pooled blocks; `maxMemoryAllocationCount` queried and headroom logged. |
| 7 | **F10 — alias transient render targets** | Peak render-target footprint measurably below the naive sum, reported in the Memory (N3) panel. |

**F5 is a prerequisite in practice, not just in the recommended order** — the position attachment can
only be dropped (needed for both F5's own "20 B/px" target and for F10's aliasing to consider a smaller
set of targets) once all 14 consumers stop reading it. v0.8.6 converted 1 of 14 (see F5 above); the
remaining 13 are unscoped past "which shaders" and belong in their own plan before this row is started.

### Deliberately not in the plan yet

The unexamined list stands: draw-call counts, shader occupancy, whether project
textures are actually cooked to BC, terrain's pipeline, editor UI cost, vertex
formats, overdraw, and the CPU side. **v0.8.2 exists partly to answer these** —
once the overlay accounts for the whole frame, what to optimise next stops being
a matter of opinion.
