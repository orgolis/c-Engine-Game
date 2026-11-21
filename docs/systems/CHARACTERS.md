# Characters and animation

A character is a rigged model -- a glTF or GLB with a skin and its animation
clips -- on a **Skinned Mesh** (Inspector). In Play it can take its clips
from what it is doing: standing, walking (ahead, back, sideways), running,
crouching, jumping, falling, landing, aiming, firing, swinging a blade,
reloading, being hit and dying. A gun in its hand is another rigged model,
held at one of its bones. Nothing about a particular game's characters is in
the engine: models, clip names and where things are held are project data.

## A model

Drop a `.glb` / `.gltf` with a skin into the project and give an entity a
Skinned Mesh with its path. What comes with it:

- the mesh, the skeleton (four bones a vertex) and every animation clip;
- its material: base colour, metal and roughness, the base colour, metal /
  roughness and normal textures -- embedded in the GLB or files next to it.
  A model uses its first primitive's material.

The same model used by many entities is loaded once: they share its mesh data,
clips and textures, and each has its own pose.

## Clips from what it does

Tick **Driven by gameplay** and name a clip for each state in **Clips**,
`state=clip` separated by commas. A clip is found by its full name, or else
by part of it (case does not matter): `aim=Aim_Rifle` finds `05_Aim_Rifle`.

| state | plays |
|---|---|
| `idle` | standing still |
| `walk` | moving (sped up and slowed down with the speed) |
| `run` | moving faster than **Run above** (m/s; default 3.2) |
| `walk_back` | moving backwards (the way it faces is the model's +Z) |
| `strafe_left`, `strafe_right` | moving sideways |
| `jump` | leaving the ground upwards (once), then `fall` |
| `fall` | in the air |
| `land` | back on the ground after a drop (once; not while it walks on) |
| `melee` | a swing of a melee weapon (once; else `fire`) |
| `aim_melee` | a melee weapon held ready (else `aim`) |
| `crouch`, `crouch_walk` | crouched, still and moving (the player) |
| `aim` | a gun held up: the player armed, a creature that sees its target in range |
| `fire` | each shot (once) |
| `reload` | each reload (once) |
| `hit` | hurt (an NPC agent's stagger) |
| `death` | dead (once; it stays in its last pose) |
| `draw` | drawing a gun |

A state with no clip falls back: no `run` walks, no `crouch_walk` walks, no
`crouch` stands, no `walk_back` or `strafe_` walks ahead, no `fall` stands.

**Which way.** The legs follow where the character goes relative to where its
model faces (+Z, as glTF models do): ahead, back, left or right, kept until
another way is clearly meant (no flicker on a diagonal). The player's own
moves come from its movement (jumped, landed, on the ground or not);
anyone else's from how its position changes (rising or falling faster than a
walk down a slope does: in the air).

**The player's model faces where the player looks.** A glTF character faces
+Z; the player (and its camera) look down -Z. A model under the player is
turned half round (`TRANSFORM_ROT=0,1,0,0`) -- without it the body walks
backwards (and other players see it, in online play, moonwalking away from
where it aims). `GWS_TEST_LOG_ANIM=1` logs the player's way (`along` 1 when
ahead) twice a second.

**Upper body bone** (`spine`, say): aim, fire, reload and hit play on that
bone and everything under it while the legs walk or run -- a soldier can
shoot on the move. Without it they replace the whole body's clip.

Whose doing it reads:

- **The player's model**: a child of the player. In first person it is not
  drawn but still casts its shadow; in third person it is seen.
- **An NPC agent's body**: the agent entity itself, or a child of it (its
  "Body"). The agent's aim, shots, stagger and death drive it
  (docs/systems/CREATURES_AI.md).

