# The test game

A small, complete first-person game, to play-test the engine and to measure
it on. It is a **project**, not part of the engine: it lives in
`GameWorldshaper Projects/TestGame` (open it from the Hub) and everything in it
is ordinary project content made with the engine's own tools -- change any of
it in the editor.

## What is in it

- **The world** (`scenes/main.scene`): rolling hills with rivers, forests of
  grown broadleaf and conifer trees, grass and rocks.
- **The player**: first person, the full movement set (sprint, crouch, slide,
  dash, mantle, climb), a rifle with 120 rounds, Firebolt / Frost Nova / Mend
  on keys 1–3, health potions; mortal, comes back from the last checkpoint.
- **Creature camps**: creatures patrol around their camp, notice the player
  in front of them, close by or when hurt, chase it along the navmesh and
  claw; every third camp spits from range. They die, drop rounds and potions,
  and their bodies go after a few seconds.
- **Supply caches** to open with E, **two checkpoints** that save, and **a
  beacon** on the far side.
- **Screens** (`ui/`): a title menu, the HUD (health and mana, the objective),
  a pause menu on Esc (resume, save, load, quit) and a win screen. Esc belongs
  to the game in this project (`escape = game` in `project.schizo`); Shift+F1
  frees the mouse for the editor.

## The rules

Logic scripts in `testgame/`:

- `GameRules.lgraph` (on the GameRules object): shows the HUD and the title
  menu at the start; Start and Quit; Esc opens and closes the pause menu (not
  over the title or the win screen) and its Resume / Save / Load / Quit; every
  `died` event of the enemy team counts down "Creatures left" on the HUD; at
  zero it sets the world flag `game.cleared`.
- `Checkpoint.lgraph` (on each checkpoint zone): Save Game "quick" on enter.
- `Goal.lgraph` (on the beacon zone): the win screen once `game.cleared` is
  set, otherwise "clear every camp first".

Weapons, items and abilities are `assets/gameplay/starter.items` (open it in
the Items editor).

## Keys

WASD move, Shift sprint, Space jump / mantle, Ctrl crouch / slide, V dash,
left mouse fire, right mouse aim, R reload, 1–3 abilities, E open / pick up,
Esc menu, F6 save, F9 load, Shift+F1 free the mouse.

## Known limits

- Creatures are capsules (no rigged models yet) and walk by their transform:
  they pass through trees.
- Spit and claw take health at once; there is no projectile to see.
- Trees are grown from flat leaf and needle cards without textures: they have
  trunks, branches and crowns, but up close they are not photographic. An
  imported tree model (a foliage layer of kind Model) looks as good as the
  model.
- Starting Play bakes the navmesh from the full-resolution terrain: about 2.2
  million triangles and a pause of a few seconds (an optimization candidate).
