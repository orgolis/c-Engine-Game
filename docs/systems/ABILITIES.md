# Abilities

Spells, skills and powers the player casts with keys 1 to 8.

## Making an ability

Abilities are data, written next to items and weapons in a `.items` file.
Open the file (double-click it in the Asset Browser) and press **+ Ability**,
or write it by hand:

```
ability firebolt
  name Firebolt
  cost Mana 15          # attribute and amount; leave out for free
  cooldown 2            # seconds
  cast 0.4              # wind-up before it goes off
  still                 # moving breaks the cast
  target aimed          # self | aimed | area | area_at_aim
  range 30              # aimed and area_at_aim
  radius 3              # area and area_at_aim
  damage 35 fire        # to each one it reaches, with its damage type
  effect Speed -2 3     # to each one reached: attribute, amount, seconds (none: at once)
  self Mana 5           # to the caster, the same way
  require state.armed   # the caster needs this tag
  block state.stunned   # ... and must not have this one
  vfx fire_burst        # a name handed out with its events
end
```

A **channel** keeps going off while the key is held: `channel 3 0.5` lasts at
most 3 seconds and goes off every half second (once as it starts). Letting go
ends it; the cooldown starts when it ends.

Who it reaches:

| target | reaches |
|---|---|
| `self` | the caster |
| `aimed` | the first one along the crosshair, up to the range; walls stop it |
| `area` | everyone within the radius of the caster |
| `area_at_aim` | everyone within the radius of where the crosshair lands (a wall, or the range) |

The cost is paid when the ability goes off, not when the cast starts, and a
cast that is broken costs nothing and starts no cooldown.

## Giving the player abilities

Add **Inspector > Gameplay Components > Ability Bar** to the Player and pick an
ability for each key. In Play, the bar shows along the bottom of the view:
the key, the name, a dark sweep and the seconds left while it cools down, and
a bar above it for a cast or a channel in progress.

In third person (with a Camera Rig) abilities and weapons leave the player's
chest towards what the crosshair is on.

## Skill trees: abilities to learn

A skill tree is data too, in a `.items` file: one `skill` block per skill.
Skills cost skill points (one per level gained: XP from **Grant XP**).

```
skill frost
  name Frost Nova
  tree hero              # the tree it belongs to
  cost 1                 # skill points
  unlocks frost_nova     # an ability put on the Ability Bar when learned (next free key)
end

skill tough
  name Toughness
  tree hero
  cost 1
  requires frost         # learned first (one line each)
  grant Health 25        # raises the attribute's maximum (and the value)
  tag perk.tough         # a gameplay tag the player gets
end
```

The Items editor shows skill blocks and keeps them as written.

| Node | |
|---|---|
| **Set Skill Tree** (Entity, Tree) | gives the entity the tree (and a progression: levels, points) -- e.g. on Begin Play |
| **Unlock Skill** (Entity, Skill) | learns it: needs its `requires` and the points; applies grants and tags; its abilities go on the bar |
| **Add Skill Points** (Entity, Points) | |
| **Skill State** (Entity, Skill) | Learned, Can Learn |
| **Skill Points** (Entity) | |

A skill-tree screen is a UI document with a button per skill:
`text="{skill.frost}" enabled=skill.frost.can` (docs/systems/GAME_UI.md). The
tree, what is learned and the points are saved with the game.

## For effects, sounds and scripts

Each stage is a gameplay event with the ability's id as its parameter:
`ability.cast` (the wind-up starts), `ability.fire` (it goes off; the value is
how many it reached), `ability.hit` (one per target, with the damage dealt),
`ability.interrupted` and `ability.end`. Logic scripts receive them like any
other gameplay event. Play draws a simple burst where each one goes off; the
`vfx` name is there for a game's own effects.

The rules are `engine/core/ecs/gameplay_ability_cast.h` (`step_ability_bar`),
checked by `tools/gameplay_check`.
