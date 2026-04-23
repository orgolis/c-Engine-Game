# GameWorldshaper Engine — Master Development Plan
> Logically ordered implementation roadmap from foundation to feature-complete engine.
> Last updated: April 13, 2026

---

## Table of Contents

1. [Overview](#overview)
2. [Phase 0 — Foundation (Weeks 1–2)](#phase-0--foundation-weeks-1–2)
3. [Phase 1 — Core Engine (Weeks 3–6)](#phase-1--core-engine-weeks-3–6)
4. [Phase 2 — Rendering Foundation (Weeks 7–12)](#phase-2--rendering-foundation-weeks-7–12)
5. [Phase 3 — Editor & Tooling (Weeks 13–18)](#phase-3--editor--tooling-weeks-13–18)
6. [Phase 4 — Gameplay Systems (Weeks 19–28)](#phase-4--gameplay-systems-weeks-19–28)
7. [Phase 5 — Advanced Rendering (Weeks 29–36)](#phase-5--advanced-rendering-weeks-29–36)
8. [Phase 6 — AI & Scripting (Weeks 37–44)](#phase-6--ai--scripting-weeks-37–44)
9. [Phase 7 — Networking & Polish (Weeks 45–52)](#phase-7--networking--polish-weeks-45–52)

---

## Overview

**Philosophy:** Build bottom-up. No system depends on a higher-layer system. Each phase adds capabilities; previous phases remain stable.

**Dependency Graph:**
```
Foundation (Allocators, Math, Logging)
    ↓
Core Engine (ECS, Events, Asset System, Time)
    ↓
Rendering (RenderGraph, GPU abstraction, basic deferred pass)
    ↓
Editor (ImGui, Scene hierarchy, Inspector)
    ↓
Gameplay (Movement, Combat, Stats, Abilities)
    ↓
Advanced Rendering (Ray Tracing, Advanced Culling, LOD)
    ↓
AI & Scripting (Behavior Trees, Visual Scripts, Language Bindings)
    ↓
Networking & Polish (Rollback, Multiplayer, Optimization)
```

**Testing Strategy:** Each phase ends with unit tests and integration tests. Automated benchmark suite tracks performance regressions.

---

# Phase 0 — Foundation (Weeks 1–2)

## Goals
- Establish build system (CMake)
- Core utility libraries
- Math library
- Memory allocator
- Logging framework

## 0.1 Project Structure & Build System

**Deliverable:** CMake build system, organized folder structure

**Tasks:**
1. Create CMake configuration for multi-platform builds (Windows MSVC, Linux Clang)
2. Set up include paths: `engine/core/` for internal headers, `third_party/` for vendored deps
3. Create library targets: `gws_core`, `gws_renderer`, `gws_ecs`, `gws_editor`, `gws_game`
4. Add compiler flags: `-Wall -Wextra` (Clang), `/W4` (MSVC), C++20 standard
5. Implement `gws/core/config.h` for build-time feature toggles

**References:**
- Engine structure: `engine/`, `editor/`, `game/`, `third_party/`

---

## 0.2 Math Library

**Deliverable:** `gws/core/math/` — SIMD-optimized vector, matrix, quaternion types

**Implementation:**
```cpp
// gws/core/math/math.h
namespace gws::math {
    struct Vec3 { float x, y, z; /* operations */ };
    struct Vec4 { float x, y, z, w; };
    struct Quat { float x, y, z, w; /* slerp, rotate */ };
    struct Mat4 { float m[16]; /* multiply, inverse, decompose */ };
    
    // SIMD batches
    struct Vec3x8 { // 8 Vec3s in SIMD registers
        __m256 x, y, z;
    };
}
```

**Tasks:**
1. Implement single-vector types: `Vec2`, `Vec3`, `Vec4`, `Quat`
2. Implement matrix types: `Mat3`, `Mat4`
3. Add SIMD intrinsic-based batch operations (`Vec3x8`, `Mat4x4Batch`)
4. Implement common operations: `dot`, `cross`, `normalize`, `lerp`, `slerp`, `matrix_multiply`
5. Add geometric helpers: `frustum_planes_from_viewproj`, `AABB`, `sphere`, `capsule`
6. Unit tests: verify math accuracy vs. GLM reference

**Dependencies:** None (std library only)

**Efficiency Notes:**
- Vec3/Vec4 align to 16 bytes for SIMD loads
- Matrix multiply uses blocked multiplication for cache efficiency
- Batch operations use AVX2 where available

---

## 0.3 Memory Allocator

**Deliverable:** `gws/core/memory/allocator.h` — Custom allocators for different use cases

**Implementation:**  
```cpp
namespace gws::memory {
    struct AllocatorStats {
        size_t allocated = 0;
        size_t freed = 0;
        size_t peak = 0;
        uint32_t allocation_count = 0;
    };
    
    class LinearAllocator {
        // Frame-based allocator: allocate freely, reset once per frame
    };
    
    class PoolAllocator {
        // Fixed-size object pool: O(1) allocate/free
    };
    
    class BuddyAllocator {
        // General-purpose: defragmenting buddy system
    };
}
```

**Tasks:**
1. Implement `LinearAllocator` (for frame data, particles, temporary objects)
2. Implement `PoolAllocator` (for entities, components with known fixed sizes)
3. Implement `BuddyAllocator` (for long-lived allocations)
4. Add global allocator registry with per-allocator stats
5. Override `operator new`/`operator delete` to route through chosen allocator
6. Add leak detection in debug builds

**Dependencies:** None

---

## 0.4 Logging Framework

**Deliverable:** `gws/core/log/logger.h` — Based on spdlog, with engine integration

**Tasks:**
1. Wrap spdlog: create `Logger` class with sinks (console, file, circular buffer)
2. Add log levels: Debug, Info, Warning, Error, Critical
3. Implement macros: `GWS_LOG_INFO()`, `GWS_LOG_WARN()`, etc.
4. Add structured logging: log + metadata (category, source file, line number)
5. Create `CircularBufferSink` (last 10,000 log entries in memory for console display)
6. Initialize at engine startup, redirect to editor console

**Dependencies:** spdlog (vendored in `third_party/spdlog/`)

---

## 0.5 Testing Harness

**Deliverable:** `tests/` directory with Catch2 framework

**Tasks:**
1. Set up Catch2 in CMake
2. Create test runner executable
3. Add first suite of tests: math library (Vec3, Quat, Mat4 operations)
4. Add memory allocator tests
5. Set up CI script to run tests on every commit

**Dependencies:** Catch2 (vendored)

---

# Phase 1 — Core Engine (Weeks 3–6)

## Goals
- ECS foundation (EnTT integration)
- Event system
- Asset management
- Game time and tick rates
- Basic file I/O

## 1.1 ECS World & Components

**Deliverable:** `engine/ecs/world.h`, component registry

**Implementation:** (Reference: `code-examples.md` § 5)

**Tasks:**
1. Thin wrapper around `entt::registry` (already included in CMakeLists)
2. Define core component types in `engine/ecs/components.h`:
   - `TransformComponent` (position, rotation, scale)
   - `RenderableComponent` (mesh ID, material ID, visible flag)
   - `VelocityComponent` (linear, angular velocity)
   - `ActiveComponent` (entity enabled/disabled)
3. Create `World` class with typed queries:
   ```cpp
   auto& view = world.view<Transform, Velocity>();
   ```
4. Add grouping API for hot paths: `world.group<Renderable, Transform>()`
5. Implement entity prefab system: serialize/deserialize ECS snapshot
6. Add entity pooling: reuse entity IDs to avoid version churn

**Tests:**
- Create/destroy entities
- Add/remove components
- Query with various component combinations
- Performance: 100k entity iteration benchmark

---

## 1.2 Event System

**Deliverable:** `engine/core/events/event_bus.h` — Decoupled pub/sub

**Implementation:**
```cpp
struct CollisionEvent {
    uint32_t entity_a, entity_b;
    Vec3 contact_point;
};

class EventBus {
    template<typename EventT>
    void subscribe(std::function<void(const EventT&)> handler);
    
    template<typename EventT>
    void publish(const EventT& event);
};
```

**Tasks:**
1. Implement `EventBus` with `std::any` or templated handlers
2. Define event types: (to be added incrementally)
   - `InputEvent` (keyboard, mouse, gamepad)
   - `CollisionEvent` (physics callbacks)
   - `DamageEvent` (combat)
   - `AbilityActivatedEvent` (ability system)
3. Add event filtering (by entity, by priority)
4. Ensure zero-copy event passing (references or move semantics)

**Tests:**
- Subscribe/unsubscribe
- Multi-subscriber per event
- Event ordering correctness

---

## 1.3 Asset System

**Deliverable:** `engine/asset/asset_manager.h` — Unified asset loading & caching

**Implementation:**
```cpp
class AssetManager {
    // Reference-counted asset cache
    // Async loading with load queue
    // Hot-reload support for development
    
    Handle<Mesh> load_mesh(const char* path);
    Handle<Texture> load_texture(const char* path);
};

struct AssetHandle<T> {
    T* get();  // immediate access (blocks if loading)
    bool ready();
    uint32_t version;  // for hot-reload detection
};
```

**Tasks:**
1. Create `AssetManager` singleton with reference counting
2. Implement basic asset types: `Mesh`, `Texture`, `Material` (data-only, no rendering yet)
3. Add JSON-based asset metadata: format, compression, dependencies
4. Implement disk cache system (serialize GPU resources to disk)
5. Add hot-reload: watch asset files, reload on change, notify systems
6. Create `assets/` directory structure: `meshes/`, `textures/`, `materials/`, `audio/`, `scripts/`

**Dependencies:** JSON (nlohmann/json, vendored)

**Tests:**
- Load/unload assets
- Reference counting
- Cache behavior under memory pressure

---

## 1.4 Game Time & Tick Rates

**Deliverable:** `engine/core/time.h` — Fixed and variable timestep management

**Implementation:**
```cpp
class GameTime {
    float dt_frame;      // variable timestep (render frame)
    float dt_fixed;      // fixed timestep (physics, gameplay)
    float total_time;
    uint64_t frame_number;
    
    // Tick rate configuration
    uint32_t fixed_tick_hz = 60;
};
```

**Tasks:**
1. Implement `GameTime` class with frame timing
2. Add fixed-step simulation: accumulator pattern (physics at 60 Hz, render at 60+ Hz)
3. Implement frame limiting (vsync, max frame rate cap)
4. Add time scaling (slow-motion, fast-forward)
5. Profiling hooks: `timer_start()`, `timer_end()` for per-system timing

**Tests:**
- Frame delta time accuracy
- Fixed timestep correctness over 1000 frames
- Time scaling behavior

---

## 1.5 File I/O & Serialization

**Deliverable:** `engine/core/io/` — File system and data serialization

**Tasks:**
1. Create abstract `FileStream` interface: read/write blocking I/O
2. Implement `FileInputStream`, `FileOutputStream` for POSIX and Windows
3. Create binary serialization: `BinaryWriter`, `BinaryReader` for POD types
4. Implement JSON serialization for complex types (using nlohmann/json)
5. Add path utilities: `FilePath::combine()`, `is_absolute()`, `get_extension()`
6. Create project folder watcher for hot-reload asset system

**Tests:**
- File read/write correctness
- Serialization round-trip (object → binary/JSON → object)

---

## 1.6 Integration Tests

**Deliverable:** `tests/integration/` — Multi-system tests

**Tasks:**
1. Create scene: 1000 entities with Transform, Velocity, Renderable
2. Spawn/despawn cycle test: create and destroy 100 entities per frame for 100 frames
3. Event ordering test: emit 10 different event types, verify order
4. Asset loading test: load 50 assets, track memory, ensure cleanup
5. Time test: run fixed-step loop, verify correctness over 10,000 frames

---

# Phase 2 — Rendering Foundation (Weeks 7–12)

## Goals
- Vulkan abstraction layer
- Basic rendering loop
- Simple opaque geometry rendering
- Depth prepass
- Frame pacing

## 2.1 Vulkan Abstraction Layer

**Deliverable:** `engine/renderer/gpu/` — Vulkan wrapper API

**Tasks:**
1. Create `GpuDevice` class encapsulating `VkDevice`, `VkPhysicalDevice`, queues
2. Implement `GpuBuffer` abstraction: CPU-GPU sync, mapping, persistent mapping
3. Implement `GpuImage` abstraction: textures, attachments, mip generation
4. Create command buffer allocator: per-thread cmd buffers, ringbuffer strategy
5. Implement descriptor allocator: bindless descriptors with update-after-bind
6. Add pipeline cache: compile shaders at engine startup, cache PSOs

**Dependencies:** Vulkan SDK, SPIR-V compiler (glslang)

**Tests:**
- Buffer creation/destruction
- Image upload/download
- Command buffer recording and submission

---

## 2.2 RenderGraph Framework

**Deliverable:** `engine/renderer/render_graph.h` — Declarative render pass graph

**Implementation:**
```cpp
class RenderGraph {
    RenderPass& add_pass(const char* name);
    RenderPass& read_buffer(const char* name);
    RenderPass& write_texture(const char* name);
    
    void compile();  // resolve dependencies, optimize ordering
    void execute();  // submit all passes in order
};

struct RenderPass {
    std::function<void(CommandBuffer&)> execute_fn;
    std::vector<ResourceHandle> reads;
    std::vector<ResourceHandle> writes;
};
```

**Tasks:**
1. Design pass declaration API (builder pattern)
2. Implement dependency resolution: topological sort
3. Implement automatic barriers: insert VkMemoryBarrier where needed
4. Add resource aliasing: reuse GPU memory for non-overlapping passes
5. Add debugging: per-pass timing, resource tracking
6. Implement pass replay: record and replay command buffers across frames

**Tests:**
- Graph compilation on various pass structures
- Barrier insertion correctness
- Performance: measure per-pass GPU time

---

## 2.3 Basic Rendering Loop

**Deliverable:** `engine/renderer/renderer.h` — Main rendering interface

**Tasks:**
1. Create `Renderer` class: manages device, swapchain, frame pacing
2. Implement frame loop:
   ```
   acquire_swapchain_image()
   → render_graph.execute()
   → present()
   ```
3. Add per-frame statistics: GPU time, triangle count, draw calls
4. Implement vsync + frame limiting
5. Add screenshot capture functionality
6. Debug visualization: show HUD with frame time, memory usage

**Tests:**
- Frame rate stability (vsync enabled/disabled)
- Triple-buffering: no stalls on acquire/present

---

## 2.4 Material System (Data Layer)

**Deliverable:** `engine/renderer/material.h` — Material definitions (no shaders yet)

**Tasks:**
1. Create `Material` data structure:
   ```cpp
   struct Material {
       Handle<Texture> albedo;
       Handle<Texture> normal;
       Handle<Texture> roughness;
       float metallic;
       // ...
   };
   ```
2. Create `MaterialAsset` (JSON format):
   ```json
   {
       "type": "PBR",
       "albedo": "textures/white_default.png",
       "roughness": 0.8,
       "metallic": 0.0
   }
   ```
3. Implement material hot-reload
4. Add material instancing: create instances with parameter overrides

---

## 2.5 Depth Prepass

**Deliverable:** `engine/renderer/passes/depth_prepass.cpp` — Z-buffer rendering

**Tasks:**
1. Create depth prepass render graph pass
2. Render all opaque geometry to depth target
3. Implement depth pyramid (HZB) mip generation via compute shaders
4. Add early-Z rejection (rendering only depth, no color)
5. Add depth visualization for debugging

**Shader Code:** (See `code-examples.md` for reference structures)

**Tests:**
- Depth values correctness
- HZB mip chain generation correctness

---

## 2.6 Simple Opaque Pass (Forward)

**Deliverable:** `engine/renderer/passes/forward_opaque.cpp` — Basic forward rendering

**Tasks:**
1. Create forward render pass
2. Implement simple Phong shading (normal, albedo, basic specular)
3. Render world with directional light + ambient
4. Add wireframe visualization option
5. Implement per-object AABB rendering (debug visualization)

**Shaders:**
- `forward_opaque.vert` – positions + normals
- `forward_opaque.frag` – Phong shading

**Tests:**
- Visual regression tests (reference images)
- Render a test scene, compare to golden image

---

# Phase 3 — Editor & Tooling (Weeks 13–18)

## Goals
- Dear ImGui integration
- Scene hierarchy panel
- Inspector panel
- 3D viewport with gizmos
- Editor play mode

## 3.1 ImGui Integration

**Deliverable:** `editor/imgui_backend.cpp` — ImGui + Vulkan backend

**Tasks:**
1. Integrate Dear ImGui into the engine
2. Create Vulkan rendering backend for ImGui
3. Set up input forwarding: mouse, keyboard, gamepad
4. Implement docking support (ImGui docking branch)
5. Create ImGui context singleton
6. Add theme customization

**Dependencies:** Dear ImGui (vendored in `third_party/imgui/`)

---

## 3.2 Scene Hierarchy Panel

**Deliverable:** `editor/panels/scene_hierarchy.h` — Entity tree view

**Tasks:**
1. Display ECS entities as tree (with hierarchy via parent-child Transform relationships)
2. Implement drag-drop parenting
3. Add visibility toggle (show/hide entities)
4. Add lock toggle (prevent selection)
5. Right-click context menu: create empty, duplicate, delete
6. Search/filter entities by name
7. Sync with ECS: reflect new/destroyed entities in real-time

---

## 3.3 Inspector Panel

**Deliverable:** `editor/panels/inspector.h` — Reflection-driven component editor

**Tasks:**
1. Create reflection system: introspect component types, enumerate fields
2. Implement reflection macros:
   ```cpp
   struct MyComponent {
       GWS_REFLECT_BEGIN
       GWS_FIELD(float, health, "Health")
       GWS_FIELD(Vec3, velocity, "Velocity")
       GWS_REFLECT_END
   };
   ```
3. For each field type, create editor widget:
   - `float` → slider with min/max
   - `Vec3` → three float sliders
   - `bool` → checkbox
   - `Handle<Texture>` → asset picker
4. Implement drag-select for numeric ranges
5. Add color picker for `Vec4` (color)
6. Add drop-down for enums

**Tests:**
- Reflection data extraction
- Widget update → component field update correctness

---

## 3.4 3D Viewport

**Deliverable:** `editor/panels/viewport_3d.h` — Render target + interaction

**Tasks:**
1. Create off-screen render target for viewport
2. Implement camera control:
   - Orbit: hold RMB, move mouse to rotate around center
   - Pan: hold middle mouse, drag to pan
   - Zoom: scroll wheel
   - Orthographic views: top/front/side with buttons
3. Add grid visualization at ground plane (y=0)
4. Render all scene entities into viewport
5. Implement gizmo overlay: show transform gizmos (translate, rotate, scale)
6. Add viewport overlays: light volumes, physics colliders (wireframe), camera frustums

---

## 3.5 Transform Gizmos

**Deliverable:** `editor/gizmos/transform_gizmo.h` — Move, rotate, scale

**Tasks:**
1. Display 3-axis gizmo at selected entity position
2. Implement translate mode:
   - Red axis = X (east-west)
   - Green axis = Y (up-down)
   - Blue axis = Z (forward-back)
   - Click axis → constrain movement to that axis
3. Implement rotate mode: 3 rotation arcs around each axis
4. Implement scale mode: uniform scale or per-axis
5. Snap options: grid snapping (0.1, 0.5, 1.0 unit increments), angle snapping (15°, 45°)
6. Real-time update: gizmo movement → inspector updates → entity updates

---

## 3.6 Editor Play Mode

**Deliverable:** `editor/editor_play_mode.h` — Run game loop inside editor

**Tasks:**
1. Implement "Play" button: pause editor, start game loop
2. Implement "Pause" button: suspend game loop, allow step-by-time
3. Implement "Step" button: advance one frame while paused
4. During play:
   - Render game to viewport
   - Update game systems (physics, AI, animation)
   - Keep inspector synced (show runtime values)
   - Forward input from editor viewport to game
5. On stop: restore scene to pre-play state (snapshot/restore pattern)
6. Add warning: "You are in Play Mode" visual indicator

---

## 3.7 Editor Layout

**Deliverable:** `editor/editor_main.cpp` — Dockable layout

**Implementation:** (Reference: `engine-full-architecture.md` § Part 1)

**Tasks:**
1. Create main window with docking
2. Layout default panels:
   - Left: Scene Hierarchy (30% width)
   - Center: 3D Viewport (50% width)
   - Right: Inspector (20% width)
   - Bottom: Console (shared with other panels)
3. Implement persistent layout saving (to editor config file)
4. Allow users to rearrange panels
5. Add panel visibility toggles in View menu

---

# Phase 4 — Gameplay Systems (Weeks 19–28)

## Goals
- Physics integration
- Movement system
- Combat system basics
- Stat system
- Ability system skeleton

## 4.1 Physics Integration

**Deliverable:** `engine/physics/physics_world.h` — Jolt Physics wrapper

**Tasks:**
1. Integrate Jolt Physics (MIT license)
2. Create `PhysicsWorld` class managing Jolt world
3. Create `PhysicsBodyComponent`:
   ```cpp
   struct PhysicsBodyComponent {
       Jolt::BodyID body_id;
       float mass;
       bool gravity_enabled;
   };
   ```
4. Implement helper functions:
   - `create_box_body()`
   - `create_sphere_body()`
   - `create_capsule_body()`
5. Integrate physics into game loop: `PhysicsSystem` ticks Jolt at fixed 60 Hz
6. Sync transforms: physics body position → TransformComponent
7. Add collision events: publish `CollisionEvent` when bodies collide
8. Implement raycasting: `physics_world.raycast(ray, out_hit)`

**Dependencies:** Jolt Physics (vendored)

**Tests:**
- Object falls under gravity
- Collisions detected correctly
- Raycasts return expected results

---

## 4.2 Transform Hierarchy

**Deliverable:** `engine/ecs/transform_hierarchy.h` — Parent-child relationships

**Tasks:**
1. Add parent/child pointers to `TransformComponent`
2. Implement hierarchical transform updates: world transform = parent world × local
3. Create `TransformHierarchySystem`: update world transforms once per frame
4. Support dynamic parenting: `add_child()`, `remove_child()`
5. Handle reparenting edge cases (cycles, deep hierarchies)
6. Sync inspector: show parent in inspector, allow drag-drop parenting

**Tests:**
- Local → world transform conversions
- Hierarchy updates with parent motion
- Reparenting correctness

---

## 4.3 Movement System (Standard)

**Deliverable:** `game/systems/movement.h` — Walk, run, jump, crouch

**Implementation:** (Reference: `engine-full-architecture.md` § Part 3.1 + `technical-pre-planning.md` § Part 4)

**Tasks:**
1. Create state machine for movement:
   ```
   Idle → Walking → Running
   Idle → Walking → Falling
   Idle → Jumping → Falling
   Idle → Crouching → Idle
   ```
2. Implement input handling:
   - WASD → movement direction, velocity
   - Space → jump (only if grounded)
   - Ctrl → crouch toggle
3. Implement ground detection: raycast down, detect floor beneath
4. Implement jump mechanics:
   - Velocity boost upward
   - Min jump height (release early = lower jump)
5. Implement animation blending: locomotion speed → animation speed
6. Add footstep sounds: state transitions → audio events

**Components:**
- `MovementComponent` (speed, acceleration, jump force, grounded flag)
- `InputComponent` (desired velocity, desired actions)

**Tests:**
- Moving character velocity correctness
- Jump height consistency
- Ground detection with various terrain slopes

---

## 4.4 Combat System (Frame Data)

**Deliverable:** `game/systems/combat.h` — Attack frame data, hitboxes, hitstop

**Implementation:** (Reference: `engine-full-architecture.md` § Part 3.2 + `code-examples.md` § 12)

**Tasks:**
1. Create `AttackAsset` data structure:
   ```cpp
   struct AttackAsset {
       uint32_t startup_frames;   // frames before attack hits
       uint32_t active_frames;    // frames attack can hit
       uint32_t recovery_frames;  // frames after hit before next action
       float poise_damage;
       Handle<Mesh> hitbox_mesh;
   };
   ```
2. Create `HitboxSet` asset: collection of per-bone capsule hitboxes
3. Implement `CombatComponent`:
   ```cpp
   struct CombatComponent {
       AttackState current_attack;
       uint32_t frame_in_attack;
       bool can_accept_new_input;
   };
   ```
4. Implement attack frame stepping:
   - Each frame: increment `frame_in_attack`
   - If frame in `[startup, startup+active)`: enable hitbox checking
   - If frame >= `startup+active+recovery`: reset attack
5. Implement hitstop: on hit confirmation, set time-scale override to 0 for N frames
6. Implement hit feedback: knockback on defender, animation feedback

**Tests:**
- Attack timing correctness
- Hitbox detection with moving targets
- Hitstop frame accuracy

---

## 4.5 Stat System (RPG)

**Deliverable:** `game/systems/stats.h` — Modifier pipeline (HP, Damage, Defense, etc.)

**Implementation:** (Reference: `engine-full-architecture.md` § Part 3.3 + `code-examples.md` § 11)

**Tasks:**
1. Define stat types: `HP`, `Mana`, `Stamina`, `AttackPower`, `Defense`, `CritChance`, `MovementSpeed`
2. Create `StatModifier` structure:
   ```cpp
   struct StatModifier {
       StatType type;
       enum ModType { Flat, PercentAdd, PercentMultiply } mod_type;
       float value;
       uint32_t source_id;  // which item/buff applied this
   };
   ```
3. Create `StatComponent`:
   ```cpp
   struct StatComponent {
       float base_values[stat_count];
       std::vector<StatModifier> modifiers;
       mutable float cached_values[stat_count];
       mutable bool dirty[stat_count];
   };
   ```
4. Implement lazy computation: `get_stat(type)` computes from base + modifiers
   ```cpp
   final = (base + sum_flat) * (1 + sum_percent_add) * product_percent_mult
   ```
5. Implement modifier add/remove with dirty tracking
6. Add stat change events: `StatChangedEvent` emitted when stat changes significantly

**Tests:**
- Flat modifier composition
- Percent modifier stacking
- Dirty flag optimization (recalculate only changed stats)

---

## 4.6 Ability System (Skeleton)

**Deliverable:** `game/systems/abilities.h` — Ability slots, cooldown manager

**Implementation:** (Reference: `engine-full-architecture.md` § Part 3.4)

**Tasks:**
1. Create `AbilityAsset` data:
   ```cpp
   struct AbilityAsset {
       float cast_time;
       float cooldown;
       float mana_cost;
       std::string vfx_path;
       Handle<VisualScript> logic_graph;
   };
   ```
2. Create `AbilitySlotComponent`:
   ```cpp
   struct AbilitySlotComponent {
       Handle<AbilityAsset> slots[4];  // 3 standard + 1 ultimate
       CooldownState cooldowns[4];
   };
   ```
3. Create `CooldownManager`:
   ```cpp
   struct CooldownState {
       float time_remaining;
       bool activate();  // returns true if not on cooldown
   };
   ```
4. Implement ability activation:
   - Check not on cooldown
   - Check resource available (mana)
   - Trigger cast animation
   - Start cooldown timer
5. Skeleton for ability effects (full implementation in Phase 6)

**Tests:**
- Cooldown timer accuracy
- Resource deduction
- No double-activation while on cooldown

---

## 4.7 Inventory System (Basic)

**Deliverable:** `game/systems/inventory.h` — Grid-based inventory

**Tasks:**
1. Create `InventoryComponent`:
   ```cpp
   struct InventoryComponent {
       static const uint32_t GRID_WIDTH = 10;
       static const uint32_t GRID_HEIGHT = 10;
       ItemStack grid[GRID_WIDTH * GRID_HEIGHT];
   };
   ```
2. Create `ItemStack` (item type + count)
3. Implement operations:
   - `add_item(item, count) → bool` (returns false if no space)
   - `remove_item(slot_index, count) → bool`
   - `find_item(type) → slot_index`
4. Create UI: grid display in editor inspector widget
5. Implement equipment slots: main weapon, offhand, armor

**Tests:**
- Grid filling and stack combining
- Inventory full behavior

---

# Phase 5 — Advanced Rendering (Weeks 29–36)

## Goals
- Clustered deferred rendering
- G-buffer layout
- Lighting pass with multiple lights
- LOD system integration
- HZB occlusion culling

## 5.1 G-Buffer Pass

**Deliverable:** `engine/renderer/passes/gbuffer_pass.cpp` — Multi-render-target deferred

**Implementation:** (Reference: `code-examples.md` § 1a–1c + `technical-pre-planning.md` § 1.1)

**Tasks:**
1. Create 4 render targets (see architectural layout):
   - RT0: RGB=Albedo, A=AO
   - RT1: RG=Oct-encoded normal
   - RT2: R=Roughness, G=Metallic, B=Emissive, A=Material ID
   - Depth (shared)
2. Write G-Buffer shader:
   ```glsl
   // Sample albedo, normal, roughness from materials
   // Apply parallax mapping if needed
   // Oct-encode normal
   // Output to 4 MRTs
   ```
3. Implement depth prepass → depth pyramid (HZB) generation
4. Hook into render graph: G-Buffer pass after depth prepass
5. Add material ID system: different shading paths (skin, hair, transparent) via material ID

**Shaders:** See `code-examples.md` for reference

**Tests:**
- Render golden reference, compare output
- Material ID correctly sampled

---

## 5.2 Cluster Assignment (Compute)

**Deliverable:** `engine/renderer/passes/cluster_assign.cpp` — Light → cluster mapping

**Implementation:** (Reference: `code-examples.md` § 1b + `technical-pre-planning.md` § 1.1)

**Tasks:**
1. Implement `ClusterBuilder` (from code examples)
2. Create cluster grid: 16×9×24 clusters (reference: `technical-pre-planning.md`)
3. Build cluster AABBs in view space:
   ```cpp
   z = near * (far/near)^(i/depth_slices)
   ```
4. Write cluster assignment compute shader
5. Per light: test against all cluster AABBs
6. Use global atomic counter to fill light index buffer compactly
7. Store per-cluster: light offset, light count

**Compute Shader:** See `code-examples.md` § 1b

**Tests:**
- Cluster grid correctness (AABBs cover frustum)
- Light assignment: verify lights in expected clusters

---

## 5.3 Lighting Pass (Compute or Raster)

**Deliverable:** `engine/renderer/passes/lighting_pass.cpp` — PBR shading via clusters

**Tasks:**
1. Write lighting compute shader (or fullscreen quad fragment):
   - Read G-buffer: albedo, normal, roughness, depth
   - Compute cluster index from pixel position + depth
   - Fetch light list for cluster
   - Iterate lights: compute PBR contribution per light (see code examples)
   - Accumulate lighting, write to HDR output
2. Implement PBR terms:
   - `distribution_ggx()` (GGX normal distribution)
   - `geometry_schlick_ggx()` (geometry shadowing)
   - `fresnel_schlick()` (fresnel effect)
3. Add IBL (image-based lighting) fallback: sample skybox for indirect
4. Add shadow mapping for directional light (atlas-based)

**Shaders:** See `code-examples.md` § 1c

**Tests:**
- Lighting is physically plausible
- Multiple lights contribute correctly
- No artifacts at cluster boundaries

---

## 5.4 LOD System

**Deliverable:** `engine/renderer/lod/lod_system.h` — Mesh simplification + selection

**Implementation:** (Reference: `code-examples.md` § 4 + `technical-pre-planning.md` § 1.4)

**Tasks:**
1. Integrate meshoptimizer into asset pipeline
2. At mesh import time:
   - Generate LOD chain: 100% → 50% → 25% → 10% → 2%
   - Store all LODs in single mesh asset
   - Compute screen-coverage thresholds per LOD
3. At runtime, in HZB culling compute:
   - Calculate screen coverage for each instance
   - Select appropriate LOD (see pseudocode in references)
   - Place draw command in LOD-specific buffer
4. Implement dithered LOD transitions (clip in shader based on screen position hash)
5. Add impostor system: beyond LOD 4, render billboard with pre-baked atlas texture

**Tests:**
- LOD selection correctness (coverage → LOD mapping)
- Dither pattern eliminates pop
- Memory savings vs quality tradeoff

---

## 5.5 HZB Occlusion Culling

**Deliverable:** `engine/renderer/culling/hzb_culling.cpp` — Two-pass culling

**Implementation:** (Reference: `code-examples.md` § 3 + `technical-pre-planning.md` § 1.3)

**Tasks:**
1. Depth prepass: render depth for large occluders (buildings, terrain)
2. Compute HZB mip chain: cascade of smaller depth pyramids
3. Culling compute shader:
   - Per instance: project AABB to screen
   - Select HZB mip appropriate for AABB size
   - Sample HZB depth at instance center
   - If object depth < HZB, object is occluded → skip (no draw command)
   - If visible, add draw command to indirect buffer
4. Indirect draw: `vkCmdDrawIndexedIndirect` with compute-filled buffers (GPU-driven rendering)
5. Debug visualization: show culled instances in wireframe

**Shaders:** See `code-examples.md` § 3a–3b

**Tests:**
- Culled objects do not appear in final render
- Visible objects pass through culling
- One-frame latency acceptable (reappear immediately next frame if unoccluded)

---

## 5.6 Transparency Sorting

**Deliverable:** `engine/renderer/passes/transparency_pass.cpp` — Back-to-front rendering

**Tasks:**
1. Create separate transparency render pass (after lighting pass)
2. Collect all transparent objects: sort by depth from camera (back to front)
3. Use forward rendering for transparency (no G-buffer)
4. Implement blend modes: additive, alpha-blend, multiply
5. Disable depth writes (enable depth tests only)
6. Render sorted back-to-front for correctness

**Tests:**
- Layered transparency renders in correct order
- No depth-fighting artifacts

---

# Phase 6 — AI & Scripting (Weeks 37–44)

## Goals
- Behavior tree system
- Visual script VM
- C# scripting support
- AI perception
- Boss AI framework

## 6.1 Behavior Tree System

**Deliverable:** `engine/ai/behavior_tree/bt_system.h` — Full BT implementation

**Implementation:** (Reference: `code-examples.md` § 7 + `technical-pre-planning.md` § 4.1)

**Tasks:**
1. Implement core BT nodes:
   - `Sequence` (execute children in order, fail on first failure)
   - `Selector` (execute until one succeeds)
   - `Parallel` (execute all children simultaneously)
   - `Decorator` (Inverter, Cooldown, Loop)
   - `Condition` (check predicate)
   - `Action` (execute lambda/function)
2. Create `Blackboard`: typed key-value store per AI agent
3. Implement XML/JSON tree definition:
   ```xml
   <tree name="enemy_ai">
     <selector>
       <condition name="is_alert"/>
       <action name="chase_player"/>
       <sequence>
         <action name="patrol"/>
       </sequence>
     </selector>
   </tree>
   ```
4. Create asset loader for tree definitions
5. Implement BT evaluation on 10 Hz tick (not every frame)
6. Add debugging: tree visualization, breakpoints, step execution

**Tests:**
- Tree execution correctness
- Async actions work across multiple ticks
- Blackboard typed access safety

---

## 6.2 AI Perception System

**Deliverable:** `game/ai/perception.h` — Sight cones, hearing, memory

**Tasks:**
1. Create `PerceptionComponent`:
   ```cpp
   struct PerceptionComponent {
       float sight_range;
       float sight_fov;  // degrees
       float hearing_range;
       Vec3 last_known_enemy_pos;
       float last_seen_time;
       bool is_aware;
   };
   ```
2. Implement sight checking:
   - FOV cone test: dot(to_enemy, forward) > cos(fov)
   - Distance check: distance < sight_range
   - Line-of-sight raycast: no occlusion
3. Implement hearing:
   - Sound events broadcast distance
   - If AI within hearing_range: update last_known_pos
4. Implement memory:
   - Forget after time-out (5 seconds without new stimuli)
   - Last-known position used for search behavior
5. Perception system: runs at 10 Hz, not every frame

**Tests:**
- FOV calculation correctness
- Line-of-sight occlusion
- Memory forgetting after timeout

---

## 6.3 Enemy AI (Basic)

**Deliverable:** `game/ai/enemy_controller.h` — Patrol, chase, attack

**Tasks:**
1. Create `EnemyAIComponent`:
   ```cpp
   struct EnemyAIComponent {
       Handle<BehaviorTree> behavior_tree;
       PerceptionComponent perception;
       PathfindingState pathfinding;
   };
   ```
2. Implement basic BT with states:
   - **Idle**: stand still
   - **Patrol**: walk predefined path (waypoints)
   - **Alert**: look around, heard noise
   - **Chase**: move toward last-known enemy position
   - **Attack**: execute attack sequence (reuse combat system)
3. Implement waypoint navigation: simple line segments (advanced pathfinding in Phase 7)
4. Add state transitions:
   - Idle → Alert (heard sound)
   - Alert → Chase (saw enemy)
   - Chase → Attack (within melee range)
   - Attack → Chase (if enemy escapes)
5. Debug visualization: show sight cone, path, current target

**Tests:**
- Enemy transitions through states correctly
- Patrol path followed
- Chase toward last-known position

---

## 6.4 Visual Script VM

**Deliverable:** `engine/scripting/visual_script/vm.h` — Graph bytecode interpreter

**Implementation:** (Reference: `code-examples.md` § 10 + `engine-full-architecture.md` § Part 4)

**Tasks:**
1. Design bytecode instruction set: ~50 opcodes
   - `CONST`, `GET_VAR`, `SET_VAR`, `CALL_FUNC`, `BRANCH`, `JUMP`
   - Stack-based VM (simpler than register-based)
2. Create node-to-bytecode compiler:
   - Graph traversal → sequential instructions
   - Link outputs to inputs via stack
3. Implement VM execution:
   - Instruction pointer, stack, local variables
   - Async support: yield instruction suspends execution
4. Create node library editor UI: drag nodes, connect pins, validate connections
5. Implement execution tracing: breakpoints, inspect stack/variables

**Tests:**
- Bytecode correctness (simple graph → verify bytecode)
- Async nodes yield and resume
- Variable scope isolation

---

## 6.5 C# Scripting Host

**Deliverable:** `engine/scripting/csharp/csharp_host.h` — .NET 8 embedding

**Tasks:**
1. Integrate .NET 8 runtime (or Mono for console platforms)
2. Create C# API bindings:
   ```csharp
   [Expose]
   public static void Log(string msg) { /* engine logging */ }
   
   [Expose]
   public static Transform GetEntityTransform(uint32_t entity_id) { /* ... */ }
   ```
3. Bind core types: Entity, Transform, Component (auto-generated)
4. Implement component attributes:
   ```csharp
   [ScriptComponent]
   public class PlayerController : MonoBehaviour {
       [Inspect] public float speed = 5.0f;
       
       public void OnUpdate(float dt) { /* called per frame */ }
   }
   ```
5. Implement hot-reload: watch C# source files, recompile on change
6. Error reporting: C# compile errors → editor console with line references

**Dependencies:** .NET 8 SDK or Mono

**Tests:**
- C# component can be attached to entity
- Hot-reload preserves state
- Entity queries work from C#

---

## 6.6 Python Scripting (Rapid Prototyping)

**Deliverable:** `engine/scripting/python/python_host.h` — CPython 3.12 embedding

**Tasks:**
1. Embed CPython 3.12 and create bindings (pybind11-style)
2. Expose engine API to Python modules
3. Allow Python scripts attached as components
4. Implement eval sandbox: restrict file I/O, network
5. Used primarily for: editor tools, AI assistant, rapid iteration
6. Not for shipping game code (performance critical code in C++)

**Dependencies:** CPython 3.12, pybind11

---

# Phase 7 — Networking & Polish (Weeks 45–52)

## Goals
- Rollback netcode for PvP
- Server-authoritative for co-op
- Replay system
- Performance optimization
- Content integration

## 7.1 Rollback Netcode (Combat)

**Deliverable:** `engine/network/rollback/rollback_manager.h` — GGPO-style

**Implementation:** (Reference: `code-examples.md` § 6 + `technical-pre-planning.md` § 3.1)

**Tasks:**
1. Create deterministic simulation layer:
   - Fixed-point arithmetic for positions (1/1000 meter = 1mm precision)
   - Deterministic RNG seeded from frame number
   - Separate simulation step (30 Hz) from render (60 Hz)
2. Snapshot system:
   - `SimulationState` = flat memory block of all gameplay data
   - Snapshot = `memcpy` (< 64 KB per player)
   - Restore = `memcpy` back
3. Implement GGPO loop:
   - Send inputs immediately
   - Predict missing remote inputs
   - On input arrival: if wrong, rollback + re-simulate
   - Limit rollback window to 10 frames
4. Create `RollbackManager` class:
   ```cpp
   void on_input_received(player_id, input, frame);
   void tick_simulation();
   void present();
   ```
5. Add spectator mode: connect without controlling, view rollbacks

**Tests:**
- Deterministic simulation: same inputs reproduce same output
- Rollback accuracy: after rollback, final state matches second playthrough
- Network lag simulation: introduce 50ms, 100ms, 200ms latency, verify correctness

---

## 7.2 Server-Authoritative Co-op

**Deliverable:** `engine/network/server/server_session.h` — Dedicated server support

**Tasks:**
1. Create server mode: headless, no rendering
2. Implement client prediction:
   - Client predicts own movement immediately
   - Server validates inputs, corrects position
   - Other clients interpolate from snapshots
3. Implement lag compensation:
   - Server buffers past states
   - Rewind to time-of-input, apply input, return new state
4. Create snapshot compression: delta encoding (send only changed fields)
5. Implement player spawning/despawning sync
6. Add anti-cheat: server validates all damage, stat changes

**Tests:**
- Client-side prediction feels responsive
- Server catches cheating (position hacks)
- Interpolation smooth

---

## 7.3 Optimization Pass

**Deliverable:** Profiling and bottleneck elimination

**Tasks:**
1. Profile with GPU debugger (Renderdoc/NSight):
   - Measure per-pass GPU time
   - Identify bottlenecks (bandwidth, pixel throughput, compute)
2. Profile CPU:
   - Measure per-system time (Physics, Rendering, AI, etc.)
   - Identify cache misses, hot loops
3. Optimize hot paths:
   - SIMD batch operations where possible
   - Cache-friendly data layouts
   - Remove unnecessary allocations
4. Target performance:
   - 60 FPS @ 1440p on mid-range PC
   - 144 FPS @ 1080p on high-end PC
5. Profiler visualization in editor: frame time breakdown by system

**Tests:**
- Performance benchmarks track regressions
- Frame time consistency (no stutters)
- Memory profiler: track allocations over time

---

## 7.4 Visual Polish

**Deliverable:** Final rendering touches

**Tasks:**
1. Implement post-process effects:
   - Bloom (HDR glow)
   - Motion blur (velocity-based)
   - Depth of field (blur distant/near objects)
   - Film grain / chromatic aberration (subtle)
2. Implement ACES tonemapping: HDR → SDR conversion
3. Add temporal anti-aliasing (TAA): smooth edges over time
4. Particle system (simple): spawn, update, render (GPU-driven if possible)
5. Add ambient occlusion (SSAO): depth-only screen-space ambient occlusion

**Tests:**
- Visual regression: post-process effects look good
- Performance: post-process doesn't tank frame rate

---

## 7.5 Editor Polish

**Deliverable:** Final editor usability

**Tasks:**
1. Implement multi-select: select multiple entities, bulk edit properties
2. Undo/redo system: all editor changes
3. Prefab system: save entity hierarchies as prefabs, spawn instances
4. Project settings: configure project-wide options (physics gravity, timestep, etc.)
5. Build system integration:
   - "Build" button: preprocess assets, create game packages
   - Output executable
6. Recent projects menu
7. Keyboard shortcuts: common operations (Ctrl+D duplicate, Ctrl+Z undo, etc.)

---

## 7.6 Content Integration

**Deliverable:** Demo game scene

**Tasks:**
1. Create sample environment:
   - Terrain mesh (simple plane or tiled heightmap)
   - A few buildings/props
   - Player spawn point
2. Populate with NPCs:
   - One patrol guard with behavior tree
   - One boss entity with phase machine skeleton
3. Implement basic interaction:
   - Pick up item → add to inventory
   - Talk to NPC → trigger placeholder dialogue
4. Record video: show engine features in action
5. Write documentation: how to create a new scene, add entities, attach components

---

# Success Criteria

At phase completion, the engine should:

✅ **Phase 0:** Build system working, math library tested, allocators functional
✅ **Phase 1:** ECS world with 100k entity stress test, events flowing, assets loading
✅ **Phase 2:** Vulkan rendering, basic opaque objects visible, frame pacing stable
✅ **Phase 3:** Editor playable, hierarchy/inspector functional, gizmos responsive, play mode works
✅ **Phase 4:** Character walks/runs, attacks connect, HP depletes, combat feels responsive
✅ **Phase 5:** Multiple lights render correctly, distant LODs invisible, culling eliminates off-screen geometry
✅ **Phase 6:** AI patrols, reacts to player, visual scripts execute deterministically
✅ **Phase 7:** 2-player rollback netcode stable (< 3 frame rollback @ 200ms ping), 60 FPS sustained

---

# Risk Mitigation

| Risk | Mitigation |
|---|---|
| Vulkan API complexity | Use established wrappers (vk-bootstrap), reference Sascha Willems samples |
| ECS performance | Profile early and often, use EnTT's built-in benchmarks |
| Physics determinism | Use Jolt deterministic mode, test across MSVC/GCC |
| Netcode instability | Prototype with 3 players, test rollback up to 10 frames |
| Editor responsiveness | Keep UI update off critical path, use threading for asset compilation |

---

# Testing Strategy

- **Unit tests:** Math, allocators, ECS, systems (Catch2)
- **Integration tests:** Multi-system coordination (spawn entity → physics → rendering)
- **Performance tests:** Benchmarks for hot paths, regression tracking
- **Gameplay tests:** Manual play testing every 2 weeks
- **Visual regression:** Screenshot comparisons against golden images

---

# Documentation

Each phase deliverable includes:
- Header file with clear API documentation
- Example usage code
- Integration points with other systems
- Performance characteristics

