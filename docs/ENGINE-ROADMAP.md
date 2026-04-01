# Worldshaper Game Engine — Development Roadmap

## Project Identity

**Game:** Worldshaper (Project Schizo)  
**Engine:** Custom C++ 3D Engine with full platform control  
**Target:** Windows (MSVC), Linux (GCC), cross-platform from day one  
**Language:** C++20, CMake build system  

### Game Requirements
- 3D third-person action RPG with open-world multiplayer
- Complex ability system (Destiny 2-style modifier pipeline)
- Fluid high-speed movement (Wuthering Waves-inspired)
- Frame-data driven combat (Elden Ring poise/parry mechanics)
- Seamless world streaming (Cyberpunk 2077 density)
- Advanced AI with phase-based boss encounters

---

## Architecture Philosophy

**Three non-negotiable decisions:**

1. **Networking from Phase 2** — Movement and ability state must be deterministic and replayable. Cannot be retrofitted in Phase 8.
2. **ECS designed for modifier pipelines** — Every component can source stat modifiers. Destiny-style builds require this from the ground up.
3. **Render graph as DAG** — Composable passes (shadow → geometry → lighting → post) from Phase 3 start. Deferred shading, VFX, and advanced effects plug in cleanly.

---

## Phase 1: Foundation (In Progress)

**Duration:** 2–3 weeks | **Status:** 90% complete  
**Deliverable:** Cross-platform build system, core utilities

### Completed
- ✅ Math library (Vec2/3/4, Mat3/4, Quaternion, Transform, easing, RNG)
- ✅ Advanced physics module (vector fields, PDE solvers, Verlet particles)
- ✅ Memory allocators (stack, pool, general)
- ✅ CMake + CI/CD setup
- ✅ Logging system (spdlog integration)

### Remaining
- [ ] File I/O abstraction (asset streaming foundation)
- [ ] Container wrappers (typed handles, async resources)
- [ ] Unit test suite (Catch2)

### Architecture Notes
- Math and physics are fully decoupled; physics is zero-cost if unused
- All components are POD types (no heap allocations)
- Column-major matrix layout for GPU compatibility
- Fixed 32-bit floats throughout (deterministic for rollback netcode)

---

## Phase 2: Platform & Input (Queued)

**Duration:** 2–3 weeks | **After:** Phase 1  
**Deliverable:** Windowed GPU context, input system w/ action mapping

### Scope
- **Windowing:** GLFW 3 abstraction layer (enables console porting later)
- **GPU Context:** OpenGL 4.6 initialization and swapchain management
- **Input System:** Raw platform events → typed action events (Jump, MoveForward, AbilityActivate[slot])
- **Gamepad Support:** DirectInput (Windows) + Linux gamepad API

### Key Design: Action Mapping Layer
```cpp
// Input layer emits typed events, not raw keys
// Game code reads AbilityActivate events, not key down events
enum class InputAction {
    MoveForward, MoveBack, StrafeLeft, StrafeRight,
    Jump, Dash, Crouch,
    AbilitySlot1, AbilitySlot2, AbilitySlot3, Ultimate
};
```

**Why this matters:** Distinguishing input events from input state allows the ability system to route ability activations correctly when slots map to different keys/gamepad buttons.

---

## Phase 3: Renderer (Planned)

**Duration:** 6–8 weeks | **Parallel with:** Phase 4, 5  
**Deliverable:** Render graph, PBR forward shading, shadow maps

### Render Graph Design

A DAG where each pass is a node. Composable, extensible, GPU-profiled.

```
Depth Prepass → Shadow Pass → Geometry Pass → Lighting Pass → 
    → Post-Process Pass (bloom, tonemapping, SSAO) → VFX Pass → Present
```

### Milestones (in order)

| Milestone | Scope |
|-----------|-------|
| **Basic** | Static geometry, single directional light, frustum culling |
| **Materials** | Phong shading → Blinn-Phong → PBR (metallic-roughness) |
| **Shadows** | Directional shadow map (PCF soft shadows) |
| **Advanced** | Deferred G-buffer, IBL from skybox, screen-space effects |
| **VFX** | Particle rendering pass, additive/subtractive blend modes |
| **Post** | Tonemapping (ACES), bloom, FXAA/TAA |

