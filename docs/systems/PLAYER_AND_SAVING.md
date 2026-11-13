# A player that plays, and saving in Play

## Player templates

**Create > Player** offers a player ready to play, not only a capsule and a
camera:

| Template | Comes with |
|---|---|
| First person | Movement, Health / Stamina, the HUD, an Inventory, a Save Id |
| Third person | the same, and a Camera Rig |
| Shooter | first person, with a rifle and 150 rounds in the inventory |
| Action RPG | third person, Mana, and Firebolt, Frost Nova and Mend on keys 1–3, three health potions |
| Bare | the capsule, collider and camera only |

The rifle, rounds, potions and abilities are ordinary data: the first time a
template needs them it writes `assets/gameplay/starter.items` into the project.
Double-click it to change them in the Items editor. Everything a template adds
is a normal gameplay component in the Inspector, so a template is a start, not
a limit.

## Life, death and loot

Give anything that can die **Inspector > Gameplay Components > Mortal**
(the templates give it to the player). When its health reaches zero:

- it is tagged `state.dead` and a `died` gameplay event goes out (scripts
  count kills on it); enemies (NPC agents) stop, and stop seeing a dead player;
- its **loot** list is rolled (item, weight, chance, how many) and what drops
  lies on the ground as small glowing pickups;
- after its **corpse time** the body disappears;
- the player instead sees "You died" and comes back after a moment: from the
  last quick save (F6) when there is one, otherwise where Play started, with
  full health.

**E** picks up loot and uses anything with an Interactable; the view shows
what E would do ("E  Pick up Health Potion") when something is in reach.
Pulling the trigger on an empty magazine reloads.

Stop puts the scene back as it was before Play: objects Play created (loot,
things scripts spawned) are removed, hidden ones come back, and every gameplay
value (health, ammunition, inventory, flags) returns to what it was. Play also
bakes the navmesh by itself when the scene has agents and no navmesh yet.

## Saving and loading in Play

- **F6** saves, **F9** loads (the slot "quick").
- Logic scripts have **Save Game** and **Load Game** nodes with a slot name,
  for checkpoints; C++ scripts have `save_game` / `load_game` in the API table.
- Saves go to `saves/<slot>.sav` in the project.

A save holds where the player stands and looks, and every object with a
**Save Id** (Inspector > Gameplay Components) with all its gameplay
components: health and other attributes, inventory, ammunition, quests,
abilities' settings. The player and the world flags are saved without being
given one by hand (they get Save Ids 2 and 1). Give an object a Save Id
(3 or more, each its own) for its gameplay state to be saved too.

Not saved yet: where other objects are, and anything that is not a gameplay
component.

The format is `ecs::PlaySave` in `engine/core/ecs/gameplay_savegame.h`,
checked by `tools/gameplay_check`.
