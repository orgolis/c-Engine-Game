# Natural character animation: what makes it feel alive

Research notes behind the animation work of 0.8.57: the techniques the games
industry uses so characters move naturally, what each one buys, and where the
engine stands with it. Sources are at the end of each part.

The short version: a clip is a recording; a character feels alive when the
recording **answers the world** -- when a change of mind flows instead of
popping, the body follows where it has been, the eyes and the gun go where
the target is, the feet meet the ground they stand on, and a blow lands when
the clip shows it. Most of that is a little procedural work laid over clips,
not more clips.

| Technique | What it fixes | In GWS |
|---|---|---|
| Inertialization | pops and stalls when the clip changes | **0.8.57**, every skinned character |
| Critically damped springs | snapping; jitter; overshoot | **0.8.57** (inertialization, looking); 0.8.56 (first-person gun) |
| Animation events (hooks) | blows, shots, steps out of step with the clip | **0.8.57** (`<model>.events`, `anim.<event>`) |
| Procedural layers over clips | stiff bodies; a long body turning like a plank | **0.8.57**: the Ender Leviathan (TestGame script) |
| Aim offset / look-at | a gun pointing level at a target on a hill; dead eyes | **0.8.57**, gameplay characters |
| Fixed-step logic, interpolated drawing | motion that stutters with the frame rate | **0.8.57**: the Leviathan (20 Hz, drawn in between) |
| Play-rate matching | feet sliding at speeds the clip was not made for | 0.8.56; **0.8.60** from each clip's measured speed, shared with the stride |
| Stride and orientation warping | sliding and crossed legs on diagonals | **0.8.60** |
| Sync markers (foot plants) | a stumble when walk changes to run | **0.8.60**: measured per clip, switches in step |
| Foot locking | the slide left after rate and stride | **0.8.60**: held per foot, one steps at a time (TestGame running: median 0.27 m/s to 0.00) |
| Animation graph, 2D blend spaces, montages | clip choice hard-wired in C++; a diagonal as one clip with the legs turned | **0.8.61**: every gameplay character runs a graph (the built-in humanoid one or its own `.animgraph`) |
| Foot IK (foot placement) | feet floating over or sunk into slopes | **0.8.58**: feet, pelvis, slope, landing compression |
| Lean into acceleration | a body that starts and stops like a statue on rails | **0.8.58** |
| Support hand IK | the second hand floating beside the gun (18 cm in TestGame) | **0.8.58** |
| Clip storage per channel, exact glTF samplers | STEP / CUBICSPLINE clips played wrong; memory | **0.8.58** (a third to a sixth of the memory) |
| Posing on the job system | animation cost growing with every character | **0.8.58** (0.84 ms to 0.32 ms for the TestGame camp) |
| Motion matching | needs hours of motion capture | not planned |

## Inertialization

A crossfade plays the old and the new clip together for a while and mixes
them: both are sampled, and a motion under way (a turn, a swing) is diluted
toward the new clip's pose -- it stalls. **Inertialization** (Bollo, Gears of
War) switches to the new clip at once and treats the change as a
post-process: per bone, the offset between what was showing and what the new
clip says, with the velocity the old pose had, is decayed to zero. The old
motion carries on into the new one, nothing is sampled twice, and a change
in the middle of another starts from wherever the body is. It is the industry
default now; Unreal ships it as an animation node.

GWS (0.8.57): `ImportedSkinnedActor` keeps the pose it showed and the one
before (for the velocity); a `play()` or `play_upper()` with a blend time
starts an offset per bone (position, and rotation as a scaled angle-axis
vector) that a critically damped spring decays with a half-life of a quarter
of the blend time. Holden's **dead blending** (extrapolate the old pose and
crossfade from that) is a later alternative with similar results; not needed.

