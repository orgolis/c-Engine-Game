# Hunting flicker

Something that shows for a frame and is gone the next is usually a **race**:
the CPU writing data for frame N+1 while the GPU is still drawing frame N from
it (two frames are in flight). Two things follow:

- **It hides from ordinary captures.** `--screenshot` and `GWS_TEST_SHOTS`
  wait for each captured frame, which stops the overlap -- and the bug with it.
- **It shows while moving.** Standing still, every frame writes the same data,
  so reading the next frame's copy looks the same.

## The flicker recorder

```
GWS_FLICKER_DIR=<dir> editor.exe --project <project.schizo>
```

Keeps the last 240 frames -- the picture at half size, read back without
waiting for anything, and a line of numbers each (`frames.csv`: the camera,
frame time, draws, what occlusion culling dropped and why, foliage) -- and
writes them to `<dir>/<n>/` when **F10** is pressed and when the editor
closes. A person plays; right after something flickers they press F10.

Then: frame-to-frame differences (a pixel that changes one frame and changes
back the next) find the frames; `frames.csv` gives the camera to put a test
run in the same place; A/B switches find the cause:

| | |
|---|---|
| `GWS_NO_HZB=1` | no occlusion culling |
| `GWS_NO_INDIRECT=1` | every draw direct (no indirect draw buffer) |
| `GWS_NO_INSTANCING=1` | every draw per object |
| `GWS_TEST_NO_NEAR_FADE=1` | foliage leaves do not fade near the camera |
| `GWS_TEST_NO_VIEWMODEL=1` | no gun in first person |

Also: `GWS_TEST_SHOTS_ASYNC=1` (test shots without waiting), `GWS_CULL_LOG=1`
(each frame's culling), `press:<action>~<frames>` in `GWS_TEST_UI` (hold a key:
walk), and Vulkan's synchronization validation
(`VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true`), which reports such races
directly -- mind `VK_KHRONOS_VALIDATION_DUPLICATE_MESSAGE_LIMIT`: two different
hazards with one message id hide each other.

## Found this way (0.8.50)

Trees' leaves vanishing for single frames while walking: the **indirect draw
buffer** (meshlet draws) was one buffer rewritten every frame from index 0
while the GPU drew the previous frame from it. Now a ring of three, like the
particle buffers (0.8.48). With it: the light list and skinning matrices go in
the command stream; the G-buffer's and shadow maps' render passes wait for the
previous frame's depth writes and reads; acceleration-structure builds no
longer overlap in their shared scratch buffer. Synchronization validation:
no messages in Play.
