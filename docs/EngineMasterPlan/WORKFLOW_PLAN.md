# Linear Workflow Plan — what to do, in order

**Created:** 2026-08-09
**Source of truth for sequencing.** Where the other plans describe *what* each system is, this describes
*when* to build it and *why that position*.

**Inputs:** [`AAA_ENGINE_MASTER_PLAN.md`](AAA_ENGINE_MASTER_PLAN.md) (runtime, 16 stages) ·
[`DEVELOPER_EXPERIENCE.md`](DEVELOPER_EXPERIENCE.md) (tool) ·
[`PRODUCT_AND_ECOSYSTEM.md`](PRODUCT_AND_ECOSYSTEM.md) (product) ·
[`REMAINING_WORK.md`](REMAINING_WORK.md) (verified state + repo health) ·
[`ENGINE_FEATURE_CATALOG.md`](ENGINE_FEATURE_CATALOG.md) (module catalog)

**Tracked on GitHub** as milestones `Phase 0` … `Phase 9` across three repositories:
`orgolis/c-Engine-Game` (engine + editor), `orgolis/WorldShaper-Hub` (launcher/product),
`orgolis/WorldShaper-Samples` (templates + sample games).

---

## How to read this

Phases are **linear** — each one is ordered after the last because of a real dependency or because it
multiplies the value of everything after it. Inside a phase, order is looser.

**Importance:**
🔴 **Critical** — blocks other work or is actively costing something today
🟠 **High** — large leverage; delaying it makes later work more expensive
🟡 **Medium** — real value, safely deferrable
⚪ **Later** — correct to postpone; listed so it is not forgotten

**The recurring principle:** *finish before you start*. Phases 0–3 are almost entirely completing things
that already exist. That is where the cheapest value is.

---

## Phase 0 · Unblock — 🔴 Critical · days

Nothing else should start first. Every item is hours of work and each one is costing something now.

