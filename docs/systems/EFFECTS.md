# Effects: what guns, abilities and attacks look like

Muzzle flashes, tracers, sparks and dust where rounds land, a fireball's trail,
a grenade's blast, a creature's wind-up: all are **particles from effects**,
drawn in Play with the scene's particle emitters. An effect is data, an
`effect` block in a project's `.items` file. The engine has a built-in set, so
every gun and ability looks like something with no data at all; a project
replaces any of them by writing an effect with the same name.

## An effect

```
effect spit_trail
  # layer <name>, then keys and values in any order
  layer glob   rate 140 life 0.08 0.14 size 0.42 0.18 color 1 3 0.5 1 to 0.4 1.2 0.2 0
  layer drips  rate 50 life 0.4 0.7 speed 0.2 1 dir any size 0.08 0.03 color 0.25 0.6 0.1 0.9 blend gravity 7
  layer fume   rate 20 life 0.5 1 speed 0.1 0.4 dir any size 0.25 0.7 color 0.35 0.6 0.2 0.35 blend gravity -0.4
end
```

| Key | |
|---|---|
| `count N` | a burst of N when the effect starts |
| `rate N` | N a second while it streams (a trail: along the path flown) |
| `path N` | N a metre along a line to the effect's reach (a beam) |
| `life a [b]` | seconds, random between a and b |
| `speed a [b]` | metres a second |
| `dir along / back / normal / up / any / out / in` | which way: along the shot or flight (default), back, off the surface hit, up, every way, away from the effect's point, towards it |
| `spread deg` | a cone round that way (180: every way) |
| `size a [b]` | width at birth, at death |
| `color r g b a`, `to r g b a` | at birth (above 1 glows, and blooms) and at death (default: the same, clear); up to three `to`s: the colours it passes through, evenly over its life -- white-hot, orange, smoke, gone |
| `add` / `blend` | adds light (fire, sparks: the default) or covers what is behind (smoke, dust, blood) |
| `gravity g` | falls (below 0: rises) |
| `drag d` | slows by this share a second |
| `streak k` | stretched along its motion, length speed x k (sparks, tracers) |
| `jitter r` | born up to r metres from the point |
| `ring r` | born on a level ring of radius r, moving outward (a nova) |
| `sphere r` | born on a sphere of radius r, moving outward (`dir in`: inward, an implosion); a burst spreads evenly over it |
| `ease out / in` | how its size grows: fast, then slow (a fireball, a shockwave), or slow, then fast (default: evenly) |
| `flat` | lies flat on the surface it came off (the ground) instead of facing the eye: a shockwave ring on the ground, a scorch |
| `delay s` | starts s seconds later (a blast's smoke after its flash) |
| `reach` | lives until it has travelled to the effect's reach (a tracer round) |
| `shape glow / smoke / soft / orb / shell / fire / ring` | how it is drawn: a hot core, a ragged puff, a soft disc, a lit ball (an orb, a sun; covering: a solid sphere lit from above), a hollow sphere bright at its rim (a shockwave, a bubble, a shield), a turbulent flame (hot tongues, darker edges), a thin ring (with `flat`: a shockwave on the ground) -- default: light-adding ones glow, covering ones are smoke, streaked ones soft |

Once in a block, `light r g b intensity range life` lights the surroundings
for `life` seconds, fading -- a muzzle flash, a blast. In a stream (a flying
fireball) the light moves with it.

An area passes its size: distances and speeds grow with its radius over 3 m,
sizes with the square root of that. Particles fade out right in front of the
eye, so nothing fills the screen, and softly where they meet the scene, so
smoke and dust have no hard line where they sink into the ground. Smoke is
drawn as ragged, turned puffs that merge into a cloud; colours above 1 glow
and bloom.

## Spheres and explosions

A sphere of light that swells and fades -- `layer core count 1 life 0.5 size 2 7
ease out color 5 3 1.2 1 to 3 1.1 0.2 0.8 to 0.5 0.1 0.02 0 shape orb` -- a
shockwave bubble -- `shape shell` with `ease out` -- and particles born on a
sphere (`sphere 0.4 dir out`) or drawn into it (`sphere 3 dir in`: a charge, a
singularity) make round things look round. A fireball that turns to smoke is
one covering layer passing through its colours: `color 5 2.8 0.9 0.95 to 2.4
0.7 0.12 0.9 to 0.16 0.11 0.08 0.75 to 0.13 0.12 0.11 0 blend shape fire`.

The built-in `explosion` (and `explosive_hit`) puts it together: a white flash
ball, a fireball core, burning puffs turning into a smoke column, a shockwave
bubble and a ring along the ground, sparks, debris and a dust wave, and a
flash of light. An ability's `force`, `falloff` and `shake` and a script's
Explode make it one (docs/systems/ABILITIES.md).

## Which effect, when

**Guns** -- for a gun with id `shotgun`: `shotgun_muzzle` when it fires,
`shotgun_tracer` along each tracer round (`tracer` in its weapon block),
`shotgun_impact` where a round hits the world, `shotgun_hit` where it hits
someone, `shotgun_splash` where it goes into water. Each falls back to
`muzzle`, `tracer`, `impact`, `impact_actor`, `splash`.

**Water** -- `splash` where a round, a beam or something flying goes into
water, `splash_big` where a blast (an area ability, a grenade) meets it; rings
spread on the water with each (docs/systems/WORLDS_AND_WATER.md).

**Abilities** -- by the ability's `vfx` name and the stage:

| Stage | When |
|---|---|
| `<vfx>_windup` | while it is cast (its cast time), at the hand |
| `<vfx>_cast` | as it goes off |
| `<vfx>_beam` | an instant aimed one: from the hand to where it reached |
| `<vfx>_trail` | a flying one (`speed`), while it flies |
| `<vfx>_hit` | where it lands or reaches |
| `<vfx>_area` | an area round the caster |
| `<vfx>_self` | on the caster |

Each falls back to the element's built-in effect, by damage type: `fire`,
`cold`, `shock`, `poison`, `physical`, `explosive`, `magic` (anything else),
and `heal` for an ability that only restores -- `fire_trail`, `cold_area`,
`heal_self` and so on.

Creatures' attacks are abilities too (docs/systems/CREATURES_AI.md): the
same stages, at the creature.

## Example

The TestGame project's `assets/gameplay/effects.items` gives the creatures'
claw and spit their own look and the shotgun a heavier muzzle; everything
else in it uses the built-in set.

The format and the built-in set are `engine/core/vfx/effects.h` (checked by
`tools/vfx_check`); which effect each moment gets is
`editor/include/play_effects.h`.
