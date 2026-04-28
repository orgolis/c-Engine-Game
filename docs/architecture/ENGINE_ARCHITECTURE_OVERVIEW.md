# GameWorldshaper Engine — Full Architecture Plan
> Building toward the game described in the lore documents, with a Unity-style editor,
> pre-built game systems, integrated AI assistant, visual scripting, multi-language support,
> hardware/software ray-tracing, and maximum runtime efficiency.

---

## Part 1 — Engine Editor (Unity 6 Style)

### Layout

The editor is built on Dear ImGui with a dockable multi-panel layout:

```
┌────────────────────────────────────────────────────────────────────────┐
│  Menu Bar  [File | Edit | Build | Tools | AI Assistant | Help]         │
├──────────┬─────────────────────────────────┬──────────────────────────┤
│          │                                 │                          │
│  Scene   │         3D Viewport             │    Inspector             │
│Hierarchy │    (Gizmos, Grid, Handles)      │  (Component Properties)  │
│          │                                 │                          │
│          │                                 │                          │
├──────────┴─────────────────────────────────┴──────────────────────────┤
│  Project Browser  |  Console  |  Profiler  |  Visual Script Editor    │
└────────────────────────────────────────────────────────────────────────┘
```

### Core Editor Panels

**Scene Hierarchy** — tree view of entities, drag/drop parenting, visibility toggles, lock toggles.

**3D Viewport** — rendered via the engine's own render graph. Supports:
- Transform gizmos (translate / rotate / scale), vertex snapping, grid snapping
- Camera orbit, fly-through, top/side/front orthographic views
- Play / Pause / Step buttons that run the full game loop inside the editor
- Object selection with mouse picking (GPU read-back of an ID buffer)
- Scene overlays: physics colliders, light volumes, AI nav meshes, audio radii

**Inspector** — reflection-driven panel. Any field tagged `[Inspect]` in C++, C#, Java, or Python appears here with appropriate widgets (sliders, color pickers, dropdowns, asset selectors, curves).

**Project Browser** — file system view of the project folder. Drag assets into the scene or Inspector. Thumbnail generation for meshes, textures, materials. Double-click opens assets in the appropriate sub-editor (Material Editor, Animation Editor, Visual Script Editor).

**Console** — filtered log output from the engine logger (spdlog backend). Filters by level (debug / info / warn / error). Click a log line to jump to the source file.

**Profiler** — frame time graph with per-system breakdown: render, physics, scripting, audio, AI. GPU pass timers displayed per render pass. Memory usage per allocator.

**Visual Script Editor** — full graph canvas (described in Part 4).

**AI Assistant Panel** — integrated sidebar (described in Part 6).

---

## Part 2 — New Project Wizard (Feature Selection Screen)

When a developer creates a new project, a guided wizard presents feature modules to include. Each selected module is fully pre-implemented and immediately usable.

### Wizard Flow

```
Step 1: Project Name & Location
Step 2: Rendering Settings
Step 3: Gameplay Systems
Step 4: World Settings
Step 5: Multiplayer
Step 6: Final Summary → Create Project
```

### Step 2 — Rendering Settings

| Option | Description |
|---|---|
| Render Pipeline | Forward / Deferred / Clustered Deferred |
| Ray Tracing | None / Software RT / Hardware RT (DXR/Vulkan RT) |
| Target Platform | PC / Console / Mobile |
| Resolution Scaling | Native / DLSS / FSR / TAA Upscaling |

### Step 3 — Gameplay Systems (tick all that apply)

**Camera Mode**
- [ ] First Person
- [ ] Third Person (orbit camera with wall collision)
- [x] Both (switchable at runtime)

**Movement System**
- [ ] Standard (walk / run / jump / crouch / slide)
- [ ] Advanced (all above + wall run / climb / vault / grapple / air dash)
- [ ] Custom (blank state machine, build from scratch)

**Combat System**
- [ ] None
- [ ] Melee (frame-data driven, hitstop, poise, parry/dodge)
- [ ] Ranged (projectile system, hitscan, spread)
- [ ] Ability-based (cooldown manager, cast states)
- [ ] Full (all above combined)

**Vehicle System**
- [ ] None
- [ ] Land (cars, tanks — wheeled rigid body physics)
- [ ] Water (boats — buoyancy simulation)
- [ ] Air (aircraft, helicopters — lift/drag aerodynamics)
- [ ] Space (spaceships — zero-G thrust model)
- [ ] All

