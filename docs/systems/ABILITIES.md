# Abilities

Spells, skills and powers the player casts with the ability keys -- up to 8
slots, or as many as the game gives (3 in the test game) -- and passives:
bonuses a skill gives for good.

## Making an ability

Abilities are data, written next to items and weapons in a `.items` file.
Open the file (double-click it in the Asset Browser) and press **+ Ability**
(or **+ From template...**: a bolt, a thrown blast, a lingering field, a close
strike, a dash, a self buff, a healing area, a beam, a pulse), or write it by
hand:

```
ability firebolt
  name Firebolt
  cost Mana 15          # attribute and amount; leave out for free
  cooldown 2            # seconds
  cast 0.4              # wind-up before it goes off
  still                 # moving breaks the cast
  target aimed          # self | aimed | area | area_at_aim
  range 30              # aimed and area_at_aim
  spread 3              # aimed and area_at_aim: goes this many degrees off the aim, at random (a soldier's rifle)
  radius 3              # area and area_at_aim
  damage 35 fire        # to each one it reaches, with its damage type
  knockdown 7           # a blast: characters it leaves standing within 7 m are knocked down (they get up again)
  effect Speed -2 3     # to each one reached: attribute, amount, seconds (none: at once)
  self Mana 5           # to the caster, the same way
  require state.armed   # the caster needs this tag
  block state.stunned   # ... and must not have this one
  vfx fire_burst        # its effects (docs/systems/EFFECTS.md), and handed out with its events
  speed 40              # aimed / area_at_aim: it flies at this speed and takes effect where it lands
  arc 9.8               # a flying one falls on the way (something thrown)
  move blink 14         # moves the caster (below)
  inflict burning 2     # a status on each one reached (docs/systems/STATUSES_AND_PERKS.md)
  self_status focused   # a status on the caster
  zone 5 0.5            # its area stays 5 s, going off again every 0.5 s (zone_hits allies: on its own side)
  labels fire thrown    # free words a game's perks and rules look for
  about A ball of fire at what you aim at.   # what it does, shown with it
end
```

**Movement abilities** move the player as they go off, along the way the
camera looks (level):

| `move` | |
|---|---|
| `blink 14` | there at once, up to 14 m ahead, stopping short of walls, set down on the ground there (at the same height above it) |
| `dash 12` | a 12 m dash, the same move as the dash key with its own distance |
| `leap 11 10` | thrown up at 11 m/s and forward at 10 (gravity 20: about 3 m high) |

A buff is an ability with a `self` effect for a while on a bonus stat (below):
`self MoveSpeed 50 6` -- Haste, 50% faster for 6 seconds.

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
| `cone` | everyone in front of the caster within the radius and `angle` degrees wide (a sweep; with `move dash`, a lunge) |

