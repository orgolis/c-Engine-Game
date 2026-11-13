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
  dash, mantle, climb), a rifle with 120 rounds, Firebolt on key 1, health
  and mana potions; mortal, comes back from the last checkpoint.
- **Levels and a skill tree**: each creature gives 100 XP (and 20 rounds); each
  level gives a skill point. The tree (K) has three branches -- Spells (Frost
  Nova, Lightning, Archmage), Body (Toughness, Mend, Iron Skin) and Mind (Focus,
  Clarity, Veteran). Learned abilities go on the next keys (2, 3, 4); the
  others raise health or mana. One point to start; a full run gives five.
- **Inventory** (I): the bag as a grid; pick an item, then Use (potions).
- **Creature camps**: creatures patrol around their camp, notice the player
  in front of them, close by or when hurt, chase it along the navmesh and
  claw; every third camp spits from range. They die, drop rounds and potions,
  and their bodies go after a few seconds.
- **Supply caches** to open with E, **two checkpoints** that save, and **a
  beacon** on the far side.
- **Screens** (`ui/`): a title menu, the HUD (health and mana, the objective,
  level, XP and skill points, the keys as bound), the inventory, the skill
  tree, a pause menu on Esc (resume, save, load, settings, quit),
  a settings screen (every control rebindable, mouse sensitivity and invert,
  quality, ambient occlusion, reflections, clouds, volumetric light, shadow
  quality, render scale, VSync, field of view) and a win screen. Esc belongs
  to the game in this project (`escape = game` in `project.schizo`); Shift+F1
  frees the mouse for the editor.

## The rules

Logic scripts in `testgame/`:

- `GameRules.lgraph` (on the GameRules object): shows the HUD and the title
  menu at the start; Start and Quit; Esc opens and closes the pause menu (not
  over the title or the win screen) and its Resume / Save / Load / Quit; every
  `died` event of the enemy team gives XP and rounds and counts down
  "Creatures left" on the HUD; at zero it sets the world flag `game.cleared`.
  It also gives the player the `hero` skill tree at the start, opens and
  closes the inventory (I) and skill tree (K) -- Esc closes them too -- and
  runs their Use and Learn buttons.
- `Checkpoint.lgraph` (on each checkpoint zone): Save Game "quick" on enter.
- `Goal.lgraph` (on the beacon zone): the win screen once `game.cleared` is
  set, otherwise "clear every camp first".

Weapons, items and abilities are `assets/gameplay/starter.items`; the skill
tree, Lightning and the mana potion are `assets/gameplay/hero.items` (open them
in the Items editor). The inventory and skills keys are in `input.cfg`.

## Keys

WASD move, Shift sprint, Space jump / mantle, Ctrl crouch / slide, V dash,
left mouse fire, right mouse aim, R reload, 1–4 abilities, E open / pick up,
I inventory, K skill tree, Esc menu, F6 save, F9 load, Shift+F1 free the
mouse. All but Esc can be rebound in Settings.

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