**Stat System**
- [ ] None
- [ ] Simple (HP, Stamina, Mana)
- [ ] RPG (HP, Stamina, Mana, Strength, Agility, Intelligence, Resilience, Speed — modifier pipeline)

**Ability System**
- [ ] None
- [ ] Basic (3 abilities + 1 ultimate, cooldown driven)
- [ ] Destiny-style (3 slots + ultimate, aspects + fragments modifier layer)
- [ ] Full skill-expression (chaining, cancels, combo windows)

**Inventory System**
- [ ] None
- [ ] Simple (grid-based, weight limit)
- [ ] Full (equipment slots: helmet, chest, gloves, legs, boots, 3 weapon slots, relics)

**Skill Tree System**
- [ ] None
- [ ] Linear (unlock nodes in sequence)
- [ ] Branching (class-based trees with synergy bonuses)
- [ ] Web (Elden Ring / Path of Exile style free-form)

**Loot System**
- [ ] None
- [ ] Basic (Common / Rare / Legendary)
- [ ] Full (Common / Rare / Legendary / Mythic / Ancient, set bonuses, infusion)

**Enemy AI**
- [ ] None
- [ ] Basic (patrol, chase, attack)
- [ ] Advanced (behavior trees, perception system, squad coordination, dynamic difficulty)
- [ ] Boss AI (phase-based state machine layered on behavior tree)

**Cutscene System**
- [ ] None
- [ ] Basic (camera path + timeline for dialogue)
- [ ] Full (sequencer with animation tracks, audio tracks, event tracks, blend shapes)

**Open World**
- [ ] None
- [ ] World Streaming (async chunk loading, LOD distance bands, occlusion streaming)
- [ ] Full (above + dynamic time of day, weather, NPC schedules, quest graph)

**Multiplayer**
- [ ] None
- [ ] Co-op (host + clients, server-authoritative, client prediction)
- [ ] PvP (rollback netcode, lag compensation, anti-cheat layer)
- [ ] Full MMO-style (dedicated server, session management, matchmaking hooks)

### What "Pre-Implemented" Means

When a system is selected, the project is created with:
- Full C++ implementation in `game/systems/`
- Editor integration (inspector widgets, scene gizmos)
- A starter Visual Script graph demonstrating the system
- C# / Java / Python bindings so scripts can query and extend it
- Documented component types ready to drop onto entities

---

## Part 3 — Pre-Built Game Systems (Implementation Detail)

### 3.1 Movement System (Advanced)

Architecture: **hierarchical state machine** with a priority-based input buffer.

States: Grounded → Walking, Running, Crouching, Sliding | Airborne → Jumping, Falling, WallRunning, Dashing, Gliding | Interaction → Climbing, Vaulting, Grappling.

Each state owns: `Enter()`, `Update(float dt)`, `Exit()`, `CanTransitionTo(State*)`. The input buffer stores inputs timestamped to the last 8 frames — a dash input pressed 3 frames before landing still registers. All state data is serialised as deterministic commands for rollback netcode compatibility.

Root motion blending: animator drives position delta per frame; movement system blends physics velocity with animation displacement based on a blend weight per state.

### 3.2 Combat System (Full)

**Frame data pipeline**: every attack asset stores startup / active / recovery frame counts. The engine steps through these every frame tick and opens/closes hitboxes accordingly.

**Hitbox layer**: completely decoupled from the render mesh. Per-bone capsule/sphere hitboxes are authored in the editor, stored as a HitboxSet asset, and sampled per animation frame. Hit detection uses swept capsule queries against the physics world.

**Hitstop**: on a confirmed hit, both attacker and defender pause their animation playback for N frames (configurable per attack). This is implemented as a time-scale override on the animator, not a global pause.

**Poise system**: each attack carries a poise damage value. Defenders have a poise threshold; exceeding it triggers a stagger animation. Poise regenerates over time.

**Combo system**: combo graph asset — nodes are attacks, edges are transition windows. Pressing an attack input during the combo-accept window of the current attack queues the next.

### 3.3 Stat System (RPG)

A **modifier pipeline** identical in spirit to Destiny 2's:

Every stat is a float. Any equipped item, active buff, passive skill, or status effect adds a `StatModifier` to the entity's `StatComponent`. Modifiers are typed: `Flat`, `PercentAdd`, `PercentMultiply`. Final value is computed lazily and cached until the modifier list changes.

```
FinalHP = (BaseHP + sum(Flat)) * (1 + sum(PercentAdd)) * product(PercentMultiply)
```

