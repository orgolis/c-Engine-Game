# Creatures: NPC agents

An entity with an **NPC Agent** (Inspector) moves and fights on its own in
Play: it patrols, notices its target, chases, fights, searches, and goes home.
Paths come from the navmesh baked when Play starts; the physics world keeps it
out of trees, rocks and walls and on the ground.

## Settings

| | |
|---|---|
| `target_name` | who it is after (an entity's name: Player) |
| `sight_range`, `sight_fov_deg` | how far and how wide it sees -- only with nothing solid in between |
| `move_speed` | its speed chasing; it patrols, searches and goes home slower |
| `patrol_radius` | it wanders about where it started, pausing between points |
| `attack_range` | it attacks from this close (0: it never attacks) |
| `ability` | what it attacks with: an `ability` block's id in the project's `.items` |
| `alert_radius` | others within this distance join in when it notices the target |
| `leash_radius` | it gives up a chase this far from where it started, and goes back |
| `keep_distance` | the distance it keeps while fighting; -1: a ranged one (attack range above 5) keeps 60% of its attack range, a melee one closes in |
| `hearing_range` | gunfire within this distance brings it to look |

## What it does

- **Noticing**: it sees the target in its sight cone with a clear line, hears
  it close by (40% of its sight range) whichever way it faces, hears gunfire
  in its hearing range, and knows where it is when it is hurt. Noticing calls
  the others within its alert radius: a camp fights together.
- **Chasing** the target -- out of sight, to where it was last seen, where it
  **searches** for a few seconds before it **goes home**. Too far from home
  (its leash) it goes back, and does not turn round at once.
- **Fighting**: a melee one closes to its attack range; a ranged one keeps its
  distance, backs away from a target that comes too close and side-steps
  while its attack recovers. It attacks only what it can see.
- **The attack** is its ability: it stands and winds up for the ability's
  cast time (its wind-up effect shows, so it can be seen coming), then the
  ability goes off from its chest at the target -- ahead of a moving target
  for something that flies. Damage, reach and effects are the ability's; it
  never hurts its own side (`team.*` tags). Something with a `speed` (a spit)
  flies and can be dodged.
- **Moving**: it steers round solid foliage and rocks ahead of it, slides
  along what it touches, keeps apart from the others, and follows the ground.
  Hurt, it staggers back for a moment.

```
ability claw
  name Claw
  cooldown 1.4
  cast 0.45           # the wind-up
  target aimed
  range 3
  damage 9 physical
  vfx claw            # its effects: claw_windup, claw_cast ... (docs/systems/EFFECTS.md)
end
```

For the player to be hit, it needs a hurtbox: a **Combat Actor** on the
player (Inspector > Gameplay Components). The HUD shows a red arc toward
where a hit came from.

Without `ability`, the agent uses its AbilitySet slot `ability_index` at once
(no wind-up), as before.

The rules are `editor/include/npc_agents.h`, checked by
`tools/npcagent_check`.
