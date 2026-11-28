# Melee combat (movesets)

A blade can be fought with **moves**: light attacks that chain, a heavy that
charges when held, attacks out of a run or a roll, a dodge roll and a backstep
with invulnerable frames, a guard, a parry and the riposte after it, and the
reactions to all of it -- staggers, a blocked hit, a broken guard, being
parried. The moves are data (a `moveset` block in a project's `.items` file);
the animations are the character's own clips; every timing is in seconds of
the move's clip, so a hit lands where the animation swings. Nothing about a
particular game's swordplay is in the engine.

A weapon without a moveset keeps the older single swing (a reach and an arc in
front, docs/systems/WEAPONS_AND_GEAR.md).

## A weapon with moves

```
weapon longsword
  name Longsword
  damage 55 physical
  melee 2.4 110
  hold melee
  model assets/models/weapons/sword.glb
  grip hand.R -0.06 0.014 -0.032 -130.3 59 0   # where the hand holds it
  moveset longsword                            # its moves (below)
  blade 0 0.12 0.02  0 0.82 0.178              # its edge: hilt to tip, in the model's space
  camera orbit                                 # the view while held (below)
end
```

- `blade x y z  x y z` -- the edge, from the hilt to the tip, in the weapon
  model's own space (`blade from to` is the same along its +Y). Each frame the
  edge is put where the hand holds it (the bone of `grip`, times `grip`), and
  what it sweeps through between two frames is hit -- the arc of the swing,
  not a box in front. A body without that bone (a server) reaches with the
  move's `reach` and `arc` instead, once at its first hit.
- `camera first | third | orbit` -- the view while it is in hand
  (below). A weapon with a moveset and no `camera` orbits.

Held, the player's controls are:

| action (Settings) | default | does |
|---|---|---|
| `fire` | left mouse | the light attack; pressed again in a swing, the next in its chain |
| `aim` | right mouse | the heavy; held into its wind-up, it charges -- let go, it swings |
| `dash` | V | a roll the way the player is pushed (standing: a backstep) |
| `guard` | Left Alt (held) | the guard |
| `parry` | F | the parry |
| `lock_on` | middle mouse | locks on to the foe nearest the middle of the view (again: lets go) |

A press made during a move waits (0.35 s, the moveset's `buffer`) for the
move's cancel point, so presses made in a rhythm come out as a combo.

## The moveset

```
moveset longsword
  name Longsword
  stance Sword_Idle            # its clip while drawn (on the upper body: the legs walk)
  stamina_regen 45             # stamina a second back...
  stamina_delay 0.45           # ...after this long without spending
  poise_regen 35               # poise a second back...
  poise_delay 1.6              # ...after this long without a hit
  buffer 0.35                  # how long a press waits for a cancel point
  riposte_reach 2.4            # a light press this near one open to a riposte is the riposte
  attack light1
    input light                # light | heavy | run_light | run_heavy | roll_light | roll_heavy
                               # | jump_light | riposte (and any other: named by `charge`)
    clip Sword_Light_1
    rate 1.15                  # the clip's speed (times below are the clip's own)
    hit 0.17 0.36              # the edge hurts; another `hit` line, another hit
    cancel 0.42                # from here a waiting press starts the next move
    finish 0.62                # from here it is over (back to the stance)
    damage 1.0                 # times the weapon's damage
    stamina 14                 # spent as it starts
    poise 22                   # poise damage it does
    hyperarmor 0.30 0.50       # not staggered while this lasts
    track 420                  # turns toward its target (degrees a second) until its first hit
    reach 2.4                  # without a blade: everyone this near...
    arc 120                    # ...in this many degrees in front
  attack light2
    input light
    after light1               # a chain: pressed again in light1
    ...
  attack heavy1
    input heavy
    clip Sword_Heavy
    charge Sword_Heavy_Hold 1.0 1.6 heavy_release 0.30
                               # held 0.30 s into it (its wind-up): its hold clip, up to 1 s,
                               # damage up to x1.6; let go, `heavy_release` (a clip from the
                               # held pose). A tap swings heavy1 as it is.
    ...
  attack heavy_release
    input charged
    clip Sword_Heavy_Release
    ...
  dodge roll                   # moving: a roll; standing: the backstep
    clip Roll
    iframes 0.04 0.44          # invulnerable
    cancel 0.52                # a roll attack (roll_light) from here
    finish 0.64
    stamina 18
  dodge backstep
    clip Backstep
    iframes 0.03 0.24
    finish 0.40
    stamina 10
  guard                        # held
    clip Sword_Guard
    absorb 0.85                # the share of a blocked hit's damage it stops
    stability 45               # how little of a hit's poise it costs in stamina (0..100)
    angle 150                  # it blocks this wide in front (degrees)
  parry
    clip Parry
    window 0.06 0.26           # a hit landing now is parried
    finish 0.50
    stamina 12
  react stagger       Stagger_Light 0.45   # a reaction: its clip and how long (else the clip's length)
  react stagger_heavy Stagger_Heavy 0.95
  react guard_hit     Guard_Hit 0.32
  react guard_break   Guard_Break 1.0
  react parried       Parried 1.25
  react riposted      Riposted 1.6
end
```

The Items editor's **+ From template... > Melee** adds a moveset (and a sword
using one) to start from; a weapon's **Blade (melee)** section picks its
moveset, its edge and its camera.

What happens, in order:

- **Stamina** (an attribute named `Stamina` on the fighter; none: unlimited).
  A move spends its `stamina` as it starts; with none left nothing starts. It
  comes back at `stamina_regen` after `stamina_delay` (half as fast guarding).
- **Poise** (the Combat Actor's). A hit takes its `poise`; at zero the fighter
  staggers (`stagger`, or `stagger_heavy` for a hit of 60% of its poise or
  more) unless its move has hyperarmor then. It comes back after `poise_delay`.
- **The guard** blocks a hit from the front (`angle`): `absorb` of the damage
  is stopped, the stamina it costs is the hit's poise less `stability`, and the
  guard plays `guard_hit`. Out of stamina, the guard breaks (`guard_break`) and
  the fighter is open to a riposte.
- **The parry**: a hit landing in its `window`, from the front, does nothing;
  the attacker plays `parried` and is open to a riposte.
- **The riposte**: a light press within `riposte_reach` of a foe open to one
  is the `riposte` attack -- the foe plays `riposted` and the riposte's hit
  lands on it.
- **A dodge's i-frames**: a hit then does nothing (`melee.dodged`).
- A dodge goes the way the fighter is pushed (it turns there first); so does an
  attack with no target. A move with `track` turns toward the target until its
  first hit -- after that it is committed.

Events on the gameplay bus carry the attacker, the target, the damage and the
move's id, for scripts and logic graphs: `melee.attack`, `melee.dodge` and
`melee.parry_try` as a move starts; `melee.hit`, `melee.riposte`,
`melee.blocked`, `melee.guard_broken`, `melee.parried`, `melee.dodged` where a
blade lands; `melee.<reaction>` as one plays (`melee.stagger`,
`melee.riposted` ...).

## In Play

- The moves play on the body as montages: an attack, a dodge or a reaction on
  the whole body (its root motion with it), the stance and the guard on the
  upper body over the legs' own walking.
- **Root motion**: the clip's root bone carries the character (its travel is
  taken out of the pose); the player's movement takes it along the ground
  (gravity, slopes and walls still the world's), an NPC is moved by it (not
  through walls). While a move plays the controls do not move the player.
- **Hit-stop**: a blade that lands holds both still a moment (0.065 s; a hit
  of 40 poise or more 0.1, a riposte 0.16, a broken guard or a parry 0.12) --
  their clips too.
- What a blade did is shown: `<weapon>_melee_hit`, `_melee_block`,
  `_melee_parry`, `_melee_riposte` effects (else the built-in `melee_*`) where
  it landed, the camera shakes when the player is in it, and a hit on the
  player shows where it came from.
- **Lock-on**: `lock_on` picks the standing foe nearest the middle of the view
  (within 20 m and 45 degrees); a ring marks it. The player turns to it, an
  orbiting camera keeps it in view. It lets go when pressed again, when the
  foe falls or is 30 m off, or when the blade is put away.

## The camera: first, third, orbit

The player's own **Camera Rig** says how it is seen (none: first person). What
it holds and what it is can ask for another, the most specific first:

1. a tag on the player: `camera.first_person`, `camera.third_person` or
   `camera.orbit` (a status can give one while it lasts -- an ability's form);
2. the weapon in hand: its `camera` (a moveset without one: `orbit`);
3. else its rig as it is.

`third` is over the shoulder (the player faces where the camera looks; guns);
`orbit` turns the camera round the player with the mouse, and the player turns
to where it goes (the Camera Rig's `orbit` and `turn rate`) -- the camera of a
game fought with a blade. Asked for without a rig of its own, the engine's
default is used (an orbit: centred, 3.8 m back). Back in first person, the
camera returns to its place on the player.

## NPCs

An NPC agent fights with moves when it has a **Melee Fighter** (Inspector) with
a `weapon` (a weapon with a moveset: its moves, damage and edge -- its held
model at the same bone and grip) or a moveset of its own. It closes to its
first swing's reach, circles while it waits, then presses a combo (now and then
a heavy); how soon and how long is its `aggression` (0..1). A swing coming at
it it guards or sidesteps as often as its `defense` (0..1) says. Melee fighters
take turns: one swings at a target while the others round it. While a move
plays, its clip moves and turns it.

Its body needs the moveset's clips: a clip library next to its model
(docs/systems/CHARACTERS.md, "Clip libraries") adds them to a model that lacks
them.

## Scripts

`ecs::melee_move_id(world, e)` is the move a fighter is in ("" standing,
"guard", a reaction's kind); `ecs::melee_invulnerable(world, e)` is true in a
dodge's i-frames. The events above reach scripts as gameplay events.

## Checks

`tools/gameplay_check` (melee): a chain with a buffered press, a stagger from
poise, a roll's i-frames and its way, a guard and its break, a riposte, a
parry, a charged heavy (held at once, and held into a wind-up), a roll attack,
stamina, a blade swept through a target, a moveset named by a weapon after it,
the Melee Fighter saved and read back. `tools/skinned_import_check`: clip
libraries and root motion.

## Known limits

- One weapon's moveset at a time (no two-handed or dual-wield switching).
- Jumping attacks are read (`jump_light`) but the sample clips have none.
- Movesets are not sent in online play yet. Single player and the host fight
  with moves; a client's blade swings the single networked way (its hits are
  the host's to decide), and a client sees NPC swordsmen move but not their
  moves.
- In first person a blade's moves play on the body you do not see; the
  first-person view model does not follow them yet (a blade asks for orbit by
  default).