All stat queries go through `StatComponent::Get(StatType)` — no hardcoded values anywhere in gameplay code.

### 3.4 Ability System

Structure matching the lore's Arcani system:

- **AbilitySlot** component holds 3 standard abilities + 1 ultimate
- Each ability is an **AbilityAsset** (data asset): cast time, cooldown, cost, effects, visual script graph for logic
- **AspectSlots** and **FragmentSlots** hold passive modifier assets that alter ability behaviour
- **CooldownManager** handles shared cooldowns, charges, and energy resources
- Abilities can be authored entirely in the Visual Script editor or in C# / Python

### 3.5 Inventory & Loot System

Grid-based inventory with slot categories. Equipment slots map to the defined loadout: 3 weapon slots, 5 armour pieces, N relic slots.

Loot tiers (Common → Ancient) carry different rules at generation time: Ancients restrict carryable count; Mythics carry set bonuses; Legendaries carry set effects. Item generation is data-driven — loot tables are JSON assets edited in the Project Browser.

### 3.6 Vehicle System

Each vehicle type has a dedicated physics model:

- **Land**: standard Jolt Physics wheeled vehicle (raycast suspension, friction model, differential)
- **Water**: buoyancy solver (submerged volume vs. displaced water mass, wave interaction with the PDE wave solver from the physics module)
- **Air**: simplified aerodynamics (lift = CLρv²A/2, drag opposing velocity)
- **Space**: pure thrust model, no drag, attitude control via reaction wheels

Enter/exit is an interaction system: proximity trigger → contextual prompt → blend from character animation into vehicle seated pose.

### 3.7 Enemy AI System

Three-tier architecture:

**Perception System** — sight cones (FOV + max range + light level attenuation), hearing radii, memory of last-known positions. Implemented as a separate update tick at lower frequency (10 Hz) than gameplay (60 Hz).

**Behavior Tree** — node-based tree evaluated per AI tick. Built-in nodes: Sequence, Selector, Parallel, Decorator (Inverter, Cooldown, Loop), Condition, Action. Custom action nodes are authored in Visual Script or C#. Trees are assets editable in the editor.

**Boss Phase Machine** — sits above the behavior tree. Phase transitions trigger on HP thresholds or timed conditions. Each phase swaps the active behavior tree and can modify stats, spawn adds, change arena geometry.

Squad coordination: a `SquadManager` component on a squad leader entity broadcasts perceived threats to squad members and coordinates flanking via role assignment (Flanker, Suppressor, Support).

---

## Part 4 — Visual Script System (Blueprint-Style)

### Architecture

The Visual Script system is a **graph-based, node-driven programming environment** compiled to an internal bytecode that runs on a lightweight VM inside the engine.

Graphs are assets (`.gws_graph` files). They can be attached to entities as a component — making them equivalent to Unity's MonoBehaviour or Unreal's Blueprint.

### Node Types

**Event Nodes** (entry points): `OnStart`, `OnUpdate(dt)`, `OnFixedUpdate(dt)`, `OnCollisionEnter`, `OnAbilityActivated`, `OnStatChanged`, `OnDeath`, custom events.

**Flow Nodes**: Branch (if/else), Sequence, ForLoop, WhileLoop, Switch, Delay, Gate.

**Data Nodes**: Variables (local / entity-scoped / world-scoped), Get/Set Component, Math operations, String operations, Array operations.

**Engine Nodes**: SpawnEntity, DestroyEntity, PlayAnimation, SetStat, AddModifier, ApplyDamage, PlaySound, PlayVFX, SetMaterial, Raycast, OverlapSphere.

**Lore-Specific Nodes**: ActivateAbility, ApplyArcanum, GrantSigil, TriggerTruthseekerEvent — pre-wired to the game systems.

### Execution Model

Graphs are synchronous by default. An `Async` wrapper node converts a subgraph into a coroutine that yields across frames. This mirrors how cutscene sequences and quest progressions work — long event chains that don't block the game loop.

### Interoperability

- C++ can expose any function to the graph system with a macro: `GWS_EXPOSE_TO_GRAPH(MyFunction, "Category/Name")`
- C# / Java / Python script methods marked with `[GraphNode]` or `@graph_node` appear in the node library automatically
- Graphs can call scripts; scripts can call into graphs via an event bus

---

## Part 5 — Scripting (C#, Java, Python)

All three languages run inside the engine via embedded runtimes:

