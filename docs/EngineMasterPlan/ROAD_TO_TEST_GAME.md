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

**Progress (v0.8.24):** A1 done. A2: logic scripts run on entities, every
`ScriptApi` verb is a node, key and gameplay-bus events arrive, Inspector fields
come from variables, scripts send events to and read variables of other
entities, and events scoped to the object (On Trigger Enter / Exit, On Event
About Me); still open are physics contact events (the physics layer reports
none yet), component-property nodes and moving the per-scene Logic Graph onto
the new VM. A3: the graph editor builds and
edits scripts (tabs, palette, wire suggestions, variables, functions, undo,
errors on the node, multi-select, copy and paste, comment boxes, knots on
wires) and opens from the Inspector and the Asset Browser. A4 done: breakpoints,
Continue and Step, values on pins and variables, live flow, per-object
selection. A5 done except macros and formal interfaces: Timeline, event dispatchers,
calling another script's functions, user enums and structs, parent scripts,
function libraries. A6 not started.

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

**Progress (v0.8.30):** B1 mostly done, B2 done, B3 first part, B4 mostly done, B5 first part, B6 first part. The brushes are plain functions in
`engine/scene/include/terrain_brush.h`, checked by `tools/terrainbrush_check`
(22 checks): Raise, Lower, Smooth, Flatten (to the height where the stroke
began), Dig / Fill Hole, and new Noise, Terrace, Ramp and Erode. Whole-terrain
tools in the Inspector, each one undo step: Generate hills, Erode, and 16-bit
heightmap Export / Import (.pgm or raw .r16). Still open in B1: more than four
texture layers, stamps. B2: each layer can have a placement rule (height range,
slope range, noise break-up, weight), kept in the scene; "Apply layer rules"
rewrites the splatmap from them as one undo step, and painting goes on top.
B5: "Water Source Mode" -- click the terrain and the water runs downhill, fills
hollows into lakes up to their rim, spills on and ends at the sea (the
terrain's water level) or the edge; the bed is carved and water surfaces are
placed along it (`engine/scene/include/terrain_hydrology.h`). Still open in
B5: flowing water (current direction on the surface, swimming and buoyancy
following it), waterfalls drawn as falls. B3: a Foliage component on a terrain
holds layers (conifer, broadleaf, bush, grass, rock, or a model file) placed by
density, rule, texture layer and a painted mask, deterministic from a seed
(`engine/scene/include/foliage.h`); drawn through `editor/include/foliage_render.h`
as draw items in 32 m cells, culled by distance and frustum, nearest first.
**Measured:** 100,000 trees placed on a 400 m terrain, the nearest 40,000 drawn
(the per-frame budget): 60 fps, GPU 3.3 ms, but about 11 ms of CPU goes to
foliage and the general culling and sorting its draw items go through. Without
the budget (all 96,000 visible) the frame was 50 ms CPU / 39 ms GPU. The done-when
of B3 needs a GPU-instanced foliage pass (instances in a buffer, culled on the
GPU, drawn into the G-buffer and the shadow map without per-tree draw items);
that belongs to the optimization upgrade. Also open in B3: wind, tree
collision, LOD meshes for model foliage, honouring "casts shadow". B4: a Biome
component divides a terrain by altitude and moisture (wet near the terrain's
water and any water surface over it), with painted overrides; each biome has a
ground texture layer, a density per foliage layer and an optional atmosphere
(ambient light, fog). "Apply biomes" bakes ground and foliage (one undo step);
atmosphere blends live under the camera; scripts get "Biome At" and a
"biome.enter" gameplay event (`engine/scene/include/biome.h`). Checked: the
valley-to-peak walk crosses marsh, forest and alpine. Not yet: ambient sound per
biome (scripts can do it with biome.enter), sections for streaming (the world
streaming that exists is separate). B6: Create > World from a preset (Rolling
hills, Mountains, Islands, Plain) builds ground, erosion, rivers, biomes,
ground colours and foliage in one undo step (`engine/scene/include/world_preset.h`);
every preset is built and checked headlessly. Still open in B6: larger worlds
(several terrains tiled, streamed).

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

**Progress (v0.8.31):** an audit at the start of phase C found much of it
built in earlier stages (items, inventory, equipment, loot, crafting, the
in-game inventory and character screens, a HUD, melee combat with hurtboxes,
weapons with ammunition, spread and projectiles, abilities with cooldowns,
save games). Gaps found: no item or weapon editor, no recoil, aim-down-sights,
muzzle flash, tracers or impact marks, weapons not authorable as data, the
player could not shoot with the mouse, no mantle or glide. C3 first part done
in v0.8.31: weapons as data (`weapon` blocks in `.items` files), recoil, spread
bloom, aim-down-sights with zoom, burst fire, rounds stopped by level geometry,
ammunition carried as an inventory item, and in Play: left mouse fires, right
aims, R reloads, a crosshair whose gap is the spread, a hit marker, an ammo
readout, muzzle flash, tracers and impact marks. Still open in C3: a weapon
editor panel, decals that are real projected decals, weapon models in the hand.

**Progress (v0.8.32):** C4 movement done apart from cameras. The old
character state machine had wall run, climb and slide as empty stubs, no
crouch, and a dash that the next frame overwrote. Replaced in Play by
`ecs/gameplay_movement.h`: walk, sprint, crouch (the capsule shrinks, and
stays down under a low ceiling), jump with coyote time, a jump buffer and air
jumps, slide (faster downhill), dash, mantle, glide, wall run, climbing
surfaces tagged `surface.climbable`, swimming and stamina. Settings are the
Movement component (Inspector), every optional move can be turned off; keys
in docs/systems/MOVEMENT.md. Checked by movement_check (59 checks against box
geometry) and a scripted Play run in Jolt (GWS_TEST_MOVES: sprint, slide,
dash, jumps, mantle onto a 1.8 m ledge).

**Progress (v0.8.33):** C4 done. A Camera Rig component gives the player a
third-person camera (distance, shoulder, pivot height) that comes in over the
shoulder while aiming and is pulled in front of walls, easing back out
(`ecs/gameplay_camera.h`, pure, checked in movement_check). A Move Animation
component picks the player's rigged-model clip for each move (sprint, crouch,
jump/fall, slide, dash, mantle, glide, wall run, climb, swim), falling back
to a close move's clip or the model's own idle/move choice. Without a rig the
authored camera is used exactly as before.

