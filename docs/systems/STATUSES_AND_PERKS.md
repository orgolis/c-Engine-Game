# Statuses and perks

Two kinds of data for a game's own rules of play, written next to abilities in
`.items` files (double-click one in the Asset Browser; **+ From template...**
starts one):

- a **status** is a condition on a character that lasts a while: it may stack,
  change its attributes and tags, hurt or heal it over time, shield it, and set
  things off at moments of its own -- a burn that flares up, a chill that
  freezes, a poison that spreads, a blessing, a stun, a hidden player;
- a **perk** is a rule a character carries for as long as it has it -- a bonus,
  tags, and "when this happens, if that, do this" -- whatever the game calls it:
  a talent, a trait, a relic, a card, a passive.

The engine gives the parts; the game decides what they are called, how many a
character may carry, how they are earned and shown. A game builds its system
from them -- elements with their own effects, classes with their own talents, a
roguelike's relics -- in its own data, logic and UI.

## Statuses

```
status burning
  name Burning
  labels fire debuff        # free words the game's rules look for (below)
  stacks 5                  # how many it holds at most (1: it does not stack)
  duration 4                # seconds; every new application refreshes it (0: until cleared)
  decay 1                   # a stack falls off every second (0: all go at the end)
  tick 0.5 3 fire           # every 0.5 s: 3 fire damage per stack (`heal` instead of a type: heals)
  mod MoveSpeed -5          # an attribute bonus while on, per stack
  taken 15                  # it takes 15% more damage, per stack
  dealt -20                 # it deals 20% less, per stack
  shield 60                 # a shield that takes damage before health
  tag status.stunned        # tags while on
  at 5 burst flare          # at 5 stacks: an action, and the status ends
  hurt 40 burst shatter     # when hit for 40 or more at once: an action (at most every 0.5 s)
  death spread burning 2 6  # its bearer dies with it on
  expire apply chilled 1    # it runs out
  vfx burning               # its look on the bearer: the effect `burning` (or `burning_on`), streamed
  about Burns; at 5 stacks it flares up.
end
```

Damage over time is dealt by whoever put the status on: it counts as their
damage and their kill. A dead character keeps no statuses. A character with
the tag `immune.<status>` or `immune.<one of its labels>` cannot get it.

`taken` and `dealt` are the bonus stats `DamageTaken` and `Damage`; `mod` takes
any attribute the character has, or a bonus stat (`MoveSpeed`,
`CooldownReduction` ...). `shield` adds to the `Shield` attribute, which damage
uses up before health; when the status ends, what is left of its shield goes.

### Tags the engine acts on

| tag | creatures (NPC Agent) | casting |
|---|---|---|
| `status.stunned` | do nothing: no moving, turning or attacking | no ability can be cast |
| `status.disarmed` | do not attack | |
| `status.hidden` (on a player) | do not see or hunt them | |

Any other tag is the game's to use (`require` / `block` on abilities, scripts).

### Actions

What a status's moments (`at`, `hurt`, `death`, `expire`) and a perk's `do`
lines do -- the same words for both:

| action | |
|---|---|
| `burst <ability>` | the ability goes off around the subject (an area there, whatever its own targeting), as the responsible one's |
| `apply <status> [stacks]` | to the subject |
| `self <status> [stacks]` | to the responsible one |
| `spread <status> <stacks> <m>` | to those on the subject's side within m metres |
| `chain <damage> <type> <n> <m>` | damage leaping from the subject to up to n others within m metres |
| `damage <amount> [type]` | to the subject |
| `heal <amount>` | the responsible one |
| `add <attribute> <amount> [per]` | to the responsible one's attribute; `per`: times the moment's amount (the damage of a hit) |
| `recharge <label|all> <percent>` | the responsible one's abilities with that label cool down that much of their cooldown sooner |
| `clear [status]` | ends a status on the subject (a status's own moment: itself) |

For a status the subject is its bearer and the responsible one is who put it
on; for a perk the subject is the one hit or killed (for `hurt` and `cast`, the
carrier) and the responsible one is its carrier. Actions run the frame after
their moment, so a chain of reactions -- a burst that sets another status off
-- travels a frame at a time.

## Abilities that put statuses on

```
ability frost_lance
  ...
  inflict chilled 2         # on each one it reaches
  self_status focused       # on the caster
  labels cold precise       # free words perks look for
end
```

Abilities have two more shapes for this kind of play:

- `target cone` with `radius` and `angle`: those in front of the caster (a
  sweep, a palm strike) -- `move dash 4` makes it a lunge.
- `zone 6 1`: its area stays for 6 seconds and goes off again every second
  (damage, effects, statuses) -- a burning field, a storm. `zone_hits allies`
  makes it work on the caster's side instead (`effect Health 10`: a healing
  well; `inflict shielded`: a ward), `all` on everyone. A `self` ability with a
  zone leaves it where the caster stands.

