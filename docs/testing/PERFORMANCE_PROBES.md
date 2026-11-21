# Performance probes

Switches for finding where a frame's time goes and why motion is not smooth,
in an automated run (all environment variables; none changes a saved file).
How they were used: the engine release notes for v0.8.52 (the first-person gun's shake, skinning from host memory, the AI's cost).

## Motion

| | |
|---|---|
| `GWS_TEST_LOG_MOTION=<file.csv>` | in Play, a line a frame: frame time, the camera drawn from, the player, and where the first-person gun is drawn in that camera's space (it should not move but for its bob) |
| `GWS_TEST_UI=press:move_forward+sprint~300@60` | keys held together for that many frames (a sprint) |

`GWS_TEST_VM_LAG=1` places the first-person gun the old way (a frame behind
the camera) -- the A/B for the gun's shake.

## Driving the editor and game UI

| | |
|---|---|
| `GWS_TEST_UI=click:<id>@<frame>,...` | a game UI button clicked at that Play frame (`click:<grid>.<cell>` picks a slot) |
| `... type:<text>@<frame>`, `key:Enter@<frame>` | text typed into what has the keyboard (a code field); Enter / Escape / Backspace pressed |
| `... press:<action>[~<frames>]@<frame>` | an action's key held (see above) |
| `GWS_TEST_EXPORT_DIALOG=<frame>` | Project > Export Game opens at that frame (absolute, not Play, frames) |
| `GWS_TEST_EXPORT_RUN=<frame>` | the export dialog's Export, with the project's saved settings |
| `GWS_TEST_EXPORT_PICK=<frame>[,<frame>]` | the export folder's Choose... pressed (the folder picker opens) |

## Where a frame's time goes

| | |
|---|---|
| `GWS_TEST_LOG_GAPS=<file>` | every frame's checkpoint gaps (frame_begin -> pre_input -> ... -> frame_end): CPU sections and the wait for the GPU (`pre_fence_wait->post_fence`) |
| `GWS_SLOW_FRAME_MS=0.001 GWS_SLOW_FRAME_MAX=3000` | the profiler's zones for every frame (a `[slow-frame]` line each) |
| `GWS_HEALTH_ALL_GPU=1`, `GWS_HEALTH_PERIOD=1` | every timed GPU pass in the `[health]` line, once a second |
| `GWS_TEST_GPU_SPAN=<file.csv>` | the GPU timeline: timestamps at the start and end of each frame's command buffer, round the scene and after the first and last character's skinning -- GPU work no zone times shows up here |
| `GWS_GPU_FRAME_ZONE=1` | one GPU zone round the whole scene (the per-pass zones step aside) |
| `GWS_GPU_CHECKPOINTS=1` | NVIDIA: a marker in the command stream at every timed GPU pass; after a GPU reset the log names the last ones reached (`[gpu] the GPU device was lost ... Last GPU checkpoints: ...`). It serializes the GPU somewhat: a race between passes may hide while it is on |
| `GWS_TEST_RT_AUDIT=1` | names every draw with a non-finite, huge or collapsed transform, every skinned character with a bad pose, and logs the lights (how many cast shadows) -- also with ray tracing off |
| `GWS_TEST_LOG_ANIM=1` | in Play, the player's body: speed and the way it goes relative to where its model faces (ahead / back / left / right) |
| `GWS_TEST_PLAY_CAMERA=1` + `GWS_TEST_CAMERA=x,y,z,tx,ty,tz` | Play drawn from a fixed camera; the player's own body is drawn there (animated), not hidden as in first person |
| `GWS_TEST_CAMERA_FOLLOW=<entity name>` (with the two above) | the six numbers become offsets from that entity (the camera, then the point looked at): a boss or a soldier kept in view as it moves |
| `GWS_TEST_LOG_ANIM_EVENTS=1` | every clip event (animation hook) as it fires: `anim.<event>`, the clip, the time, the entity |
| `GWS_TEST_LOG_FEET=1` | for one character standing on a slope, twice a second: each foot's height over the ground under it against its standing height, the pelvis' drop |
| `GWS_ANIM_SERIAL=1` | characters posed one after another instead of on the job system (to compare the `characters` zone) |
| `GWS_NO_FOOT_IK=1`, `GWS_NO_LEAN=1`, `GWS_NO_SUPPORT_HAND=1`, `GWS_NO_RAGDOLL=1` | a layer of the pose off (to compare) |
| `GWS_TEST_KILL_NEAR=<play frame>[,radius]` | at that frame of Play every NPC agent within the radius (30 m) of the player loses its health: how bodies fall |

A/B switches: `GWS_NO_ANIM_LOD=1` (every character animated every frame),
`GWS_TEST_SKIN_HOST=1` (skinning input in host memory, the old way),
`GWS_TEST_NO_ARMS=1` (the first-person gun without the arms).

## Reading them

- The wait for the GPU large and the GPU's timed passes small: look at the
  span log -- work outside the zones, or the GPU waiting (a present, a
  barrier on everything before).
- Time that moves from one segment to another when one gets faster is a wait,
  not work.
- A game that cannot hold the display's refresh judders with VSync on: on a
  75 Hz display a frame over 13.3 ms is shown for two refreshes.
- A hot GPU throttles: compare runs only at the same temperature (or A/B/A/B).