**Progress (v0.8.34):** the item and weapon editor. A `.items` file opens
from the Asset Browser (or View > Items and Weapons) as a form: the file's
items and weapons in a list (add, duplicate, delete, reorder, filter; clashing
ids in red), every setting of the selected one beside it, a damage-per-second
readout for weapons, and the ammunition picked from the known items. Saving
writes the file back (`ecs::items_doc_to_text`: blocks keep their order, the
comment lines at the top stay) and registers it at once. Checked by
gameplay_check (the file read, written and read again is identical; every
setting survives). Still open in C3: real projected decals, weapon models in
the hand.

**Progress (v0.8.35):** C5 abilities. The old ability primitive (cost,
cooldown, tag gates, effects at once) could not be authored, had no input,
cast time, channel, targeting or display. New `ecs/gameplay_ability_cast.h`:
abilities as data (`ability` blocks in .items files, editable in the Items
editor), cast time (optionally broken by moving), channels that pulse while
held, four kinds of reach (self, aimed, around the caster, around the aimed
point) that walls stop, damage and lasting effects, and events at every stage
(ability.cast / fire / hit / interrupted / end, with a vfx name) as the hooks
for effects and scripts. The Ability Bar component puts them on keys 1..8 in
Play with a hotbar (cooldown sweeps, cast bar) and a burst where each goes
off. In third person, abilities and weapons now leave the player's chest
towards what the crosshair is on. Checked by gameplay_check and Play
screenshots (docs/systems/ABILITIES.md).