- D. Bollo, "Inertialization: High-Performance Animation Transitions in *Gears of War*", GDC 2018: https://gdcvault.com/play/1025165/Inertialization-High-Performance-Animation-Transitions
- D. Bollo, "High Performance Animation in Gears of War 4", SIGGRAPH 2017 talk: https://history.siggraph.org/wp-content/uploads/2022/09/2017-Talks-Bollo_High-Performance-Animation-in-Gears-of-War-4.pdf
- D. Holden, "Dead Blending": https://theorangeduck.com/page/dead-blending

## Springs

A critically damped spring follows a moving goal as fast as it can without
overshooting, and Holden's closed form is exact for any time step -- a
character looks the same at 30 and at 144 fps. In GWS it decays
inertialization offsets, settles a character's look and aim, and moves the
first-person gun (`editor/include/anim_hooks.h`, checked by
`tools/animhooks_check`). Note on its "half-life": from rest, 60% of an offset
is left after one, 20% after two, 3% after four.

- D. Holden, "Spring-It-On: The Game Developer's Spring-Roll-Call": https://theorangeduck.com/page/spring-roll-call

## Animation events (hooks)

Gameplay that happens *in* a clip -- the jaws closing, a foot touching down,
a bolt leaving its port -- must happen when the clip shows it, at any frame
rate, once. Minecraft mods do it with GeckoLib's keyframe triggers (sound,
particle and custom-instruction keyframes authored in Blockbench); Unreal
with anim notifies. glTF has nowhere to store them, so GWS reads a text file
beside the model (`<model>.events`: `clip seconds event [param]`) and
publishes `anim.<event>` on the gameplay event bus as the clip passes it
(docs/systems/CHARACTERS.md). TestGame's Leviathan lands every blow on these,
with the Arcan mod's tick as a fallback.

- GeckoLib wiki, "Keyframe Triggers": https://github.com/bernie-g/geckolib/wiki/Keyframe-Triggers-(Geckolib3)

## Procedural layers over clips

Few clips, much life: Overgrowth animates with a handful of key poses and
procedural blending, springs and physics; Rain World's creatures are points
joined by distance constraints, moved by code. The Arcan mod's Ender Leviathan
does the same over its clips (`LeviathanMotion`): a swimming wave runs down
the body, faster and wider with speed; every segment behind the trunk turns
to the heading the body had a few ticks ago, so a turn passes down the body
as a bend and a roll as a twist (follow-the-leader); the neck carries the aim
of the head; fins beat with the wave and sweep back with speed. While a big
clip owns the body (the tail strike) the layer gives way to it but for 15%,
so the body never goes stiff. TestGame's port keeps all of it.

- D. Rosen, "Animation Bootcamp: An Indie Approach to Procedural Animation", GDC 2014: https://gdcvault.com/play/1020049/Animation-Bootcamp-An-Indie-Approach
- J. Jakobsson, J. Therrien, "The Rain World Animation Process", GDC 2016: https://gamedeveloper.com/art/video-animating-i-rain-world-i-and-its-many-squishy-stretchy-creatures

## Aim offset and look-at

A soldier whose rifle points level while its target stands on a roof is
plainly not aiming. An **aim offset** turns the spine and chest toward the
target over the aiming clip (the gun, held at the chest, comes along); a
**look-at** turns mostly the head and neck. GWS (0.8.57) does both on the
chain from a character's upper-body bone to its head, spreading the angle
down the chain, in the model's own space (so any rig works), smoothed by a
spring.

