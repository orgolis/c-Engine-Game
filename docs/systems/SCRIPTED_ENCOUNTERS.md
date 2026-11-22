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
| `bone_point(e, bone, model_point, out)` | `engine.animation_events` | a point of the model (its own space, as modelled) carried by a bone as posed now, in the world: a hit region's middle, a jaw's tip |
| `play_montage(e, clip, slot, rate, blend_in, blend_out)` | `engine.animation_graph` | a one-shot over a character's graph on a slot's bones (`upper`, `full`, or its graph's own); without a graph, the whole body plays it |
| `stop_montage(e, slot, blend_out)` | `engine.animation_graph` | the slot's montage goes before its end |
| `set_anim_param(e, name, value)` | `engine.animation_graph` | a parameter of its graph by name (a bool 0 or 1; a trigger fires when the value is above 0) |
| `anim_state(e, layer, out, size)` | `engine.animation_graph` | the state a layer of its graph is in (its index; its name into `out`), -1 without a graph |

**Animation hooks.** With `engine.animation_events`, a model's clips can carry
events (`<model>.events`, docs/systems/CHARACTERS.md): as a clip passes one,
the event feed (`event_count` / `event_get`) has `anim.<event>`, instigator
the entity, param the clip (and `|` and the event's own param). A boss can
land its blow on the frame the clip shows it -- and keep the time it knows
as a fallback, for an engine without them.

**`set_bone_rotation` turns about the joint's own axes**, as the rig has
them: a rig made in Blender keeps Blender's Z-up axes inside (its root stands
them up), so a turn about a model's up is about the joint's Z there. A
script that knows its rig converts (TestGame's `ender_leviathan.cpp`,
`joint_degrees`); the engine's own looking and aiming work in the model's
space for any rig.

Damage through a receiver: a shot or an aimed ability hits the part it meets
with that part's multiplier; areas and melee count each owner once. Tags the
engine reads: `status.stunned` (no moving input, no firing, no casting; a cast
or channel in progress is interrupted) and `status.shredded` (physical and
kinetic resistance at one fifth).

Scripted player teleports move the physics controller too, and the scene's
**Sky Intensity** is applied to the sky as drawn.
