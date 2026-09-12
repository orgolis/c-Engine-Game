#!/bin/sh
# Team tracker sync — generated, review before running.
#
# Requires an authenticated gh:  gh auth login
#
# Creates the tracker issues that the workflow plan implies but that do not
# exist yet, and closes the Phase 4 issues whose work has shipped. Deliberately
# NOT creating: Phase 5 items 5.1/5.2/5.3/5.7/5.8/5.9 (already #38/#39/#40/#41/
# #43/#42) and 5.5/5.6 (already WorldShaper-Hub#5 and #6).
set -e
REPO="orgolis/c-Engine-Game"

command -v gh >/dev/null 2>&1 || { echo "gh not found"; exit 1; }
gh auth status >/dev/null 2>&1 || { echo "Run: gh auth login"; exit 1; }

# Two halves, separately runnable. Creating issues and posting status comments
# is additive; closing four is visible to everyone on the tracker, so it is
# opt-in rather than bundled.
#   sh tools/team_issues.sh              -> create only (default)
#   sh tools/team_issues.sh --close-only -> close the shipped Phase 4 issues
#   sh tools/team_issues.sh --all        -> both
DO_CREATE=1
DO_CLOSE=0
case "${1:-}" in
  --close-only) DO_CREATE=0; DO_CLOSE=1 ;;
  --all)        DO_CREATE=1; DO_CLOSE=1 ;;
  "")           ;;
  *) echo "usage: $0 [--close-only|--all]"; exit 1 ;;
esac


if [ "$DO_CREATE" = "1" ]; then
echo "=== creating 5 issue(s) ==="

gh issue create --repo "$REPO" \
  --title "Replay as a regression test: replay a session against a new build and diff world state" \
  --label "enhancement,runtime,high-leverage" \
  --milestone "Phase 5 — Differentiate" \
  --body "$(cat <<'ISSUE_BODY_EOF'
Workflow-plan item **5.4**, and the only Phase 5 row with no issue — 5.1/5.2/5.3/5.7/5.8/5.9 are
#38/#39/#40/#41/#43/#42 and the Hub half is WorldShaper-Hub#5 and #6.

### What it is

Once a session can be recorded as input + seed (#38), the same recording can be replayed against a *different
build* and the resulting world state compared against the original. A green run means the build did not change
observable behaviour; a red one names the first divergence.

### Why it is worth doing right after 5.1

It is the cheapest possible consumer of deterministic replay, and it is what turns replay from a debugging toy
into infrastructure. It also gives the engine a regression suite made of **real sessions** rather than
hand-written scenes, which is exactly the class of coverage the check suite cannot produce.

### Done when

- A recorded session replays against a build it was not recorded on, and reports either "identical" or the
  first frame and field where the two diverge.
- The comparison is on **world state**, not on rendered pixels — pixels differ for legitimate reasons and
  would make the whole thing untrustworthy.
- Divergence output names *what* differed (entity, component, field), not just *that* something did.
- Runnable from `gws` so CI can drive it.

### Depends on

#38 (record sessions as input + seed). Do not start before that lands — the recording format is the input to
this and guessing at it will need redoing.

### Trap worth knowing before starting

This project has twice shipped a benchmark that measured its own absence: a probe scene with no mesh colliders
reported play-mode entry at 7 ms while a real project paid 609 ms. A replay corpus of trivial sessions will do
the same thing. The first recordings should come from real use, not from a synthetic scene built to be easy to
replay.
ISSUE_BODY_EOF
  )"

gh issue create --repo "$REPO" \
  --title "terrainmat_check is red on main (UBO size assertion)" \
  --label "bug,runtime" \
  --body "$(cat <<'ISSUE_BODY_EOF'
`terrainmat_check` fails **1 of 35** assertions and has done for some time. It is one of two checks
deliberately excluded from CI by name (see `.github/workflows/ci.yml`) so that adding the other 31 orphaned
checks did not turn CI red for faults that predate the change.

### Symptom

```
FAIL block is 144 bytes (vec4 + vec4[4] + vec4[4])
34 passed, 1 failed
```

### Why it matters

The assertion is about the terrain material UBO's size matching what the shader declares. A C++ struct and a
GLSL uniform block that disagree do not error — they misread each other's fields, so tint, tiling and
metal/rough values land in the wrong slots. That is a silent visual fault, which is precisely the kind this
check exists to catch.

### Where to start

`tools/terrainmat_check/main.cpp` for the expectation, `tools/gbuffer_terrain.frag` for the declared block,
and the C++ struct that is memcpy'd into it. Establish which of the two is wrong before changing either — the
check may be asserting a stale expectation rather than catching a real drift.

