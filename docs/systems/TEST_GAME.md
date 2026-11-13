# The test game

A small, complete game built from the engine's own parts, to play-test the
engine and to measure it on. **Create > Test Game** builds it into the open
scene in one undo step; save the scene to keep it.

## What it builds

- **A world**: the Rolling hills preset -- terrain, erosion, rivers, biomes,
  forests, grass and rocks.
- **The player** (the scene's Player, moved to the start, or a new one):
  third-person camera, the full movement set (sprint, crouch, slide, dash,
  mantle, climb), a rifle with 120 rounds, Firebolt / Frost Nova / Mend on
  keys 1–3, health potions, mortal (respawns from the last checkpoint).
- **Creature camps** (three or four, along the way): creatures patrol around
  their camp, notice the player in front of them or close by, chase it along
  the navmesh and claw; every third camp spits from range. They die, drop
  rounds and potions, and their bodies go after a few seconds.
- **Supply caches** to open with E (rounds or a potion).
- **Two checkpoints** that save the game when walked into.
- **A beacon** at the far side.

## The rules

The rules are logic scripts the build writes next to the scene, in
`testgame/` (open them from the Asset Browser):

- `GameRules.lgraph` (on the GameRules object): the opening message; on every
  `died` event of the enemy team it counts down "Creatures left: N"; at zero
  it sets the world flag `game.cleared` and says so.
- `Checkpoint.lgraph` (on each checkpoint zone): on trigger enter, Save Game
  "quick" and a message. Dying brings the player back here.
- `Goal.lgraph` (on the beacon zone): on trigger enter, "You win!" when
  `game.cleared` is set, otherwise "clear every camp first".

Clear every camp, then reach the beacon.

## Keys

WASD move, Shift sprint, Space jump / mantle, Ctrl crouch / slide, V dash,
left mouse fire, right mouse aim, R reload, 1–3 abilities, E open / pick up,
F6 save, F9 load, Esc releases the mouse.

## Known limits

- Creatures are capsules (no rigged models yet) and walk by their transform:
  they pass through trees.
- Spit and claw take health at once; there is no projectile to see.
- Starting Play bakes the navmesh from the full-resolution terrain: about 2.2
  million triangles and a pause of a few seconds (an optimization candidate).

## Checks

Test hooks, run with `GWS_TEST_PLAY`: `GWS_TEST_GAME=<dir>` builds it before
Play; with `GWS_TEST_GAME_RULES=1` the rules are walked through (checkpoint,
every creature dead, the beacon: "You win!"); with `GWS_TEST_GAME_FIGHT=1` the
player stands by a camp and the creatures must chase and kill it; with
`GWS_TEST_GAME_WALK=1` the player runs towards the beacon (for measuring).
Keep `GWS_SAVE_DIR` and `GWS_GAMEPLAY_DIR` pointed away from the project in
automated runs.
