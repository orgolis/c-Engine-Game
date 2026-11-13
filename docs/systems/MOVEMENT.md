# Player movement

How the player moves in Play, and how to change it.

The player moves with defaults even without setup. To change how it moves, add
**Inspector > Gameplay Components > Movement** to the Player entity. Every
setting is grouped by move, and a move a game does not want can be turned off.

## Keys in Play

| Key | Move |
|---|---|
| W A S D | walk |
| Shift | sprint (forwards only) |
| Space | jump; at a ledge, mantle onto it; held while falling, glide (if on) |
| Ctrl or C | crouch; while sprinting, slide |
| V | dash |

## The moves

- **Walk, sprint, crouch.** Crouching lowers the capsule (and the view). Under
  something low the player stays crouched until there is room to stand.
- **Jump.** The height is set in metres. *Coyote time*: a jump just after
  running off an edge still works. *Jump buffer*: a jump pressed just before
  landing happens on landing. *Air jumps*: 1 gives a double jump.
- **Slide.** Crouch while running at least the slide speed. Starts with a
  boost, slows on flat ground, speeds up downhill. Jump out of a slide to keep
  the speed.
- **Dash.** A short burst in the direction you are moving, with a cooldown;
  in the air once per jump (can be turned off).
- **Mantle.** Jump at a ledge between the lowest and highest ledge height (0.5
  to 1.9 m by default), or hold forward against it in the air, and the player
  climbs onto it. Only when there is room on top.
- **Glide** (off by default). Press jump in the air when there is no jump left
  and hold it: a slow fall and a steady forward speed. Let go to drop.
- **Wall run** (off by default). Sprint, jump along a wall and keep holding
  forward: the player runs along it for a while. Jump to kick off it.
- **Climb.** Walk into a surface whose entity has the gameplay tag
  `surface.climbable` (Inspector > Gameplay Components > Gameplay Tags) —
  ladders, vines, climbing walls. Forward climbs, A/D move sideways, jump
  pushes off, and at the top the player climbs over onto the floor above.
- **Swim.** In physical water: slower, Space swims up, Ctrl dives, otherwise
  a slow sink.
- **Stamina** (unlimited by default). Give it a value and sprinting drains it,
  a dash costs it, and it comes back after a short rest. A bar shows while it
  is not full.

## Third-person camera

By default Play looks through the player's camera as you placed it (first
person). Add **Inspector > Gameplay Components > Camera Rig** to the Player
for a third-person camera: behind the player by *distance*, to the right by
*shoulder* (negative for the left), looking where the player looks. Holding
the aim button with a weapon brings it in over the shoulder (*aim distance*,
*aim shoulder*). A wall between the player and the camera pulls the camera in
front of it at once; when the wall is gone it eases back out. Untick *third
person* to keep the component but use the placed camera.

## Animation for each move

If the player (or its rigged model) has **Move Animation**, the model plays a
clip for each move: sprint, crouch, crouch walk, jump/fall, slide, dash,
mantle, glide, wall run, climb, swim. Numbers are the model's clip numbers;
-1 leaves it to the model's own idle / move clip. A move without a clip of
its own borrows a close one (slide uses crouch, mantle uses climb, glide uses
jump/fall, dash and wall run use sprint).

## For programmers

The moves live in `engine/core/ecs/gameplay_movement.h`: settings, state and
one function, `step_movement`, that sees the world only through a ray callback
and the capsule the physics reports, and returns the velocity to move with and
the capsule height it wants. Play mode feeds it from Jolt
(`ScenePlaybackManager::DriveMovement`); `tools/movement_check` runs every move
against boxes. Each step reports what happened (`MovementState::events`:
jumped, landed with the fall height, dashed, mantled, slid, wall run, glide,
climb) for sounds, animation and scripts.