### Dependencies
- **Mesh Loading:** Implement OBJ first (validation), then GLTF via cgltf
- **Texture Loading:** stb_image wrapper
- **Shader Compilation:** glslang (compile offline to SPIR-V)

### Phase 3 Note
The VFX pass is critical early because Orgolis's ability visuals (Reality Bending Illusions, Ethereal Form) drive performance requirements. Don't defer VFX integration.

---

## Phase 4: Core Systems (Planned)

**Duration:** 7–10 weeks | **Parallel with:** Phase 3, 5  
**Deliverable:** ECS, physics wrapper, asset pipeline, audio

### ECS Design Constraint

**Must support modifier pipelines from day one.** Every ability buff, equipment perk, status effect feeds into a unified stat calculation. Archetype-based ECS (like EnTT) is the right choice.

```cpp
// Example: Character stat aggregation
struct Character {
    int base_health = 100;
    std::vector<ModifierSource*> active_modifiers;
    
    int current_health() {
        int total = base_health;
        for (auto mod : active_modifiers) {
            total += mod->health_bonus();
        }
        return total;
    }
};
```

### Components
- **Transform** — Position, rotation, scale (from Phase 1 math)
- **Physics** — Jolt Physics body reference + shape
- **Animator** — Current animation state, blend tree data
- **Stats** — Health, mana, armor, resistances (aggregated from modifiers)
- **Inventory** — Equipment slots feeding stat modifiers
- **Abilitystate** — cooldowns, active effects, charges

### Physics Integration
- Thin wrapper around Jolt Physics (MIT license)
- PhysicsWorld interface (allows swapping physics backends in Phase 7+)
- Character controller component (player + NPCs)

### Asset Pipeline
- Handle<T> reference-counted asset handles
- Async loading on thread pool
- Asset hot-reloading (editor workflow)

### Audio
- miniaudio wrapper (public domain, single header)
- AudioSource component (spatial 3D audio)
- Mixer + effect slots

---

## Phase 5: Editor & Tools (Planned)

**Duration:** 7–10 weeks | **Parallel with:** Phase 4, 6  
**Deliverable:** In-engine editor, scene hierarchy, component inspector, asset browser

### Tools
- **UI Framework:** Dear ImGui (immediate-mode, renders into scene)
- **Scene Editor:** Hierarchy panel, add/remove entities, component inspector
- **Viewport:** Gizmos (translate/rotate/scale), camera controls
- **Asset Browser:** Drag-and-drop mesh/texture loading into scenes
- **Profiler:** Frame time graph, GPU timers per render pass, memory breakdown

### Why Phase 5 (early)?

Every subsequent phase benefits exponentially from iteration speed. The editor is not a luxury; it's infrastructure. Authoring 100 NPC behavior trees is 10x slower without a visual editor.

---

## Phase 6: Character Controller & Movement (Planned)

**Duration:** 5–7 weeks | **After:** Phase 4, 5  
**Deliverable:** Movement state machine, input buffer, networked movement state

### Movement Complexity

Wuthering Waves-style movement requires:

- **States:** Idle, Walk, Sprint, Jump, AirDash, WallRun, Climb, Swim, Dive, Crouch, Slide, Falling, Recovery
- **State Machine:** Priority-based transitions (jump cancels into wall-run, dash cancels into jump)
- **Input Buffer:** Queued inputs during animation recovery execute when permitted
- **Root Motion:** Animation drives position, not just physics capsule
- **Networking:** All movement must be replayable from input commands (deterministic)

### Architecture

**Command-based movement:** Store inputs as commands, replay on server for validation.

```cpp
struct MovementCommand {
    uint32_t frame;
    Vec3 input_direction;
    bool jump_pressed;
    bool dash_pressed;
};

// Server replays: character.process_movement_command(cmd);
// Client predicts: character.process_movement_command(cmd);
// Server corrects: if (server_pos != client_pos) smoothly interpolate
```

### Animation Integration

- Blend tree system (velocity → blend space)
- IK attachment (foot placement, aim direction)
- Animation-driven hitbox activation (next phase)

### Hardest Problem

**Client-prediction with server reconciliation** is brutal when abilities, hitboxes, and combat overlap movement. Plan 3–6 months of refinement here.

---

## Phase 7: Physics & Collisions (Planned)

