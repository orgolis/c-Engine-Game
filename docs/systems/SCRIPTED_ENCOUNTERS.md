# Scripted encounters

Script verbs for bosses and set pieces (all three backends: Python, native
C++, C#; the full table is docs/reference/SCRIPT_API.md). Native scripts built
against a newer engine can check the runtime first: `get_setting("<probe>")`
returns "1" when the verbs after it in the table are there.

| Verb | Probe | |
|---|---|---|
| `play_animation(e, clip, loop, speed, blend, restart)` | `engine.skeletal_animation_api` | a named clip on a rigged character; it takes over the automatic (gameplay) clip choice |
| `bone_position(e, bone, out)` | `engine.skeletal_animation_api` | where a bone is now (an attack's origin, an effect's anchor) |
| `spawn_effect(name, pos, dir, scale, reach)` | `engine.encounter_api` | a project effect (an `effect` block) at a place |
| `set_environment(darkness, whiteout)` | `engine.encounter_api` | for this frame: the scene darkened (light, sky, clouds, fog, shafts) or washed white; saved settings untouched |
| `set_bone_rotation(e, bone, degrees)` | `engine.encounter_api` | a turn added on top of the animated pose (a head that tracks) |
| `spawn_mesh(path, pos, scale)` | `engine.encounter_api` | a model with no collision (debris, a visual effect mesh) |
| `set_damage_receiver(part, owner, multiplier)` | `engine.combat_api` | a hurtbox `part` passes its damage to `owner`'s health, times `multiplier` (a weak point) -- many parts, one health pool |

Damage through a receiver: a shot or an aimed ability hits the part it meets
with that part's multiplier; areas and melee count each owner once. Tags the
engine reads: `status.stunned` (no moving input, no firing, no casting; a cast
or channel in progress is interrupted) and `status.shredded` (physical and
kinetic resistance at one fifth).

Scripted player teleports move the physics controller too, and the scene's
**Sky Intensity** is applied to the sky as drawn.
