  # GameWorldshaper Engine — Technical Pre-Planning Document
> All systems researched before implementation. Every decision is grounded in
> production evidence, academic papers, or open-source precedent.
> Last updated: April 2026

---

## How to Use This Document

Each system section follows this structure:
1. **What it does** — one-paragraph plain-English description
2. **Research findings** — what we learned from existing engines / papers
3. **Chosen approach** — the specific technique and why
4. **Key dependencies** — libraries or standards it relies on
5. **Efficiency notes** — concrete optimizations specific to this system
6. **Implementation order** — what must exist first
7. **Risks** — known failure modes and mitigation

---

# PART 1 — RENDERING

---

## 1.1 Render Pipeline Strategy

### Research Findings

The three main pipelines — Forward, Deferred, and Clustered — each have specific tradeoffs. Production evidence from the community:

- **Deferred shading** separates material evaluation (G-buffer pass) from lighting (fullscreen pass). It eliminates overdraw cost during shading but introduces memory bandwidth cost from writing and reading G-buffers. MSAA requires special treatment and is expensive. Material variety requires a material ID lookup system.
- **Tiled Forward / Forward+** (Harada et al.) subdivides the screen into 2D tiles. Lights are sorted per tile before shading. Solves the multi-light problem for forward renderers, but tile granularity misses depth complexity (a tile containing both close and far geometry wastes light tests on far geometry that isn't lit by close lights).
- **Clustered Deferred/Forward** (Olsson et al., 2012 — the foundational paper) subdivides the view frustum into 3D clusters. Lights are assigned to clusters in a compute pass. Each fragment/pixel only iterates lights assigned to its cluster. This is strictly better than tiled for scenes with significant depth variation. Scales to 1M point lights in the benchmark. Both Doom (2016) and Unreal Engine 5 use clustered forward variants.

The community debate: clustered forward vs. clustered deferred? The answer depends on:
- MSAA support: forward handles it trivially, deferred requires 2x–8x G-buffer cost
- Material variety: deferred gives all material data before shading, forward needs care
- GPU compute availability: both run the light clustering in compute

**Decision:** Hybrid Clustered approach. G-buffer pass for opaque geometry (deferred path), clustered light assignment via compute, optional forward path for transparents. This matches Frostbite's production approach (Battlefield 3 presentation by Johan Andersson).

### Chosen Architecture

```
Depth Prepass           → Z-buffer, early-Z rejection, HZB mip generation
G-Buffer Pass           → albedo, normal (oct-encoded), roughness/metallic, emissive
Cluster Assignment      → compute shader, 3D AABB test per light per cluster
Shadow Pass (Atlas)     → directional CSM + spot lights in a single atlas texture
Lighting Pass           → compute over G-buffer, clusters provide light lists
Transparency Pass       → clustered forward, sorted back-to-front for correctness
Post Process Stack      → SSAO/RTAO, bloom, TAA, tonemapping (ACES), film grain
```

### Cluster Grid Specification

Based on the Olsson 2012 paper and community implementations:
- Grid resolution: 16×9×24 clusters (3,456 total) — enough for 1080p–4K with depth coverage
- Depth subdivision: exponential in view space (more clusters near camera, fewer far)
  `cluster_z = floor(log(z / near) / log(far / near) * depth_slices)`
- Per-cluster structure: `{ uint light_offset; uint light_count; }` stored in SSBO
- Light index list: global flat array, cluster's offset + count slices into it
- Cluster assignment: one thread per light, broad-phase AABB test to cluster grid, narrow-phase sphere/cone test per cluster

### G-Buffer Layout (4 render targets, minimizing bandwidth)

| RT | Format | Contents |
|---|---|---|
| RT0 | RGBA8 sRGB | Albedo (RGB) + Ambient Occlusion (A) |
| RT1 | RG16F | Normal (oct-encoded, 2 channels) |
| RT2 | RGBA8 | Roughness (R) + Metallic (G) + Emissive mask (B) + Material ID (A) |
| RT3 | — | Depth (shared with depth prepass) |

Oct-encoding normals saves a full RT channel. Material ID allows deferred decals and special shading (hair, skin SSS) via lookup.

### Key Dependencies
- `VK_KHR_shader_storage_buffer_object` or OpenGL 4.3+ SSBO for cluster lists
- Compute shaders (GL 4.3 / Vulkan 1.0)
- `glDrawElementsIndirect` or `vkCmdDrawIndexedIndirect` for GPU-driven draw submission

---

## 1.2 Ray Tracing

### Research Findings

Two distinct paths exist:

**Hardware RT (DXR / VK_KHR_ray_tracing_pipeline):** Requires dedicated RT cores (NVIDIA Turing+, AMD RDNA2+, Intel Arc). Uses a two-level BVH: BLAS (Bottom Level Acceleration Structure) per mesh, TLAS (Top Level) for scene instances. Khronos confirmed VK_KHR is feature-parity with DXR and ports are straightforward. NVIDIA's best practices documentation (2023, Juha Sjoholm) gives concrete performance rules:
- Prefer global root table for scene-wide resources (avoids per-geometry table replication)
- Use bindless resources for hit shaders (indexed by InstanceID / PrimitiveID)
- Avoid state object creation on the critical path — compile PSOs at load time
- Manually manage stack size (RT stack = trace depth × per-shader stack size × wave size)
- Use SKIP_PROCEDURAL_PRIMITIVES flag when mesh-only scenes are traced

**Software RT:** No dedicated hardware required. A BVH is built over the scene and traversed in a compute shader. Used as fallback for hardware without RT support. Performance is 5–20× slower than hardware RT at the same quality, so resolution and sample count are reduced.

**Denoising:** Both paths produce noisy output at 1 sample/pixel. SVGF (Spatiotemporal Variance-Guided Filtering) is the standard denoiser: accumulates samples across frames using motion vectors, rejects history on disocclusion using a variance estimate. NVIDIA NRD (NVIDIA Real-Time Denoisers SDK — open source) implements optimized SVGF variants.

### Chosen Approach

BVH architecture:
- **BLAS**: built at mesh import time. Stored as compact data. For dynamic meshes (skinned characters), BLAS is rebuilt or refit each frame using compute (refit is ~5× faster than rebuild, acceptable for small deformation)
- **TLAS**: rebuilt every frame (transforms change). Built via `vkCmdBuildAccelerationStructuresKHR` with `FAST_BUILD` flag (prefer build speed over trace speed for the TLAS)
- **BLAS Update policy**: static geometry → `ALLOW_COMPACTION` flag + compaction step at load → ~50% memory saving. Dynamic geometry → `ALLOW_UPDATE` flag, no compaction.

RT usage per quality tier:
```
Ultra:   RTGI (1spp) + RT Shadows + RT Reflections + RTAO — all denoised via SVGF
High:    RT Shadows + RT Reflections — SVGF denoised
Medium:  RT Shadows only (replaces shadow maps for primary directional light)
Low:     No RT — shadow maps + SSAO + SSR
```

Ray payload size minimization (directly affects stack usage and memory bandwidth):
```glsl
// Shadow ray: 1 float (occlusion factor)
struct ShadowPayload { float visibility; };

// Reflection ray: 12 bytes (HDR color)
struct ReflectionPayload { vec3 color; };

// GI ray: 16 bytes (color + distance)  
struct GIPayload { vec3 irradiance; float distance; };
```

### Key Dependencies
- `VK_KHR_ray_tracing_pipeline` + `VK_KHR_acceleration_structure`
- `VK_KHR_ray_query` for inline ray queries in other shaders (AO, shadows in raster passes)
- NVIDIA NRD SDK (open source, Apache 2.0) for denoising
- Software fallback: custom BVH-in-compute, 256×144 resolution RT for GI approximation

---

## 1.3 Culling Systems

### Research Findings — Hierarchical Z-Buffer (HZB)

HZB is a mip chain of the depth buffer where each texel at mip N stores the maximum (or minimum for reversed-Z) depth of the 4 texels it covers at mip N-1. To test an object: project its AABB into screen space, select the mip level where the AABB covers ~4 texels, sample that mip level's depth. If the AABB's minimum depth is farther than the HZB sample, the object is fully occluded and skipped.

Research from Milos Kruskonja (Two-Pass HZB) and RasterGrid shows:
- **One-frame latency** is acceptable (objects reappear within 1 frame if they become visible)
- **Non-power-of-2 resolution** requires careful mip generation (Mike Turitzin's blog: max reduction is only valid for power-of-2 tiles; use conservative max for odd-dimension borders)
- **Depth reprojection** from the previous frame (using motion vectors) reduces reliance on hand-authored occluders (Assassin's Creed Unity implementation)
- **Two-pass variant**: Pass 1 = depth prepass with hand-authored occluders, builds conservative HZB. Pass 2 = GPU compute tests all instances against HZB, fills indirect draw buffer. This is what Luc Momber's implementation uses.

ARM's OpenGL ES documentation shows combining HZB occlusion with LOD selection in the same compute pass: a thread both determines visibility AND which LOD buffer to place the draw into. This avoids a second pass for LOD assignment.

### Chosen Culling Pipeline

```
Stage 1: Frustum Culling (CPU, SIMD)
  - Per-entity AABB vs. 6 frustum planes
  - AVX2: test 8 AABBs per cycle
  - Output: visible entity list

Stage 2: Distance Culling (CPU, same pass)
  - Per-entity max draw distance (type-dependent)
  - Fold into frustum pass, no extra traversal

Stage 3: Two-Pass HZB Occlusion (GPU)
  - Pass A: render depth prepass (occluder meshes only — buildings, terrain, large props)
  - Build HZB mip chain via compute downsampling (reversed-Z: store minimum depth)
  - Pass B: compute shader, 1 thread per instance
    * Frustum early-out (skip Stage 1 survivors that escaped edge cases)
    * Project AABB to screen, compute screen-space extent
    * Select mip = floor(log2(max(extent.x * width, extent.y * height)))
    * Sample 4 corners of projected AABB from HZB at selected mip
    * If min_Z_AABB > max(4 samples): cull; else: write to indirect draw buffer
  - Output: indirect draw buffer per LOD tier (up to 5 buffers: LOD0–LOD4)

Stage 4: Portal Culling (CPU, indoor only)
  - Active only when player is inside a portal-connected interior
  - Walk portal graph from camera's room, frustum-clip portals
  - Only submit rooms reachable through visible portals
  - Implemented as a separate system, activated by an EnvironmentType flag on the room entity

Stage 5: GPU-Driven Indirect Draw
  - glMultiDrawElementsIndirect / vkCmdDrawIndexedIndirect
  - No CPU draw call loop
  - One indirect buffer per LOD tier → sorted front-to-back within each tier for early-Z
```

---

## 1.4 LOD & Mesh Simplification

### Research Findings

**meshoptimizer** (zeux, MIT license, production-proven in Cyberpunk 2077 and elsewhere) is the definitive C++ library for this. It implements:
- `meshopt_simplify`: quadric error metrics simplification (Garland & Heckbert 1997 + Hoppe 1999 extensions for attributes). Preserves attribute seams, borders, UV islands. Produces a new index buffer reusing the original vertex buffer.
- `meshopt_simplifySloppy`: faster, ignores topology. For aggressive LODs where accuracy is less important.
- `meshopt_simplifyWithAttributes`: takes per-vertex attributes (normals, UVs) into account for error metric. Produces higher-quality LODs.
- `meshopt_optimizeVertexCache`: improves GPU vertex transform cache utilization.
- `meshopt_optimizeVertexFetch`: reorders vertices to reduce vertex fetch bandwidth.
- `clusterlod.h`: hierarchical cluster LOD (Nanite-style), new in meshoptimizer v1.0.

LOD chain generation strategy from the docs: re-simplify from the previous LOD (not always from the original) for deeper chains. Accumulate errors across levels for view-distance selection.

### Chosen LOD System

LOD generation (offline, at asset import time):
```
LOD0  100% triangles  — view distance 0–15m
LOD1   50% triangles  — 15–40m    (meshopt_simplify, target=50%, error<1%)
LOD2   25% triangles  — 40–80m    (from LOD1, target=50% of LOD1, accumulated error)
LOD3   10% triangles  — 80–150m   (from LOD2)
LOD4    2% triangles  — 150–300m  (from LOD3, meshopt_simplifySloppy)
Impostor billboard   — 300m+
```

At runtime, the HZB culling compute pass selects the LOD tier based on:
```
screen_coverage = projected_AABB_area / (screen_width * screen_height)
LOD = select_from_coverage_thresholds(screen_coverage)
```

**Dithered LOD transitions:** A screen-space stipple pattern (dither matrix) fades between LOD levels across a transition range, eliminating pop. Implemented as a clip() in the pixel shader based on a hash of screen position and a per-mesh LOD blend weight.

**Vertex cache optimization:** After simplification, all LODs are assembled into one large index buffer (coarsest first) and `meshopt_optimizeVertexFetch` is called on the combined buffer. This ensures coarser LODs need a smaller vertex range, benefiting mobile GPUs.

### Impostor System

For objects beyond LOD4: pre-render the object from octahedron-mapped directions (e.g., 8×8 = 64 views) into an atlas texture. At runtime, the impostor quad looks up the correct atlas region based on view direction. Implemented as an optional asset flag (`impostor: true` in asset metadata). Atlas generation runs at project build time via a bake tool.

---

# PART 2 — ENTITY COMPONENT SYSTEM (ECS)

---

## 2.1 ECS Architecture

### Research Findings

The ECS FAQ (Sander Mertens, author of Flecs — the most cited ECS resource) establishes the key distinction: **Sparse-set ECS** vs. **Archetype ECS**.

- **Sparse-set ECS** (EnTT): each component type has its own densely-packed array. Entities have sparse IDs that map into the dense arrays. Very fast single-component iteration, fast add/remove (O(1)). Slower multi-component iteration (requires intersection). Best for games with frequent component add/remove.
- **Archetype ECS** (Unity DOTS, Flecs): entities with the same set of components are stored together in contiguous "chunks." Multi-component iteration is optimal (all data for an archetype is contiguous). Add/remove requires moving entity data between archetypes (more expensive). Best for games that iterate large numbers of entities with stable component sets.

Unity's DOTS demonstrates: archetype ECS + Burst Compiler + Job System = 100× performance vs. OOP on entity-heavy scenes. Unreal's Mass Entity uses the same archetype approach for crowd simulation.

The ECS FAQ's data-oriented design guidance: cache lines are 64 bytes. A CPU loads 64 bytes per cache miss. If you're iterating a component array of `float health` values, each cache line holds 16 floats — near 100% cache efficiency. If health is inside a `Character` object alongside 200 other bytes, each cache line holds less than 1 useful float — massive waste.

For our game: we'll have both frequent-mutation components (AI state, physics) and stable-iteration components (rendering, transform). The hybrid approach used by production engines is to use an archetype layout but allow sparse-set exceptions for volatile components.

**Decision:** Use EnTT (sparse-set) as the foundation (it's already a dependency, MIT license, production-proven, excellent C++ integration). For hot paths requiring archetype-like performance (rendering system, physics batch), manually group component storage using EnTT's group/exclude queries which provide sorted, contiguous iteration.

### ECS Structure for This Engine

```cpp
// Entity ID: 32-bit index + 16-bit version (for safe stale-reference detection)
// Component storage: EnTT sparse-set per component type
// System execution: ordered list of systems, each queries relevant components

// Component types — pure data, no methods
struct TransformComponent    { Vec3 position; Quaternion rotation; Vec3 scale; };
struct RenderableComponent   { Handle<Mesh> mesh; Handle<Material> material; uint8_t lod_bias; };
struct StatComponent         { float values[STAT_COUNT]; StatModifier* modifiers; };
struct AbilitySlotComponent  { Handle<AbilityAsset> slots[4]; CooldownState cooldowns[4]; };
struct PhysicsBodyComponent  { Jolt::BodyID body_id; };
struct AIAgentComponent      { Handle<BehaviorTree> tree; BehaviorTreeState state; };
struct ScriptComponent       { ScriptHandle scripts[MAX_SCRIPTS_PER_ENTITY]; };

// System execution order (registered at startup, runs each frame)
// 1. InputSystem         — read raw input, write InputEvents
// 2. NetworkSystem       — receive inputs, write to input buffer
// 3. PhysicsSystem       — Jolt step, write transforms
// 4. MovementSystem      — read input, write velocity to physics
// 5. AnimationSystem     — sample blend tree, write pose
// 6. IKSystem            — modify pose from IK targets
// 7. AISystem            — tick behavior trees (10Hz subset)
// 8. CombatSystem        — process hitboxes, apply damage
// 9. AbilitySystem       — process activations, apply effects
// 10. StatSystem         — recompute dirty stat caches
// 11. RenderPrepSystem   — extract render data into render thread queue
// 12. AudioSystem        — update spatial audio sources
```

### Efficiency Notes
- Use EnTT's `entt::group<Renderable, Transform>()` — returns a sorted view where both components are contiguous. Render system iterates this, maximizing cache efficiency.
- Mark AI components as "dirty" rather than processing every agent every frame. The AISystem maintains a priority queue of agents sorted by next-update time.
- Physics bodies are identified by Jolt BodyID, not EnTT entity ID. A bidirectional map (`entt::entity ↔ BodyID`) is maintained in PhysicsSystem.

---

# PART 3 — NETWORKING

---

## 3.1 Rollback Netcode

### Research Findings

**GGPO** (Tony Cannon, MIT license) is the foundational rollback implementation. Its core mechanism:
1. Each frame, send local player inputs to all peers immediately (no waiting)
2. If remote inputs haven't arrived, predict them (simplest: repeat last known input)
3. When remote inputs arrive, if prediction was wrong: roll back to the last confirmed frame, replay all inputs forward to the present
4. Game simulation must be **100% deterministic**: same inputs + same state = same result, every time, on every machine

Critical determinism requirements (from the Coherence documentation and community posts):
- Never use `float` for gameplay-affecting values in the simulation — use **fixed-point arithmetic** (integer math with a fixed decimal scale, e.g., 1 unit = 1/1000 of a meter). Floating-point has platform-dependent rounding.
- Random number generator must be seeded identically on all clients and stepped identically per frame. Feed the seed as an input (or derive from frame number + game seed).
- Physics: use a deterministic physics engine. Jolt Physics has a deterministic mode — use fixed-point positions or at minimum verify identical results across MSVC and GCC.
- Never use `std::sort` with an unstable comparator on shared data — sort must be stable and order-preserving.

Performance constraint: to support 300ms ping, need to roll back and re-simulate up to 18 frames (at 60fps). That means the simulation must run 18× in ~16.6ms = ~0.9ms per simulation tick. This is extremely tight. Mitigation strategies:
- Reduce simulation tick rate to 30Hz (rollback up to 9 frames per render frame)
- Separate simulation from rendering: simulation runs at fixed 30Hz, rendering interpolates at 60Hz
- Profile aggressively — AI ticks should NOT run during rollback (AI state is snapshot/restored, not re-simulated)

**Delta rollback** (David Dehaene, 2024): instead of saving the full game state every frame, save only the delta since the previous save. Reduces memory and copy time for saves/loads. Relevant for complex state (our game has deep ability/stat state that's expensive to memcpy).

### Chosen Network Architecture

**For combat (PvP, co-op combat):** GGPO-style rollback. Deterministic fixed-point simulation for all gameplay-affecting physics. AI state is snapshot-and-restore (not re-simulated during rollback). Maximum rollback window: 10 frames.

**For open world co-op (non-combat):** Server-authoritative with client prediction + lag compensation. Each client predicts its own movement immediately, server validates and corrects. Other players are interpolated from received snapshots (entity interpolation with a 3-frame buffer).

**Separation of concerns:**
```
SimulationLayer:  deterministic, fixed-point, rollback-safe
  - Movement, combat, abilities, stat changes, hitbox detection
  
PresentationLayer: non-deterministic, floating-point, never rolled back
  - VFX, particles, audio, camera, UI
  - Re-triggered from simulation events during rollback (fire-and-forget)
```

State snapshot format: the entire `SimulationLayer` state is a flat memory block (ECS components relevant to simulation, stored contiguously in a pool). Snapshot = `memcpy` of this pool. Restore = `memcpy` back. Target snapshot size: < 64KB per player per frame.

### Key Dependencies
- Custom UDP transport layer (no TCP — latency is critical)
- GGPO SDK (MIT) or custom implementation following GGPO paper
- ENet library (reliable UDP + sequencing, for non-rollback data)

---

# PART 4 — AI SYSTEMS

---

## 4.1 Behavior Trees

### Research Findings

**BehaviorTree.CPP** (Davide Faconti, MIT license) is the most production-ready open-source BT library. It is used in ROS2 robots and specifically designed for asynchronous, non-blocking execution. Key features:
- XML-defined tree morphology loaded at runtime (hot-reload possible)
- Type-safe blackboard (key-value store per agent) for inter-node communication
- Async actions are first-class — a node can return `RUNNING` across multiple ticks
- Plugin architecture: custom nodes as shared libraries, loaded at runtime

The blog post by lisyarus introduces a critical optimization: **waiting status with duration**. Instead of returning `RUNNING` every tick from a node that's just waiting for a timer, return `WAITING(duration)`. The system puts the agent in a priority queue sorted by wake time. Only agents whose timers have expired are ticked. In a world with 500 NPCs, typically only 3–5 are actively computing; the rest are waiting. This makes large AI populations cheap.

Unreal Engine's behavior trees use an event-driven approach: **Decorator observers** abort running branches when their condition changes (e.g., "lose sight of player" aborts the entire "pursue player" branch immediately, without waiting for the branch to naturally fail). This is more responsive than pure tick-based evaluation.

GameAIPro Chapter 6 (Champandard & Dunstan) establishes: BT return statuses must be `SUCCESS`, `FAILURE`, `RUNNING`. Adding additional statuses creates complexity — avoid. Sequences bail on first failure; selectors advance on failure.

### Chosen Architecture

BT system built as a standalone module wrapping BehaviorTree.CPP (or implementing the same architecture for deeper integration):

```
BehaviorTree asset (.bt.xml)
  └── Loaded at runtime, hot-reloadable in editor
  └── Compiled to internal node graph (not C++ classes per node)

Blackboard
  └── Per-entity typed key-value store
  └── Observer pattern: value changes can trigger decorator re-evaluation
  └── Shared blackboard (squad-level data) inherited by squad members

Execution
  └── AISystem maintains priority queue sorted by next-wake-time
  └── Each tick: process only agents whose wake time has passed
  └── Parallel nodes use JobSystem for multi-agent batch processing
  └── BT tick budget: 1ms per frame total for all agents (configurable)

Built-in Node Library
  └── Composite: Sequence, Selector, Parallel, RandomSelector
  └── Decorator: Inverter, Cooldown, Loop(N), ForceSuccess, TimeLimit
  └── Condition: CheckBlackboard, CheckDistance, CheckStat, IsInFOV, HasLineOfSight
  └── Action: MoveTo, Attack, PlayAnimation, SetBlackboard, Wait, SpawnEntity
  └── Custom: registered from C++, C#, or Python via binding layer
```

**Boss Phase Machine (above BT):**
```
PhaseStateMachine
  ├── Phase 1 (100%–60% HP)  → references BehaviorTree "boss_phase1.bt.xml"
  ├── Phase 2 (60%–30% HP)   → references BehaviorTree "boss_phase2.bt.xml"
  │     └── transition: play cutscene, modify arena, change stats
  └── Phase 3 (<30% HP)      → references BehaviorTree "boss_phase3.bt.xml"
```

---

# PART 5 — ANIMATION

---

## 5.1 Skeletal Animation & IK

### Research Findings

**ozz-animation** (Guillaume Blanc, MIT license) is the gold-standard open-source C++ skeletal animation library. It provides:
- Runtime skeletal pose sampling (SIMD-optimized on all platforms)
- Blend trees (linear blend, additive blend, partial skeleton blend)
- Two-bone IK (foot placement), look-at IK
- Motion extraction (root motion)
- GLTF/FBX/Collada import via offline library

**IK Algorithms:**
- **CCD (Cyclic Coordinate Descent):** iteratively rotates each joint toward the target. Simple, fast, but may produce unnatural poses with many joints. Good for N≥4 chain IK.
- **FABRIK (Forward And Backward Reaching IK):** works on positions directly, no rotation matrices, fewer iterations to converge. FABRIK paper (Aristidou & Lasenby) shows it's faster and produces more realistic poses than CCD. Avoids the Jacobian matrix inversion that makes Jacobian methods slow. **Chosen for foot IK and hand IK.**
- **Two-bone analytical IK:** exact closed-form solution for 2-bone chains (leg/arm). No iteration needed. Use for all 2-joint chains — shoulder+elbow, hip+knee.

**Motion Matching** (GDC 2016, Kristjan Zadziuk): instead of a blend tree, a large database of animation clips is searched each frame for the clip that best matches current velocity, foot phases, and trajectory. Produces extremely natural locomotion. Requires ~45 minutes of motion capture data. Marked as Phase 5+ feature due to data requirements — the initial system uses a conventional blend tree.

**Pose pipeline (production standard from MechWarrior, Gears of War 4):**
```
AnimationClip sampling → base pose
  ↓
Animation Blend Tree (velocity-driven state weights)
  ↓
Partial blend (upper body: aim override, lower body: locomotion)
  ↓
Additive layer (lean, damage reaction impulse)
  ↓
IK pass (foot placement, aim IK, look-at)
  ↓
Secondary motion (cape/hair chain physics applied to specific bones)
  ↓
Final skinned pose → GPU skinning compute
```

**Dual Quaternion Skinning:** eliminates the "candy-wrapper" artifact (volume collapse at twisted joints like wrists) that affects linear blend skinning. ozz-animation supports both. Use DQ skinning for characters, linear for rigid/static objects.

### Chosen Animation Stack

Based on ozz-animation with additions:
- FABRIK solver for foot placement IK (terrain adaptation)
- Two-bone analytical IK for aim (spine + shoulder → hand tracks target)
- Look-at IK (head bone tracks nearest enemy/point of interest)
- Hit reaction: additive impulse applied to spine/chest bones on hit, decays over 0.3s using a spring-damper model
- Secondary motion: chain solver (already in Phase 1 physics module) applied to cape/hair bones, driven by velocity delta and gravity

---

# PART 6 — WORLD STREAMING

---

## 6.1 Open World Chunk Streaming

### Research Findings

Production patterns (Meta Quest documentation, Libstream project, community implementations):

The standard approach is a **2D grid of cells** (chunks). Each chunk has a bounding volume. On each update, compute player-to-chunk distance. Chunks within `load_radius` are in the loaded set; outside `unload_radius` (with hysteresis) are unloaded. The hysteresis gap between load and unload radii prevents rapid load/unload cycling at the boundary.

Key problems identified in research:
1. **Race conditions**: async load + async unload can interleave. A chunk requested for load, then immediately unload before load finishes, must be handled. Pattern: track pending operations with state machine (`UNLOADED → LOADING → LOADED → UNLOADING → UNLOADED`). A load arriving while `UNLOADING` is queued as a re-load request.
2. **Main thread stalls**: spreading chunk activation across multiple frames (Meta documentation: "spread work over multiple frames"). Activate at most N entities per frame.
3. **Asset memory leaks**: in Unity, unloading a scene doesn't unload its assets. Explicit `UnloadUnusedAssets` is required. In our engine, assets use reference-counted handles (`Handle<Mesh>` etc.). When the last chunk referencing an asset is unloaded and the count drops to 0, the asset is unloaded from the asset manager.
4. **LOD at chunk boundaries**: LOD selection must be continuous — chunks at different distances must not pop visible seams. `meshopt_SimplifyLockBorder` locks vertices on chunk edges during LOD generation, ensuring the edge geometry matches across LOD levels.

Velocity prediction: load chunks ahead of player's projected trajectory, not just current position. A simple linear extrapolation of velocity × look-ahead time (e.g., 3 seconds) identifies which direction the player is likely to move.

### Chosen Architecture

```
WorldGrid:
  - Cell size: 256×256 world units (configurable per game)
  - Grid stored as sparse hashmap (cell coord → CellState)
  - Load radius: 4 cells (1024m), unload radius: 5 cells (1280m, hysteresis)

CellState machine:
  DORMANT → QUEUED_LOAD → LOADING (async) → ACTIVE → QUEUED_UNLOAD → UNLOADING (async) → DORMANT

LoadingSystem (background thread pool, 2–4 threads):
  - Priority queue of pending loads, sorted by distance to player
  - Each load: read cell manifest from disk, instantiate entities, load asset handles
  - Activation spread: max 50 entity activations per frame on main thread

NPCScheduler:
  - Dormant NPCs have no AI, physics, or animation
  - "Dormant simulation": world state changes (item pickups, killed enemies) tracked as events
  - When cell becomes ACTIVE: replay events to reconstruct current state

Streaming budget:
  - Max memory budget: configurable (e.g., 2GB for open world assets)
  - LRU cache: when budget exceeded, evict least-recently-used dormant cell assets
```

---

# PART 7 — SCRIPTING

---

## 7.1 Multi-Language Scripting

### Research Findings

Three language runtimes to embed:
- **C# via .NET 8 CoreCLR**: Microsoft's cross-platform .NET runtime. `hostfxr` provides a public embedding API (used by Godot, Stride engine). JIT compilation at runtime, AOT compilation for consoles via NativeAOT. Hot reload via AssemblyLoadContext unload/reload pattern.
- **Python via CPython 3.12**: standard embedding via `Py_Initialize()`, GIL management, Python C API. Performance adequate for AI scripts, editor tools. GIL means Python can't run truly parallel — invoke Python in serial within AI update. Consider Cython or PyPy if performance becomes an issue.
- **Java via GraalVM native image**: produces a native shared library callable from C++. The `espresso` subsystem provides Java embedding. More complex than Python/C# embedding. Primary use: editor extensions, build tools.

**Binding layer**: the approach taken by Godot 4 (GDExtension), Unreal (Script modules), and Mono-based engines is to auto-generate binding code from C++ API headers. Tool like `libclang` parses headers and generates:
- C# P/Invoke wrappers
- Python ctypes or Cython wrappers
- Java JNI wrappers

The alternative (less code but more overhead) is a reflection-based approach: C++ registers functions into a scripting VM table, scripts call into the table by name (string lookup). We use **both**: pre-generated static bindings for performance-critical hot paths (transform, stats), reflection-based dynamic registration for user-defined custom types.

**Hot reload**: C# uses `AssemblyLoadContext` isolation. Each script assembly is loaded in its own context. To reload: create new context, load updated assembly, transfer serialized component state (serialized via a lightweight attribute-reflection system), unload old context. This is how Unity's hot reload works internally.

### Chosen Architecture

```
ScriptingLayer
├── C# Host (dotnet-hosting API)
│   ├── AssemblyLoadContext per script module
│   ├── Hot reload: serialize → unload → reload → deserialize
│   └── [Component] / [System] / [Inspect] attributes drive editor integration
├── Python Host (CPython embedding)
│   ├── GIL managed by engine: Python scripts run in a dedicated time slice
│   ├── @component / @system / @inspect decorators
│   └── Primary use: AI scripts, editor macros, data processing
├── Java Host (GraalVM espresso — optional, enabled on project creation)
│   └── Primary use: editor plugins, tool integrations
└── Binding Generator (runs at build time)
    ├── Parses engine headers with libclang
    ├── Generates C#/Python/Java bindings
    └── Any C++ function tagged [GWS_SCRIPT_BIND] is exported

// Example C++ export tag
GWS_SCRIPT_BIND void ApplyDamage(Entity target, float amount, DamageType type);
```

---

# PART 8 — VISUAL SCRIPTING

---

## 8.1 Node Graph & VM

### Research Findings

Unreal Blueprints VM (Intax's reverse-engineering blog, 2023) is the most studied game visual scripting VM:
- Each entry point (Custom Event, Function) compiles to a `UFunction` containing a `TArray<uint8>` of bytecode
- The graph is **topologically sorted** before compilation — the spaghetti node layout is linearized
- Bytecode instructions are members of `EExprToken` enum (roughly 60 opcodes)
- Execution: a simple switch-based interpreter walks the bytecode array, dispatching to native C++ functions for action nodes
- Data pins resolve at compile time — pin connections become variable reads/writes in the bytecode

For our engine: the VM does not need to be as complex as Blueprints. A simpler design:
1. Graph editor creates a JSON/binary representation of nodes + connections
2. Compiler topologically sorts the DAG, detects cycles (feedback through explicit "event" nodes)
3. Compiler emits bytecode for a simple stack-based VM
4. VM executor: `ExecuteGraph(GraphHandle, Entity, Context)` — iterates bytecode, dispatches node actions

For the **async/coroutine** case (quest sequences, cutscene logic that runs over many frames): the VM state is serializable. An async graph stores its program counter and stack in a `CoroutineState` component on the entity. Each frame, pending coroutines are advanced one step if their wait condition is satisfied.

### Chosen Architecture

```
Visual Script Graph (.gws_graph JSON)
  └── Nodes: { id, type, properties, input_pins, output_pins }
  └── Connections: { from_node, from_pin, to_node, to_pin }

Compiler (runs when graph is saved or on Play):
  1. Parse JSON → intermediate graph representation
  2. Validate: type-check all connections, check for missing required pins
  3. Topological sort (Kahn's algorithm)
  4. Generate bytecode:
     LOAD_CONST <value>         ; push constant to stack
     LOAD_VAR <name>            ; push variable value
     STORE_VAR <name>           ; pop and store
     CALL_NATIVE <func_id>      ; call registered C++ function
     CALL_GRAPH <graph_id>      ; call sub-graph
     BRANCH <true_offset> <false_offset>  ; conditional jump
     WAIT <condition_type>      ; yield until condition
     RETURN                     ; end execution

VM:
  - Stack-based, simple switch interpreter
  - Execution context: { Entity entity; float dt; Blackboard* bb; }
  - Native function table: `map<uint32_t, NativeFn>` — O(1) lookup by ID
  - Coroutine state: program counter + stack snapshot, stored on entity
```

---

# PART 9 — NEW PROJECT WIZARD

---

## 9.1 Feature Module System

### Architecture

Each module (movement, combat, etc.) is defined in `engine/modules/<system>/`:
```
engine/modules/movement/
  ├── module.json        — metadata: name, version, dependencies, conflicts
  ├── components/        — C++ component definitions
  ├── systems/           — C++ system implementations
  ├── assets/            — default assets: animation state machines, config JSONs
  ├── scripts/           — C# / Python starter scripts
  ├── visual_scripts/    — starter .gws_graph files
  └── editor/            — editor panel extensions (Inspector widgets, gizmos)
```

The wizard reads all `module.json` files, presents the selection UI, and on "Create":
1. Creates project folder structure
2. Copies selected module implementations into `game/systems/`
3. Generates `game/project_config.json` listing active modules
4. Generates a starter `main.cpp` that initializes selected systems
5. Generates C# / Python bindings for selected systems

Module dependencies are resolved: selecting "Full Combat" automatically also selects "Stat System" (dependency declared in module.json).

### Module Metadata Format
```json
{
  "name": "advanced_movement",
  "display_name": "Advanced Movement",
  "version": "1.0.0",
  "description": "Full movement system: walk/run/jump/dash/wallrun/climb/vault/grapple",
  "dependencies": ["stat_system"],
  "conflicts": [],
  "implements": ["IMovementSystem"],
  "editor_panel": "editor/MovementDebugPanel.cpp",
  "starter_assets": ["assets/movement_state_machine.json"]
}
```

---

# PART 10 — DEPENDENCIES FINALIZED

---

## Confirmed Third-Party Libraries

| System | Library | License | Notes |
|---|---|---|---|
| Windowing/Input | GLFW 3 | zlib | Cross-platform, already in .gitmodules |
| ECS | EnTT | MIT | Already in .gitmodules |
| Physics | Jolt Physics | MIT | Deterministic mode for rollback |
| Audio | miniaudio | MIT/Public Domain | Single-header, spatial audio |
| Editor UI | Dear ImGui | MIT | Already in .gitmodules |
| Logging | spdlog | MIT | Already in .gitmodules |
| Math | Custom (Phase 1 ✅) | — | Already done |
| Animation | ozz-animation | MIT | Add to third_party/ |
| Mesh LOD | meshoptimizer | MIT | Add to third_party/, includes clusterlod.h |
| AI Behavior Trees | BehaviorTree.CPP | MIT | Add to third_party/ |
| Mesh Loading | cgltf | MIT | Single-header GLTF loader |
| Image Loading | stb_image | Public Domain | Single-header |
| RT Denoising | NRD (NVIDIA) | MIT | Optional, hardware RT quality |
| Networking | ENet | MIT | Reliable UDP transport layer |
| Scripting C# | .NET 8 CoreCLR | MIT | Via hostfxr embedding API |
| Scripting Python | CPython 3.12 | PSF | Via standard embedding API |
| Shader compilation | glslang / shaderc | Apache 2 | GLSL→SPIR-V for Vulkan |
| Unit testing | Catch2 | BSL-1.0 | Already in .gitmodules |

**New additions to add to .gitmodules:**
```ini
[submodule "third_party/ozz-animation"]
    path = third_party/ozz-animation
    url = https://github.com/guillaumeblanc/ozz-animation.git

[submodule "third_party/meshoptimizer"]
    path = third_party/meshoptimizer
    url = https://github.com/zeux/meshoptimizer.git

[submodule "third_party/BehaviorTree.CPP"]
    path = third_party/behaviortree_cpp
    url = https://github.com/BehaviorTree/BehaviorTree.CPP.git

[submodule "third_party/cgltf"]
    path = third_party/cgltf
    url = https://github.com/jkuhlmann/cgltf.git

[submodule "third_party/JoltPhysics"]
    path = third_party/jolt
    url = https://github.com/jrouwe/JoltPhysics.git

[submodule "third_party/enet"]
    path = third_party/enet
    url = https://github.com/lsalzman/enet.git
```

---

# PART 11 — IMPLEMENTATION ORDER (REVISED)

Based on all research, this is the correct order with no avoidable rework:

```
Phase 1  ✅ DONE  Foundation (math, memory, physics module, logging, file I/O)

Phase 2  Platform layer
         GLFW window, OpenGL 4.6 context, raw input system
         DEPENDENCY: None beyond Phase 1

Phase 3  Renderer — Forward first, then clustered deferred
         3a. Minimal forward renderer (triangles on screen, no lighting)
         3b. PBR materials (metallic-roughness, IBL)
         3c. Shadow maps (CSM for directional light)
         3d. Depth prepass + HZB generation
         3e. G-buffer + clustered deferred
         3f. RT infrastructure (software first, hardware extension)
         3g. Post processing (SSAO, bloom, TAA, ACES)
         DEPENDENCY: Phase 2

Phase 4  Engine core
         4a. ECS (EnTT integration, component definitions)
         4b. Job system (work-stealing thread pool)
         4c. Asset pipeline (Handle<T>, async loader, hot reload)
         4d. Audio (miniaudio spatial)
         DEPENDENCY: Phase 2, 3a

Phase 5  Animation
         5a. ozz-animation integration, GLTF loading (cgltf)
         5b. Blend trees, state machines
         5c. FABRIK IK, two-bone IK
         5d. Secondary motion chains
         DEPENDENCY: Phase 4

Phase 6  Scripting
         6a. C# embedding (.NET 8 hostfxr)
         6b. Python embedding (CPython)
         6c. Binding generator (libclang)
         6d. Hot reload system
         DEPENDENCY: Phase 4

Phase 7  Physics (Jolt integration)
         7a. Rigid bodies, character controller
         7b. Vehicle physics
         7c. Hitbox system (per-bone, frame-data driven)
         DEPENDENCY: Phase 4, 5

Phase 8  Networking
         8a. ENet UDP transport
         8b. Client prediction + server reconciliation
         8c. Rollback netcode (deterministic simulation layer)
         8d. Session management, matchmaking hooks
         DEPENDENCY: Phase 4, 7 (deterministic physics required)

Phase 9  Culling & Streaming
         9a. Two-pass HZB culling (GPU indirect)
         9b. meshoptimizer LOD generation
         9c. Chunk streaming system
         9d. Portal culling (indoor)
         DEPENDENCY: Phase 3, 4

Phase 10 AI
         10a. BehaviorTree.CPP integration
         10b. Perception system (FOV, hearing, memory)
         10c. Pathfinding (Recast/Detour)
         10d. Squad AI, boss phase machine
         DEPENDENCY: Phase 4, 7

Phase 11 Game Systems (pre-built modules)
         All wizard-selectable systems
         DEPENDENCY: Phases 4–10 (all engine systems must be stable)

Phase 12 World & Narrative
         Time of day, weather, quest graph, NPC schedules
         DEPENDENCY: Phase 11

Phase 13 Visual Script Editor + VM
         Node graph canvas (Dear ImGui), compiler, bytecode VM
         DEPENDENCY: Phase 6 (scripting bindings)

Phase 14 Editor (full Unity-style)
         All panels, viewport, profiler, project wizard
         DEPENDENCY: Phase 4, 6, 13

Phase 15 AI Assistant Integration
         Project context API, LLM backend, action sandbox
         DEPENDENCY: Phase 14

Phase 16 The Game
         Built on top of everything above
         DEPENDENCY: All phases
```

---

*This document should be committed as `docs/technical-pre-planning.md` and updated
whenever a technical decision changes. Reference this document before implementing
any new system to ensure choices are grounded in research.*