One lesson from the Leviathan: a rig made in Blender keeps Blender's Z-up axes
inside every joint (the exporter stands the root up). A turn "about the
model's up" given in a joint's own axes is a roll about its length. Work in the
model's space and carry the turn into the joint (`L' = P^-1 D P L`).

## Fixed-step logic, drawn in between

The Arcan mod runs at Minecraft's 20 ticks a second and the client draws
between ticks. Porting it to run every frame would change its feel with the
frame rate (its turn rates and easing are per tick). The TestGame script runs
the creature on a 20 Hz clock and draws its position, heading and procedural
pose interpolated between the last two ticks: identical behaviour, smooth at
any frame rate.

## Play rate, stride and orientation warping

Feet slide when the body moves faster or slower than the clip was made for.
Scaling the play rate fixes it within limits (beyond about 15-20% either way
the cadence looks wrong). **Stride warping** lengthens or shortens the
stride instead; **orientation warping** turns the legs toward the way the
body actually moves on diagonals while the chest keeps facing ahead;
**distance matching** drives starts and stops by the distance left, not
time. GWS 0.8.56 matched play rate (clamped to 0.6-1.8, wider than ideal)
and picked walk-back and strafe clips by direction.

**0.8.60** (`engine/renderer/include/locomotion_warp.h`): each locomotion clip
is measured once -- its foot plants (sync markers) and, from how its planted
feet slide under an in-place body, its speed and way. The ratio of the real
speed to that is split between stride (square root, 0.8-1.3; 1.15 sideways)
and rate (the rest, 0.5-2.5); the legs turn up to 50 degrees toward the real
way with the upper body turned back; switches start the new clip at the same
point of the same foot's step. Then **foot locking** (in `foot_placement.h`)
holds each planted foot in the world for its stance and releases it on
lift-off, on a stretch past a third of the leg or a 35-degree turn (one foot
at a time) or after a moment standing still off its spot. In the TestGame
the player's planted feet went from sliding 2.0 m/s (running at 5 m/s, before
this round) through 0.27 (warps alone) to 0.00 (median, with the lock).
Distance matching (starts and stops) waits for start and stop clips.

- Epic Games, "Pose Warping in Unreal Engine": https://dev.epicgames.com/documentation/unreal-engine/pose-warping-in-unreal-engine
- Epic Games, "Locomotion in Unreal Engine" (distance matching): https://dev.epicgames.com/documentation/unreal-engine/locomotion-in-unreal-engine
- Epic Games, "Adapting Lyra animation to your UE5 game": https://www.unrealengine.com/tech-blog/adapting-lyra-animation-to-your-ue5-game

## Foot IK (0.8.58; the plan it followed)

The clips are made on flat ground; TestGame's soldiers stand on hills. The
standard pipeline, after the pose is sampled and blended:

1. For each foot, cast a ray down from above the ankle to below it; the hit
   and its normal are where the foot should be.
2. Lower the pelvis by the larger of the two legs' shortfalls (so the lower
   foot can reach), smoothed and never raised.
3. Two-bone IK on hip-knee-ankle so each ankle reaches its target, keeping the
   knee's direction (GWS has the solver: `two_bone_ik.h`, used for the
   first-person arms).
4. Turn the ankle to lie on the ground's normal.
5. Fade it out while a foot is in the air in the clip (its height in the clip
   above the ground it was made on) and in jumps and falls; springs on every
   offset so a step onto a kerb does not snap.

In GWS: a `place_feet()` on the actor beside `look_at()`, leg chains found by
name (`thigh`/`shin`/`foot`, `UpLeg`/`Leg`/`Foot`, `.L`/`.R`), the physics ray
cast through the scene's world, for gameplay characters within the full-rate
level of detail only.

- G. Blanc, ozz-animation, "Foot IK" sample: https://guillaumeblanc.github.io/ozz-animation/samples/foot_ik
- Khronos Vulkan tutorial, "Foot Placement on Uneven Terrain": https://docs.vulkan.org/tutorial/latest/Advanced_glTF/Procedural_Animation_IK/04_foot_placement.html

## Motion matching

Instead of a graph of clips, search a large database of motion capture every
few frames for the piece that best continues the current pose toward where
the player wants to go (For Honor; Unreal 5.4's Pose Search). Superb with the
data; without hours of capture it has nothing to search. Not planned for the
engine's procedural, hand-made clips.

- S. Clavet, "Motion Matching and The Road to Next-Gen Animation", GDC 2016: https://www.gdcvault.com/play/1022985/Motion-Matching-and-The-Road

## The overhaul roadmap: where each item stands

The animation overhaul report (Parts I-II: architecture, graphs, motion
matching, IK, physics, combat, traversal, pipeline, GPU, faces, networking)
item by item, with what was decided.

| Item | Status |
|---|---|
| Skeleton: parents first, one pass, no allocation | **0.8.58** (`Skeleton::UpdateWorldTransforms`) |
| Pose sampled into a buffer, not the skeleton | **0.8.58** (`AnimationClip::SampleLocal`) |
| Full SoA / SIMD pose (ozz-style) | next round of the core: the linear pass already took the posing from 0.84 to 0.32 ms for 40 characters; SoA pays off at hundreds |
| glTF STEP / LINEAR / CUBICSPLINE, channels on their own keys | **0.8.58** |
| Morph targets (`weights`) | with faces: needs a GPU morph pass first; no TestGame asset has them |
| Clip compression (ACL) | after the clip library: per-channel uniform tracks with constant channels already cut memory 3-6x; quantization next, ACL when clips stream |
| Notifies / events on clips | **0.8.57** (`<model>.events`, `anim.<event>`) |
| Float curves on clips | with the clip library (the `.events` file grows a curve line) |
| Inertialization | **0.8.57** (spring decay; Bollo's quintic is the alternative) |
| Dead blending | not needed while inertialization covers the transitions |
| Hemisphere-safe blending | already so (`glm::slerp`); multi-input nlerp aligned to the first (**0.8.58** in channel sampling) |
| One graph runtime from `.animgraph`, montages / slots | **0.8.61**: parameters, layered state machines, any-state and exit-time transitions, montage slots; every gameplay character runs one (built-in humanoid or its own file) |
| Sync groups and markers | **0.8.60**: foot plants measured per clip, in-step switches |
| 2D blend spaces, per-bone layer masks | **0.8.61**: Delaunay blend spaces (radial clamp for locomotion, samples at measured velocities), layer and slot masks; inertialization masked to the bones a change touches |
| Additive layers, montage sections | with the first additive clips / multi-part montages |
| Posing on the job system | **0.8.58** |
| Fixed simulation tick; animation on it, drawn in between | **evaluated, not adopted for animation (0.8.62)**: clip events fire on the time window a frame passes, exact at any frame rate; online play is host-authoritative (no lockstep to keep); scripted encounters that want a fixed clock keep one (the Leviathan's 20 Hz). Animation stays per frame, as in the big engines; physics keeps its fixed step |
| Look-at / aim chain | **0.8.57**, in the engine as `pose_layers.h` in **0.8.58** |
| Foot IK, pelvis spring, landing compression | **0.8.58** |
| Support hand IK (third person) | **0.8.58** |
| Lean into acceleration | **0.8.58** |
| Head stabilization | not for third-person characters (the bob is small); first-person view has its own |
| Breathing / idle noise layers | in the clips (TestGame's soldiers, 0.8.56); an additive layer comes with additive clips |
| FABRIK, CCD, full-body DLS | with climbing (the first need for multi-contact IK) |
| Stride / orientation warping | **0.8.60** |
| Foot locking | **0.8.60** |
| Distance matching | with start and stop clips |
| Motion warping | with melee combat's lunges and traversal |
| Motion matching | not planned: it needs hours of motion capture; the toolkit above serves hand-made clips |
| Ragdolls (Jolt) | **0.8.59**: twelve bodies by name on any rig, hinged knees and elbows, settle and freeze |
| Hit reactions | **0.8.59**: a flinch away from the attacker on the lean's spring; powered ragdoll for stumbles and getting up later (no TestGame character gets up) |
| Spring bones, XPBD cloth, strand hair | spring bones with the first asset that has a chain; cloth and hair later |
| Procedural creature step planner | with the first legged creature |
| Facial animation | when a character has a face rig |
| Replicate animation parameters, not poses | **0.8.62**: NPCs' look, aim and flinch, scripts' montages and graph parameters; each machine's graph poses (scripted bone turns still go per bone) |
| GPU: shared bind buffers, previous positions for motion vectors, DQS | with the renderer's motion-vector work |
| Budgeted animation LOD | the distance tiers stay until scenes have hundreds of characters |
