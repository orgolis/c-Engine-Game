# The test game

A small, complete first-person game, to play-test the engine and to measure
it on. It is a **project**, not part of the engine: it lives in
`GameWorldshaper Projects/TestGame` (open it from the Hub) and everything in it
is ordinary project content made with the engine's own tools -- change any of
it in the editor.

## What is in it

- **Six lands** (biomes, painted -- the editor's Apply biomes gives the same):
  the Meadow in the west where the game starts, a Pine Forest to the north,
  Autumn Woods in the middle, Dry Plains to the south, the Snowy Highlands in
  the east round the beacon, and Wetland along the rivers. Each has its ground
  (textured: meadow grass, forest floor, dry earth, snow), its plants and its
  air: flowers and bushes in the meadow; ferns, fallen logs, stumps and
  mushrooms under the pines; orange trees in the autumn woods; dry grass,
  cacti and dead bushes on the plains; snow-laden pines and boulders in the
  highlands; reeds by the water.
- **Buildings**: a village and a farm in the meadow (cottages, barns), ruins
  in the forests and on the plains, log cabins in the snow -- walk in through
  their doors -- and at every soldiers' outpost a bunker, sandbag walls and a
  watchtower.
- **The world** (`scenes/main.scene`): 1024 m of rolling hills (built with
  `gws world`, docs/systems/WORLDS_AND_WATER.md) with four rivers and their
  lakes under an HDR
  sky (`assets/sky/`) and a sun that shines from where the sky's sun is and
  casts the trees' shadows (and, with Volumetric light on, light shafts), forests of grown broadleaf and conifer trees from 5 to
  18 m tall, grass and rocks. Trees and rocks are solid: they stop the player,
  rounds, grenades and creatures (docs/systems/FOLIAGE.md); the ground round
  the start, the caches, checkpoints, the beacon and the camps is kept clear.
- **The player**: first person (a rigged soldier, seen in third person and in
  its shadow), the gun in hand seen and animated, the full movement set (sprint, crouch, slide,
  dash, mantle, climb), three weapon slots -- a rifle, a pistol and a sword
  to start (keys 1, 2, 3; the mouse wheel or Q switches too), held by the
  player's own arms in first person. The sword is fought with moves
  (0.9.1, docs/systems/MELEE.md): drawn, the camera orbits in third person;
  light attacks chain (three), the heavy charges when held, V rolls the way
  you are pushed (standing: a backstep), Left Alt guards, T parries, the
  middle mouse button locks on; a stamina bar under the health. Guns go back
  to first person; a super's form is seen in third person while it lasts -- two
  frag grenades (G, thrown: aim up to throw far), health potions; mortal, comes back from the last
  checkpoint. No mana: abilities cost only their cooldowns. The HUD has
  health, the objective and the level at the top; in the lower left corner the
  four powers (key, cooldown darkening the slot with the seconds left) and
  the gun (name, rounds, reload, weapon slots, grenades). It shows where a hit
  came from and glows red at low health.
- **Guns and gear** (`assets/gameplay/arsenal.items`): rifle, pistol,
  shotgun, marksman rifle, SMG, burst carbine, light machine gun and revolver
  (rifle rounds, pistol rounds or shotgun shells); a combat helmet or a mage
  hood (head) and a kevlar vest (chest) that raise health, shorten ability
  cooldowns (the hood: 15%) and take off damage; frag grenades. The other six guns, the armour and more grenades
  are in the supply caches. A gun found while all three slots are full goes
  into the bag; Use it in the inventory to swap it with the gun in hand.
- **Levels and a skill tree**: each kill gives 100 XP (and ammunition); each
  level gives two skill points (six to start). The tree (K) is the elements'
  (since 0.8.69): four branches of 16 skills, each from its Attunement -- on the
  left the element's other abilities and its Discipline (grenades back sooner)
  and Strength (melees), on the right its aspects and fragments, at the top its
  Mastery. A click learns one (`assets/scripts/powers.cpp`).
- **Elements** (since engine 0.8.68; `assets/gameplay/elements.items`,
  `assets/scripts/powers.cpp`): Ember, Storm, Rime and Umbra, each with its own
  verbs -- statuses its powers put on enemies and on the player (Kindle and its
  Flashpoint, Blaze, Mend; Charged lightning that leaps, Dazed, Surge; Chill,
  Frozen and Shatter, Frost Plate; Hex, Unstable and Rupture, Veil, Hunger) --
  two supers, three grenades, two melees, two class abilities, three aspects
  and six fragments, opened one by one in its branch of the skill tree. The
  player takes up one element: a grenade on Z, a melee on X, a class ability on C,
  the super on F (it fills with time, hits and kills); up to two aspects, and as
  many fragments as the aspects open. A form super (Phoenix Mantle,
  Thunderhead, Winter's Edge, Eclipse) changes the melee while it lasts.
  Every power has its own look (`assets/gameplay/ability_effects.items`, since
  0.8.70): explosions that throw and shake (Incendiary, Cinderfall, Rift
  Charge, Glacial Tomb), a solar burst around the player (Sunspark), chain
  lightning (Arc Bolt), a slow orb (Ball Lightning), an eruption at the aim
  (Ice Spike), pulls (Vortex, Void Grasp, Event Horizon).
- **Powers** (P): the element, its powers, aspects and fragments to pick; the
  HUD shows the four powers, the super's charge and the statuses on the player.
  All of it is project data and a project script built from the engine's
  statuses, perks, zones and cones (docs/systems/STATUSES_AND_PERKS.md).
- **Inventory** (I): the three weapon slots (click a gun to draw it; Put in
  bag), what is worn (click to take it off) and the bag (pick an item, then
  Use / equip: potions, gear, guns).
- **84 enemies**: 37 soldiers at ten outposts and 47 creatures in thirteen
  camps, spread over every land.
- **Soldiers** (docs/systems/CHARACTERS.md): ten outposts of riflemen and
  heavy gunners -- rigged, animated models with guns in their hands -- who
  patrol, aim, shoot (rifle shots and machine-gun bursts that spread at range)
  from cover distance, stagger when hit and fall when killed. Their guns and
  attacks are `assets/gameplay/soldiers.items`. At two outposts (the first by
  the river, and one to the south-west: five in all) **swordsmen** fight with
  the sword's moves (a Melee Fighter with `weapon soldier_sword`, its clips
  from `rifleman@melee.glb`): they close in, circle, swing in combos, guard
  and sidestep, and take turns.
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
- **Screens** (`ui/`): a title menu, the HUD (health, the objective,
  level, XP and skill points, the ability slots and the gun, the keys as
  bound), the inventory, the skill tree, the abilities, a pause menu on Esc (resume, save, load, settings, quit),
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
  "Enemies left" (creatures and soldiers) on the HUD; at zero it sets the world flag `game.cleared`.
  It also gives the player the `hero` skill tree at the start, opens and
  closes the inventory (I), the skill tree (K) and the powers (P) -- Esc
  closes them too -- and runs their Use and Put in bag buttons (the skill
  tree's clicks are powers.cpp's).
- `Checkpoint.lgraph` (on each checkpoint zone): Save Game "quick" on enter.
- `Goal.lgraph` (on the beacon zone): the win screen once `game.cleared` is
  set, otherwise "clear every camp first".

Weapons, items and the first abilities are `assets/gameplay/starter.items`;
the skill tree is `assets/gameplay/hero.items`, the elements' powers
`assets/gameplay/elements.items`, the other
guns, armour and grenades `assets/gameplay/arsenal.items` (open them in the
Items editor). The player's three weapon slots are its Weapon Loadout
(Inspector). The inventory, skills, grenade, weapon-slot and ability keys are
in `input.cfg`; the ability slots are the player's Ability Bar (Slots 4).

## Keys

WASD move, Shift sprint, Space jump / mantle, Ctrl crouch / slide, V dash
(with the sword: roll), left mouse fire (sword: light attack), right mouse aim
(sword: heavy, held to charge), Left Alt guard, T parry, middle mouse lock on,
R reload, 1 / 2 / 3 weapon slots, mouse
wheel / Q switch gun, G grenade, Z / X / C abilities, E open / pick up,
I inventory, K skill tree, P abilities, Esc menu, F6 save, F9 load, Shift+F1 free the
mouse. All but Esc can be rebound in Settings. Controls a player saved
before (Settings) keep their keys: Reset controls there gives these.

## Known limits

- Creatures are capsules (the soldiers are rigged models); they steer round
  trees but can still get stuck in a dense thicket.
- No inverse kinematics: a soldier's support hand follows its clip, not the gun.
- A creature's claw lands at the end of its wind-up if the player is still in
  reach (the creatures have no moves); the swordsmen at two camps fight with the
  sword's moves -- they guard, sidestep and take turns.
- Trees are grown from flat leaf and needle cards without textures: they have
  trunks, branches and crowns, but up close they are not photographic. An
  imported tree model (a foliage layer of kind Model) looks as good as the
  model.
- Starting Play bakes the navmesh from the full-resolution terrain: about 2.2
  million triangles and a pause of a few seconds (an optimization candidate).
