# Developer Experience — Tool · Power · Variety

**Created:** 2026-08-09
**Companion to:** [`AAA_ENGINE_MASTER_PLAN.md`](AAA_ENGINE_MASTER_PLAN.md) (the 16-stage runtime roadmap) ·
[`REMAINING_WORK.md`](REMAINING_WORK.md) (verified done/remaining) ·
[`PRODUCT_AND_ECOSYSTEM.md`](PRODUCT_AND_ECOSYSTEM.md) (everything *around* the engine)

> **Why this document exists.** The 16-stage plan describes what the engine can **simulate**. It says nothing
> about what the engine is like to **use**, how fast it lets someone iterate, or how wide a range of games it can
> express. Those three decide whether anyone ever ships with it — and almost none of them are runtime systems.
>
> Researched against Unity and Unreal's real, documented pain points (sources at the end). The recurring finding:
> **developers do not compare renderers.** Unity's most-hated feature is a load-time delay; Unreal's is a stutter.
> Both are workflow problems in engines with world-class rendering.

**Status:** ✅ built + verified · 🟡 partial · 🔴 planned

---

## 0. The three structural advantages every item here leans on

Everything below was chosen because it compounds on something the engine *already has*, not because it sounds
impressive. The three advantages, in priority order:

1. **Everything is text and data** — scenes, `.logic`, `.gameplay`, `.items`, prefabs, and scripts in three
   languages. Diffable, generatable, agent-authorable.
2. **The simulation is deterministic** — Jolt compiled `CROSS_PLATFORM_DETERMINISTIC`, verified bit-identical
   across runs; prediction re-simulates unacked input; item rolls use a seedable RNG so they are net-safe.
3. **Components self-register** — one `make_authorable<T>("Name")` line gives a component inspector UI, sidecar
   save, and replication at once.

---

# PART 1 · TOOL — the inner loop is the product

A developer makes a change and waits. That wait, repeated a few hundred times a day for two years, is most of
what they will remember about this engine.

## 1.1 Inner-loop budget — declare it, then gate it in CI 🔴

The single highest-leverage discipline in this document. Unity's domain reload is so slow that the official
guidance is to **turn correctness off** to get speed back (disable domain reload, then reset static state by
hand). On the Unreal side, one team reported a single shader-cache fingerprint saving ~45 s per iteration —
≈120 hours of waiting over a two-year project.

We already have the profiler infrastructure to measure this and the `tools/*_check` binaries to enforce it.
So state the budget as a promise and **fail the build when it regresses**, exactly as we would treat a
frame-time regression.

