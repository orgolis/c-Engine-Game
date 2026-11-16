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

## Where a frame's time goes

| | |
|---|---|
| `GWS_TEST_LOG_GAPS=<file>` | every frame's checkpoint gaps (frame_begin -> pre_input -> ... -> frame_end): CPU sections and the wait for the GPU (`pre_fence_wait->post_fence`) |
| `GWS_SLOW_FRAME_MS=0.001 GWS_SLOW_FRAME_MAX=3000` | the profiler's zones for every frame (a `[slow-frame]` line each) |
| `GWS_HEALTH_ALL_GPU=1`, `GWS_HEALTH_PERIOD=1` | every timed GPU pass in the `[health]` line, once a second |
| `GWS_TEST_GPU_SPAN=<file.csv>` | the GPU timeline: timestamps at the start and end of each frame's command buffer, round the scene and after the first and last character's skinning -- GPU work no zone times shows up here |
| `GWS_GPU_FRAME_ZONE=1` | one GPU zone round the whole scene (the per-pass zones step aside) |

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