| # | Item | Repo | Why now |
|---|---|---|---|
| ~~0.1~~ | ✅ **DONE 2026-08-09** — declare the four missing submodules, **plus three further clean-clone blockers found by testing**: ufbx build glue living inside the submodule, the tinyusdz flag patch existing only in a working tree, and zstd's whole source tree hidden by an unanchored `lib/` ignore rule (issue #2, commits `c1ac242` + `6e2266d`) | engine | Verified by a fresh `--recurse-submodules` clone: 10/10 submodules populate, configure exits 0, `zstd` + `ufbx` + `tinyusdz_static` all build |
| ~~0.2~~ | ✅ **DONE 2026-08-09** — old `main` (the abandoned OpenGL lineage, **unrelated history**, no merge base) archived as `legacy-opengl-2026-04`; working branch promoted to `main`; default re-pointed (issue #3) | engine | A bare `git clone` now lands on the real engine, and `blob/main/...` doc links resolve (were 404) |
| ~~0.3~~ | ✅ **DONE 2026-08-09** — CI on every PR and push to `main` (issue #4). First green run: **18 passed, 0 failed, 3 skipped**; found a latent GCC 14+ `<cstdint>` bug in tinyusdz on the way | engine | The 3 skipped need a GPU — compiled but not run, tracked by #63 |
| ~~0.4~~ | ✅ **DONE 2026-08-09** — releases carry `engine-<tag>-win64-symbols.zip` with a BUILDINFO (issue #5); first exercised by **v0.2.0** | engine | Shipped package size is unchanged: `-g` goes to the build tree while CPack still strips the packaged copy |

**Exit criteria:** a fresh `git clone --recurse-submodules` configures and builds; CI is green; a crash report
from a released build can be symbolized.

---

## Phase 1 · Automation foundation — 🟠 High · 1–2 weeks

Everything in later phases assumes these exist. The CLI in particular is the single highest-leverage item
in this document.

| # | Item | Repo | Notes |
|---|---|---|---|
| ~~1.1~~ | ✅ **DONE** — `gws` (#7): version · test · cook · validate · docs · project · run · build. CI now drives the suite through it | engine | `screenshot` remains (#64) — it needs a Vulkan device |
| ~~1.2~~ | ✅ **DONE** — `gws project` (#8): list/add/remove/set over gameplay state, addressed by stable SaveId | engine | Operates on files, so every edit is a diff in version control. `set` is limited to reflected components |
| ~~1.3~~ | ✅ **DONE** — 42 components + 53 script verbs (#9, #10) in `docs/reference/` | engine | Components walked from the registry; script verbs parsed from the header, which has no runtime metadata |
| ~~1.4~~ | ✅ **DONE** — `gws crash` (#11) | engine | Signature = top 5 **non-system** frames, module+RVA not absolute addresses (ASLR). Engine never files at crash time: no credentials in a shipped game |
| ~~1.5~~ | ✅ **DONE** — Hub #2: LFS configured before the first commit, engine text formats kept as text | hub | Never fatal without git; 15 assertions incl. "LFS rules are in the first commit" |

**Exit criteria:** CI runs the full check suite through the CLI ✅ · a new project from the Hub is a correctly
configured Git repo ✅ · API docs regenerate from source ✅ — **all met (2026-08-09)**.

**Phase 1 status: 9 of 10 done.** Remaining: `gws screenshot` (#64) and the three GPU-requiring checks
(#63) — both blocked on a Vulkan device, which standard CI runners do not have. Shipped in **v0.3.0**.

---

## Phase 2 · Inner loop — 🟠 High · 2–4 weeks

The wait between a change and seeing it is most of what a developer will remember about this engine.
Protect it before the project is large enough to make it slow.

| # | Item | Repo | Notes |
|---|---|---|---|
| 2.1 | ~~**`innerloop_check` + published budgets + CI gate**~~ | engine | ✅ **Done** (`cc4e2f7`). CI publishes every metric to the run summary each build and gates at **4× budget** — a shared runner cannot hold a 1× wall-clock line, and a gate that cries wolf gets ignored. Gate verified to actually fire (`--tolerance 0.001` → exit 1). **editor cold start is no longer unmeasured**: `--startup-probe` makes it real (798 ms / 5000) wherever a GPU exists. 4 measured, 3 unmeasured |
| 2.2 | ~~Precompile every shader pipeline~~ | engine | ✅ **Already true — this item was written in error.** `GWS_HAS_GLSLANG` is defined nowhere, so every one of the 17 `compile_glsl` sites already falls back to precompiled SPIR-V, and all of them run at pass-creation, not first draw. Measured: **1.2 ms of a ~830 ms cold start (0.1%)**. The item imported Unreal's documented PSO problem without checking whether it applies here — it does not. The one real defect found was **14 false `[error]` lines per startup**; fixed in `2db10a7`, now one accurate info line and 0.0 ms |
| 2.3 | ~~"PSO compiled during gameplay" counter~~ | engine | ✅ **Dropped with 2.2** — a counter that is structurally always zero is noise. Reopen only if a frame capture shows real pipeline-creation hitches |
| 2.4 | ~~**Hot reload everything else**~~ | engine | ✅ **Done.** One shared `AssetWatcher` replaced three ad-hoc mtime loops and **settles** before firing, so a file still being written is never read half-finished — a latent bug all three copies had. Textures, meshes, **shaders** (2 → 11, via a disk `.spv` override + `gws shaders`) and **scenes** all reload. Scene reload is deliberately *not* automatic when there are unsaved changes: it would discard them, so it asks, and discarding takes an explicit click. Verified end to end by editing a scene file from outside a running editor |
| 2.5 | ~~**Keep changes made while playing**~~ | engine | ✅ **Done** (`31eb938`). Diff on Stop + per-row keep. Built so `ScenePlaybackManager` is untouched — Capture → Diff (before Stop) → existing restore → Apply only ticked rows, so "discard all" is byte-for-byte the old behaviour and writing to the authored scene is opt-in. Generic over the authorable registry, so new components are diffable for free. Spawned/destroyed entities are out of scope and **reported as counts** rather than hidden. `playchanges_check`: 26 assertions, headless |
| 2.6 | ~~Never block the editor thread~~ | engine | ✅ **Done for the interactive paths.** `gws_jobs` was the wrong tool — fork-join, so a task outliving hundreds of frames either blocks the caller or starves frame work. `TaskRunner` owns a small pool, and **completion callbacks run on the editor thread** so a finished import may legally touch the GPU. Drag-drop import and **both OBJ and glTF parsing** run on workers with progress + cancel; a cancelled import deletes its partial file. Shader compilation is moot (nothing compiles GLSL at runtime). Navmesh bake stays synchronous — it is not on the interactive path |
| 2.7 | ~~Undo covering gameplay components, terrain, logic graph, script/agent edits~~ | engine | ✅ **Done.** Was 12 create commands + rename — and the creates were **wrong**, undoing via `GetEntityByName` so they deleted the wrong entity when two shared a name. Now covers create/delete/rename, inspector fields, gizmo drags, component add/remove, **reparenting, terrain sculpt, logic-graph edits and keeping play-mode changes** — one entry per gesture. Terrain stores only the changed rectangle (a full heightmap per stroke would be megabytes); the logic graph stores the whole thing (a few nodes). Same problem, different right answer |

**Exit criteria:** the budget table in `DEVELOPER_EXPERIENCE.md` §1.1 is measured, met, and enforced in CI.

✅ **MET.** All seven rows are measured. The four that used to read "needs a running editor" now do, because the
editor drives itself (`--startup-probe`, `--probe-inner-loop`). Every row is inside budget, and CI publishes them
on each build while gating at 4× — a shared runner cannot hold a 1× wall-clock line, and a gate that cries wolf
gets ignored.

| metric | measured | budget |
|---|---|---|
| cli_startup | 37.5 ms | 300 |
| sim_1s_of_gameplay | 1.6 ms | 16.6 |
| single_check | 120.8 ms | 5000 |
| editor_cold_start | 865 ms | 5000 |
| play_mode_entry | 7.0 ms probe · **609 ms measured in a real project** ⚠️ | 1000 |
| asset_to_viewport | 7.4 ms (OBJ) · 12.0 ms (glTF) | 2000 |
| shader_to_screen | 1064 ms | 2000 |

> **⚠️ `play_mode_entry` was green and wrong (found 2026-08-23).** The probe scene has no mesh colliders, so it
> never touched the path that costs anything. In a real project, entering play mode ran `BuildPhysicsWorld`, which
> re-parsed every mesh collider's OBJ off disk on the main thread — 609 ms predicted from the project's own assets
> (Porsche 341 ms, building_04 238 ms, a tree mesh 5 ms paid by three colliders separately), against observed
> stalls of 591, 596, 605 and 566 ms.
>
> It hid for six releases because `BeginPlayMode` is called from inside `ShowViewport`, so the whole cost was
> billed to a profile zone named `ui_viewport` and read as a UI problem while a GPU freeze was being hunted.
> Fixed by taking triangles from the already-loaded `MeshAsset` and memoising per path; `BuildPhysicsWorld` now
> logs where each collider's triangles came from, because "it got faster" and "it stopped reading the disk" are
> different claims.
>
> **The lesson is about the probe, not the bug.** A synthetic scene measured a path the real one does not take,
> and the budget table said 7 ms for something that cost 609. A probe scene must contain the thing that is
> expensive, or the metric is measuring its own absence.

> **⚠️ The diagnostics were lying in two ways (found and fixed 2026-08-24, v0.7.1).** Both were discovered while
> auditing the freeze evidence, and both had been quietly degrading every investigation before it.
>
> **The log corrupted itself whenever two editors ran.** `rotating_file_sink_mt` is thread-safe, not *process*-safe,
> and sat at one fixed path — two instances each held their own handle and mutex. Four torn lines are visible in the
> shipped `editor.log`, one of them mid-timestamp; past the 5 MB rotation threshold it stops being cosmetic and
> starts *deleting* lines, because one process renames the file while the other keeps writing into the renamed copy.
> The cost was real: a session where two editors ran at once — one throttled to 20 fps in the background — merged
> into what read as a single session stalling **492 times**, and the GPU-contention artefacts looked exactly like the
> bug being hunted. Now one process per file, claimed with a named mutex (released by the OS on death, so a crash
> never leaves a stale claim); the second instance logs to `editor-<pid>.log` and says so.
>
> **The hang watchdog cried wolf.** All three hang reports on the reporting machine showed the main thread in
> `NtUserWaitMessage` — *idle*, waiting for input — and one showed the full chain
> `NtUserWaitMessage ← IsDialogMessageA ← DialogBoxIndirectParamW ← comdlg32`: a file dialog sitting open. Each wrote
> an 8–9 MB minidump and a report titled HANG. The watchdog now checks the innermost frame first and logs one line
> instead. A genuine freeze parks in a fence wait, a lock or a spin, and still reports.
>
> `crash_check` grew three assertions that spawn two concurrent loggers and assert two files, zero torn writes, and
> no mixing — **verified to fail with the fix disabled**, because a regression test that passes either way proves
> nothing.

> **⚠️ Two lighting defaults that made the renderer impossible to judge (fixed 2026-08-24, v0.7.2–v0.7.3).**
>
> **Every metal reflected white.** F0 for a metal is its own base colour; both SSR shaders hard-coded `vec3(1.0)`,
> and the coloured Fresnel was collapsed to a scalar before storage, so the hue was discarded even where it
> survived. Fixed in both variants together — had only the ray-traced one carried colour, ticking *Ray-traced*
> would have changed what metals **look like** as well as how accurate the reflections are.
>
> **Nothing could ever look dark.** `global_ambient` was **1.0** against a `0.3` ambient colour, so every surface
> received 0.3 × albedo with no light source in the scene, and a further 40% of the ambient term came from IBL
> whose intensity had **no UI at all**. Turning ambient down never reached darkness, because the sky half kept
> shining. One slider now drives both halves and 0 means 0.
>
> **Shadows sat wrong on terrain**, and the bug was in the seam between two individually correct decisions. The
> shadow pass draws one LOD tier coarser than the G-buffer — a real and deliberate saving, since a silhouette
> needs less detail than a shaded surface, and it dates from April. Terrain then gained LOD tiers (P2), where a
> tier is not a decimated copy but a **different heightfield** with peaks cut and hollows filled. From that point
> the shadow map held depth for a surface that was not the one being shaded: false shadow where the coarse tier
> rose above the fine one, light leaking where it fell below. Neither change was wrong on its own. Terrain now
> draws the tier it renders at; `shadow_lod_for()` states the rule in one place, and 10 assertions fail without it.

> **⚠️ The terrain shadow bug was in the ray-query path all along (v0.7.4).** The v0.7.3 fix — terrain drawing the
> same LOD tier for shadows as for shading — was correct, and changed nothing, because `directionalShadow()`
> **returns before the shadow map is ever sampled** when ray-traced shadows are on. RT and shadow-map shadows are
> either/or, not blended, and the reporting machine had `Ray tracing toggle ON` in every session. The fix was
> landing on a code path that did not execute.
>
> The real cause was the ray origin: `worldPos + normal * 0.01`. One centimetre, on terrain whose triangles are
> metres across. At a grazing sun angle the shadow ray re-enters the triangle it just left, and float precision
> hundreds of units from the origin is itself coarser than the offset — so broad, slope-correlated regions
> reported as occluded. That is the contour-following blotching seen on the ground. It became visible when
> **v0.6.16 fixed the `shaderInt64` UB** that had made the whole ray-query path undefined: once RT shadows
> genuinely worked, they started self-intersecting. The offset now scales with both surface slope and distance
> from the origin.
>
> **Ray-traced shadows had no control of any kind** — `set_rt_enabled(true)` ran at startup for any capable GPU,
> with no checkbox and not even an environment variable. With two entirely separate shadow paths and no way to
> switch, there was no way to tell which one an artefact belonged to. There is a toggle now.
>
> **And ray-query shadows are binary** — one ray, `1.0 : 0.0` — so with RT on there was no penumbra whatsoever: a
> shadow's edge was exactly as dark as its centre. Two controls now shape that. *Light scattering* widens the PCF
> radius on the shadow-map path and samples a cone the width of the sun's disc on the ray-query path, which is
> where the gradient actually comes from. *Light persistence* lifts the umbra, since with ambient correctly low
> there is nothing left to keep a fully occluded point from being pure black.
>
> Both parameters had to fit a push block already **exactly at Vulkan's guaranteed 128-byte minimum**. Three
> booleans occupying three whole floats were packed into one bit field to make room, rather than assuming a
> device offers more than the spec promises — the same assumption that once shipped a build unable to start on an
> RX 5700. `static_assert`s pin the size and every changed offset, checked against `spirv-dis`.

> **⚠️ The terrain shadow bug, actually found (v0.7.5) — and the two wrong answers before it.** The acceleration
> structure contained **every LOD tier of every mesh, superimposed**. `ensure_blas` handed the builder
> `mesh->index_count()`, the whole index buffer — but an index buffer holds all tiers back to back, and each
> coarser tier is the same surface again at lower density, in the same space.
>
> On terrain that is not subtle: a coarser tier samples the heightmap at a wider step, cutting peaks off and
> filling hollows in, so its triangles sit metres above the visible ground in places. Shadow rays hit them and
> the ground darkens in broad, slope-following patches. **Measured before the fix: every terrain chunk's BLAS
> carried 1.4× its visible geometry** (35,520 indices against 26,112 in LOD 0).
>
> That measurement is also why the two earlier attempts failed, and both failures were informative. The v0.7.3
> LOD fix was correct but landed on the shadow-map path, which ray-traced shadows bypass entirely. The v0.7.4
> ray-epsilon fix assumed self-intersection — but the occluder was never the surface the ray started on, so no
> offset could ever have been large enough. **The symptom said "shadow", the cause was in the geometry.**
>
> Now one BLAS geometry per submesh, using that submesh's LOD 0 range. Regular meshes had the same defect; it was
> merely less visible, because a decimated LOD hugs the surface it came from while a terrain tier does not.

Two items were **retired by measurement rather than implemented** (2.2, 2.3). The engine has always shipped on
precompiled SPIR-V, so the PSO-stutter work — inherited from Unreal's well-documented problem — was addressing
something this engine does not have.

---

## Phase 3 · Collect the ceilings already paid for — ✅ COMPLETE (all 10 items)

Every item here finishes something already built and verified. Highest value per hour in the whole plan.

| # | Item | Repo | Notes |
|---|---|---|---|
| 3.1 | ~~**Adopt `parallel_for`** in culling, LOD and animation sampling~~ | engine | ✅ **Done — and the item was two-thirds wrong** (`258031e`). **Culling** was genuinely serial and is now parallel above 2048 items (the test fans out; the compaction stays serial because draw order must remain stable). **LOD** is not applicable: `select_lod` runs *inside* the command-buffer recording loop, which is serial by construction, and is a few comparisons over a usually-one-element list. **Animation sampling does not run at all** — no `Animator` is ever instantiated and `SkinnedMeshComponent` does not exist, so this is blocked on 3.8, not on threading. Draw collection and AABB computation, which the item did not mention, were already parallel. Threshold measured by `cullbench_check`, which also asserts both paths agree on what is visible |
| 3.2 | **Enable the GPU-driven indirect path** | engine | ✅ **Done** (`7581358`). **Measured, one run split in half so the visibility set is identical:** indirect **2 draw calls / 734 triangles** vs direct **10 / 734** — same geometry, one fifth the submissions (`--probe-indirect`). **I had this item wrong.** I recorded it as *"cannot be completed: needs a shared/pooled vertex+index buffer redesign first"* and recommended dropping it. Indirect draws read the **currently bound** buffers, and `VkDrawIndexedIndirectCommand` is a 1:1 mapping of the arguments `vkCmdDrawIndexed` already takes — per-mesh multi-draw needs **no pooling at all**. Pooling is what a *whole-scene* single call would need. `multiDrawIndirect` is requested and checked (drawCount > 1 without it is **undefined behaviour**, not a validation error); without it the path still runs unbatched. The buffer declares STORAGE usage so a compute pass can zero `instanceCount` for culled draws — **that** is GPU-driven culling, and it is unbuildable while draw args are only function parameters |
| 3.3 | ~~**World-streaming integration**~~ | engine | ✅ **Done** (`1c12b74`). Camera-driven cell residency + floating-origin rebase on the live scene. A cell's content is the entities inside it (activate/deactivate) — genuine residency behaviour without inventing a partitioned content format that would be thrown away later. **The rebase contract is honoured explicitly**: `update()` *returns* the shift rather than moving the camera itself, because the editor owns the camera — entities-but-not-camera makes the world teleport, camera-but-not-entities makes the camera fly away. Roots only; children inherit. Off by default with a Tools toggle, and switching off restores everything | **Rebase-contract audit (`7c7de18`/`ef6ae20`/`6035815`): three real bugs in the seams.** Streaming, agents, physics and multiplayer were each verified alone; a rebase moves every entity, and three subsystems cached world state that did not follow. **(1) Navmesh + agents** — and the symptom is not a failed path, which is why it survived: `find_path` snaps off-mesh endpoints, so the agent gets a confident straight line and **the navmesh silently stops constraining it**. **(2) Physics** — Jolt stayed at the old origin and its write-back snapped the dynamic half of the level back while the static half moved; plus the restore snapshot, water volumes and follow camera. The player is a `CharacterVirtual`, not a body, so it is left behind on its own. **(3) Multiplayer** — peers rebase on their own cameras, so they stop sharing a coordinate frame; remote players drift apart as the session runs. `navrebase_check` + `physrebase_check` + groups in `npcagent_check`/`mp_check`
| 3.4 | ~~**AI: bake from real geometry**~~ | engine | ✅ **Done** (`252bd5d`). Terrain heightmap cells (holes skipped — a hole must be a hole for navigation, or agents path across a cave mouth) plus the **top face** of box colliders (emitting the sides would let agents path up walls). Curved and mesh colliders are **counted as skipped**, because a navmesh that silently omits geometry does not error — agents just walk through the world. `Tools → Bake Navmesh`. `navbake_check`: 19 assertions, all about silent failures |
| 3.5 | ~~AI perception + NPC behaviour trees + combat AI~~ | engine | ✅ **Done** (`1863cea`, `dfc025b`) — **pulled forward** because 3.4 and 3.8 were finishing as green rows nothing consumed. Tree is `Selector[ attack, chase, patrol ]`, so an agent in range swings rather than closing, and a cooldown makes the attack branch fail so it falls through to chase instead of freezing. **The AI decides WHEN, the ability data decides WHAT** — activation goes through the ECS ability system, so cost/cooldown/tags stay out of the agent. The assertion that proves it: a one-second cooldown fires **once** in half a second, not sixty times. `npcagent_check`: 20 assertions, mostly negatives — "it attacked" is easy, "it attacked only when it should" is the property |
| 3.6 | **ECS authority flip** — gizmo/inspector/save write ECS; retire the OOP shadow | engine | ✅ **Done** (`71ff298` + `8b85dae` + `e7497c1`). **Scope agreed with the user: full parity + authority, not full retirement** — the OOP scene stays the authoring layer. (1) An entity tagged `EcsAuthoritative` syncs **ECS → OOP**, so an ECS write survives the frame. (2) First real consumer: `drive_vehicle` claims authority, so a scripted vehicle **actually moves** — before, the speed climbed and the car did not budge. (3) **All 13 component kinds** now cross the bridge; only 4 did. My "5 missing types" estimate was wrong twice: Audio already existed (never populated, like Light, Collider, RigidBody, Skeleton, AnimationState, Material), and the real gap was **9 of 13** having no path at all. **One-way by design:** Terrain (heightfield is megabytes with its own tools) and Script (carries a path *hash*, which cannot be reversed). `componentsync_check` (21) asserts values not presence — a bridge adding default-constructed components looks wired and reads zero |
| 3.7 | Editor inspector + scene save onto core reflection/serialization | engine | ✅ **Done for the reflected components** (`4f719e6` + `2394279` + `3b1eb32`) — inspector *and* save, off one source. Doing it surfaced **data loss**: particle emitters and NPC agents were **never written to the scene file at all** — configure one, save, reopen, and every setting was silently back at its default. Both had been reflected specifically so that could not happen; the reflection was never connected to the serializer. The recorded blocker (**binary core vs text scene format**) is **resolved in favour of text** — that is the whole reason the format is text, since scenes live in version control and a binary blob makes every edit an unreviewable diff (#58). What the two share instead is the *reflection metadata*, so the text format is now generated from it: adding a field went from **four hand edits with a silent failure mode to zero**. Old files still load — `apply_reflected_line` declines lines it does not own rather than consuming them. `reflectio_check` (12) + `sceneio_check` (13); the scene format had **no round-trip test at all**, which is how this got in. The inspector half had the **same** gap as the save half: emitters and agents had **no inspector UI at all** — they existed, ran every frame, and could only be configured from a script. Both halves now draw from the same reflection, so a new field appears in the inspector *and* the file with no edit in either. **Left:** the older components whose private members offset reflection cannot reach — and those are the ones 3.6 supersedes |
| 3.8 | ~~**`SkinnedMeshComponent`** + drive the player character~~ | engine | ✅ **Done** (`d5f0348`, `72cf9c0`). A rigged character is an entity: data-only component + actors cached by **entity id**, so two entities using one character animate independently. **Locomotion drives the clip** — idle/walk chosen from how fast the transform is actually moving, which connects 3.5's walking agents to their walk cycle without either system knowing the other exists. The rule is an ImGui-free header with 13 assertions because its failure mode is purely visual: hysteresis (a character hovering at the threshold is the *common* case), vertical motion is not walking, and a floating-origin rebase must not read as a sprint. **Left:** instancing — mesh data is duplicated per instance |
| 3.9 | VFX GPU billboard draw | engine | 🟡 **Simulation half done** (`c50968e`). `ParticleEmitterComponent` makes an entity an emitter; simulations run per entity, seeded from the entity id, and honour the floating-origin rebase — particles are world-space, so missing that would silently walk an emitter away from its own smoke. **The GPU draw is not done and is not claimed**: it needs a dedicated pipeline (BillboardVertex has a per-particle colour, SceneVertex has no colour channel, and dropping the fade-over-life would gut the feature), and a graphics pipeline written without being able to see its output can pass validation and draw nothing. `vertex_count()` gives that work a verifiable input. ✅ **GPU pass done** (`1b63d7d`): particles are on screen. Sits **after** the transparent composite, never between Lighting and Transparent (WBOIT owns its own targets — a recorded trap). Depth **test** on, **write** off. **The bug a "did it draw?" check misses:** my first version drew the vertices as a triangle list, but `build_billboards` emits **four corners per particle with no indices**, so it stitched triangles across *different* particles — validation-clean, healthy counts, garbage on screen. The log arithmetic gave it away (multiples of 4, not 6). Now indexed. **Pixels still need a human** |
| 3.10 | ~~**Remove or salvage ~7,000 lines of dead pre-ECS code**~~ | engine | ✅ **Done — 5,838 lines** (`73c7544`, `b4af8f3`). Sweep 1: the `gpu/vulkan/` Phase-6 editor/debug cluster (3,182), two of which **shadowed live headers of the same name** — a hazard, not clutter. Sweep 2, scanning **every** directory including `tests/` after the first missed one: `simple_renderer`, `viewport_renderer_3d`, `constraints_new`, `material_asset`, `texture_loader` (2,656). **Kept deliberately:** `player_entity.cpp` (uncompiled but play mode uses its header) and the module aggregator headers — unused is not unwanted. **The item's premise was wrong about the rest:** `engine/core/{gameplay,ability,character}` (~5,800 lines) are **live** — play mode, network input sync and two editor panels depend on them. "Salvage into G1" is a *migration* that belongs to 3.6, not a deletion |

**Exit criteria:** no verified library is unreferenced by a running scene; the engine renders an animated
character walking a streamed world with an AI agent pathing on baked geometry.

**✅ MET — run, not inferred** (`c130ccb`). `phase3exit_check` drives one scene through the whole sentence at
once: terrain in → navmesh baked from that actual geometry → an agent walking it → locomotion driven by the
agent's real motion → streaming following a moving camera until it forces an origin rebase → and everything
still working afterwards. Measured: **2048 navmesh triangles** from real terrain (0 shapes skipped), **32 live
cells**, **1 rebase of (-896, -448)**, entities streamed out and back. Every clause already had its own passing
check; none covered the sentence, and this phase's recurring lesson is that the bugs live *between* the parts —
three of them did, in the rebase audit alone.

**Two items are explicitly deferred rather than done**, and the phase closes with them named:
- **3.2 GPU-driven indirect** — cannot be enabled because there is nothing to enable: **zero** `vkCmdDrawIndirect*`
  calls exist. It needs a shared/pooled vertex+index buffer redesign first — a multi-week feature, not a ceiling
  already paid for. **Recommend moving it out of this phase.**
- **3.9 VFX GPU billboard pass** — the simulation half is done and wired; the draw pass needs human pixel
  verification, because a render path can pass validation and draw nothing. This engine has a documented instance
  of exactly that (the "skybox gray" bug that turned out to be SSR).
- **3.6** landed its mechanism and its first real consumer; the remainder (moving each reader across, and the five
  ECS component types that do not exist) is larger than its one line implied and is tracked in #26.

---


> **v0.6.2 could not start on any GPU without hardware ray tracing.** Reported from an AMD RX 5700; fixed in
> **v0.6.3** (`7208223`). The lighting fragment shader was *always* the SPIR-V declaring `OpCapability RayQueryKHR`
> — invalid usage on a device without `VK_KHR_ray_query`, and drivers are not required to report it, so AMD's
> crashed inside `vkCreateGraphicsPipelines`. **RDNA 1, GTX 10-series and essentially every integrated GPU** were
> affected.
>
> It shipped because **every development GPU here supports ray query, so the non-RT path had never once executed** —
> not under-tested, *never run*. `GWS_FORCE_NO_RT=1` now makes the device report no ray tracing, so both paths are
> exercised locally. **Any capability-dependent branch needs a way to force it, or it is untested by construction.**

> **Release-build hygiene (added after v0.6.0 failed to build).** `command_palette.h` used `uint32_t` with no
> `<cstdint>`. It compiled here and failed on the CI runner, because include order is not a guarantee — a green
> local build can be green by accident.
>
> The obvious guard, compiling every header alone, **does not catch it**, and that was measured rather than
> assumed: on this toolchain `<memory>` pulls `<cstdint>` in transitively, so the header compiles standalone and
> the missing include stays invisible. CI's libstdc++ does not. Anything that depends on *which* standard header
> leaks *which* type can only reproduce the bug by luck.
>
> Two guards, doing different jobs:
> - `includes_check` — a text scan: a file naming a fixed-width type must include `<cstdint>`. No toolchain
>   dependence, so it is true everywhere. Found **191 files** relying on someone else's include; all fixed.
> - `gws_add_header_selfcontain_check` (`cmake/HeaderSelfContain.cmake`) — compiles each public header alone,
>   twice, which also proves the include guard works. 63 headers covered; 8 excluded as visible debt.

## Phase 4 · Make it authorable — 🟠 In progress (7 of 9 done) · 4–8 weeks

Until these exist, using the engine means writing C++. This is the phase that turns it into a tool other
people can use.

| # | Item | Repo | Notes |
|---|---|---|---|
| 4.1 | **Material / shader graph** | engine | ✅ **Done** (`74444f0` → `e16cd01`). Node graph → GLSL → SPIR-V → `VkPipeline` → bound for draws; `--probe-shadergraph` runs the whole chain and reports it, because each link working proves nothing about the next. **Runtime shader compilation never worked in this engine** — `GWS_HAS_GLSLANG` is defined nowhere, the glslang wrapper links only under MSVC and this is MinGW — so a **bundled** `glslangValidator` (4.6 MB) compiles on save. That makes codegen **determinism** a correctness requirement, not a nicety. The generated shader **mirrors `gbuffer_scene.frag` exactly** (5 vertex inputs, set 1, four attachments): my first preamble invented its own interface, compiled perfectly, and could never have been bound — so the tests now parse the SPIR-V and assert its locations, sets and bindings. **Limits, stated:** preview is scene-wide (draw items carry no entity id), and Time nodes read 0 (no per-frame clock in the G-Buffer layout). `shadergraph_check` 22 + `shadercompile_check` 23 + `canvasview_check` 15 |
| 4.2 | **Command palette + universal search** | engine | ✅ **Done** (`2746b8a`) — Ctrl+P, three letters, Enter. Commands are registered in **one place** rather than beside each menu item, which is the point: a command living only inside a menu callback is invisible to the palette *and* to anything else driving the editor by name — the uniform-entry-point argument only holds if the list **is** the list. Entities are searched with the **same** matcher, so a scene's `Guard` is reachable by typing `gu` exactly as a command is. **Ranking is the feature**: a palette that finds the right command and puts it fourth is one people stop using. Registry and matcher are ImGui-free so they are testable; `palette_check` (16) asserts **order**, not membership. 15 commands registered at startup |
| 4.3 | ~~**VFX graph**~~ | engine | ✅ **Done — and the item was wrong about its own shape.** It is not a node graph: a VFX effect is an ordered **stack of modules per stage** (Spawn / Initialize / Update), which is a *list*, so it is drawn as a stack panel and `node_canvas.h`'s "four Phase 4 items need a canvas" is corrected to three. A canvas earns its place one level down, if module parameters ever become computed. **What actually blocked authoring was not the missing editor:** `ParticleEmitterComponent` exposed 6 of `EmitterConfig`'s 14 fields and the cache copied exactly those six, so lifetime, velocity, spread, gravity, drag and size took library defaults and could not be set from anywhere — inspector, scene file or C++. **The architectural bet is one restriction:** every module is a pure function of one particle given dt, normalised age, the emitter transform and an RNG — no scene reads, no cross-particle reads, no persistent state. That is the shape a compute shader executes, so GPU simulation later is transcription rather than redesign, and it is enforced by what `vfx_exec.h` can *see* rather than by review. **Order is semantic, not cosmetic**: the integrator applies gravity then damps the result, so a serialiser that emitted modules in map order would silently alter every saved effect on load — asserted directly (`Gravity·Drag ≠ Drag·Gravity`), as is the default stack reproducing the pre-4.3 integrator to 1e-4 over 20 steps against the old arithmetic. `EmitterConfig` survives as the façade that builds that stack, which is load-bearing: `vfx_check` constructs a `ParticleSystem` from one in all six of its groups, and a rewritten regression test proves nothing about the rewrite it polices. **`.vfx` is the first Phase 4 document that persists** — `MaterialGraph`, `AnimGraph` and `Sequence` still have zero serialisation — and its `MODULE.n` index is *grouping, not position*, so a hand-edited file with descending indices loads in file order instead of reordering the effect. `vfxgraph_check` (34); `vfx_check` (12) and `emitter_check` (14) pass **unmodified** through a full rewrite of the integrator. **Left:** GPU simulation (enabled by the purity contract, deliberately not built), sub-emitters, collision, ribbons/trails/mesh particles, and event/data interfaces. **Pixels still need a human** |
| 4.4 | Timeline / sequencer | engine | ✅ **Done** (`d4d4fe7`). Keyframed tracks over time, built on `gws::anim::Curve` from 4.5 and drawn with the same curve editor — a sequencer with its own interpolation would ease differently from the widget showing the same data. **Evaluation is a pure function of time**: scrubbing backwards must give exactly what playing forwards gave, or preview and result disagree invisibly. Asserted over 31 instants in both directions. Looping uses `fmod` so a stalled frame does not leave the playhead past the end; values apply **only** while playing or scrubbing, so the timeline never fights the gizmo. `sequence_check` (24). **Left:** saving sequences to disk, and event/trigger tracks |
| 4.5 | Curve + gradient editors | engine | ✅ **Done** (`7e679e9`) — `gws::anim::Curve` (keyed cubic Hermite, **clamped** not extrapolated, tangents scaled by segment length) and `Gradient`, plus ImGui editors that draw from the **real** `evaluate()`/`sample()` — straight lines between keys would show a curve that eases nothing, an editor lying about the runtime. The gradient renders over a checkerboard, without which a fade to *transparent* is indistinguishable from a fade to *black*. `curve_check` (23) asserts the **shape**, not the endpoints: a flat-tangent Hermite hits **0.15625** at t=0.25, which a linear or stepping implementation fails while surviving an endpoint-only test. **Wired**, not shelved — the emitter's colour ramp is the first consumer, and its two-colour storage limit is shown in the UI rather than silently dropping a dragged stop. `Curve`'s own first consumer is still ahead (4.3 / 4.4) |
| 4.6 | Animation state-machine graph editor | engine | ✅ **Done** (`8a10883`). `AnimGraph` is the editable **document** — states and transitions as plain data, kept apart from the runtime machine, which owns a skeleton and is mid-blend at any moment. **Proves the `NodeCanvas` reuse claim**: a state machine is about as unlike a material graph as this editor gets, and the canvas is *untouched* by it (verified with `git diff`, not assumed). Validation is the point, because a state machine **fails silently** — dangling transitions never fire, unreachable states never play, unconditional transitions fire instantly, and every symptom reads as "the animation is broken". Any-State transitions are **excluded** from the reachability walk or the check would never fire at all. `animgraph_check` (22). **Left:** converting the document to a runtime machine, which needs a clip library that does not exist yet |
| 4.7 | Level-design toolkit — snapping, arrays, splines, scatter, in-editor greybox | engine | ✅ **Done** (`d3d4aea` + `5ec2fd4`). Snapping (Ctrl per-drag, relative mode, `std::round` not a truncating cast) plus the operations that place **many** things — arrays, scatter, splines, greybox. **Found a real bug doing it:** the editor's existing *Duplicate* copies the **transform only**, so a copy is an invisible empty entity; an array of forty would have been forty invisible objects and the new tool would have taken the blame. Scatter samples `sqrt(radius)` (uniform radius gives density per *ring*, so it looks like a target) with its **own PRNG** — `std::rand` is process-global, so a fixed seed would not reproduce a layout. Splines are **Catmull-Rom** (interpolates its control points, so the path goes where you put it) distributed by **arc length** (even `t` bunches at corners). Unsatisfiable densities return **fewer** rather than overlapping. `leveltools_check` (26) |
| 4.8 | **User-extensible editor** via the three scripting backends | engine | Lets users solve their own workflow problems |
| 4.9 | Audio mixer / bus UI | engine | Rides the Stage-6 bus graph |

**Exit criteria:** a developer can build and light a level, author a material, a particle effect and a
cutscene without opening a C++ file.

---

## Phase 5 · Differentiate — 🟠 High · 4–8 weeks

Two capabilities no competitor can answer quickly, both of which fall out of architecture already in place.

| # | Item | Repo | Notes |
|---|---|---|---|
| 5.1 | **Deterministic replay** — record sessions as input + seed | engine | Gets harder the longer systems accumulate non-deterministic state. **Do it before Phase 6.** |
| 5.2 | Time-travel debugging — scrub backwards, attach profiler/inspector to a past frame | engine | |
| 5.3 | **Replay attached to crash reports** | engine | A bug report becomes "press play and watch it happen" |
| 5.4 | Replay as regression test — replay against a new build, diff final world state | engine | |
| 5.5 | **Hub: upgrade preflight** — run the project's tests against a new engine version in a sandbox first | hub | **No engine offers this.** Both halves already exist |
| 5.6 | Hub: project health dashboard — builds, tests, crashes, cook size, last commit | hub | |
| 5.7 | "Explain why this is broken" — walk the invisible-object chain and report the first failure | engine | Largest beginner time sink in every engine; nobody does it well |
| 5.8 | Remote profiling on device | engine | |
| 5.9 | Automated play testing — scripted sessions asserting game-level outcomes | engine | Catches "the quest became uncompletable" |

**Exit criteria:** a crash report from a player replays frame-exact locally; upgrading engine version reports
breakage before the developer commits to it.

---

## Phase 6 · Widen reach — 🟡 Medium · 8–12 weeks

Everything so far serves 3D real-time games. This is where "any kind of game" starts being true.

| # | Item | Repo | Notes |
|---|---|---|---|
| 6.1 | **2D as a first-class mode** — sprites, atlases, pixel-perfect camera, tilemaps + auto-tiling, 2D physics, sorting layers, 2D lights, nine-slice | engine | **The largest single expansion of what games are possible.** Most games small teams make are 2D |
| 6.2 | Tilemap editor | engine | |
| 6.3 | **Camera system library** — orbit, first-person, over-shoulder, fixed, top-down, isometric, 2.5D, cinematic | engine | Small effort; camera is what most defines how a game reads |
| 6.4 | Non-photoreal rendering — cel, outlines, painterly | engine | Depends on 4.1 |
| 6.5 | Split-screen / local multiplayer | engine | Invasive if retrofitted late; cheap if camera + input assume it now |
| 6.6 | Text- and UI-heavy support — rich text, CJK atlases, right-to-left, tables, data grids | engine | Couples with localisation |
| 6.7 | **Genre starter templates** (8, from the feature catalog) | samples | The design work is already written down. Cheapest credibility available |
| 6.8 | Sample complete games, one per genre | samples | Doubles as a regression suite against new engine versions |
| 6.9 | Hub: template gallery | hub | Surfaces 6.7 |
| 6.10 | Validate lockstep RTS — thousands of units synced by determinism alone | engine | We are accidentally close and never planned for it |

**Exit criteria:** a 2D game and a 3D game can both be started from a template and played.

---

## Phase 7 · Ship a game — 🟡 Medium · ongoing

The lifecycle currently has no ending. Until this phase exists, nothing built with the engine can reach a player.

| # | Item | Repo | Notes |
|---|---|---|---|
| 7.1 | **Ship tab** — build targets, icons, version strings, code signing | hub | |
| 7.2 | **Steam integration** — depot upload, achievements, cloud saves, workshop | hub | The only platform that matters first for an indie |
| 7.3 | Patching / delta updates | engine | The pak container is TOC-indexed and content-addressed already |
| 7.4 | **Localisation** — string tables, CJK font atlases, right-to-left, subtitle timing | engine | **Only cheap while the UI has no baked-in strings. That window is open now** |
| 7.5 | Accessibility — remappable input, colourblind-safe palettes, text scaling, screen-reader hooks | engine | Increasingly a store requirement |
| 7.6 | Budgets that fail the build — memory, draws, texture footprint, build size, startup | engine | The cook pipeline already knows these numbers; nothing watches them |
| 7.7 | Upscaling + frame generation — DLSS / FSR / XeSS / TSR | engine | Table stakes in 2026; render graph, TAA and motion vectors already exist |
| 7.8 | Scalability presets + dynamic resolution | engine | |
| 7.9 | Latency measurement + frame pacing | engine | A correctness concern for frame-data combat, not only feel |
| 7.10 | Photo mode | engine | Nearly free once 5.1 exists |

**Exit criteria:** a sample game ships to a Steam page from the Hub, localised, with a measured frame budget.

---

## Phase 8 · Ecosystem — ⚪ Later

Only worth starting once there are users. Building a marketplace before a community is the classic mistake.

| # | Item | Repo | Notes |
|---|---|---|---|
| 8.1 | **Module format (`module.schizo`) + dependency resolution + install from Git** | engine | Works unusually well here because components self-register. **Format only — not a storefront** |
| 8.2 | Semantic scene diffs — "2 entities added, 1 material changed" | engine | Our formats are text; Unity and Unreal cannot do this cleanly |
| 8.3 | Asset locking UI over LFS locks | engine | Artists hate Git because every workflow assumes a terminal |
| 8.4 | In-editor source-control panel | engine | |
| 8.5 | DCC round-tripping — watched folder re-import preserving material bindings | engine | |
| 8.6 | Asset provenance + licensing tracking | engine | Increasingly a legal requirement; nobody does it well |
| 8.7 | Shared derived-data cache across machines and CI | engine | We already have content addressing + a dependency graph |
| 8.8 | Modding / UGC — ship a restricted editor to players | engine | Decisions in 8.1 either enable or foreclose this |
| 8.9 | Telemetry, funnels, remote config / feature flags | hub | Gameplay is already data-driven, so values are separated from code |
| 8.10 | Backend services — **integrate, do not build** | hub | Accounts, saves, matchmaking, lobbies |

---

## Phase 9 · Platform and the reach tier — ⚪ Later

| # | Item | Repo | Notes |
|---|---|---|---|
| 9.1 | Platform abstraction layer | engine | Windows assumptions are spread through the tree |
| 9.2 | **Linux** (+ the headless dedicated server) | engine | Where dedicated servers actually deploy; the server binary already exists |
| 9.3 | Web export | engine | Highest-reach target after Linux, and no NDA |
| 9.4 | XR (OpenXR) | engine | Discrete and well-bounded; the Vulkan renderer is ready |
| 9.5 | Mobile / touch | engine | |
| 9.6 | Virtual-texture runtime; virtual shadow maps; material layering / hair / skin | engine | Stage 3 + Stage 8 remainder |
| 9.7 | Audio depth — bus graph, reverb zones, DSP inserts, streaming, procedural | engine | |
| 9.8 | Networking depth — component-granular delta, rotation slerp, bandwidth budget, zone handoff | engine | |
| 9.9 | Gameplay depth — per-bone hitboxes, dialogue, cutscenes, save slots, targeting/lock-on | engine | |
| 9.10 | Jolt vehicle constraints; cloth/ropes; destruction | engine | |
| 9.11 | Stage 15 reach — Nanite-like geometry, Lumen-grade GI, crowd sim, world sim | engine | Do not chase; revisit only if a funded title demands it |
| 9.12 | Consoles | engine | NDA SDKs + certification; only with a funded title |

---

## Explicitly not doing

Recorded so the decision is not re-litigated. Rationale in
[`PRODUCT_AND_ECOSYSTEM.md`](PRODUCT_AND_ECOSYSTEM.md) §4 and
[`DEVELOPER_EXPERIENCE.md`](DEVELOPER_EXPERIENCE.md) §4.

- Our own version control system — integrate; never reimplement
- Hosted backend services — an operations business with on-call attached
- A paid marketplace — payments, tax, fraud, disputes, curation
- Training our own models — Unity tried, deprecated Muse, moved to third-party frontier models
- Real-time collaborative editing — the wrong problem while the audience is small teams
- A second rendering backend — DirectX 12 buys almost nothing while Vulkan runs everywhere we ship
- Our own physics or audio engine — Jolt and miniaudio were correct calls
- Chasing Unreal on fidelity — nobody picks an engine because its GI is third-best

---

## Phase summary

| Phase | Theme | Importance | Rough size |
|---|---|---|---|
| 0 | Unblock | 🔴 Critical | days |
| 1 | Automation foundation | 🟠 High | 1–2 weeks |
| 2 | Inner loop | 🟠 High | 2–4 weeks |
| 3 | Collect paid-for ceilings | 🟠 High | 2–4 weeks |
| 4 | Make it authorable | 🟠 High | 4–8 weeks |
| 5 | Differentiate | 🟠 High | 4–8 weeks |
| 6 | Widen reach | 🟡 Medium | 8–12 weeks |
| 7 | Ship a game | 🟡 Medium | ongoing |
| 8 | Ecosystem | ⚪ Later | — |
| 9 | Platform + reach tier | ⚪ Later | — |
