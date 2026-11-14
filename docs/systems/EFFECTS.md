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
| `dir along / back / normal / up / any` | which way: along the shot or flight (default), back, off the surface hit, up, every way |
| `spread deg` | a cone round that way (180: every way) |
| `size a [b]` | width at birth, at death |
| `color r g b a`, `to r g b a` | at birth (above 1 glows, and blooms) and at death (default: the same, clear) |
| `add` / `blend` | adds light (fire, sparks: the default) or covers what is behind (smoke, dust, blood) |
| `gravity g` | falls (below 0: rises) |
| `drag d` | slows by this share a second |
| `streak k` | stretched along its motion, length speed x k (sparks, tracers) |
| `jitter r` | born up to r metres from the point |
| `ring r` | born on a level ring of radius r, moving outward (a nova) |
| `delay s` | starts s seconds later (a blast's smoke after its flash) |
| `reach` | lives until it has travelled to the effect's reach (a tracer round) |

An area passes its size: distances and speeds grow with its radius over 3 m,
sizes with the square root of that. Particles fade out right in front of the
eye, so nothing fills the screen.

## Which effect, when

**Guns** -- for a gun with id `shotgun`: `shotgun_muzzle` when it fires,
`shotgun_tracer` along each tracer round (`tracer` in its weapon block),
`shotgun_impact` where a round hits the world, `shotgun_hit` where it hits
someone. Each falls back to `muzzle`, `tracer`, `impact`, `impact_actor`.

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