| Language | Runtime | Use Case |
|---|---|---|
| C# | .NET 8 (Mono fallback for consoles) | Primary gameplay scripting, closest to Unity feel |
| Java | GraalVM native image | Tools, editor extensions, data processors |
| Python | CPython 3.12 embedded | Rapid prototyping, AI assistant scripts, pipeline tools |

### Binding Layer

A unified C++ API is exposed to all three languages via an auto-generated binding layer (similar to how Godot does it with GDExtension). Any engine type — `Entity`, `Transform`, `StatComponent`, `AbilitySlot` — is accessible from all three languages.

Custom components can be written in any language:

```csharp
// C# Component
[Component]
public class TruthseekerBehaviour : ScriptComponent {
    [Inspect] public float SigilPulseRate = 1.5f;
    
    public override void OnUpdate(float dt) {
        if (GetComponent<StatComponent>().Get(StatType.Essence) < 20f)
            GetComponent<AbilitySlot>().Activate(0);
    }
}
```

```python
# Python Component
@component
class OrgolisVoice(ScriptComponent):
    @inspect
    pulse_rate: float = 1.5
    
    def on_update(self, dt: float):
        if self.entity.stats.get("essence") < 20:
            self.entity.ability_slot.activate(0)
```

### Hot Reload

Scripts support hot reload during Play mode: the file watcher detects a save, recompiles only the changed script, swaps the running instance preserving serialised state. This is the primary quality-of-life feature matching Unity's workflow.

---

## Part 6 — Integrated AI Assistant

The AI assistant is a panel in the editor that has **full read/write access to the project**. It is not a chatbot overlay — it is a first-class development tool.

### Capabilities

**Code Generation**: describe a system in natural language → assistant writes the C++ component, C# script, or Visual Script graph and places it in the correct project folder.

**Scene Assistance**: "Place 12 patrol guards in a grid around this building" → assistant reads the scene hierarchy, calculates positions, and creates the entities.

**Lore-Aware Suggestions**: the assistant has the full lore document indexed. "Create a Truthseeker NPC with Orgolis-granted Solorig abilities" will wire up the correct ability assets, stat values, and faction alignment automatically.

**Error Diagnosis**: when the console shows a compile error or runtime crash, the assistant has the stack trace and the relevant source files in context and proposes a fix.

**Refactor**: "rename all references to OldSystemName to NewSystemName across the project" — assistant performs a safe multi-file rename with preview before applying.

**Documentation**: "explain what this Visual Script graph does" → plain-language walkthrough of the selected graph.

### Implementation

The assistant panel makes API calls to an LLM endpoint (configurable — local model via Ollama or cloud API). The engine provides a structured project context object: file tree, open scene graph, selected entity components, recent console output, current compilation errors. This context is injected into every request.

A **sandboxed action API** lets the assistant perform write operations only through reviewed actions the developer confirms before they execute.

---

## Part 7 — Rendering: Ray Tracing

### Software Ray Tracing

Available on all hardware. Implemented as a compute shader pass using a **BVH (Bounding Volume Hierarchy)** built over the scene's meshes every frame (rebuilt incrementally for dynamic objects).

Used for:
- Indirect diffuse GI (irradiance caching at low resolution, upscaled via temporal accumulation)
- Soft shadow fallback when hardware RT is unavailable
- Reflections for planar surfaces (mirrors, water)

Performance: software RT is expensive. Reserved for mid-range PC and above. Mobile uses screen-space fallbacks.

### Hardware Ray Tracing (DXR / Vulkan Ray Tracing)

Activated automatically when the GPU supports `VK_KHR_ray_tracing_pipeline` or DirectX 12 DXR.

Pass architecture:

```
Shadow Pass (RT)       → per-light shadow rays, replaces shadow maps for primary light
Reflection Pass (RT)   → mirror-quality reflections for PBR surfaces
GI Pass (RT)           → one-bounce indirect diffuse, denoised via SVGF
Ambient Occlusion (RT) → ray-traced AO replaces SSAO on high settings
```

A **denoiser** (spatial + temporal filter, optionally NVIDIA NRD or AMD RadeonRays) runs after each RT pass to reconstruct clean output from 1 ray per pixel.

The render settings dropdown maps to preset configurations:
- Low: no RT, SSAO + shadow maps + screen-space reflections
- Medium: software RT GI, shadow maps, SSR
- High: hardware RT shadows + reflections, software RT GI
- Ultra: full hardware RT for all passes + DLSS/FSR reconstruction

---

## Part 8 — Culling Systems

All culling runs before the draw call submission stage, progressively filtering the visible set.

### Frustum Culling