| Inner-loop action | Budget | Where we stand |
|---|---|---|
| Script edit → running in game | < 500 ms | ✅ three hot-reload backends already exist |
| Press Play → controllable | < 1 s | ✅ **7.0 ms measured** — no domain-reload equivalent to pay. *Protect this* |
| Save asset → visible in viewport | < 2 s | ✅ **7.4 ms (OBJ) / 12.0 ms (glTF)**, measured by `editor --probe-inner-loop` through the real async path |
| Shader edit → on screen | < 2 s | 🟡 **~1.2 s measured** (`4602a03`) — was **17.7 s**. The renderer now prefers `<exe_dir>/shaders/<name>.spv` over the baked array and `gws shaders` compiles into it, so the loop is `gws shaders && editor`. **Covers 11 shaders** since `607c5d9`, up from 2 — the inline GLSL was extracted to files after proving every block compiles to the SPIR-V that ships. A few passes still have no file source (#67). Still a restart, not live reload |
| Editor cold start → project open | < 5 s | ✅ **865 ms**, measured by `editor --startup-probe` via `innerloop_check` |
| Full clean build | tracked, not capped | 🔴 the number nobody optimises until it is 40 minutes |

**Steps:** ~~(1) add an `innerloop_check` tool~~ ✅ · ~~(2) record a baseline~~ ✅ · ~~(3) fail CI on regression
beyond a tolerance~~ ✅ `cc4e2f7` · (4) publish the numbers in the README as a product claim.

**How the gate is set, and why not tighter.** CI publishes every metric to the run summary on each build and
fails only above **4× budget**. A shared runner is virtualised, noisily co-tenanted and an order slower on I/O
than a dev box, so a 1× wall-clock line would fail honest PRs — and a gate that cries wolf gets ignored, which
is the precise failure this section exists to prevent. The division of labour is: **the gate catches
order-of-magnitude regressions, the published trend catches drift.** The gate was verified to actually fire
(`--tolerance 0.001` → exit 1) rather than assumed; a gate nobody has watched fail is not known to be a gate.

Three rows still print as `UNMEASURED` on every run — play-mode entry, asset-to-viewport, shader-to-screen —
because they need an editor driven with input. They are printed rather than dropped, since an unmeasured budget
is exactly where a regression hides.

## 1.2 Iteration

| Item | Status | Notes |
|---|---|---|
| Hot reload: Python / C++ (fresh-DLL swap) / C# | ✅ | Genuinely better than Unity's default. **The strongest workflow card we hold.** |
| Hot reload everything else — shaders, textures, meshes, scenes, component definitions | ✅ | **Done.** One shared `AssetWatcher` that *settles* before firing, so a file mid-write is never read half-finished. Textures, meshes, shaders (2 → 11 sources) and scenes all reload. Scene reload asks rather than discarding unsaved work — the one case where automatic would be destructive. Component definitions remain C++, reachable only through the script/DLL path. |
| **Keep changes made while playing** | ✅ | **Done `31eb938`.** On Stop, a diff of what play changed with a per-row keep choice. Built around the existing restore rather than replacing it (Capture → Diff → restore → Apply ticked rows), so *discard all* is byte-for-byte the old behaviour and writing to the authored scene is opt-in. Generic over the authorable registry — new gameplay components are diffable for free, with field-level summaries from reflection. Spawned/destroyed entities are out of scope and **reported as counts**, never silently dropped. `playchanges_check`: 26 assertions, headless. |
| Never block the editor thread | ✅ | **Done for the interactive paths.** `TaskRunner` (not `gws_jobs` — that is fork-join and wrong for tasks outliving a frame) runs drag-drop import and both OBJ and glTF parsing on workers, with progress and cancel. Completion callbacks land on the editor thread, so a finished import may legally touch the GPU. Navmesh bake stays synchronous; it is not on the interactive path. |
| Undo covering *everything* | ✅ | Partial undo is worse than none — and part of this was **actively wrong**, undoing creates via `GetEntityByName` so it deleted the wrong entity when two shared a name. Now: create/delete/rename, inspector fields, gizmo drags, component add/remove, reparenting, terrain sculpt, logic-graph edits, and keeping play-mode changes. One entry per gesture, via an ImGui-free coalescer whose gesture cases are headless assertions. |

## 1.3 Authoring surfaces — "supported" vs "authorable"

The runtime can already draw and simulate most of this. What is missing is the interface to create it without
writing C++.

| Surface | Status | Why it matters |
|---|---|---|
| **Material / shader graph** | 🔴 | **The most impactful missing editor.** Without it every custom look requires editing engine source, so in practice every game made with this engine will look identical. This is an art-direction unlock, not a convenience — it is what allows cel-shaded or painterly instead of photoreal. Also the prerequisite for NPR (§3.3). |
| **VFX graph** | 🔴 | Particles simulate on CPU with no authoring UI, so effects are code. A node graph over emitters/curves/forces turns VFX into a design task. |
| **Timeline / sequencer** | 🔴 | Cutscenes, scripted moments, camera moves, ability choreography. What makes a game feel directed rather than assembled. |
| **Curve + gradient editors** | 🔴 | Damage falloff, difficulty ramps, colour-over-lifetime, camera easing. Small, used constantly; their absence forces magic numbers into data files. |
| **Level-design toolkit** | 🔴 | Grid/vertex snapping, pivot control, array + mirror tools, spline placement, prop scattering, in-editor greybox modelling. Unreal added modelling mode precisely because round-tripping to Blender for a blockout is intolerable. |
| **Animation state-machine graph editor** | 🔴 | The state machine is authored in code because no graph editor exists. **The logic graph already proves the node canvas works in this editor — port, don't invent.** |
| Audio mixer / bus UI | 🔴 | Rides the audio bus graph (Stage 6 remaining). |
| Tilemap editor | 🔴 | Rides 2D (§3.1). |

## 1.4 Ergonomics — what makes it feel professional

| Item | Status | Notes |
|---|---|---|
| **Command palette + universal search** | 🔴 | One keystroke to any command, asset, entity, component or setting. VS Code proved this is how modern tools are navigated; Unity bolted a version on later. Cheap, disproportionate effect on perceived quality — **and it gives an AI agent one uniform entry point to every capability.** |
| **User-extensible editor** | 🔴 | Custom panels, inspectors, importers, menu commands without forking the engine. Unity's editor-scripting API is a genuine superpower — studios build entire internal pipelines on it. **We have three scripting backends already; letting them drive editor tools costs far less than a separate plugin system.** |
| **"Explain why this is broken"** | 🔴 | "Why is my object invisible?" is the largest beginner time sink in every engine. The chain is mechanical — culled, no material, shader failed, no light, wrong layer, zero scale, behind camera. A button that walks it and reports the first failure. **No competitor has this**, and it is mostly a debug traversal we can already write. |
| Autosave + crash recovery | 🔴 | Recovery of unsaved work. `gws_diagnostics` already catches the crash; it does not yet preserve the work. |
| Asset validation + broken-reference detection | 🔴 | Fail loudly at cook time, not silently at runtime (cf. the "OBJ stays primitive" class of bug). |
| **Project migration across engine versions** | 🟡 | `gws::serialize` already has per-type version **upgrade hooks**. Pointing them at scenes gives painless upgrades — the thing Unity users fear most. See also *upgrade preflight* in [`PRODUCT_AND_ECOSYSTEM.md`](PRODUCT_AND_ECOSYSTEM.md). |
| Safe mode | 🔴 | Boot with scripts/modules disabled when one breaks the editor. |

---

# PART 2 · POWER — ceilings, and the absence of hitches

Average frame rate is the least interesting performance number. What players notice is the stutter; what
developers notice is the ceiling they hit before the game is finished.

## 2.1 Shader / pipeline compilation stutter ✅ — measured, and this engine does not have it

**The industry problem is real.** A PSO compiled the first time it is encountered stalls the render thread and
produces a freeze; Epic built automatic PSO precaching specifically to attack it. That is why this section was
written.

**This engine does not have it. Measured 2026-08-10, and this is the third correction to this section — the
first two were also wrong.**

| claim made here | what measurement showed |
|---|---|
| "a stutter source waiting for a large scene" | all 17 sites run at **pass-creation**, never on first draw |
| "a startup and re-init cost" | **1.2 ms of a ~830 ms cold start — 0.1%** |
| "move all 17 sites to precompiled SPIR-V" | **already the case.** `GWS_HAS_GLSLANG` is defined nowhere, so every site already falls through to `create_from_spirv` |

The engine has shipped on precompiled SPIR-V the whole time. The `#ifdef` scaffolding is deliberate and stays —
a future glslang-enabled build should still work — but nothing in a normal build ever compiles GLSL.

**The one real defect here was a reporting defect.** Every startup logged **14 lines of
`[error] Failed to compile shader ...`** describing completely normal behaviour. Fixed in `2db10a7`: the
unavailable branch returns empty instead of throwing, and the condition is announced **once** at info level.
14 false errors → 1 accurate line, and the cost drops to 0.0 ms because nothing is thrown at all.

An error channel that always fires is one nobody reads. That is the same failure that let CI sit red for six
commits — the value of this investigation was the log hygiene, not the milliseconds.

**Why this section was wrong three times.** Every claim came from importing Unreal's well-documented problem
into this engine's plan without reading this engine's code. The measurement that settled it took about twenty
minutes — less time than was spent writing the wrong claim into three documents. Worth remembering the next
time a section cites an industry-wide problem as though it were an observation about this project.

**What would reopen it:** a frame-time capture during real gameplay showing hitches attributable to pipeline
creation. That is a legitimate future investigation; it is simply not established today.

**What stayed:** `editor --startup-probe` initialises, reports `startup_ms` / `runtime_glsl_ms`, and exits
before the main loop — so *editor cold start* is now a number anyone can re-check rather than an unmeasured
budget row.

## 2.2 The rest

| Item | Status | Notes |
|---|---|---|
| **Upscaling + frame generation** | 🔴 | Table stakes in 2026: DLSS 4.5, FSR 4.1, XeSS 3.0, TSR — and frame gen is now decoupled from the upscaler, so developers mix vendors. Shipping without any upscaling path reads as an unfinished engine. **Prerequisites exist:** render graph, TAA, motion vectors. The work is a render-resolution split plus vendor SDK hooks, not new research. |
| **Actually use the job system** | 🟡 | Work-stealing + `parallel_for` verified across 12 workers — and culling, LOD and animation sampling still run single-threaded. **The ceiling is already paid for and not yet collected.** |
| Scalability presets + dynamic resolution | 🔴 | Same project must run on a laptop and a 5090 without hand-authoring two versions. Quality tiers + dynamic res against a frame-time target. |
| **Budgets that fail the build** | 🔴 | Memory, draw calls, texture footprint, build size, startup time — declared per platform, enforced at cook time. The cook pipeline and memory tagging already know these numbers; **nothing is watching them.** |
| Editor performance at scale | 🔴 | 100k-object scenes must stay navigable: virtualised hierarchy lists, culled inspector updates, background asset scans. Editors collapse here long before runtimes do. |
| Latency, not just frame rate | 🔴 | Input-to-photon, frame pacing, present modes, a latency readout beside frame time. **For frame-data combat at a fixed 60 Hz this is a correctness concern, not only feel.** |
| Async everything on load | 🟡 | No synchronous disk access on the frame thread; `pak_file` mmap is the foundation. |
| Determinism as a shipped feature | ✅ | Already true. Underexploited — see replay in [`PRODUCT_AND_ECOSYSTEM.md`](PRODUCT_AND_ECOSYSTEM.md) and lockstep in §3.4. |

---

# PART 3 · VARIETY — how many kinds of game can it express

The feature catalog already proves genre breadth **within one shape**: a 3D, real-time, PBR-rendered game. The
gaps are not more genres inside that shape — they are the shapes themselves.

## 3.1 2D as a first-class mode 🔴 — the largest single gap in reach

Most games made by small teams are 2D, and a 2D-first engine reliably delivers better results with less effort
than a 3D engine retrofitted for it — the problems are genuinely different. Even casting a shadow requires a
fundamentally different approach than a 3D renderer's.

**Needs:** sprite atlases, pixel-perfect camera, tilemaps with auto-tiling + collision, 2D physics, sorting
layers, nine-slice UI scaling, 2D lights and shadows, sprite animation.

**Why strategically:** "any kind of game" currently excludes the majority of games people actually make with a
new engine — and a 2D game exercises a small fraction of the systems already built.

## 3.2 Platform + presentation reach

| Target | Status | Notes |
|---|---|---|
| **Web export** | 🔴 | How small games get discovered; a playable demo converts far better than a trailer, and it makes the engine demonstrable in a link. Large effort with Vulkan, but the highest-reach target after Linux — and unlike consoles, no NDA. |
| **Camera system library** | 🔴 | Third-person orbit, first-person, over-the-shoulder, fixed survival-horror angles, top-down, isometric, 2.5D side-on, cinematic rigs. Already listed as missing in G1. **Camera is what most defines how a game reads.** Small effort, immediately changes which genres feel native. |
| Split-screen / local multiplayer | 🔴 | Multiple viewports, per-player input devices and UI. Structurally invasive if retrofitted late; cheap if camera + input assume it now. |
| XR (OpenXR) | 🔴 | Stereo rendering, hand/controller input, comfort options. Discrete, well-bounded; the Vulkan renderer is architecturally ready. |
| Mobile / touch | 🔴 | Rides Stage 13. |

## 3.3 Art-direction variety 🔴

Cel shading, outlines, halftone, painterly, NPR generally. An engine that only does photoreal PBR quietly forces
every game made with it to look the same — the most common complaint about engine-defined visual identity.
**Depends on the material graph (§1.3): one unlock, two problems solved.**

## 3.4 Game shapes we are accidentally close to

| Shape | Status | Notes |
|---|---|---|
| **Lockstep strategy (RTS)** | 🟡 | Thousands of units, no per-entity replication, sync by determinism alone. We have bit-identical physics and a seeded RNG — **closer to an RTS-capable engine than almost anyone, and it was never planned for.** |
| **Photo / replay mode** | 🟡 | Free camera, DoF, filters, shareable capture. Trivial once deterministic replay exists, and worth significant free marketing. |
| Text/UI-heavy games (strategy, management, VN) | 🔴 | Mostly interface: rich text, font atlases incl. CJK, right-to-left, scrolling lists, tables, data grids. The current `gws_ui` has none of these. Couples with localisation. |

---

## 4. Sequencing

**First — completing what exists** (note the pattern: almost everything here finishes something already built)
1. **Precompile + cache every shader pipeline** (§2.1) — removes the defining performance failure of the era before any project is large enough to expose it.
2. **Move culling / LOD / animation onto `gws_jobs`** (§2.2) — the ceiling is built and unused.
3. **Material graph** (§1.3) — unlocks art direction and NPR in one move.
4. ~~**Keep play-mode changes**~~ (§1.2) — ✅ done `31eb938`; the reflection-driven component registry is what made it a day's work rather than a rewrite.
5. **Inner-loop budgets in CI** (§1.1) — prevents the slow decay that made Unity's reload times what they are.
6. **Command palette + universal search** (§1.4) — days of work, disproportionate effect.

**Then — widening reach**
7. **2D as a first-class mode** (§3.1) — the largest single expansion of what games are possible.
8. **Camera system library** (§3.2) — small effort, big genre effect.
9. **Timeline + VFX graph** (§1.3) — cutscenes and effects become design work.
10. **Upscaling integration** (§2.2).
11. **Editor extensibility via the scripting backends** (§1.4) — users solve their own workflow problems.
12. **Explain-why-it-is-broken diagnostics** (§1.4).

**Resist — where effort disappears**
- **Chasing Unreal on fidelity.** Nanite/Lumen equivalents are hundreds of engineer-years, and nobody picks an engine because its GI is third-best. (Stage 15 stays a reach tier.)
- **A second rendering backend.** DirectX 12 buys almost nothing while Vulkan runs everywhere we ship.
- **Our own physics or audio engine.** Jolt and miniaudio were correct calls; do not revisit them.
- **Perfect editor UI before the loop is fast.** A beautiful editor with a 4-second play button loses to an ugly one with an instant button, every time.
- **Breadth without depth in the G-modules.** 17 partial gameplay modules are worth less than 5 genuinely finished and documented.

---

## Sources (researched 2026-08-09)

1. [Unity — enter Play mode without domain reload](https://docs.unity3d.com/6000.5/Documentation/Manual/domain-reloading.html) — trading correctness for iteration speed
2. [Unity Discussions — why is domain reload so slow](https://discussions.unity.com/t/why-is-domain-reload-sooo-slow-and-will-this-ever-be-fixed/798176)
3. [Epic — game engines and shader stuttering](https://www.unrealengine.com/tech-blog/game-engines-and-shader-stuttering-unreal-engines-solution-to-the-problem) — PSO precaching
4. [UE5 PSO precaching playbook](https://www.strayspark.studio/blog/ue5-shader-stutter-pso-precaching-playbook) — iteration-time savings, remaining gaps
5. [Frame generation explained, 2026](https://www.valhallapc.com/blogs/news/frame-generation-explained-dlss-fsr-xess) — DLSS 4.5 / FSR 4.1 / XeSS 3.0 / TSR
6. [Choosing a 2D engine](https://300mind.studio/blog/best-2d-game-engine-for-game-development/) — why 2D-first beats a 3D retrofit
7. [2D lighting techniques](https://www.slembcke.net/blog/2DLightingTechniques/) — why 2D shadows are a different problem
8. [Custom editor tools](https://medium.com/@lemapp09/beginning-game-development-custom-editor-tools-a59ebb6027bf) — editor extensibility as a productivity multiplier