**Duration:** 4–6 weeks | **After:** Phase 6  
**Deliverable:** Jolt Physics integration, character-obstacle interaction, raycast queries

### Scope
- Rigid body dynamics (vehicles, environmental objects)
- Character capsule collisions with terrain
- Raycast queries (footstep detection, target detection)
- Constraint solver stability testing
- Vehicle physics abstraction (ground vs. air/space flight)

### Game-Specific Note

Vehicles (boats, cars, tanks, aircraft, helicopters, spaceships) are listed in the game design. Ground vehicles use Jolt; air/space vehicles need custom flight physics overlayed.

---

## Phase 8: Networking (Planned)

**Duration:** 6–10 weeks | **After:** Phase 6 movement is stable  
**Deliverable:** Client-side prediction, server reconciliation, ability replication

### Architecture

**Server-authoritative hybrid:**
- Client predicted: player position, movement state
- Server authoritative: ability effects, hit confirmation, other player positions

```
Player Input → Movement Command → Client: simulate locally
                                → Server: validate + simulate
                                → Broadcast: other players' corrected states
```

### Critical Constraints from Game Design

- Orgolis's "Abomination" power (suppress nearby magic) is a **networked status effect** that affects other players' ability states
- Ability activations must feel immediate (client-side) but effects must be server-confirmed (anti-cheat)
- Hit detection in frame-data driven combat has tight timing windows — latency compensation is critical

---

## Phase 9: Combat System (Planned)

**Duration:** 6–8 weeks | **Parallel with:** Phase 8 stability work  
**Deliverable:** Frame-data driven hit detection, ability execution, status effects

### Combat Data Model

Every attack (player ability or enemy move) is authored as:

```
Attack {
    startup_frames: 8,
    active_frames: 12,
    recovery_frames: 10,
    hitbox_bone: "weapon",
    damage: 25,
    poise_damage: 10
}
```

Hitbox activation is driven by animation frame timing, not a static collider.

### Poise System (Elden Ring-inspired)

```
Poise:
    Current: 100 / 120
    When you take hit: current -= poise_damage
    If current <= 0: enter "stagger" state (5 frames)
    Recovers at 20 per second while not in combat
```

---

## Phase 10: Animation & Procedural Motion (Planned)

**Duration:** 6–8 weeks | **Parallel with:** Phase 9  
**Deliverable:** Blend trees, IK solvers, secondary motion chains, hit reactions

### Three-Tier Animation Stack

**Base Layer:** Hand-authored animations (attacks, idles, movement clips)

**Procedural Adjustment:** Foot IK (even terrain), aim IK (camera direction), head look-at

**Fully Procedural:** Cape/hair physics, weapon sway, ragdoll, hit reaction impulses

### Implementation Priority

1. **IK Solvers** (highest priority)
   - Two-bone leg IK for foot placement
   - Spine/shoulder IK for aiming
   - ~200 lines C++ per solver

2. **Secondary Motion** (high priority)
   - Verlet chain solver (cape, hair, equipment)
   - Makes high-speed movement feel alive

3. **Hit Reactions** (medium priority)
   - Procedural impulse to spine/shoulder
   - Decay over frames
   - Works regardless of current animation

### Why Not Full Procedural Locomotion?

Motion Matching (Ubisoft) is powerful but requires a massive animation database and months of engineering. Defer to post-launch if we have motion capture budget.

---

## Phase 11: Ability System (Planned)

**Duration:** 6–10 weeks | **After:** Phase 9, 10  
**Deliverable:** Data-driven ability graph, modifier pipeline, Destiny-style builds

### Ability Architecture

Abilities are authored as data (JSON or binary), not code:

```json
{
  "id": "ethereal_form",
  "cooldown": 8.0,
  "duration": 4.0,
  "cast_time": 0.5,
  "effects": [
    { "type": "invulnerability", "duration": 4.0 },
    { "type": "speed_boost", "amount": 1.5 },
    { "type": "vfx", "particle_effect": "ethereal_shimmer" }
  ],
  "modifiers": {
    "duration": [
      { "source": "Key_of_Wisdom", "bonus": 2.0 },
      { "source": "Eyes_of_Orgolis_intrinsic", "bonus": 6.0, "cap": 14.0 }
    ]
  }
}
```

### Modifier Aggregation

Every gear piece, passive skill, and buff is a modifier source:

