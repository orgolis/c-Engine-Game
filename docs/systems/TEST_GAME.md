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
  dash, mantle, climb), three weapon slots -- a rifle and a pistol to start,
  the third empty (keys 1, 2, 3; the mouse wheel or Q switches too) -- two
  frag grenades (G), Firebolt on Z, health and mana potions; mortal, comes
  back from the last checkpoint.
- **Guns and gear** (`assets/gameplay/arsenal.items`): rifle, pistol,
  shotgun, marksman rifle, SMG, burst carbine, light machine gun and revolver
  (rifle rounds, pistol rounds or shotgun shells); a combat helmet or a mage
  hood (head) and a kevlar vest (chest) that raise health or mana and take
  off damage; frag grenades. The other six guns, the armour and more grenades
  are in the supply caches. A gun found while all three slots are full goes
  into the bag; Use it in the inventory to swap it with the gun in hand.
- **Levels and a skill tree**: each creature gives 100 XP (and ammunition); each
  level gives a skill point. The tree (K) has three branches -- Spells (Frost
  Nova, Lightning, Archmage), Body (Toughness, Mend, Iron Skin) and Mind (Focus,
  Clarity, Veteran). Learned abilities go on the next keys (X, C, F); the
  others raise health or mana. One point to start; a full run gives five.
- **Inventory** (I): the three weapon slots (click a gun to draw it; Put in
  bag), what is worn (click to take it off) and the bag (pick an item, then
  Use / equip: potions, gear, guns).
- **Creature camps**: creatures patrol around their camp, notice the player
  in front of them, close by or when hurt, chase it along the navmesh and
  claw; every third camp spits from range. They die, drop rounds and potions,
  and their bodies go after a few seconds.
- **Ten supply caches** to open with E (guns, armour, grenades), **two
  checkpoints** that save, and **a beacon** on the far side.
- **Screens** (`ui/`): a title menu, the HUD (health and mana, the objective,
  level, XP and skill points, the weapon slots, the keys as bound), the inventory, the skill
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
  runs their Use, Put in bag and Learn buttons.
- `Checkpoint.lgraph` (on each checkpoint zone): Save Game "quick" on enter.
- `Goal.lgraph` (on the beacon zone): the win screen once `game.cleared` is
  set, otherwise "clear every camp first".

Weapons, items and abilities are `assets/gameplay/starter.items`; the skill
tree, Lightning and the mana potion are `assets/gameplay/hero.items`, the other
guns, armour and grenades `assets/gameplay/arsenal.items` (open them in the
Items editor). The player's three weapon slots are its Weapon Loadout
(Inspector). The inventory, skills, grenade, weapon-slot and ability keys are
in `input.cfg`.

## Keys

WASD move, Shift sprint, Space jump / mantle, Ctrl crouch / slide, V dash,
left mouse fire, right mouse aim, R reload, 1 / 2 / 3 weapon slots, mouse
wheel / Q switch gun, G grenade, Z / X / C / F abilities, E open / pick up,
I inventory, K skill tree, Esc menu, F6 save, F9 load, Shift+F1 free the
mouse. All but Esc can be rebound in Settings. Controls a player saved
before (Settings) keep their keys: Reset controls there gives these.

## Known limits

- Creatures are capsules (no rigged models yet) and walk by their transform:
  they pass through trees.
- Guns have no models in the hand yet (the HUD names the one held), and a
  grenade goes off at once where the crosshair lands -- there is no thrown
  grenade to see; trees do not stop it.
- Spit and claw take health at once; there is no projectile to see.
- Trees are grown from flat leaf and needle cards without textures: they have
  trunks, branches and crowns, but up close they are not photographic. An
  imported tree model (a foliage layer of kind Model) looks as good as the
  model.
- Starting Play bakes the navmesh from the full-resolution terrain: about 2.2
  million triangles and a pause of a few seconds (an optimization candidate).