A zone shows its own effect `<vfx>_zone` streamed over it, else its area effect
now and then; statuses and perks that go off show the ability's area effect,
and a `chain` a beam of its damage type (docs/systems/EFFECTS.md).

## Perks

```
perk kindling_hands
  name Kindling Hands
  labels ember major        # free words: the game's own groups and slots
  mod CooldownReduction 5   # a bonus while carried
  tag perk.kindling         # tags while carried
  when hit label close      # a moment and its filters
  if target burning         # a condition
  chance 0.5
  every 4                   # at most once in 4 s
  do apply burning 3        # one or more actions
  do recharge close 25
  when kill label close     # another moment
  do self blazing
  about Close hits set targets alight.
end
```

Moments: `hit` (its carrier hurt someone with an ability or a weapon), `kill`,
`cast` (its carrier used an ability), `hurt` (its carrier was hit), `applied`
(its carrier put a status on someone), `triggered` (a status its carrier put on
went off at its stack count).

Filters, all of which must hold: `ability`, `weapon`, `status` (what the hit or
kill came from), `label <word>` (the ability's or status's labels), `id <id>`
(that ability or status -- a bare word is an id: `when applied chilled`).

Conditions: `if target <status>` (the one hit or killed has it -- had it, for a
kill), `if self <status>`, `if tag <tag>` (on the carrier),
`if health_below <percent>`.

A character gets and loses perks through the game: a script or logic graph
(`Add Perk`, `Remove Perk`, `Has Perk`), and they are kept with the player's
progress between modes (docs/systems/ONLINE_PLAY.md). How many it may carry,
which ones go together, and what unlocks them is the game's own rule -- labels
and a skill's tags (`tag unlock.<perk>`) are made for it.

A meter a game fills (a rage, a charge, an ultimate) is an attribute the
character has (`AttributeSet`), filled by perks (`do add Charge 5` on a kill,
`do add Charge 0.05 per` on a hit) or by a Regeneration entry over time, spent
by an ability's `cost Charge 100`, and drawn by a bar `bind=player.Charge`.

## In scripts and logic graphs

| graph node | script call | |
|---|---|---|
| Apply Status | `apply_status(e, status, stacks, by)` | stacks on (by: who put them on); returns the stacks it has |
| Status Stacks | `status_stacks(e, status)` | |
| Clear Status | `clear_status(e, status)` | |
| Add Perk / Remove Perk / Has Perk | `add_perk` / `remove_perk` / `has_perk(e, perk)` | |
| Equip Ability By Id | `equip_ability_id(e, ability, slot)` | an ability into a bar slot (from 0); known from then on. An empty ability (`""`) empties the slot -- what was in it stays known |
| Ability In Slot | `ability_in_slot(e, slot)` | the ability's id |

A native script asks `get_setting("engine.statuses_perks")` before using them.
Gameplay events: `status.applied` (instigator: who put it on; value: the stacks
now), `status.triggered`, `status.ended`.

A HUD shows the player's statuses with a slots grid bound to
`player.statuses`: each its name, its stacks, its time running out
(docs/systems/GAME_UI.md).

## Where it lives

`engine/core/ecs/gameplay_status.h` (statuses, actions queued),
`engine/core/ecs/gameplay_perks.h` (perks, the actions run, the frame),
`engine/core/ecs/gameplay_ability_cast.h` (cones, zones, `inflict`), the
damage they go through `engine/core/ecs/gameplay_damage.h` (`DamageTaken`,
`Shield`, who hit last). Checked by `tools/gameplay_check`, including every
Items editor template (`editor/include/items_templates.h`).
