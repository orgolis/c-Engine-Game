# The test game

A small, complete first-person game, to play-test the engine and to measure
it on. It is a **project**, not part of the engine: it lives in
`GameWorldshaper Projects/TestGame` (open it from the Hub) and everything in it
is ordinary project content made with the engine's own tools -- change any of
it in the editor.

## What is in it

- **The world** (`scenes/main.scene`): rolling hills with rivers under an HDR
  sky (`assets/sky/`) and a sun that shines from where the sky's sun is and
  casts the trees' shadows (and, with Volumetric light on, light shafts), forests of grown broadleaf and conifer trees from 5 to
  18 m tall, grass and rocks. Trees and rocks are solid: they stop the player,
  rounds, grenades and creatures (docs/systems/FOLIAGE.md); the ground round
  the start, the caches, checkpoints, the beacon and the camps is kept clear.
- **The player**: first person, the full movement set (sprint, crouch, slide,
  dash, mantle, climb), three weapon slots -- a rifle and a pistol to start,
  the third empty (keys 1, 2, 3; the mouse wheel or Q switches too) -- two
  frag grenades (G, thrown: aim up to throw far), Firebolt on Z, health and
  mana potions; mortal, comes back from the last checkpoint. The HUD shows
  where a hit came from and glows red at low health.
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
- **Creature camps** (docs/systems/CREATURES_AI.md): creatures wander about
  their camp, see the player only with a clear line (not through hills or
  trees), hear it close by and hear gunfire, and call their camp in. They
  chase, search where it was last seen, and give up far from home. A creature
  rears up (a red glow) and claws; every third camp spits from range -- a
  green glob that arcs and can be dodged -- keeping its distance and
  side-stepping. Their attacks are abilities in
  `assets/gameplay/creatures.items`. They die, drop rounds and potions, and
  their bodies go after a few seconds.
- **Effects** (docs/systems/EFFECTS.md): muzzle flashes and smoke, tracers,
  sparks and dust, a firebolt that flies with a fiery trail, grenades that
  arc and burst, frost and lightning; the creatures' claw and spit and the
  shotgun's muzzle are the project's own (`assets/gameplay/effects.items`).
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

- Creatures are capsules (no rigged models yet); they steer round trees but
  can still get stuck in a dense thicket.
- Guns have no models in the hand yet (the HUD names the one held).
- A claw lands at the end of its wind-up if the player is still in reach;
  there is no blocking or dodging animation.
- Trees are grown from flat leaf and needle cards without textures: they have
  trunks, branches and crowns, but up close they are not photographic. An
  imported tree model (a foliage layer of kind Model) looks as good as the
  model.
- Starting Play bakes the navmesh from the full-resolution terrain: about 2.2
  million triangles and a pause of a few seconds (an optimization candidate).