### Done when

The check passes, the fix names which side was wrong, and `terrainmat_check` is added back to the CI target
list in `.github/workflows/ci.yml`.
ISSUE_BODY_EOF
  )"

gh issue create --repo "$REPO" \
  --title "shadersource_check is red on main: 3 shaders no longer compile to the SPIR-V that ships" \
  --label "bug,runtime" \
  --body "$(cat <<'ISSUE_BODY_EOF'
`shadersource_check` fails **3 of 9**: `gbuffer_scene.vert`, `gbuffer_scene.frag` and
`shadow_caster.frag` no longer compile to the SPIR-V embedded in the shipped headers.

This is the second of the two checks excluded from CI by name, for the same reason as terrainmat_check.

### Why this is worse than it sounds

The engine ships **precompiled SPIR-V** — editing a `.frag` changes nothing until the header is regenerated,
which is a **manual** step (`tools/regen_*_spirv.sh`, not wired into the build). So for these three shaders,
what runs on the GPU is not what the source says, and anyone reading the source to understand or modify them
is reading a file that is not in the build.

### Related

#67 covers the broader problem — shader GLSL split between inline `.cpp` strings and a partly-stale
`shaders/` directory. This issue is the narrower, immediately actionable half: three specific shaders whose
shipped binary has drifted from source.

### Where to start

Run `./build/<preset>/bin/shadersource_check.exe` for the list. Regenerate with the matching
`tools/regen_*_spirv.sh`, which rebuilds RT and non-RT variants **together** — regenerating only one is how
they drift apart, and the script's own header explains why.

**Before regenerating, diff the compiled output against the shipped bytes and understand what changed.** A
regen that silently alters shading is worse than the drift: the check goes green and the picture changes.

### Done when

The check passes, the diff is understood rather than merely resolved, and `shadersource_check` is back in the
CI target list. Consider wiring regeneration into the build so this cannot drift again.
ISSUE_BODY_EOF
  )"

gh issue create --repo "$REPO" \
  --title "Unattributed SIGSEGV on a worker thread (v0.8.2, reproducible)" \
  --label "bug,critical,editor" \
  --body "$(cat <<'ISSUE_BODY_EOF'
Two crashes 58 seconds apart while testing v0.8.2, with **byte-identical stacks** — reproducible, not a
one-off. Full write-up with all evidence: `docs/EngineMasterPlan/OPEN_CRASH_20260830.md`.

| | |
|---|---|
| App | editor 0.8.2 |
| Reason | SIGSEGV |
| GPU | NVIDIA GeForce RTX 3060, driver 560.376.0, Vulkan 1.3.280 |
| Frame health | 240 frames, mean 16.53 ms, worst 18.32 ms — no stall, no hitch |

### What is known

- The crashing thread is a **worker**, not the render thread (frames sit under `BaseThreadInitThunk`).
- An editor-extension **reload** was logged 29 s before the fault, and the project held four identical copies
  of the starter template — four live pocketpy VMs registering the same command titles.
- Every symbol resolves to `pkpy_new_vm`, which is **nearest-exported-symbol noise** in a stripped MinGW
  binary and should not be read as a pocketpy attribution.

### What has been ruled out

`extension_check` gained a group reproducing that exact configuration — four duplicates, five reload cycles,
invoke-by-shared-title, deleting one. All pass. The loader's bookkeeping is not the fault.

### The step that would settle it

The report prints it:

```
addr2line -f -C -e editor.exe 0x2E1C20 0x19DC 0x9184CB 0x1157
```

against the unstripped build from `engine-v0.8.2-win64-symbols.tar.gz`, published on the release. Everything
recorded so far is circumstantial; this is not. **Run it first** rather than reasoning from symbol names.

### Reproduction lead

Four copies of the starter extension were present. v0.8.4 changed "New Extension…" to stop creating numbered
duplicates, so the configuration is now harder to reach by accident — but that is not a fix, and the crash
should be assumed live until symbolized.
ISSUE_BODY_EOF
  )"

gh issue create --repo "$REPO" \
  --title "Present semaphores are reused before the swapchain releases them (validation warning every run)" \
  --label "bug,runtime" \
  --body "$(cat <<'ISSUE_BODY_EOF'
Every editor run with validation enabled logs this from the first frames:

```
vkQueueSubmit(): pSubmits[0].pSignalSemaphores[0] (VkSemaphore ...) is being signaled by VkQueue ...,
but it may still be in use by VkSwapchainKHR ...
Swapchain image 1 was presented but was not re-acquired, so VkSemaphore ... may still be in use and
cannot be safely reused with image index 2.
```

