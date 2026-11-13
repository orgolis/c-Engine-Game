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

A player carries every gun it has been given, in the order they came, and
holds one. Each keeps the rounds left in its magazine while it is put away;
the one drawn fires after its `draw` time.

| Action | Default |
|---|---|
| `next_weapon` / `prev_weapon` | Wheel Down / Wheel Up |
| `last_weapon` | Q (the gun in hand before) |
| `weapon_1` .. `weapon_4` | unbound (the gun at that place) |

Like every action they can be changed in `input.cfg` and rebound in the game;
`Wheel Up` and `Wheel Down` are keys like any other there.

- **A gun on the ground** -- a Pickup (Inspector > Gameplay Components) whose
  item is a weapon block's id -- is carried from then on when the player picks
  it up (E). One already carried gives a magazine of its ammunition instead.
- The player's **Weapon** component is the gun in hand; **Weapon Loadout**
  lists those carried (it is made from the gun in hand when missing). Both are
  saved with the game.

| Node | |
|---|---|
| **Give Weapon** (Entity, Weapon) | carried from now on, full magazine; the first goes into the hand |
| **Select Weapon** (Entity, Index) | draws the gun at that place (from 0) |
| **Current Weapon** (Entity) | the gun in hand: Weapon (id), Index |

Gameplay events: `weapon.given` and `weapon.switched` (param: the gun's id).

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

`slots ... bind=player.weapons` shows the guns carried (the rounds in each
magazine, the one in hand marked "in hand"; a click reports its Index for
Select Weapon). `{player.equipped.<slot>}` is what is worn ("-" when nothing;
`enabled=player.equipped.<slot>` greys a button while the slot is empty),
`{player.item.<id>}` how many of an item the bag holds, `{player.weapon}` the
gun in hand, and `{<grid>.info}` the picked item's `about` line. See
docs/systems/GAME_UI.md.

The rules are `engine/core/ecs/gameplay_loadout.h`, `gameplay_inventory.h`
and `gameplay_weapons.h`, checked by `tools/gameplay_check`.