Every entity with a renderable component has an AABB in world space (updated when the transform changes). At render time, the camera frustum (6 planes) is tested against each AABB. Entities outside the frustum are skipped entirely — no draw call, no shader bind.

Implementation: SIMD-batched plane/AABB tests. 8 entities tested per CPU cycle using AVX2.

### Occlusion Culling

Two-phase system:

**Phase 1 — HZB (Hierarchical Z-Buffer)**: the previous frame's depth buffer is downsampled into a mip chain. For each object, its projected bounding sphere is tested against the HZB mip at an appropriate resolution level. Objects determined to be behind existing depth are culled. One frame of latency — acceptable because objects reappear immediately if they become visible.

**Phase 2 — Software Occlusion (optional)**: large occluder meshes (buildings, terrain chunks) are rasterised in software on the CPU at 256×144 resolution. Used for interior/exterior transitions where the HZB lags.

### Portal Culling (Indoor Spaces)

For dungeon and interior environments (matching the Abyss and Tarlahem sections in the lore): rooms are bounded volumes connected by portals (doorways, archways). The renderer walks the portal graph from the camera's room outward. Only rooms reachable through visible portals are submitted. This eliminates entire geometry batches for indoor environments.

### Distance Culling

Per-component max draw distance. Configurable per object type (foliage: 80m, props: 150m, architecture: unlimited). Objects beyond their threshold are culled before frustum testing.

### GPU Culling (Compute)

On high-end hardware, the entire visible set (post-frustum-cull) is passed to a compute shader as a buffer of bounding data. The GPU issues indirect draw calls only for objects that pass HZB occlusion in parallel. This is the Nanite-adjacent approach — the CPU never iterates the final draw list.

---

## Part 9 — Mesh Simplification & LOD

### Static LOD

Every mesh asset automatically generates LOD levels at import time using a **quadric error metrics simplification** algorithm (similar to Meshopt):

| LOD | Target Poly Count | Switch Distance |
|---|---|---|
| LOD 0 | 100% | 0–15m |
| LOD 1 | 50% | 15–40m |
| LOD 2 | 25% | 40–80m |
| LOD 3 | 10% | 80–150m |
| LOD 4 | 2% (impostor) | 150m+ |
| Culled | 0 | > max draw distance |

LOD transitions are dithered (screen-space stipple pattern fades between levels) to eliminate pop.

### Impostor System

Beyond LOD 4, 3D meshes are replaced by **camera-facing quads** textured with a pre-rendered atlas of the mesh from multiple angles (octahedron mapping). Trees, distant rocks, and secondary structures all use impostors at extreme distance. Atlas is generated at project build time.

### Nanite-Style Virtual Geometry (Future Phase)

A clustered mesh system where geometry is subdivided into 128-triangle clusters, a cluster BVH is built, and only clusters that are visible and at the correct detail level for screen-space error budget are rendered. This removes the concept of discrete LOD levels for hero assets. Marked as Phase 3+ feature — the LOD system above is the Phase 1 implementation.

### Terrain-Specific LOD

Open world terrain uses a **CDLOD (Continuous Distance-Dependent Level of Detail)** quadtree. Terrain geometry is generated procedurally from a height map at variable tessellation density based on camera distance. Morphing between levels is smooth. This is the correct approach for Wuthering Waves / Cyberpunk scale open worlds.

---

## Part 10 — Efficiency Systems Summary

| System | Technique | Phase |
|---|---|---|
| Rendering | Clustered deferred shading (lights in 3D clusters) | Phase 3 |
| Rendering | Bindless textures (reduce CPU bind overhead) | Phase 3 |
| Rendering | Indirect draw calls via GPU culling compute | Phase 3 |
| Memory | Custom allocators (stack/pool/general — Phase 1 done) | Phase 1 ✅ |
| Memory | Asset streaming with reference-counted handles | Phase 4 |
| CPU | Job system (work-stealing thread pool for parallelism) | Phase 4 |
| CPU | ECS data-oriented layout (components in SoA arrays) | Phase 4 |
| AI | Spatial hashing for perception queries | Phase 10 |
| Physics | Broad phase AABB tree (Jolt Physics built-in) | Phase 7 |
| Scripting | JIT compilation for C# (CoreCLR), AOT for consoles | Phase 6 |
| Networking | Deterministic fixed-point simulation for rollback | Phase 8 |

---

## Part 11 — Updated Phase Roadmap