**Changes flow (inertialization).** A new clip plays at once; what was
showing and what the new clip says differ, bone by bone, and that difference
-- with the speed the old pose was moving at -- dies away on a critically
damped spring (a quarter of the blend time is its half-life). The old motion
carries on into the new one instead of the two being mixed: no clip is
sampled twice, a turn already under way does not stall, and a change in the
middle of another settles from wherever the body is (Bollo, GDC 2018; the
spring from Holden's "Spring-It-On"). It is the same for clips a script plays.

## A frame's pose

Each character in Play is posed in this order: its clips (sampled and
blended, transitions inertialized), then its feet on the ground, then its
body's lean, then where it looks and aims, then -- once the gun in its hand
is placed -- its support hand on the gun. The first four touch only its own
skeleton, so all characters are posed at once on the job system;
`GWS_ANIM_SERIAL=1` poses them one after another (to compare).

## Feet on the ground

A character with two legs (a foot bone on each side, by name: `foot.L`,
`LeftFoot`, `l_foot`...; its parent the knee, that one's the hip) stands on
the ground under it, not on the flat floor its clips were made on. Under each
foot the ground is found (terrain and buildings only -- never a character or a
crate) and the foot goes up or down by as much as it differs; the pelvis
drops as far as the lower foot needs and never rises; each leg bends onto
its foot by two-bone IK, the knee keeping its way; a foot the clip has down is
laid on the slope (up to 30 degrees). A foot the clip lifts mid-step follows
the ground under it too but is not laid flat. Every offset moves on a spring:
a kerb does not snap a foot. In the air or dead it eases off.

A landing compresses the legs: the pelvis dips with the fall's speed (about
10 cm from a jump) and comes back on a softer spring, the feet staying put.

`GWS_NO_FOOT_IK=1` turns it off; `GWS_TEST_LOG_FEET=1` logs, for a character
on a slope, each foot's height over the ground under it.

## Leaning

The body leans into its acceleration -- forward as it sets off, back as it
stops, into a turn -- by half the angle that balances it against gravity, at
most 12 degrees, over the spine and chest. Its velocity goes through a spring
first, so a frame's jitter is no lean. `GWS_NO_LEAN=1` turns it off.

## The support hand

A gun held in the right hand whose model has a `support_grip` bone gets the
left hand on it: the arm (found by the hand's name, `hand.L` / `LeftHand`) is
bent so the hand is at the grip, its elbow keeping its way, its hand its
turn -- whatever the aim, the lean or the clip did. The test game's soldiers'
clips held it 18 cm off. While the clip needs the hands (a reload, a blade,
a death) it eases back to the clip. `GWS_NO_SUPPORT_HAND=1` turns it off.

## Looking and aiming

A character driven by gameplay with a `head` bone under its **Upper body
bone** turns the chain between them -- spine, chest, neck, head -- toward
what it looks at: an NPC agent at the target it sees (its gun, held at the
chest, points there: an aim offset) or, hunting, toward where it last saw
one (a glance, the head leading). Up to about 70 degrees to the side and 57
up or down; the angles are the model's own (it faces +Z) and are carried
into each joint's axes, so any rig works; a spring settles the look instead
of snapping it, and a dead or calm character's look eases back.
`GWS_TEST_LOG_ANIM=1` logs what agents look at.

## Animation hooks (clip events)

A blow should land when the clip shows it: the jaws closing, a foot touching
down, a bolt leaving its port. A model's clips can carry **events** -- a text
file beside the model, the model's name with `.events` for its extension:

```
# clip   seconds  event      [param]
bite     0.45     bite_open
salvo    1.05     bolt       0
walk     0.40     footstep   left
```

A clip is found by name as **Clips** finds one (whole, or part of it). As the
playing clip passes an event, the engine publishes the gameplay event
`anim.<event>` (instigator: the character; param: the clip, then `|` and the
event's param; value: the time): scripts read it from the event feed, logic
graphs hear it like any other. Each event fires once a pass, at any frame
rate (a long frame passes several at once, in order); a looping clip fires
on every pass. A model with events is animated every frame, whatever its
level of detail. `GWS_TEST_LOG_ANIM_EVENTS=1` logs them.

Scripts that pose a model also get `bone_point(e, bone, model_point, out)`:
a point of the model, as modelled, carried by a bone as it is posed now (the
middle of a hit region, the tip of a jaw) -- probe `engine.animation_events`
(docs/systems/SCRIPTED_ENCOUNTERS.md).

## Something held

A child with a Skinned Mesh and **Held at parent bone** set (a bone of the
parent's model, `weapon_socket` or a hand) is drawn at that bone as it moves.
**Hold offset** (metres) and **Hold turn** (degrees) place it in the bone's
space -- most rigs need a turn to bring a gun's barrel along the hand.

Driven by gameplay, a held model plays its own `fire` and `reload` clips
when its holder fires and reloads, and `idle` between (a gun's bolt and
magazine move).

**The muzzle**: a bone named `muzzle` in a held model is where its holder's
attacks start: an NPC agent's rounds and tracers leave the barrel, not its
chest.

## The gun in first person

A gun in a `.items` file can name its model and where it is held in front of
the eye:

```
weapon carbine
  ...
  model assets/models/military/weapons/carbine.glb
  view 0.21 -0.23 0.40     # right, up, forward from the eye, metres (default 0.16 -0.17 0.34)
end
```

The drawn gun is shown in first person with its clips found by name: `Draw`
when it is switched to, `Fire` on each shot, `Reload`, and `Idle` between. It
is alive in the hands: it bobs and rolls with the walk, breathes when the
player stands still, trails the look a little when the mouse moves, is
lowered and canted in a sprint, dips in a crouch, rises with a jump and
bounces on landing (a spring, harder after a longer fall), kicks back with
each shot and comes to the middle when aiming -- which steadies most of the
rest. Its `muzzle` bone is where the player's rounds, tracers and flash
start. It casts no shadow (the player model's own does). The model faces +Z
(its barrel), as glTF models do. Against something solid right ahead (a
trunk, a rock, a wall within about a metre) it comes back and points down
instead of going through it. Auto-exposure does not meter it: a dark gun in
the corner of the view does not make the rest of the scene brighter. It is
placed from the camera as it is drawn that frame (placed before the player
moved, it shook while running: 15 mm a frame at a sprint).

**The player's arms hold it.** In first person a second copy of the player's
model is drawn fixed to the eye, its head folded away, posed by the player's
clips for the way the gun is held (`aim`, `fire`, `reload`; `aim_pistol=` ...
for a pistol -- the weapon's `hold`). Each arm is bent (two-bone inverse
kinematics, the elbow down and out) so the right hand is where the weapon's
`grip` puts it on the gun and the left where the clip holds it relative to a
gun in the right hand: on the handguard of a rifle, cupping a pistol. The
hands follow the gun's `weapon` bone, so they kick, tilt in a reload and swing
a sword with it. A long gun is held bladed (the body turned 30 degrees, the
left shoulder forward); a melee weapon is held in the right hand alone. The
arm bones are found as `upper_arm`/`forearm`/`hand` with `.R`/`.L` (or
`RightArm`-style names); without such a model the gun is drawn alone.

## Level of detail

In Play a character is animated (posed on the CPU, skinned on the GPU) every
frame when it is within 30 m or in view within 70 m; farther ones every 2nd
or 4th frame and ones well out of view every 8th, each turn with the time it
skipped -- drawn meanwhile with the pose it has, moving smoothly. The cost
follows how many characters are near, not how many there are.
`GWS_NO_ANIM_LOD=1` animates them all every frame.

A skinned model is culled by a box that holds it in any pose (not its bind
pose), whole: a limb posed away from where it is bound is never culled.

## In the scene file

```
SKINNED_PATH=assets/models/military/characters/rifleman.glb
SKINNED_CLIP=0
SKINNED_PLAY=1
SKINNED_SPEED=1
SKINNED_GAMEPLAY=3.2                     # driven by gameplay; run above 3.2 m/s
SKINNED_CLIPS=idle=00_Idle,walk=01_Walk,run=02_Run,aim=05_Aim_Rifle,fire=06_Fire_Rifle,...
SKINNED_UPPER=spine
SKINNED_ATTACH=weapon_socket|0,0,0|90,0,180   # held: bone | offset | turn (a gun's entity)
```

## The test game's soldiers

TestGame (docs/systems/TEST_GAME.md) uses the military pack in
`assets/models/military/`: player, rifleman and heavy characters (43 bones,
28 clips: idle with breathing and weight shifts, walk, run, walk back, strafes,
crouch, aim, fire, reloads, draw, hit, death, jump, fall, land, a sword's
ready stance and swing -- made by
`art_source/military_pack/animate_characters.py` in Blender, procedurally) and the eight guns (bolt, magazine, muzzle, grip and trigger bones;
idle, fire, reload and draw clips). Each soldier is an NPC agent with a
"Body" (rifleman or heavy, driven by gameplay, upper body from `spine`) and a
"Gun" held at the body's `weapon_socket`.

## Known limits

- No ragdolls yet: a death is its clip (docs/research/NATURAL_ANIMATION.md has
  what comes next: physical hit reactions and ragdolls, stride and
  orientation warping, sync markers, spring bones).
- Root motion is not used: characters move by their controller or agent and
  the clip's speed follows.
- One material per model.
- The editor's Inspector shows the pose of the clip set in **Clip** while not
  playing; the gameplay states are seen in Play.

## How clips are kept

Each channel of a bone's motion (position, rotation, scale) is kept on its
own, resampled when the model is loaded onto a uniform rate -- its own when
its keys already are evenly spaced, 60 a second otherwise -- exactly as glTF
says: STEP holds a key, LINEAR interpolates (slerp for rotations),
CUBICSPLINE follows its Hermite curve with the keys' tangents. A channel that
never moves is one value. Sampling is an index, not a search, straight into
the pose (the skeleton is touched once a frame). The test game's soldiers'
28 clips take 448 KB (2.8 MB as they were kept before), the Ender
Leviathan's 13 take 1.8 MB (6.1 MB). The load line in the log says how much.

The rules are in `editor/include/imported_skinned_actor.h` (clips, blending,
upper body, bones), `engine/renderer/include/foot_placement.h` (feet) and
`pose_layers.h` (looking, aiming, leaning), `editor/include/anim_hooks.h`
(the spring and the clip events), `engine/renderer/src/animation.cpp` (clips)
and `editor/src/main.cpp` (which state, held models, the support hand, the
first-person gun); checked by `tools/skinned_import_check` (the import: STEP,
LINEAR, CUBICSPLINE, channels of their own lengths), `tools/animhooks_check`
(hooks, the spring) and `tools/footik_check` (feet, looking, aiming, leaning).
