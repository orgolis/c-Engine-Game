# Road to the test game

**Set:** 2026-11-11. **Starting point:** v0.8.13.

The optimization work ranked in [OPTIMIZATION_RESEARCH.md](OPTIMIZATION_RESEARCH.md)
is parked. It will be done as one engine upgrade, measured on a real game
instead of on a stress scene. Four things come first, in this order:

1. **Visual scripting** at the level of Unreal's Blueprints.
2. **Terrain and world building**: water that runs downhill, regions and
   biomes, altitude rules, grass, texturing, forests, and tools that are
   pleasant to use.
3. **Gameplay modules, finished**: items, inventory, weapons, combat, guns,
   advanced movement, abilities.
4. **A test game** built from that state, with the editor's own tools.

Every milestone below ships as a release (0.8.n) with a headless check.

## Where each area stands today

| Area | What exists | What is missing |
|------|-------------|-----------------|
| Visual scripting | One graph per scene (`gameplay_logic.h`): 28 node kinds in a fixed enum, four value types, events → actions, graph variables, a 414-line editor panel | Typed pins beyond four types, Branch with two outputs, loops, functions, macros, graphs on entities, engine API as nodes, a compiler, a debugger |
| Terrain and world | Sculpt (raise, lower, smooth, flatten), four painted layers with PBR materials, holes, chunked LOD, a flat water plane with waves and buoyancy, array/scatter/spline placement tools | Flowing water, biomes and regions, rule-based texturing, grass, trees, more brushes |
| Gameplay | About two dozen data-driven modules in `engine/core/ecs/gameplay_*.h` (attributes, effects, abilities, items, inventory, equipment, loot, weapons, projectiles, melee frame data, state machines), each with a headless check | Authoring as assets in the editor, UI, animation and VFX hooks, cameras, the movement set, and use from visual scripts |

The gameplay pattern is the one REMAINING_WORK.md already names: the core is
built and verified, the integration is not.

## Phase A: visual scripting ("Logic Graph")

The design is in [VISUAL_SCRIPTING.md](VISUAL_SCRIPTING.md).

| # | Milestone | Done when |
|---|-----------|-----------|
| A1 | **Core.** Typed values, a node registry that turns C++ functions into nodes, the graph asset, a compiler to bytecode, a resumable VM, a text format, the standard library (flow, math, string, array) | `vscript_check` passes: flow control, pure-node evaluation, variables, functions, custom events, latent delays, arrays, error reporting |
| A2 | **Engine binding and runtime.** Every script API verb and every reflected component property is a node. Logic scripts attach to entities; the level graph runs on the new VM. Lifecycle, input, collision, trigger and gameplay events | A scripted entity moves, reacts to a trigger and deals damage in Play mode; old `.logic` files load converted |
| A3 | **Editor.** Typed pins and wires, a searchable palette, drag-off-pin suggestions, variables and functions panel, pin defaults, comments, reroutes, copy/paste, undo, compile errors on the node | A graph of 50 nodes can be built without touching a text file |
| A4 | **Debugger.** Breakpoints, step, watches, live flow on the wires, per-instance selection | A breakpoint stops Play, the values on the pins are readable, step moves one node |
| A5 | **Class features.** Function and macro libraries, event dispatchers, interfaces, parent scripts, construction script, timelines, user enums and structs, data tables | Each has a check and a sample |
| A6 | **Breadth.** Nodes for each system as phases B and C finish it; find references; collapse to function | The test game needs no text script |

**Progress (v0.8.19):** A1 done. A2: logic scripts run on entities, every
`ScriptApi` verb is a node, key and gameplay-bus events arrive, Inspector fields
come from variables, scripts send events to and read variables of other
entities; still open are collision events, component-property nodes and moving
the per-scene Logic Graph onto the new VM. A3: the graph editor builds and
edits scripts (tabs, palette, wire suggestions, variables, functions, undo,
errors on the node, multi-select, copy and paste, comment boxes, knots on
wires) and opens from the Inspector and the Asset Browser. A4 to A6 not started.

Deliberately later than this phase: replication (RPC nodes and replicated
variables), a UI designer, animation graphs as logic graphs.

## Phase B: terrain and world building

| # | Milestone | Done when |
|---|-----------|-----------|
| B1 | **Tool feel.** More brushes (noise, terrace, ramp, erode, stamp), a brush preview, more than four layers, heightmap import and export | A mountain valley can be shaped in minutes |
| B2 | **Rule-based texturing.** Layers placed by slope, altitude and noise, with painting on top | Rock appears on cliffs and snow above a height without painting |
| B3 | **Foliage.** Instanced grass and trees, painted or placed by rule, with LOD, culling, wind and tree collision | A forest of 100,000 trees and a meadow render inside the frame budget |
| B4 | **Regions, biomes, sections.** A biome map (painted, or from altitude and moisture), a rule set per biome (textures, foliage, fog, ambience), sections for streaming | Walking from a valley to a peak crosses three biomes |
| B5 | **Hydrology.** Water placed at a source runs downhill, fills basins into lakes and falls over drops; rivers have flow direction; swimming and buoyancy follow the current | A spring on a hill becomes a river that reaches the sea |
| B6 | **Workflow.** World templates, larger worlds | A new world starts from a preset |

## Phase C: gameplay modules, finished

Each module is done when it has: an asset type, an editor to author it,
runtime behaviour in Play mode, UI where a player needs it, visual scripting
nodes, sample content and a check.

| # | Milestone |
|---|-----------|
| C1 | Items and inventory: definitions as assets, pick-up, world items, inventory and equipment screens |
| C2 | Melee combat: animation-driven hitboxes, hit reactions, lock-on |
| C3 | Guns: hitscan and projectiles, recoil, spread, aim-down-sights, ammunition, muzzle flash, tracers, impact decals |
| C4 | Advanced movement and cameras: sprint, crouch, slide, dash, mantle, climb, glide; first-person, third-person and aim cameras |
| C5 | Abilities: assets with cast, channel and targeting, effects, cooldown display, VFX hooks |
| C6 | Glue: a player template, a HUD, save and load |

## Phase D: the test game

A small game built only with the editor and logic graphs: a terrain with
biomes and a river, a player with the full movement set, loot, an inventory,
guns and melee, abilities, enemies. Its purpose is to be the scene the
optimization upgrade is measured on, and to find what the first three phases
missed.

## Then: the optimization upgrade

The 16 ranked candidates of OPTIMIZATION_RESEARCH.md, re-measured on the test
game before any of them is implemented.
