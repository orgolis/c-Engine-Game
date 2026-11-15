# Characters and animation

A character is a rigged model -- a glTF or GLB with a skin and its animation
clips -- on a **Skinned Mesh** (Inspector). In Play it can take its clips
from what it is doing: standing, walking, running, crouching, aiming, firing,
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
| `crouch`, `crouch_walk` | crouched, still and moving (the player) |
| `aim` | a gun held up: the player armed, a creature that sees its target in range |
| `fire` | each shot (once) |
| `reload` | each reload (once) |
| `hit` | hurt (an NPC agent's stagger) |
| `death` | dead (once; it stays in its last pose) |
| `draw` | drawing a gun |

A state with no clip falls back: no `run` walks, no `crouch_walk` walks, no
`crouch` stands.

**Upper body bone** (`spine`, say): aim, fire, reload and hit play on that
bone and everything under it while the legs walk or run -- a soldier can
shoot on the move. Without it they replace the whole body's clip.

Whose doing it reads:

- **The player's model**: a child of the player. In first person it is not
  drawn but still casts its shadow; in third person it is seen.
- **An NPC agent's body**: the agent entity itself, or a child of it (its
  "Body"). The agent's aim, shots, stagger and death drive it
  (docs/systems/CREATURES_AI.md).

Changes blend: a new clip fades in over a fraction of a second.

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
bobs as the player walks, kicks back with each shot and comes to the middle
when aiming. Its `muzzle` bone is where the player's rounds, tracers and flash
start. It casts no shadow (the player model's own does). The model faces +Z
(its barrel), as glTF models do.

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
22 clips) and the eight guns (bolt, magazine, muzzle, grip and trigger bones;
idle, fire, reload and draw clips). Each soldier is an NPC agent with a
"Body" (rifleman or heavy, driven by gameplay, upper body from `spine`) and a
"Gun" held at the body's `weapon_socket`.

## Known limits

- No inverse kinematics: the support hand follows the clip, not the gun.
- Root motion is not used: characters move by their controller or agent and
  the clip's speed follows.
- One material per model.
- The editor's Inspector shows the pose of the clip set in **Clip** while not
  playing; the gameplay states are seen in Play.

The rules are in `editor/include/imported_skinned_actor.h` (clips, blending,
upper body, bones) and `editor/src/main.cpp` (which state, held models, the
first-person gun); the import is checked by `tools/skinned_import_check`.