With a **speed**, an `aimed` or `area_at_aim` ability flies instead of taking
effect at once: from the caster's hand toward what the crosshair is on,
falling by its `arc`. It takes effect on the first one it meets (not of the
caster's own side) or where it meets the world; an area one goes off there,
or where its flight ends. Flying things can be side-stepped.

Nothing an ability does reaches the caster's own side (the same `team.*` tag).

The cost is paid when the ability goes off, not when the cast starts, and a
cast that is broken costs nothing and starts no cooldown.

## Giving the player abilities

Add **Inspector > Gameplay Components > Ability Bar** to the Player and pick an
ability for each key. In Play, the bar shows along the bottom of the view:
the key, the name, a dark sweep and the seconds left while it cools down, and
a bar above it for a cast or a channel in progress. A HUD document that shows
the slots itself (a `slots` grid bound to `player.abilities`,
docs/systems/GAME_UI.md) replaces it.

**Slots and known abilities.** The bar's **Slots** (0: 8) limits how many
abilities are on keys. An ability learned beyond them is still *known*: the
player puts it in a slot on an abilities screen -- a `slots` grid bound to
`player.known_abilities`, another to `player.abilities`, and the **Equip
Ability** node (Entity, Known: the known grid's selected cell, Slot: the slot
clicked). Equipping an ability already on the bar swaps the two; one taken off
keeps cooling down while it rests.

| Node | |
|---|---|
| **Equip Ability** (Entity, Known, Slot) | Equipped: false for an empty cell, a slot past the bar, or while casting |

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
  unlocks frost_nova     # an ability given when learned: into the next free slot, else known
  about Ability: a freezing burst around you.   # what it does (a drawn tree shows it)
  pos 2 1                # its place in a drawn tree: column, row (leave out: placed by what it needs)
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

**Passives and bonus stats.** `grant` raises an attribute for good; a few
attributes are read by the engine's systems when the player has them, and a
`grant`, a gear `mod` or a `self` effect on one adds it (from 0) -- no setup:

| Bonus stat | |
|---|---|
| `MoveSpeed` | % faster walking, running, crouching, swimming (and NPCs) |
| `Damage` | % more damage from guns and abilities |
| `CooldownReduction` | % shorter ability cooldowns (at most 75) |
| `ReloadSpeed` | % faster reloads |
| `HealthRegen` | health a second, while alive |
| `ManaRegen` | Mana a second, while alive (a game that has Mana) |
| `ExtraJumps` | jumps in mid-air |
| `WeaponDamage` | % more damage from guns and melee only (on top of `Damage`) |
| `AbilityDamage` | % more damage from abilities only (on top of `Damage`) |
| `WeaponHandling` | % less spread and recoil (at most 60) |
| `resist.<type>` | a share of that damage type taken off (0.25: a quarter; above 1 immune; below 0 a weakness: more) |

`grant MoveSpeed 10` is +10% move speed for good; `self Damage 30 8` is +30%
damage for 8 seconds. Cast again while it lasts, a buff starts its time again
rather than stacking; when it ends it takes back exactly what it gave (a stat
at its limit is not pushed below where it was).

The dead (Health 0) and the stunned (`status.stunned`) cannot cast, and a
stun interrupts a wind-up or a channel. Letting go of a channel ends it at
once, without one more pulse. A skill that costs points needs a progression
(levels, points) on the entity. A UI shows them as `{player.MoveSpeed}` (0 until raised).

| Node | |
|---|---|
| **Set Skill Tree** (Entity, Tree) | gives the entity the tree (and a progression: levels, points) -- e.g. on Begin Play |
| **Unlock Skill** (Entity, Skill) | learns it: needs its `requires` and the points; applies grants and tags; its abilities go on the bar |
| **Add Skill Points** (Entity, Points) | |
| **Skill State** (Entity, Skill) | Learned, Can Learn |
| **Skill Points** (Entity) | |

A skill-tree screen draws the tree with a `skilltree` widget: a box per skill,
a line to each skill it needs, learned ones gold and ones that can be learned
now bright; a click on one is the button `learn_<skill id>`
(docs/systems/GAME_UI.md). A screen of buttons works too:
`text="{skill.frost}" enabled=skill.frost.can`. The tree, what is learned and
the points are saved with the game.

## Statuses, zones and perks

Abilities can put **statuses** on who they reach (`inflict`) or on the caster
(`self_status`) -- a burn that stacks and flares up, a slow that becomes a
freeze, a shield, a stun -- leave their area behind as a **zone**, and carry
**labels** that a character's **perks** look for ("when a `close` ability hits,
set them burning"). All of it is data a game arranges as its own system:
docs/systems/STATUSES_AND_PERKS.md.

## For effects, sounds and scripts

Each stage is a gameplay event with the ability's id as its parameter:
`ability.cast` (the wind-up starts), `ability.fire` (it goes off; the value is
how many it reached), `ability.hit` (one per target, with the damage dealt),
`ability.interrupted` and `ability.end`. Logic scripts receive them like any
other gameplay event.

Play shows each stage as an effect named after the ability's `vfx`: a wind-up
at the hand while it is cast, the cast, a beam or a trail while it flies, and
where it lands -- or, without effects of its own, the built-in ones for its
damage type (docs/systems/EFFECTS.md). Creatures attack with abilities too
(docs/systems/CREATURES_AI.md).

The rules are `engine/core/ecs/gameplay_ability_cast.h` (`step_ability_bar`),
checked by `tools/gameplay_check`.