Found on an RTX 3060 while verifying PR #70. It **predates that merge**; #70 touched no semaphore code.

### Cause

`editor/src/main.cpp` signals `render_sems[current_frame]`, one render-finished semaphore per **frame in
flight**, and hands it to present. The presentation engine holds that semaphore until the *image* is
re-acquired, not until the frame's fence signals. With 3 swapchain images, a semaphore can be signalled again
while presentation may still be waiting on it.

### Why it matters

It is a real synchronisation hazard, not a noisy warning. The spec gives no guarantee the semaphore is free,
and what happens is up to the driver: nothing on most, sporadic corruption or device loss on others. That is
the worst kind of bug to meet on a user's machine rather than a developer's.

### Fix

The standard one, per https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html: one render-finished
semaphore **per swapchain image**, indexed by the acquired image index. Acquire semaphores stay per frame in
flight. Recreate the per-image set whenever the swapchain is recreated, because the image count can change.

### Done when

A validation-enabled `editor --frames 300` logs no swapchain-semaphore warnings, including across a window
resize.
ISSUE_BODY_EOF
  )"

echo "=== posting 2 status comment(s) ==="

gh issue comment 61 --repo "$REPO" --body "$(cat <<'COMMENT_EOF'
Status after PR #70 (merged 2026-09-12, ships in v0.8.5).

**Done and verified**
- The editor, `gws`, `dedicated_server` and the whole check suite build on Linux. CI runs them headlessly on Ubuntu 24.04 on every push, next to the Windows job.
- The editor has run on Intel Iris Xe under Mesa.
- Per-user files (settings, pipeline cache, crash reports) go to XDG directories instead of the working directory (`userdirs_check`).

**Remaining**
- [ ] C++ script host: returns nullptr on non-Windows
- [ ] C# script host: loads hostfxr.dll from Program Files
- [ ] Embedded terminal: a native PTY is in review in #71
- [ ] Crash reports carry no stack trace or minidump on Linux
- [ ] A Linux release package (the release workflow builds win64 only), and Hub support for it
- [ ] The headless dedicated server as a deployable artifact: it builds, but nothing packages it
- [ ] Verification on NVIDIA's proprietary driver and on AMD
COMMENT_EOF
  )"

gh issue comment 63 --repo "$REPO" --body "$(cat <<'COMMENT_EOF'
This just bit. PR #70 made Vulkan instance creation take its surface extensions from GLFW, which returns nothing when GLFW is not initialised. `texture_check`, `skinning_check` and `model_check` create a headless device and never initialise GLFW, so all three failed on any machine with a GPU, while both CI jobs stayed green because runners have no GPU and the checks skip. It was found locally with `gws test --gpu` and fixed in v0.8.5.

Until this lands, the cheapest mitigation is procedural: run `gws test --gpu` locally before merging anything that touches the renderer or device setup. The README now says so.
COMMENT_EOF
  )"

fi

if [ "$DO_CLOSE" = "1" ]; then
echo "=== closing 4 shipped Phase 4 issue(s) ==="

gh issue close 29 --repo "$REPO" --comment "$(cat <<'CLOSE_EOF'
Phase 4 is complete — 10 of 10 items, shipped through v0.8.1. Every authoring surface exists and is reachable from a menu, and as of item 4.10 every authoring document persists as well. Closing this epic; see `docs/EngineMasterPlan/WORKFLOW_PLAN.md` §Phase 4 for the per-item record.
CLOSE_EOF
  )"

gh issue close 32 --repo "$REPO" --comment "$(cat <<'CLOSE_EOF'
Done in v0.7.0 as workflow-plan item 4.3. Worth noting the item was wrong about its own shape: a VFX effect is an ordered STACK of modules per stage, not a node graph, so it is drawn as a stack panel. `vfxgraph_check` (34) covers it. Details in the plan's 4.3 row.
CLOSE_EOF
  )"

gh issue close 36 --repo "$REPO" --comment "$(cat <<'CLOSE_EOF'
Done in v0.8.1 as workflow-plan item 4.8. Scripts in `<project>/editor_scripts/` register commands into the palette and can run any editor command by name, in all three backends; `Window > Extensions` lists them. `extension_check` (63). Details in the plan's 4.8 row.
CLOSE_EOF
  )"

gh issue close 69 --repo "$REPO" --comment "$(cat <<'CLOSE_EOF'
Done in v0.8.0 as workflow-plan item 4.9. Note the item's premise was wrong — it said this rode an existing Stage-6 bus graph and there was none, so the routing layer had to be built first. `audiobus_check` (33). Details in the plan's 4.9 row.
CLOSE_EOF
  )"

fi

echo "done"