```
Phase 1  Foundation (✅ Active)
         Math, memory, physics module, logging, file I/O
         
Phase 2  Platform & Window
         GLFW abstraction, OpenGL 4.6 context, input system
         
Phase 3  Renderer
         Render graph, deferred pipeline, PBR, shadows,
         RT infrastructure (software first, hardware extension)
         
Phase 4  Core Engine Systems
         ECS (EnTT), asset pipeline, job system, audio (miniaudio)
         
Phase 5  Animation
         Blend trees, IK solvers, secondary motion (cloth/cape chains),
         procedural hit reactions, root motion
         
Phase 6  Scripting Layer
         C# (.NET 8 embed), Python (CPython embed), Java (GraalVM),
         binding generator, hot reload, Visual Script VM
         
Phase 7  Physics Integration
         Jolt Physics rigid bodies, character controller,
         vehicle physics (land/water/air/space), hitbox system
         
Phase 8  Networking
         UDP transport, client prediction + server reconciliation,
         rollback netcode for combat, session management
         
Phase 9  LOD, Culling, Streaming
         HZB, portal culling, GPU indirect, CDLOD terrain,
         impostor system, async chunk streaming
         
Phase 10 AI Systems
         Behavior trees, perception, squad AI, boss phase machines,
         pathfinding (Recast/Detour integration)
         
Phase 11 Pre-Built Game Systems
         All wizard-selectable systems fully implemented:
         movement, combat, stats, abilities, inventory, loot, vehicles
         
Phase 12 Cutscene & World Systems
         Sequencer timeline, quest graph, NPC schedules, time of day
         
Phase 13 Visual Script Editor
         Node graph canvas, bytecode compiler, VM, debugger
         
Phase 14 Editor (Unity-style)
         Full ImGui editor: hierarchy, inspector, viewport, project browser,
         profiler, console, new project wizard
         
Phase 15 AI Assistant Integration
         Project context API, LLM backend, action sandbox, lore indexing
         
Phase 16 Game Layer
         The actual game built on top of all the above using its own systems
```

---

## Part 12 — Folder Structure Addition

```
engine/
├── core/           (Phase 1 ✅)
├── platform/       (Phase 2)
├── renderer/
│   ├── graph/      render graph DAG
│   ├── passes/     shadow, geometry, lighting, post, RT passes
│   ├── rt/         ray tracing backend (software + hardware)
│   └── culling/    frustum, HZB, portal, GPU indirect
├── ecs/            (Phase 4)
├── animation/      (Phase 5)
├── scripting/
│   ├── csharp/     .NET 8 host
│   ├── python/     CPython embed
│   ├── java/       GraalVM embed
│   ├── visual/     Visual Script VM + bytecode compiler
│   └── bindings/   auto-generated cross-language bindings
├── physics/        (Phase 7 - Jolt integration)
├── network/        (Phase 8)
├── streaming/      (Phase 9)
├── ai/             (Phase 10)
└── audio/          miniaudio

game_systems/       (Phase 11 — pre-built, wizard-selectable)
├── movement/
├── combat/
├── stats/
├── abilities/
├── inventory/
├── loot/
├── vehicles/
├── cutscenes/
└── world/

editor/             (Phase 14)
├── panels/         hierarchy, inspector, viewport, browser, console
├── wizard/         new project creation flow
├── gizmos/
└── ai_assistant/   (Phase 15)
```

---

## Notes on the Lore Integration

The engine's pre-built systems map directly to the game's lore design:

- The **Arcani system** (Arcanum, Runik, Eldritch, Solorig, etc.) is implemented as **ability type tags** on AbilityAssets. Each tag can carry different cost models, visual effect sets, and restriction rules. Solorig's life-essence tithing is a modifier that drains the `Essence` stat on every ability activation and routes it to a designated recipient entity.

- The **Sigil system** maps to the **Relic slot system** — equipping a Sigil is equipping a relic with passive modifiers. The Sigil of Knowledge grants `TrueSight` buff implemented via the Eyes of Orgolis armor logic already designed.

- **Tarlahem and the Abyss** as explorable locations use **portal culling** for the dungeon interior and a dedicated `AbyssEnvironment` system that modifies Arcani cost rules, disables certain ability types (Orgolis's prison suppressing magic is a world-space `StatModifier` applied to all entities inside the zone).

- **Truthseeker faction logic** is a quest-graph asset. Faction reputation is a float stat on the player entity. NPC reaction behaviours check this stat through the Visual Script graph on their AI component.

---

*This document supersedes `docs/planning-session.md` on all engine architecture topics. 
Both files should be kept — planning-session.md for historical context, this file for current spec.*