```cpp
class AbilityModifierPipeline {
    float compute_duration(Ability* ability, Character* character) {
        float base = ability->base_duration;
        for (auto mod : character->active_modifiers) {
            base += mod->get_ability_duration_bonus(ability);
        }
        return std::min(base, ability->max_duration);
    }
};
```

### Destiny 2 Integration

- Aspects (inherent class abilities)
- Fragments (modifiers to aspects)
- Exotic weapons/armor (unique modifier rules)
- Infusion system (stat bumps)

---

## Phase 12: AI & Boss Encounters (Planned)

**Duration:** 8–12 weeks | **Parallel with:** Phase 11  
**Deliverable:** Behavior trees, squad tactics, boss phase system, pathfinding

### AI Architecture

**Three-tier system:**

| Tier | Scope | Example |
|------|-------|---------|
| **Behavior Tree** | Micro-decisions (attack, dodge, evaluate) | "Is target in range? If yes, attack." |
| **Squad Layer** | Group tactics (focus fire, flanking, retreat) | "3 enemies coordinate to surround player" |
| **Phase System** | Boss state machine (phase transitions, moveset swap) | "Boss at 66% HP → exit phase 1, enter phase 2 with new abilities" |

### Boss AI Example: Orgolis

```
Phase 1 (100–66% HP): Ability set A (Ethereal Form, Reality Bending)
Phase 2 (66–33% HP): + Summon minions
Phase 3 (33–0% HP):  + Rampage phase (faster, more aggressive)

Each phase has its own behavior tree controlling micro-decisions.
Transitions are gated on HP, not arbitrary timers.
```

### Pathfinding

- Navmesh generation from collision geometry
- A* pathfinding for NPC movement planning
- Shared memory: multiple agents query same navmesh without lock contention

---

## Phase 13: World Streaming & Persistence (Planned)

**Duration:** 8–10 weeks | **After:** Phase 11, 12  
**Deliverable:** Chunk-based world loading, world state flags, seamless transitions

### Streaming System (Cyberpunk-inspired)

**Challenge:** Thousands of NPCs, props, and interior spaces with zero visible seams.

**Solution:** Chunk-based loading with priority queue:

```
Priority = distance_to_player + velocity_prediction + player_look_direction
Load chunks in priority order on background thread pool.
Unload distant chunks.
NPCs in loaded chunks update schedules; unloaded chunks are serialized.
```

### World State Graph

Every quest, secret, and NPC interaction sets a persistent world state flag:

```cpp
enum class WorldFlag {
    SECRET_FOUND_001,
    QUEST_DRAGON_DEFEATED,
    NPC_VENDOR_UPGRADED,
    THRONE_WORLD_UNLOCKED,
    // ...
};

// Quests, NPCs, environments query these flags
if (world.has_flag(WorldFlag::QUEST_DRAGON_DEFEATED)) {
    play_celebration_scene();
}
```

### Throne Worlds

Dimensional domains (separate levels) with different physics/lighting/gravity. Each is a discrete world partition that loads as a unit.

---

## Phase 14: UI & Game Systems (Planned)

**Duration:** 8–10 weeks | **After:** Phase 13  
**Deliverable:** HUD, menus, inventory, quest system, dialogue

### Scope

- **HUD:** Health bar, ability cooldowns, minimap, target health
- **Menus:** Main menu, settings, inventory, ability loadout customizer
- **Inventory:** Equipment, consumables, mod system
- **Dialogue:** NPC conversations, quest updates
- **Quest System:** Quest log, objective tracking, world flag integration

---

## Phase 15: Game Content & Vertical Slice (Planned)

**Duration:** Ongoing | **After:** Phase 14  
**Deliverable:** First playable level with all systems integrated

### Scope

- Tutorial level (teach movement, combat, ability swapping)
- One Throne World (demo of seamless streaming)
- Boss encounter (demo of AI + combat + multiplayer)
- Loot drops and build experimentation

---

## Development Timeline

**Solo Developer Estimate:** 18–24 months to vertical slice  
**Small Team (3–5 engineers) Estimate:** 12–16 months to vertical slice  