**Progress (v0.8.36):** C6 glue, and with it phase C. Create > Player has
templates (first person, third person, shooter, action RPG, bare) that come
with their gameplay components and starter data (`assets/gameplay/
starter.items`, written on first use). In Play F6 saves and F9 loads; logic
scripts have Save Game / Load Game nodes (and the C API save_game /
load_game) for checkpoints. A save (`ecs::PlaySave`) is the player's place
and look plus every entity with a SaveId; the player and the world flags get
theirs automatically. The HUD (HP / SP / now MP) sits in the game view's
corner instead of the window's. Open in C: projected decals, weapon models in
the hand, saving other objects' positions.

## Phase D: the test game

A small game built only with the editor and logic graphs: a terrain with
biomes and a river, a player with the full movement set, loot, an inventory,
guns and melee, abilities, enemies. Its purpose is to be the scene the
optimization upgrade is measured on, and to find what the first three phases
missed.

**Progress (v0.8.37):** D0, the game loop's glue the test game needs, found
missing on the way in: nothing acted on health reaching zero (enemies kept
fighting, the player kept playing), Play had no interact key, and Stop put
back transforms only -- health, ammunition and inventory spent in Play stayed
spent in the editor, and objects Play made stayed. Now: a Mortal component
(`ecs/gameplay_life.h`): death tags, a "died" event, loot dropped as pickups,
bodies removed after a while, the player respawning from the last save or the
start; E interacts with an on-screen prompt; an empty magazine reloads on the
trigger; agents ignore the dead; Play bakes the navmesh when there are agents;
Stop removes what Play made, shows what it hid and restores every gameplay
value. Checked by gameplay_check and a Play run (GWS_TEST_LIFE) that kills an
enemy, picks up its loot, dies, respawns and stops.

**Progress (v0.8.38):** D1, the test game. Create > Test Game builds it into
the open scene (docs/systems/TEST_GAME.md): the Rolling hills world, the
player (third person, rifle, abilities), creature camps that patrol, chase,
claw or spit, die and drop loot, supply caches, two checkpoints and a
beacon. The rules are logic scripts it writes (GameRules, Checkpoint, Goal):
a kill count from the "died" events, Save Game at checkpoints, a win at the
beacon. A new Show Message node puts text in the game view. Playing it found
two engine bugs: NPC agents never turned (their sight is a cone ahead, so
they saw one way only), and their behaviour tree used the plain Selector,
which remembers its running child -- once patrolling (always Running) an
agent never looked at chase again; the check had passed only because the
target was in sight on the first tick. Agents now face where they walk,
notice what is close or hurts them, and use new ReactiveSelector /
ReactiveSequence nodes. Checked in Play: the rules (checkpoint save, the
count, "You win!"), a fight (creatures close in and kill a player who stands
still), a run across the map (3 ms CPU, 3-5 ms GPU). Found for the
optimization upgrade: the navmesh bake at Play start (2.2 M triangles, 4.7 s).

**Progress (v0.8.39):** the test game moved out of the engine. Jonas's
review: the game had been built into the engine (a Create > Test Game
generator); a game belongs in a project. The generator is gone; the game is
the TestGame project (docs/systems/TEST_GAME.md), now first person, with its
title menu, HUD, pause menu and win screen as `.ui` files and its rules as
logic scripts -- all project files. The engine gained only what any game
needs: UI documents (`.ui`, ui/ui_document.h) with Show UI / Hide UI / Set UI
Text / On UI Button / Pause Game / Quit Game (docs/systems/GAME_UI.md); a
project setting giving Esc to the game, with Shift+F1 always freeing the mouse
for the editor; and grown trees with trunks, branches and leaves for the
Conifer and Broadleaf foliage kinds (scene/tree_mesh.h). Bugs from playing it:
the player slid down every slope (the movement pressed down on the ground and
the physics projected it along the slope); in Play the hidden cursor still
clicked editor panels; a project's own `assets/gameplay` was never loaded when
the project was opened (it was read before the project folder was known), so
its abilities and weapons did not exist.

## Then: the optimization upgrade

The 16 ranked candidates of OPTIMIZATION_RESEARCH.md, re-measured on the test
game before any of them is implemented.
