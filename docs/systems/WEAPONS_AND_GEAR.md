# Weapons and gear

Guns, armour and grenades are data in a project's `.items` files (open one in
the Items editor, or write it by hand). Nothing about a particular game's
weapons is in the engine.

## Guns

```
weapon shotgun
  name Shotgun
  fire semi             # semi | auto | burst (with "burst 3")
  damage 11 kinetic     # per pellet, with its damage type
  pellets 9             # more than 1: a shotgun
  rpm 70                # rounds a minute
  magazine 6
  reload 2.4            # seconds
  draw 0.6              # seconds from switching to it until it fires (default 0.4)
  spread 5              # degrees from the hip
  bloom 0.6 3 8         # spread added a shot, its most, how fast it recovers
  recoil 3 0.6          # up and sideways kick, degrees
  aim 0.9 0.8 0.2       # sights up: field of view, spread and time
  range 35
  tracer 1              # every Nth round shows
  impact 0.1            # marks where rounds hit
  flash                 # a muzzle flash
  ammo shotgun_shells   # the item it loads from the bag
end
```

## Carrying several and switching

A player carries guns in **weapon slots** and holds one. Each keeps the rounds
left in its magazine while it is put away; the one drawn fires after its
`draw` time.

| Action | Default |
|---|---|
| `next_weapon` / `prev_weapon` | Wheel Down / Wheel Up (empty slots are skipped) |
| `last_weapon` | Q (the gun in hand before) |
| `weapon_1` .. `weapon_4` | unbound (the gun in that slot) |

Like every action they can be changed in `input.cfg` (`weapon_1 = 1`) and
rebound in the game; `Wheel Up` and `Wheel Down` are keys like any other there.

- **Weapon slots**: the player's **Weapon Loadout** component (Inspector >
  Gameplay Components) has `weapon slots` -- how many guns can be carried
  (0: any number) -- and the gun in each slot. A slot keeps its place when it
  is emptied, so the key for slot 2 stays slot 2's.
- **A gun on the ground** -- a Pickup whose item is a weapon block's id -- goes
  into the first empty slot when the player picks it up (E); an empty hand
  takes it. With **every slot full it goes into the bag**, with a magazine of
  its rounds. One already carried or in the bag gives a magazine of its
  ammunition instead.
- **A gun in the bag** is an item with the gun's id. Using it (Use Item, the
  inventory's Use button) puts it into the first empty slot -- or, with every
  slot full, in place of the gun in hand, which goes into the bag and the new
  one is drawn. **Stow Weapon** puts the gun in a slot into the bag.
- A gun's magazine goes into the bag as its ammunition and comes out of it
  when the gun is taken out (a gun without an `ammo` item comes out full), so
  moving guns about neither makes nor loses rounds.
- The player's **Weapon** component is the gun in hand (empty when every gun
  is in the bag). The loadout is made from the gun in hand when missing. Both
  are saved with the game.

| Node | |
|---|---|
| **Give Weapon** (Entity, Weapon) | carried from now on, full magazine, in the first empty slot; with all full, into the bag |
| **Select Weapon** (Entity, Index) | draws the gun in that slot (from 0) |
| **Current Weapon** (Entity) | the gun in hand: Weapon (id), Index |
| **Stow Weapon** (Entity, Index) | the gun in that slot into the bag; the gun in hand before is drawn |
| **Equip Weapon** (Entity, Weapon, Slot) | a gun from the bag into a slot (from 0; -1: as Use Item) |
| **Set Weapon Slots** (Entity, Count) | how many guns can be carried (0: any number); more go into the bag |
| **Weapon In Slot** (Entity, Index) | the gun in that slot: Weapon (id), Name |

Gameplay events (param: the gun's id): `weapon.given`, `weapon.switched`,
`weapon.bagged` (picked up with every slot full), `weapon.stowed`,
`weapon.equipped` (value: the slot).

A gun in the bag shows its name and "gun"; `{<grid>.info}` is its damage,
magazine and rate of fire.

## Gear

An item with a `slot` is gear: worn, its `mod` lines apply.

```
item kevlar_vest
  name Kevlar Vest
  kind armor
  slot chest                # head, chest, ... -- one item a slot
  mod Health 30             # raises the maximum (and the value) while worn
  mod resist.physical 0.2   # takes 20% off physical damage
  about Chest: +30 max health, 20% less damage
end
```

- **Use Item** on gear wears it: it leaves the bag, and what was in its slot
  goes back into the bag. **Unequip Item** (Entity, Slot) takes it off, into
  the bag (not when the bag is full). **Equip Item** does the same as Use.
- `resist.<damage type>` (0..1) is the share of that damage taken off. A
  wearer without the attribute gets it at 0 -- armour works on anyone.
- **Equipped Item** (Entity, Slot): what is worn there (Item, Name).

## Grenades and other items that do something

`casts <ability>` on an item: using it fires that ability from the player's
view, at once (no cost or cooldown -- the item is used up instead). A grenade
is an `area_at_aim` ability; aimed at the sky, it comes down on the ground
below the end of its range.

```
item frag_grenade
  name Frag Grenade
  kind consumable
  max_stack 5
  casts frag
end

ability frag
  target area_at_aim
  range 25
  radius 5
  damage 90 explosive
end
```

A key for it is the project's: an action in `input.cfg` (`grenade = G`) and a
script, Action Pressed (grenade) → Use Item (Player, frag_grenade).

## In UI documents

`slots ... bind=player.weapons` shows the weapon slots (empty ones empty; the
rounds in each magazine; the one in hand framed and marked "in hand"; a click
reports its Index for Select Weapon or Stow Weapon). `enabled=<grid>.filled`
greys a button while the picked slot is empty. `{player.weapon_slot.<n>}` is
the gun in slot n, from 1 ("-" empty), `{player.weapon_slots}` how many there
are. `{player.equipped.<slot>}` is what is worn ("-" when nothing;
`enabled=player.equipped.<slot>` greys a button while the slot is empty),
`{player.item.<id>}` how many of an item the bag holds, `{player.weapon}` the
gun in hand, and `{<grid>.info}` the picked item's `about` line. See
docs/systems/GAME_UI.md.

The rules are `engine/core/ecs/gameplay_loadout.h`, `gameplay_inventory.h`
and `gameplay_weapons.h`, checked by `tools/gameplay_check`.