**Critical Path:**
```
Phase 1 (2w) → Phase 2 (2w) → Phase 3 (6w) parallel with Phase 4 (8w)
  ↓                                           ↓
  Phase 5–6–7 overlap (15w total)     Phase 9–10–11 overlap (20w)
  ↓
  Phase 8 Networking (10w) — started after Phase 6
  ↓
  Phase 12–13–14 overlap (26w)
  ↓
  Phase 15 (ongoing)
```

**Longest dependency chain:** Phase 1 → 2 → 6 → 8 (movement must be stable before networking)

---

## Known Hard Problems

| Problem | Duration | Impact |
|---------|----------|--------|
| **Movement × Networking × Animation intersection** | 3–6 months | Core gameplay feel; cannot be rushed |
| **ECS + Ability modifier pipeline data layout** | 2–4 weeks | Fundamental; changes architecture everywhere if wrong |
| **Render graph composability at scale** | 4–8 weeks | VFX, deferred shading, and post-process depend on this |
| **World streaming with NPC schedules** | 6–10 weeks | Technical implementation is complex; state management is critical |
| **Boss AI phase transitions with multiple behavior trees** | 4–6 weeks | Tuning and debugging is iterative |

---

## Architectural Decisions (Locked)

✅ **Deterministic 32-bit floats** — enables rollback netcode  
✅ **Column-major matrices** — GPU compatibility  
✅ **Archetype ECS** — modifier pipeline support  
✅ **Render graph DAG** — composable passes  
✅ **Command-based movement** — replayable, deterministic  
✅ **Frame-data driven combat** — authored per-attack timing  
✅ **Data-driven abilities** — JSON/binary, not hardcoded  
✅ **Server-hybrid networking** — client predicted, server authoritative

---

## Dependencies (All Permissive Licenses)

| System | Library | License | Why |
|--------|---------|---------|-----|
| **Windowing** | GLFW 3 | zlib | Simple, multi-platform, GPL-compatible |
| **GPU API** | OpenGL 4.6 | Khronos | Industry standard, no licensing pain |
| **AI** | behavior3cpp or custom | MIT/BSD | Behavior tree framework or write from scratch |
| **Physics** | Jolt Physics | MIT | Modern, well-maintained, excellent character controller |
| **ECS** | EnTT | MIT | Header-only, data-oriented, archetype-based |
| **Audio** | miniaudio | Public Domain | Single header, no dependencies, trivial to integrate |
| **UI (Editor)** | Dear ImGui | MIT | Immediate-mode, renders into our passes, battle-tested |
| **Math** | Custom (Phase 1) | — | We build this; GLM is reference |
| **Logging** | spdlog | MIT | Fast, structured logging, thread-safe |
| **GLTF Loading** | cgltf or tinygltf | MIT | Lightweight GLTF parsers |
| **Image Loading** | stb_image | Public Domain | Single header, no deps |
| **Testing** | Catch2 | Boost | Modern C++ unit testing |

---

## Success Criteria

- [ ] Phase 1 complete, all tests pass on Windows + Linux
- [ ] Phase 2 complete, window and GPU context working
- [ ] Phase 3 complete, rotating lit cube on screen
- [ ] Phase 4 complete, ECS alive with sample entities
- [ ] Phase 5 complete, editor can add/remove entities with inspector
- [ ] Phase 6 complete, character moves with fluent input response
- [ ] Phase 7 complete, character slides on slopes, no clipping
- [ ] Phase 8 complete, multiplayer movement validated in stress tests
- [ ] Phase 9 complete, demo boss with full hit detection
- [ ] Phase 10 complete, cape physics on model, IK foot placement working
- [ ] Phase 11 complete, ability editor, modifier pipeline confirmed
- [ ] Phase 12 complete, boss phases transition, squad tactics work
- [ ] Phase 13 complete, one Throne World streams seamlessly, 100 NPCs alive
- [ ] Phase 14 complete, main menu, full HUD, inventory functional
- [ ] Phase 15 complete, first level playable end-to-end

---

## Notes

- **Never over-engineer Phase 1.** It's done. Move on.
- **Measure before optimizing.** Use RenderDoc, NSight, Valgrind, perf. The hot path is not where you think.
- **Data-oriented thinking from day one.** Cache misses will kill performance if you're careless with struct layout.
- **Iterate in vertical slices.** A lit cube on screen beats a perfectly architected invisible system.
- **Profile networking early.** Rollback netcode complexity explodes when abilities interact with movement.
