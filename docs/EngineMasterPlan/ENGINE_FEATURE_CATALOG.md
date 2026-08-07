# GameWorldshaper — Engine Feature Catalog

The complete list of parts and features the engine contains (or should), organized
so a **project picks only the modules it needs**. This is the design source for the
modular feature system: today the launcher/Hub exposes 11 coarse toggles (see
[`project_features.h`](../../editor/include/project_features.h)); this catalog is
the full breakdown to grow that into fine-grained, dependency-aware selection.

## Scope: a general-purpose 3D engine (any genre)

This is **not** an action-RPG engine — it is a general 3D engine, and modularity is
how one engine serves wildly different games. The bar is to support (at least) the
range below; the [genre coverage matrix](#target-genres--module-coverage) maps each
to the modules it needs.

| Target game | Genre it proves | Stresses |
|---|---|---|
| **Cyberpunk 2077** | open-world RPG + FPS + driving + branching narrative | dense streaming, gunplay, vehicles, quests/dialogue |
| **Elden Ring** | souls-like action-RPG | melee frame-data combat, bosses, stamina |
| **Destiny 2** | looter-shooter / MMO-lite | gunfeel, abilities, loot, raids/instancing |
| **Guild Wars 2** | MMORPG | large-scale MP, dynamic events, WvW |
| **Phasmophobia** | co-op horror | small-session MP + **proximity voice**, investigation gear, fear |
| **Forza** | racing sim | **vehicle physics**, tracks, laps, racing AI, replay |
| **Resident Evil** | survival horror | inventory mgmt, puzzles, fixed/over-shoulder camera, tension |
| **Satisfactory** | first-person factory builder | **building/automation/logistics**, massive object counts |

The catalog is organized: **Core** (always-on) → **Engine modules** (M-series, cross-genre
systems) → **Gameplay frameworks** (G-series, general + genre-specific) → the
[coverage matrix](#target-genres--module-coverage).

**Status:** ✅ built + verified · 🟡 partial (core built, integration/breadth left) · 🔴 planned
**Tier:** **Core** = always compiled in (every project needs it) · **Module** = optional, toggle per project
Companion docs: [`AAA_ENGINE_MASTER_PLAN.md`](AAA_ENGINE_MASTER_PLAN.md) (the 16-stage plan) ·
[`REMAINING_WORK.md`](REMAINING_WORK.md) (verified done/remaining).

---

## How modularity works

- **Core** modules are always present — the substrate every game needs (runtime, ECS,
  assets, base renderer, editor). Not toggleable.
- **Modules** are optional and selected per project at creation (and addable later via
  Project Settings). Each declares **dependencies** that auto-enable.
- The manifest (`project.schizo`) stores the enabled set; the editor feature-gates panels
  and the runtime only pays for what's on.
- **Goal of this catalog:** move from 11 top-level toggles to a hierarchy — a project can
  enable "Rendering: Ray Tracing" or "AI: GOAP" individually, and unused sub-systems are
  compiled/loaded out.

---

# CORE (always included)

### C1 · Foundation / Runtime ✅
The substrate under everything.
| Part | Status | Notes |
|---|---|---|
| Memory allocators (stack/pool/arena/frame/general/segregated) | ✅ | + tracking, tags, leak detect; frame allocator wired into the loop |
| Job system (work-stealing) + `parallel_for` | ✅ | not all hot systems on it yet (🟡) |
| Reflection (core) | ✅ | editor inspector still uses its own (🟡 migrate) |
| Serialization (reflection-driven) | ✅ | drives ECS snapshots; editor scene-save still JSON (🟡 migrate) |
| Logging | ✅ | |
| Profiler (CPU / GPU / memory / net / frame-capture) | ✅ | unified in-editor overlay |
| Math (glm), containers, time/clock, config | ✅ | |

### C2 · Entity Component System ✅
| Part | Status | Notes |
|---|---|---|
| Entities / components / systems / queries | ✅ | 100k entities → draw in one parallel pass @ ~3.7 ms |
| Transforms + hierarchy | ✅ | |
| Prefabs / archetypes | 🟡 | |
| **ECS-authoritative migration** | 🟡 | OOP scene still authoritative; ECS is a per-frame shadow |

### C3 · Asset Pipeline ✅
| Part | Status | Notes |
|---|---|---|
| Importers: OBJ, glTF/GLB, FBX (ufbx), USD (tinyusdz), textures, audio | ✅ | |
| Cook → mmap runtime, incremental (dependency graph) | ✅ | |
| GUID linking, hot-reload | ✅ | |
| Texture pipeline (BC/`.ctex`, sRGB, mips, anisotropy, streamable) | ✅ | |
| Virtual-texture **format** (tiled/page-aligned) | ✅ | VT **runtime** is Stage 8 (🔴) |
| Codecs: BC6H / Oodle | 🔴 | optional |

### C4 · Rendering Core ✅
The base renderer every project draws through.
| Part | Status | Notes |
|---|---|---|
| RHI / Vulkan backend, swapchain, render graph | ✅ | |
| Deferred G-buffer + PBR materials | ✅ | |
| Forward / transparent (WBOIT) | ✅ | |
| Shadows (cascaded) | ✅ | virtual shadow maps 🔴 |
| Camera, culling (frustum / meshlet / HZB), LOD (meshopt) | ✅ | HZB + back-face off by default |
| Sky + image-based lighting | ✅ | |
| Tonemap (ACES) + auto-exposure, FXAA | ✅ | |
| Draw-list / Mesh / Material / Texture management, glTF bridge | ✅ | |

### C5 · Editor & Tooling ✅
| Part | Status | Notes |
|---|---|---|
| Viewport, gizmos, hierarchy, inspector | ✅ | |
| Asset browser (full-project, drag-drop, import-into-project) | ✅ | |
| Scene save/load, undo/redo | ✅ | |
| Docking layout, embedded terminal (ConPTY), log output | ✅ | |
| Play-in-editor | ✅ | |
| **Project system**: modular feature selection + **Hub** (versioned installs, updates) | ✅ | Hub is its own repo/product |
| Performance overlay + frame capture | ✅ | baseline regression gating 🔴 |

---

# MODULES (optional, pick per project)

## Simulation

### M1 · Physics ✅ *(default on)*
Jolt-backed. **Depends on:** —
| Sub-feature | Status |
|---|---|
| Rigid bodies + colliders (box/sphere/capsule/convex/mesh) | ✅ |
| Character controller (CharacterVirtual, collide-and-slide, stairs) | ✅ |
| Raycasts / shape queries / triggers | ✅ |
| Constraints / joints | 🟡 |
| Determinism (fixed-step, bit-identical) | ✅ |
| Cloth / ropes / soft-body | 🔴 |
| Destruction / fracture | 🔴 |

### M2 · Combat ✅ core *(default on)* — **Depends on:** Physics
| Sub-feature | Status |
|---|---|
| Frame-data actions (startup / active / recovery hitbox windows) | ✅ |
| Per-attack hit dedup | ✅ |
| Dodge i-frames, parry, poise / stagger / break | ✅ |
| Per-bone hitboxes (rides Animation) | 🔴 |
| Wiring `CombatActor` onto entities + a live hit exchange | 🔴 |

## Animation & Presentation

### M3 · Animation ✅ core *(default on)*
| Sub-feature | Status |
|---|---|
| Skeleton, GPU compute-skinning | ✅ |
| Blend trees, state machine, transitions | ✅ |
| Two-bone IK, foot IK | ✅ |
| Rigged-glTF import → render | ✅ |
| SkinnedMeshComponent scene authoring + drive the player | 🔴 |
| Motion matching, animation streaming | 🔴 |
| State-machine **graph editor** (Stage 11) | 🔴 |

### M4 · VFX ✅ core *(default on)*
| Sub-feature | Status |
|---|---|
| CPU particles + camera-facing billboards + floating-origin rebase | ✅ |
| GPU billboard draw | 🔴 |
| GPU particles / compute sim | 🔴 |
| Decals, trails, ribbons | 🔴 |

### M5 · Advanced Rendering 🟡 *(sub-toggles)*
Optional high-fidelity passes on top of the core renderer.
| Sub-feature | Status |
|---|---|
| SSAO, SSR | ✅ |
| Ray tracing: shadows / AO / reflections (auto on RT GPUs) | ✅ |
| DDGI (real-time GI) | ✅ |
| Volumetric fog / froxel, light shafts / volumetric sun | ✅ |
| Volumetric clouds | ✅ |
| GPU-driven rendering (indirect / mesh shaders) completion | 🟡 |
| Virtual shadow maps | 🔴 |
| Nanite-like virtual geometry | 🔴 |
| Lumen-grade GI | 🔴 |
| Hair / skin / material layering | 🔴 |
| Contact shadows | ✅ |

### M6 · Audio ✅ core *(default on)*
| Sub-feature | Status |
|---|---|
| Spatial mixer (256 voices, lock-free), distance/pan/Doppler | ✅ |
| Listeners, occlusion (raycast lowpass/duck) | ✅ |
| Bus graph, reverb zones, DSP inserts | 🔴 |
| Streaming decode, procedural audio | 🔴 |

### M7 · Game UI ✅ core *(default on)*
| Sub-feature | Status |
|---|---|
| Widgets, anchored layout, draw-list, input/focus | ✅ |
| HUD demo (ImGui-rasterised) | ✅ |
| Native Vulkan UI backend | 🔴 |
| Interactive menus wired to gameplay | 🔴 |

## World

### M8 · Terrain & Water ✅ *(default on)*
| Sub-feature | Status |
|---|---|
| Sculpt, splat paint, chunked LOD, Jolt collision | ✅ |
| Holes / caves (per-cell mask carves mesh + collision) | ✅ |
| Integrated water (waves, fresnel, buoyancy, swimming, physical vs visual) | ✅ |
| Water W3: refraction, underwater fog, foam, air pockets | 🔴 |
| Brushes/undo/LOD polish (Phase D) | 🟡 |

### M9 · World Streaming 🟡 — **Depends on:** (couples with large worlds)
| Sub-feature | Status |
|---|---|
| Grid partition, streaming manager (radius hysteresis) | ✅ |
| Floating origin (double-precision, rebase) | ✅ |
| Editor-camera + `gws_jobs` async integration | 🔴 |
| HLOD, material/anim LOD | 🔴 |
| Procedural terrain / biomes / scatter | 🔴 |

## Intelligence

### M10 · AI 🟡
| Sub-feature | Status |
|---|---|
| Triangle navmesh (A\* + funnel) | ✅ |
| Behavior trees (blackboard, running-memory, cooldown) | ✅ |
| Bake navmesh from the scene | 🔴 |
| Agent path-following + local avoidance (RVO/ORCA) | 🔴 |
| Perception (sight/sound) | 🔴 |
| NPC behavior trees (patrol/chase/attack) | 🔴 |
| Utility AI, GOAP | 🔴 |
| Crowds, dynamic navmesh updates | 🔴 |

## Connectivity

### M11 · Networking ✅ *(default off)* — **Depends on:** Physics (server sim)
| Sub-feature | Status |
|---|---|
| Transport (ENet, reliable/unreliable) | ✅ |
| Replication (reflection-driven snapshots) | ✅ |
| Prediction + reconciliation | ✅ |
| Interest management (AOI) | ✅ |
| Ack-based delta-encoding, snapshot interpolation | ✅ |
| Headless dedicated server (+ authoritative physics in tick) | ✅ |
| In-editor multiplayer + PIE launcher | ✅ |
| Component-granular delta, bandwidth-budget prioritization, zone handoff | 🔴 |

## Scripting

### M12 · Scripting ✅ *(default off)*
| Sub-feature | Status |
|---|---|
| Python (pocketpy), C++ (hot-DLL), C# (.NET) backends | ✅ |
| Shared C `ScriptApi`, hot reload | ✅ |
| Visual scripting (Stage 11) | 🔴 |

# GAMEPLAY FRAMEWORKS (the action-RPG layer)

The systems that turn the engine into *the game*. This is the biggest greenfield
area — almost everything here is 🔴. Grouped into gameplay modules (**G-series**),
each a candidate toggle. Most ride the engine modules above (Physics, Animation,
AI, Networking, Scripting, Game UI). A shared **G0 gameplay framework** underpins
the rest, so build it first.

### G0 · Gameplay Framework Core 🟢 — the shared substrate (core complete)
> Attributes, tags, effects, abilities, damage, event bus, timers, triggers all ✅ + verified
> (`tools/gameplay_check`, 57 checks). Only *data-def-as-assets* remains = the deferred **F5**
> (authored per-module as each lands). All data-driven — the engine bakes in no fixed stats/types.
The GAS-style backbone everything else plugs into. Build this first; the other
G-modules become data + rules on top.
| Sub-feature | Status | Notes |
|---|---|---|
| Attribute set (data-driven stats: health, stamina, STR/DEX/INT, resist…) | ✅ | **`AttributeSet`** — define ANY named attributes per game; authorable/saveable/prefab-able (custom drawer). Engine bakes in no fixed stats. |
| Gameplay Ability System (abilities, costs, cooldowns, cast/channel) | ✅ | **`AbilitySet`/`Ability`** — cost (attribute) + cooldown + require/block tags + GameplayEffects on self/target; `try_activate_ability` + `tick_abilities`. All data. |
| Gameplay Effects (instant / duration / periodic; modifiers, stacking) | ✅ | **`GameplayEffect`** — data-defined modifiers on attributes by name + granted tags; instant/DoT/HoT/timed-buff w/ revert; live `tick_effects`. |
| Gameplay Tags (hierarchical tags for state/conditions) | ✅ | **`GameplayTags`** — any user-defined tags; hierarchical match (child satisfies parent query); authorable/saveable/prefab-able. |
| Damage / mitigation pipeline (types, resist, crit, execution) | ✅ | **`apply_damage`/`DamageInfo`** — data-driven damage types by name; `resist.<type>` attribute mitigation + `immune.<type>` tag; resolves to an instant effect on the health attribute. |
| Event / messaging bus + trigger volumes + cooldown/timer manager | ✅ | **`GameplayEventBus`** (named + `*` pub/sub, deferred flush, no re-entrancy) + **`TimerManager`** (after/every/cancel) + **`TriggerVolume`/`TriggerActor`** (box/sphere overlap → enter/exit events). Owned + ticked by the bridge (`tick_gameplay`). |
| Data-driven definitions (items/abilities/quests as assets) + scripting hooks | 🔴 | rides Scripting/Assets |

### G1 · Player & Movement 🟡 — **Depends on:** Physics, Animation
| Sub-feature | Status |
|---|---|
| Character controller (first / third person) | ✅ |
| Camera systems (third-person orbit, first-person, aim, lock-on, cinematic) | 🔴 |
| Movement set: sprint, dodge/roll, jump, crouch, climb, swim, glide, dash | 🟡 (swim ✅) |
| Mounts / vehicles | 🔴 |
| Stamina / resource-gated actions | ✅ (via G0) — a resource-gated action = a G0 `Ability` (cost=an attribute like Stamina + cooldown + granted state tags). |
| Player state machine (idle/move/combat/interact/dead) | ✅ **`StateMachine`** — data-driven FSM; states grant/remove `GameplayTags`, transitions fire on named events + are tag-gated, states auto-time-out; enter/exit broadcast on the event bus. Ticked live (`tick_state_machines`). |

### G2 · Combat & Abilities 🟡 — **Depends on:** G0, Physics, Animation, VFX
| Sub-feature | Status |
|---|---|
| Melee combat: combos, frame-data hitboxes, i-frames, parry, poise | ✅ **`CombatActor`** (ECS) — frame-data attacks (startup/active/recovery), sphere hit/hurtboxes, single/multi-hit dedup, dodge i-frames, parry (attacker punished), poise→stagger. Damage routed through G0 (`apply_damage` + resist/immunity); on-hit `GameplayEffect`s; hit/parry/whiff/stagger events + drives the target FSM. Fixed 60 Hz. |
| Per-bone hitboxes (rides Animation) | 🔴 |
| Ranged combat: aiming, projectiles/ballistics, hitscan | 🔴 |
| Spells / abilities: cast, channel, cooldown, resource cost, targeting | 🟡 (via G0) — `Ability` gives cost/cooldown/tag-gate + effects; cast/channel time + targeting modes remain. |
| Targeting / lock-on / soft-aim | 🔴 |
| Status effects: stun, slow, burn, poison, bleed, root (via G0 effects) | ✅ — G0 `GameplayEffect`s (DoT/tags/timed) applied as `on_hit_effects` or by abilities; verified bleed-on-hit. |
| Threat / aggro | 🔴 |
| Hit reactions, knockback, launch, ragdoll, death | 🟡 — hitstun/stagger + FSM death state ✅; knockback/launch/ragdoll pending (rides Physics/Animation). |

### G3 · Stats & Progression 🟢 — **Depends on:** G0
| Sub-feature | Status |
|---|---|
| Attributes + derived stats | ✅ `AttributeSet` (G0) + **`DerivedStats`** — target = base + Σ(coeff*source), `set_max` for resource caps; `recompute_derived`. |
| Health / mana / stamina resources + regen | ✅ **`Regeneration`** — per-attribute rate, `tick_regen` (clamped). |
| Leveling & XP curves | ✅ **`Progression`** — data-driven `XpCurve` (base+linear+quad), `grant_xp` levels up, points/level, per-level attribute growth, `level.up` event. |
| Skill / talent trees, perks | ✅ **`SkillTree`/`UnlockedSkills`** — nodes with cost/prerequisites, permanent stat grants + granted tags; `unlock_skill` (spends points), `skill.unlocked` event. |
| Classes / builds, respec | ✅ `respec` reverts every node exactly + refunds points. A "class" = a prefab (starting `AttributeSet` + `SkillTree` + abilities). |

### G4 · Items & Inventory 🟢 — **Depends on:** G0
| Sub-feature | Status |
|---|---|
| Item definitions (data-driven) + rarity / tiers | ✅ **`ItemDef`** registry (id/kind/rarity/stack/weight/slot/modifiers/tags/on-use/affix-pool/set); no engine change per item. |
| Procedural item generation (affixes / rolls) | ✅ `roll_item` picks from the affix pool with a deterministic seedable `Rng` (repeatable + net-safe); rolls bump rarity. |
| Inventory (grid/list, stacking, weight/slots) | ✅ **`Inventory`** — stacking to max_stack, slot + weight caps, add/remove/count. |
| Equipment / loadout (slots, set bonuses) | ✅ **`Equipment`** — one item/slot; equip applies modifiers→AttributeSet + tags (reverts EXACTLY); `SetBonus` registry applies at N pieces. |
| Loot system (drop tables, rolls, ground loot) | ✅ **`DropTable`** weighted + per-entry chance + qty range + affix rolls → `roll_loot` (deterministic). Ground-loot spawn = game code (spawn an entity holding the instance). |
| Consumables (potions, food, scrolls), currency | ✅ `use_item` applies the def's on-use `GameplayEffect`s + decrements; currency = an `ItemKind::Currency` stack (or a G0 attribute). |
| Item comparison / tooltips | 🟡 data-ready (`item_modifiers`/`find_item` expose everything a tooltip/compare needs); UI later. |

### G5 · Economy & Crafting 🟢 — **Depends on:** G4
| Sub-feature | Status |
|---|---|
| Vendors / shops (buy / sell, restock) | ✅ **`Vendor`** — stock list (item/price/qty), `vendor_buy` (pay currency, decrement stock) + `vendor_sell` (pay `ItemDef::value` × ratio); currency = a G0 attribute wallet (`wallet_*`, default "Gold"). Timed restock = follow-up. |
| Crafting (recipes, stations, materials) | ✅ **`RecipeDef`** registry — inputs→outputs, station-tag + skill-tag gated; `can_craft`/`craft` over the G4 inventory. |
| Gathering / harvesting nodes | ✅ **`HarvestNode`** — yields items on `harvest`, then respawns after a cooldown (`tick_harvest`). |
| Upgrading / enchanting / salvage | ✅ **`SalvageDef`**/`salvage` (item → materials); upgrade/enchant = a recipe variant over the same primitives. |
| Trading (player↔player, MP) | 🟡 `transfer_item` (inventory→inventory) is the local primitive; full P2P trade rides Networking. |

### G6 · Quests & Narrative 🔴 — **Depends on:** G0, Scripting
| Sub-feature | Status |
|---|---|
| Quest system (objectives, stages, triggers, branching, rewards) | 🔴 |
| Quest log / journal | 🔴 |
| Dialogue system (branching, choices, conditions, skill checks) | 🔴 |
| Dialogue / conversation UI | 🔴 |
| Cutscenes / scripted sequences | 🔴 |
| Lore / codex, voice-over hooks | 🔴 |

### G7 · World Interaction 🟡 — **Depends on:** G0, Physics
| Sub-feature | Status |
|---|---|
| Interaction system (prompts, usables, focus) | ✅ **`Interactable`** (prompt/event/range/enabled/consume) + `nearest_interactable` (proximity focus) + `interact`/`try_interact` (fires the event on the bus). Exposed to scripts as `interact(e)`. |
| Containers / chests / lootables | ✅ a chest = an entity with an `Inventory` + an `Interactable` ("container.open"); loot via `transfer_item` (G5). |
| Doors / gates / levers / switches / puzzles | 🟡 any `Interactable` fires a named event; wire the door/switch reaction via a G0 event handler / state machine (no dedicated door mover yet — rides Physics/anim). |
| Pickups, harvest nodes, traps / hazards | ✅ pickups (**`Pickup`** → grants an item + consume-on-use) + harvest nodes (G5); traps/hazards = a trigger volume (G0) applying a damage effect. |
| Gameplay destructibles | 🔴 (rides Physics) |
| Fast travel / waypoints, points-of-interest / discovery | 🔴 |

### G8 · NPCs & Encounters 🔴 — **Depends on:** AI, G0, G2
| Sub-feature | Status |
|---|---|
| NPC framework (schedules / routines / needs) | 🔴 |
| Enemy archetypes + spawners + encounters (waves, arenas) | 🔴 |
| Bosses (phases, mechanics) | 🔴 |
| Companions / pets / summons | 🔴 |
| Factions & reputation | 🔴 |
| Wildlife / ambient life, dynamic world events | 🔴 |

### G9 · Persistence & World State 🔴 — **Depends on:** Serialization
| Sub-feature | Status |
|---|---|
| Save / load game state (slots, autosave) | 🔴 |
| World-state persistence (chests opened, bosses killed, quest flags) | 🔴 |
| Checkpoints / respawn / bonfire-style rest | 🔴 |
| Day-night cycle + weather (gameplay-affecting) | 🔴 |
| Difficulty / level scaling, New Game+ | 🔴 |
| Achievements / stats tracking | 🔴 |

### G10 · Multiplayer Social 🔴 — **Depends on:** Networking, G0
| Sub-feature | Status |
|---|---|
| Parties / groups, shared quests | 🔴 |
| Guilds / clans, text chat | 🔴 |
| **Proximity / positional voice chat** (push-to-talk) — rides Audio + Networking | 🔴 |
| Sessions / matchmaking / lobbies | 🔴 |
| Shared world / instancing / sharding | 🔴 |
| PvP (arenas, open-world), co-op | 🔴 |
| Trading, leaderboards | 🔴 |

### G11 · Gameplay UI 🟡 — **Depends on:** Game UI, and the G-modules it visualizes
| Sub-feature | Status |
|---|---|
| HUD (health/mana/stamina, hotbar, buffs, crosshair) | 🟡 (HUD demo ✅) |
| Menus: inventory, character/stats, skills, map, quest log | 🔴 |
| Minimap / world map + markers / objective waypoints | 🔴 |
| Floating damage numbers, notifications / toasts, tooltips | 🔴 |
| Vendor / crafting / dialogue screens | 🔴 |

> G0–G11 are the *general* gameplay layer (most games use some of them). The **genre
> modules** below are the specialised systems that define a specific kind of game.

## Genre modules (define a specific kind of game)

### G12 · Shooter / Gunplay 🔴 — **Depends on:** G0, G2, Physics, Animation, VFX
*(Destiny 2, Cyberpunk, Phasmophobia, Resident Evil)*
| Sub-feature | Status |
|---|---|
| Weapons framework (data-driven: fire modes, magazines, reload) | 🔴 |
| Ballistics: projectiles + hitscan, penetration, damage falloff | 🔴 |
| Recoil / spread / bloom patterns, aim-down-sights (ADS) | 🔴 |
| Gunfeel: muzzle flash, tracers, shells, camera shake, impact decals/FX | 🔴 |
| Cover system, weapon sway/handling | 🔴 |
| Ammo + weapon inventory, attachments / mods | 🔴 |
| Hit markers, headshots, weak points | 🔴 |

### G13 · Vehicles & Driving 🔴 — **Depends on:** Physics *(Forza, Cyberpunk)*
| Sub-feature | Status |
|---|---|
| Vehicle physics: suspension, tires (friction/slip), drivetrain, engine/gearbox | 🔴 |
| Handling models (arcade ↔ sim), aerodynamics, damage model | 🔴 |
| Vehicle types: cars, bikes, hover, aircraft, boats | 🔴 |
| Enter/exit, seats, driving camera, cockpit | 🔴 |
| **Racing framework**: track splines, checkpoints, laps, timing, grid/standings | 🔴 |
| Racing AI (racing line, rubber-band), ghosts / replay, photo mode | 🔴 |

### G14 · Building & Automation 🔴 — **Depends on:** G0, Physics *(Satisfactory, base-builders)*
| Sub-feature | Status |
|---|---|
| Placement (grid + free, snapping, rotation, validation, ghost preview) | 🔴 |
| Structures: foundations / walls / floors / supports | 🔴 |
| Resource logistics: conveyors, pipes, storage, splitters/mergers | 🔴 |
| Networks: power / fluid / signal (graph simulation) | 🔴 |
| Machines / crafting chains / production simulation at scale | 🔴 |
| Blueprints (copy-paste), persistence of built structures | 🔴 |
| Massive-object-count perf | 🔴 (rides GPU-driven rendering + streaming) |

### G15 · Survival & Horror 🔴 — **Depends on:** G0 *(Resident Evil, Phasmophobia, survival)*
| Sub-feature | Status |
|---|---|
| Needs: hunger / thirst / temperature / oxygen / fatigue | 🔴 |
| Sanity / fear / tension systems | 🔴 |
| Light & dark gameplay (flashlight, darkness, exposure) | 🔴 |
| Constrained inventory / weight / item management (RE-style) | 🔴 |
| Investigation gear (EMF, camera, sensors — Phasmophobia) | 🔴 |
| Horror presentation: scripted/dynamic scares, VHS/handheld-cam post-FX, audio stingers | 🔴 |
| Permadeath / consequence, scarce resources | 🔴 |

### G16 · Stealth & Detection 🔴 — **Depends on:** AI, G0 *(Cyberpunk, horror, many)*
| Sub-feature | Status |
|---|---|
| Perception: sight cones, hearing, light/shadow visibility (rides AI) | 🔴 |
| Alert states / search / investigation | 🔴 |
| Takedowns / stealth kills, distractions / lures | 🔴 |
| Cover / lean / peek, noise generation | 🔴 |
| Detection meter / awareness UI | 🔴 |

> **Cross-cutting features the reference games also need:** proximity/positional **voice chat**
> (Phasmophobia — rides Audio M6 + Networking M11); **replay & photo mode** (Forza — rides the
> render graph); dense **crowds** (GW2/Cyberpunk — rides AI M10); huge **object counts**
> (Satisfactory/MMOs — rides GPU-driven rendering M5 + streaming M9); **day-night + weather**
> (open worlds — G9). These are noted on their home modules rather than duplicated here.

## Platform

### M14 · Platform 🔴 (Windows only today)
| Target | Status |
|---|---|
| Windows | ✅ |
| Linux (+ headless dedicated server) | 🔴 |
| Consoles | 🔴 |
| Mobile | 🔴 |

---

## Target genres & module coverage

Each target game ≈ a **starter preset** — a module selection over the always-on Core.
This is the proof the catalog spans the genres (and the seed for the "presets" in the
open questions). **Bold** = the module that defines that genre.

| Game | Engine modules (beyond Core) | Gameplay modules |
|---|---|---|
| **Cyberpunk 2077** | Physics, Animation, Audio, VFX, Adv-Render, Terrain, **World Streaming**, AI (+crowds), Scripting, Game UI | G0, G1, G2, **G12 Shooter**, **G13 Vehicles**, G3, G4, G6, G7, G8, G9, G16, G11 |
| **Elden Ring** | Physics, Animation, Audio, VFX, Adv-Render, Terrain, World Streaming, AI, Game UI | G0, G1, **G2 melee combat**, G3, G4, G7, **G8 bosses**, G9, G11 |
| **Destiny 2** | Physics, Animation, Audio, VFX, Adv-Render, **Networking**, AI, Game UI | G0, G1, G2, **G12 Shooter**, G3, **G4 loot**, G8, **G10 raids/instancing**, G11 |
| **Guild Wars 2** | Physics, Animation, Audio, VFX, Terrain, **World Streaming**, **Networking**, AI (+crowds), Game UI | G0, G1, G2, G3, G4, G6, **G8 dynamic events**, **G10 large-scale MP**, G11 |
| **Phasmophobia** | Physics, Animation, **Audio (+proximity voice)**, VFX, Adv-Render (dynamic lights), **Networking (co-op)**, Scripting, Game UI | G0, G1, G7, **G15 Survival/Horror + gear**, **G10 voice**, G11 |
| **Forza** | **Physics (vehicle)**, Audio, VFX, Adv-Render, Terrain | G0, **G13 Vehicles & Racing**, G11 (racing HUD) |
| **Resident Evil** | Physics, Animation, Audio, VFX, Adv-Render, AI, Game UI | G0, G1 (over-shoulder), G2, G12, **G15 inventory-mgmt + puzzles**, G7, G9, G11 |
| **Satisfactory** | Physics, Audio, VFX, **Adv-Render (GPU-driven, huge counts)**, Terrain, **World Streaming**, Scripting, Game UI | G0, G1 (FP build cam), **G14 Building & Automation**, G4, G9, G11 |

**What that tells us about build priority** — the recurring, cross-genre foundations are:
`G0` (every game), `G1/G2` (most), and the engine-core integrations these presets lean on that
are still 🟡/🔴: **AI integration (M10)**, **World-streaming integration (M9)**, **GPU-driven
rendering (M5)**, and **vehicle physics (M1/G13)**. Build the broadly-shared pieces before the
genre-specific ones.

## Snapshot: where we stand

- **Core (C1–C5):** ✅ solid, with known integration debt (ECS-authoritative, reflection/serialization adoption, all-hot-systems-on-jobs).
- **Simulation / Presentation / World / Connectivity / Scripting cores:** ✅ built + verified as libraries; several need **integration** (AI, world streaming) or **wiring onto entities** (combat, animation player, VFX GPU draw).
- **Gameplay frameworks (G0–G11):** the biggest greenfield area — this is where "engine" becomes "action-RPG."
  Nearly all 🔴. **Build order:** G0 (framework core) → G1/G2 (player + combat, ride Animation/Physics) →
  G3/G4 (stats + items) → the rest layer on fast. G8 (NPCs) needs AI (M10) wired first.
- **Advanced-rendering reach + platform:** the fidelity/scale tier.

## Readiness: what the core must provide before gameplay (G-series)

*Verified against [`REMAINING_WORK.md`](REMAINING_WORK.md) + the master plan.* The G-modules are
almost all 🔴, but the real blocker isn't "write the gameplay code" — several sit on **core
integration debt**. Build gameplay on today's core and you build it twice.

### F · Foundation gaps — block nearly ALL gameplay (do these first)
| # | Core gap (source) | Why gameplay needs it | State |
|---|---|---|---|
| **F1** | **Make ECS authoritative** — retire the OOP shadow; inspector/gizmo/save write ECS; gameplay off `GetComponent<T>()` *(Stage 1)* | Gameplay = components + systems. G0 attributes/abilities/effects must live in a real authoritative ECS. | 🟡→ **gameplay ECS is now the authoritative home** (persistent world exposed; components survive sync; `authorable_components` registry). Transform/mesh still OOP. |
| **F2** | **Editor inspector → core reflection** *(Stage 0)* | A new gameplay component should auto-appear + be editable with **no hand-written UI per component**. | ✅ **generic reflection inspector** — "Gameplay Components (ECS)" section draws/edits any authorable component via `for_each_field` (verified `gameplay_check`). |
| **F3** | **Scene-save + game-save → core serialization** *(Stage 0)* | Gameplay state must persist → authoring saves **and** G9 save/load. | ✅ **gameplay components persist** via a reflection-driven `.gameplay` sidecar (index-keyed like NetId; generic; verified). Seeds game-save. |
| **F4** | **Prefabs / archetypes** *(Stage 1)* | Spawning is fundamental — enemy / item / projectile / NPC = instantiate a template. | ✅ **`Prefab` capture/apply/serialize** (`prefab.h`) + editor "Save as Prefab" / "From Prefab" spawn (verified round-trip). |
| **F5** | **Gameplay data-definitions as assets** *(rides Assets ✅ + reflection)* | Items / abilities / quests authored as data, cooked + loaded. | 🟡 — **items done**: `.items` text data-files (`gameplay_item_file.h`: parse/register/`load_items_dir`) auto-loaded from `assets/gameplay/` at editor startup + Tools ▸ Reload/Create example. Same pattern extends to skills/loot/abilities. |

> **F1–F3 share one root:** make the **ECS + core reflection/serialization** the authoritative spine and
> have the editor adopt it. That is the "finish the ECS migration" Tier-1 item — the single
> highest-leverage core work for gameplay. Do it once, and G0 + every gameplay component author,
> save, and network cleanly.

### Per-module core integrations — block specific G-modules
| Core integration *(stage)* | Unblocks | State |
|---|---|---|
| **AI integration**: bake navmesh from scene, agent path-follow, perception *(Stage 9)* | G8 NPCs/enemies, G16 stealth | 🔴 (core lib ✅) |
| **Animation → player**: drive the character with the state machine *(Stage 5→10)* | G1/G2 visible character motion | 🔴 (core ✅) |
| **VFX GPU billboard draw + decals** *(Stage 10 / rendering)* | G12 gunfeel, G2/G4 effects | 🔴 (CPU particles ✅) |
| **Vehicle physics**: expose Jolt vehicle constraints *(Stage 4)* | G13 vehicles / racing | 🔴 (rigid bodies ✅) |
| **Per-bone hitboxes** *(Stage 4/10)* | G2 precise melee / hit locations | 🔴 |
| **GPU-driven rendering (3.1) + world-streaming integration (Stage 8)** | G14 factory counts, open-world (Cyberpunk/GW2/Satisfactory) | 🟡 |

### Already READY to build gameplay on
ECS core (fast, verified — needs the *authoritative flip*, not a rewrite) · Physics (rigid bodies,
character controller, raycasts) · Animation core (skeleton/state-machine/IK/skinning — needs player
wiring) · Combat frame-data (G2 melee) · Networking (full stack) · Scripting (Py/C++/C#) · Audio ·
Game-UI framework · Assets/cook · reflection-serialization core.

### Recommended core-work sequence to unlock gameplay
1. **Finish the ECS migration (F1–F3)** — authoritative ECS + inspector/save on core reflection/serialization.
   *Unlocks G0 and clean authoring/save/net of every gameplay component.*
2. **Prefabs / spawning (F4)** — *unlocks spawning enemies / items / projectiles.*
3. **G0 · Gameplay Framework Core** — attributes / abilities / effects / tags on the now-authoritative ECS.
4. Then **per-module integrations** (AI, animation-player, VFX-GPU, vehicle-physics) as each G-module is built.

---

## Open design questions for the modular system
1. **Granularity:** promote sub-features (e.g. "Advanced Rendering: DDGI", "AI: GOAP") to individual toggles, or keep coarse modules + a "quality/preset" per module?
2. **Compile-out vs. runtime-off:** should disabled modules be excluded at build time (smaller binary) or just not initialized? (Today only panel visibility + init are gated.)
3. **Dependencies:** encode the full dependency graph here (e.g. Combat→Physics, Networking→Physics, per-bone hitboxes→Animation) so selection auto-resolves.
4. **Presets:** ship starter templates ("Multiplayer FPS", "Open-world RPG", "Top-down") that pre-select module sets.
