# Remaining Work — verified current state

**Last verified:** 2026-08-09 (full audit: all 20 check binaries executed, source cross-checked, repos + CI inspected)
**Companion to:** [`AAA_ENGINE_MASTER_PLAN.md`](AAA_ENGINE_MASTER_PLAN.md) (its §1 has the quick
16-stage status table; this file is the detailed done/remaining breakdown) ·
[`DEVELOPER_EXPERIENCE.md`](DEVELOPER_EXPERIENCE.md) (the engine as a tool: iteration, authoring, performance, genre reach) ·
[`PRODUCT_AND_ECOSYSTEM.md`](PRODUCT_AND_ECOSYSTEM.md) (version control, packaging, shipping, LiveOps, replay, agent CLI, the Hub)

> "Verified" below means a headless `tools/*_check` **or** a live editor session confirms it — not
> that a doc claims it. The master plan's older per-stage STATUS blocks predate this work; this
> file supersedes them.

> **Scope note added 2026-08-09.** The 16 stages cover what the engine can *simulate*. Two whole
> categories of work sit outside them and are now tracked in the two companion docs above: **how the
> engine feels to use** (inner-loop speed, authoring surfaces, genre reach) and **everything around the
> engine** (source control, packaging, shipping, telemetry, the Hub). Finishing all 16 stages does not
> produce a usable product on its own.

---

## Audit snapshot — 2026-08-09

**Engine v0.1.20** (20 releases) · **Hub 0.1.7** (5 releases) · ~141k lines C++
(engine 107k · editor 24k · tools 6k · tests 4k) · Hub 2.4k lines · 158 mapped subsystems.

**All 20 headless check binaries pass**, 500+ assertions:
`gameplay_check` 238 · `audio_check` 41 · `texture_check` 36 · `server_check` 35 · `profiler_check` 32 ·
`ai_check` 28 · `combat_check` 23 · `world_check` 21 · `ui_check` 19 · `vfx_check` 12 · `anim_check` 12 ·
`crash_check` 10 · `skinning_check` 8 · `model_check` 7 · plus `physics_check`, `script_check`, `net_check`,
`repl_check`, `prediction_check`, `hub_check`.

### Corrections to earlier doc claims (verified against source)

| Earlier claim | Reality |
|---|---|
| "AI: no bake-from-scene, path-follow or perception wiring" | `nav_builder.h` (**`NavMeshBuilder`**, slope-filtered bake) and `path_follow.h` (**`PathFollower`**) both exist, pass 28 assertions, and **run live in the editor** via `editor/include/skinned_demo.h:196`. What is actually missing: baking from **real scene/terrain geometry** (the demo feeds a synthetic grid), perception, and NPC BTs. |
| "GPU-driven: indirect draw incomplete" | `vulkan/indirect_dispatcher.cpp` issues real `vkCmdDrawIndirectCount` / `vkCmdDrawIndirect` and `hzb_culler.h` references it — but `gpu_driven_enabled` defaults to `false` and **nothing sets it true**, so the path is dead code. Mesh shaders (`vkCmdDrawMeshTasks`) genuinely absent. |
| "Stage 10 remaining: loot/itemization, vehicles, quests" | All three shipped as **G4**, **G13** and **G6**. See the [feature catalog](ENGINE_FEATURE_CATALOG.md) — **G0–G16 all landed** (2026-08-06 → 08-07). |
| Feature catalog: "gameplay frameworks … nearly all 🔴" | Superseded. All 17 G-modules exist in `engine/core/ecs/gameplay_*.h`. |

---

## Repo & release health — ⚠️ action required

Found 2026-08-09. None of this affects the shipped product; all of it affects contributors, CI and clean clones.

| # | Finding | Detail |
|---|---|---|
| **R1** | ✅ **FIXED 2026-08-09** (commits `c1ac242`, `6e2266d` — issue #2) | Testing a real clean clone found **four** independently fatal problems, all the same class — *a file or reference that existed only on the author's machine*: (1) `JoltPhysics`, `enet`, `tinyusdz`, `ufbx` were gitlinks with **no `.gitmodules` entry**; (2) `third_party/ufbx/CMakeLists.txt` was **authored by us but placed inside the submodule**, so it was untracked and absent from every clone → moved to `third_party/ufbx.cmake`; (3) the tinyusdz `"-Wa, -mbig-obj"` fix was an **uncommitted hand edit** in the submodule working tree → now applied idempotently at configure time; (4) **zstd's entire source tree was gitignored** by an unanchored `lib/` rule → 103 files never committed, clean clone died at `add_library(zstd ...)`. Also narrowed a blanket `*.cmake` rule that hid hand-written CMake modules. **Verified:** fresh `--recurse-submodules` clone populates all 10 submodules, configure exits 0, and `zstd` + `ufbx` + `tinyusdz_static` all build from it. ⚠️ The CI *badge* stays red until the next `v*` tag, because `release-engine.yml` only triggers on tags — see R5. |
| **R5** | 🟡 CI health is unobservable without cutting a release | `release-engine.yml` triggers only on `v*` tags, so nothing validates the tree on an ordinary push. This is why R1 went unnoticed for 20 releases. Fixed by issue #4 (run the `*_check` suite on pull requests), which adds a non-tag trigger. |
| **R2** | 🔴 **The default branch is not the engine** | GitHub serves `main`, but all work is on `Rework-with-Vulkan-without-Project-Hard-Reset`, **117 commits ahead**. The two have diverged — `main` carries 16 commits the working branch does not. A visitor sees an engine that predates nearly everything above. |
| **R3** | 🟡 **~7,000 lines compile but nothing calls them** | Pre-ECS libraries still in the build, superseded by G0/G1/G3: `engine/core/ability` (1,814), `engine/core/character` (2,041), `engine/core/gameplay` (1,952). Separately `engine/core/containers` and `engine/core/file_io` have **zero includes outside their own directories** — written, never adopted. Clearest cleanup target in the tree. **Salvage** the character library's input-buffer + locomotion work into G1 rather than deleting it. |
| **R4** | 🟡 No baseline regression gating in CI | Stage 14's last item. Now also covers the **inner-loop budgets** in [`DEVELOPER_EXPERIENCE.md`](DEVELOPER_EXPERIENCE.md) §1.1. |

---

## The recurring pattern: "core built + verified, integration/breadth pending"

Most systems are **built and headlessly verified as libraries**, but not fully **adopted** by the
older editor/gameplay code or exercised at full scale. The remaining engine work is largely this
integration debt, plus rendering breadth and the reach tier — very little is greenfield.

The cross-cutting integration debt specifically:
- The editor **inspector** still uses its own reflection, not core `gws_reflection`.
- The editor **scene save** still uses the JSON `SceneSerializer`; core `gws::serialize` only drives ECS snapshots.
- The **ECS is a per-frame shadow** of the OOP scene (`EcsSceneBridge`); the OOP scene is still authoritative.
- Not all hot systems (culling / LOD / animation sampling) run on the job system's `parallel_for` yet.

---

## Core (Stages 0–3) — ≈ 80–85%

- **Stage 0 — Foundation** 🟢 *core built + verified.* All substrate libs exist under `engine/core/`
  (memory: stack/pool/general/arena/**frame** (wired into the editor loop)/segregated + tracking/tags/leak-detect;
  jobs: work-stealing + `parallel_for`; **core** reflection + serialization; logging; profiler N1–N5).
  **Remaining:** editor inspector → core reflection; editor scene-save → core serialization (`TextArchive`);
  finish moving hot systems onto `parallel_for`.
- **Stage 1 — ECS** 🟢 *acceptance met* (100k entities → draws in one parallel pass @ 3.7 ms, deterministic).
  **Remaining:** retire the OOP-shadow model — make ECS authoritative (gizmo/inspector/save write ECS;
  gameplay off `GetComponent<T>()`).
- **Stage 2 — Asset pipeline** 🟢 *acceptance met.* OBJ/glTF/FBX/USD importers → incremental cook →
  mmap runtime; BC7 renders; tiled/page-aligned virtual-texture **format**.
  **Remaining:** optional BC6H/Oodle codecs; the VT **runtime** (page table/feedback/atlas/streamer) is Stage 8.
- **Stage 3 — Rendering completion** 🟡 *≈ 75%.* Done: DDGI, volumetric fog/froxel, clouds, water,
  contact shadows, sky/IBL, SSR/SSAO/RT, culling (frustum/meshlet/HZB), post-FX.
  **Remaining:** GPU-driven **indirect/mesh-shader** completion (3.1), **virtual shadow maps** (3.4),
  **material layering / hair / skin** (3.5).

---

## Feature & system stages (4–15)

- **Stage 4 — Physics (Jolt)** 🟢 acceptance met (1k bodies @ 66 Hz, deterministic, character collide-and-slide).
  **Remaining:** per-bone combat **hitboxes** (rides Stage 10); cloth/ropes/destruction (E4, deferred).
- **Stage 5 — Animation** 🟢 core done + verified — skeleton, blend trees, state machine, two-bone/foot IK,
  **GPU compute-skinning**, **rigged-glTF import**.
  **Remaining (not Stage-5 core):** motion matching + anim streaming (later-tier); state-machine **graph editor** (Stage 11);
  wiring to the real player (Stage 10); job-parallel sampling benchmarked at 1k.
- **Stage 6 — Audio** 🟢 core acceptance met (256-voice lock-free spatial mixer + occlusion).
  **Remaining:** bus graph, reverb zones, DSP inserts, streaming, procedural.
- **Stage 7 — Networking** 🟢 **feature-complete.** Core + multiplayer live in-editor + **headless dedicated
  server** (H3) with **authoritative Jolt physics in the tick** + **interest management / AOI** (H4) +
  **ack-based delta-encoding** + **client-side snapshot interpolation**.
  Transport + replication + prediction; `NetSession` + PIE launcher; `DedicatedServer` (authoritative ECS
  world + input ingest + a pluggable per-tick sim-step running a `PhysicsScene`, no renderer/audio) + a
  deployable `tools/dedicated_server` binary. **AOI** (`set_aoi_radius`) makes only nearby entities relevant;
  **delta** keeps a per-client acked baseline and sends only entities whose bytes changed (+ despawns),
  self-healing over the Unreliable channel; **interpolation** (`interpolation.h`) renders remote entities at
  a trailing delay between buffered samples. Verified `mp_check` (editor path un-regressed), **`server_check`
  (35)** (physics settle/replicate/determinism; AOI filter/enter/leave/trim; delta changed-in/unchanged-out/
  idle-shrink/convergence; interpolation midpoint/clamp/delay), + live 2-process.
  **Remaining (depth, not blocking):** component-granular delta (currently per-entity Transform blob);
  rotation slerp in interpolation; entity prioritization under an explicit bandwidth budget; zone-server
  handoff (couples with Stage 8).
- **Stage 8 — World** 🟡 core lib verified (grid partition + streaming manager + floating origin, `world_check` 21/21),
  **but not integrated** — confirmed 2026-08-09: **no file under `editor/` references the streaming library.**
  **Remaining:** wire to the editor camera + `gws_jobs` async load; apply origin-rebase to the live scene; HLOD;
  material/anim LOD; procedural terrain/biomes/scatter; authored streamable content.
- **Stage 9 — AI** 🟡 nav (A\*+funnel), **navmesh builder** and **path follower** built + verified (`ai_check` **28**)
  and driving a live editor demo (`skinned_demo.h` — patrol with detour around an obstacle).
  **Remaining:** bake from **real scene/terrain geometry** (the demo bakes a synthetic grid); perception; NPC BTs;
  combat AI (target → ability → execute); utility AI (J3), GOAP (J4), crowds (J5); local avoidance (RVO/ORCA);
  dynamic navmesh updates.
- **Stage 10 — Gameplay** 🟢 **G0–G16 all landed** (2026-08-06 → 08-07), verified by `gameplay_check` (238 assertions):
  framework core (attributes/tags/effects/abilities/damage/events/timers/triggers), player FSM, ECS melee combat,
  stats + progression, items + inventory, economy + crafting, quests, world interaction, NPCs/factions/spawners,
  persistence, multiplayer social, gameplay UI, and the genre modules — shooter, vehicles/racing, building/automation,
  survival/horror, stealth/detection.
  **Remaining:** depth over breadth (see §Sequencing) — per-bone hitboxes, dialogue/cutscenes, save slots + autosave,
  camera systems, ranged targeting/lock-on, and **server-authoritative wiring of G10 onto the live net stack**.
  Four engine-side integrations gate five modules: AI (G8/G16), animation→player (G1), VFX GPU draw (G12),
  Jolt vehicle constraints (G13).
- **Stage 11 — Editor & tooling** 🟢 strong — panels/gizmo/inspector/undo/asset-browser/terminal + **terrain editor** +
  **scene-bound sky**, **Blueprint-style logic graph** (`gameplay_logic.h` + `.logic` sidecar + node canvas),
  **Unreal-style asset browser** (tiles/grid + categorized New), **Unity-style inspector** (component cards +
  Add-Component search), **`gws_diagnostics`** crash reporting, + the project **Hub** (own repo, `WorldShaper-Hub`).
  **Remaining:** animation state-machine graph editor, richer debug visualization (navmesh/AI/froxels), and the whole
  of [`DEVELOPER_EXPERIENCE.md`](DEVELOPER_EXPERIENCE.md) — material graph, VFX graph, timeline, curve editors,
  level-design toolkit, command palette, editor extensibility.
- **Stage 12 — Scripting** 🟢 done — **exceeds plan**: Python + C++ + C# backends all built + verified (`script_check`)
  with hot reload (plan only asked for Lua).
- **Stage 13 — Platform** 🔴 Windows-only. **Remaining:** Linux (+ the headless server), consoles, mobile.
- **Stage 14 — Performance infrastructure** 🟢 all 5 pillars built + in the unified overlay (`profiler_check`).
  **Remaining:** captured-baseline regression gating in CI.
- **Stage 15 — Modern AAA** 🔴 mostly not started. DDGI is a step toward Lumen; **remaining:** Nanite-like virtual geometry,
  Lumen-grade GI, crowd sim, world sim, destruction.

---

## Prioritized remaining work

Rewritten 2026-08-09 to span all three tracks: **runtime** (this file), **tool**
([`DEVELOPER_EXPERIENCE.md`](DEVELOPER_EXPERIENCE.md)) and **product**
([`PRODUCT_AND_ECOSYSTEM.md`](PRODUCT_AND_ECOSYSTEM.md)).

**Tier 0 — hygiene, hours not weeks (do first, unblocks everything else)**
- **R1 · Declare the four missing submodules** in `.gitmodules` — turns CI green, makes clean clones build, ends
  manual release uploads. Smallest fix with the largest reach in the whole repo.
- **R2 · Point the repo at the branch that has the engine on it** — merge into `main` or change the default branch,
  then resolve `main`'s 16 divergent commits.
- **The engine CLI** (cook · test · build · run-headless · screenshot) — unlocks CI, automation and agent workflows
  at once; nearly every later item assumes it exists.

**Tier 1 — unblocks the actual game**
- Finish the **ECS migration** (make ECS authoritative; retire the OOP shadow) — cleans up gameplay/save/net.
- **World-streaming integration** (wire `gws_world` into the editor camera + `gws_jobs` async load; apply rebase to
  the live scene) — makes the open world real.
- **AI integration** — point `NavMeshBuilder` at real terrain + collider geometry, then perception and NPC BTs.
- **GPU-driven rendering completion** (3.1) — the instance-count enabler for open world *and* G14 factory scale.
  Note the indirect path already exists and is switched off; mesh shaders do not.
- **Animation → player** (`SkinnedMeshComponent` + drive the character) — the gap between a working animation stack
  and a visible animated character.
- ✅ **Networking** — Stage 7 feature-complete.

**Tier 2 — depth and reach**
- **Shader/PSO precompilation** — removes the defining stutter of the era before any project is big enough to expose it.
- **Adopt `parallel_for`** in culling / LOD / animation sampling — the ceiling is built and unused.
- **Material graph** — unlocks art direction and non-photoreal rendering; without it every game on this engine looks the same.
- **Deterministic replay** — cheapest while systems are still deterministic; feeds crash reports, regression tests and photo mode.
- **2D as a first-class mode** — the largest single expansion of what games are possible here.
- Gameplay depth: per-bone hitboxes, dialogue/cutscenes, camera systems, save slots.
- Audio: bus graph, reverb, DSP, streaming.
- Animation: motion matching; anim state-machine graph editor.

**Tier 3 — fidelity / scale / platform**
- Virtual-texture runtime + Nanite-like virtual geometry; virtual shadow maps; hair/skin/material layering.
- Destruction/cloth/ropes; crowd + world simulation.
- Platform layer (Linux + headless server first — it is where dedicated servers deploy — then web, then consoles/mobile).
- Foundation adoption cleanup (editor inspector/save onto core reflection/serialization) + **R3 legacy removal**.

---

## The two non-runtime tracks (new 2026-08-09)

The 16 stages are necessary and **not sufficient**. Two companion plans now carry the rest:

| Track | Doc | Covers |
|---|---|---|
| **Engine as a tool** | [`DEVELOPER_EXPERIENCE.md`](DEVELOPER_EXPERIENCE.md) | Inner-loop budgets gated in CI · hot-reload everything · keep play-mode changes · material/VFX/timeline/curve editors · level-design toolkit · command palette · editor extensibility · "explain why this is broken" · PSO stutter · upscaling · scalability + budgets · **2D**, cameras, NPR, web, XR, split-screen, lockstep RTS |
| **Product & ecosystem** | [`PRODUCT_AND_ECOSYSTEM.md`](PRODUCT_AND_ECOSYSTEM.md) | Version control for binaries · repo scaffolding + LFS · semantic scene diffs · package registry · shared DDC · symbol server · **deterministic replay** · remote profiling · Ship tab + Steam · patching · localisation + accessibility · telemetry + remote config · **agent-ready CLI** · **GitHub integration** · **Hub as an ops console w/ upgrade preflight** · modding/UGC |

### Editor-as-product track — the Hub

The Hub is a shipped product in its own repo (`WorldShaper-Hub` — v0.1.7, 5 releases, **green CI**): standalone
launcher, per-project sandbox, versioned engine installs, self-update, uninstall flows, per-project module toggles,
folder import. It is not part of the 16-stage roadmap — but it is **not finished as a product**: the Hub is where
templates, the Ship tab, project health and upgrade preflight land. See
[`PRODUCT_AND_ECOSYSTEM.md`](PRODUCT_AND_ECOSYSTEM.md) §3.2. The Hub repo has **no planning doc of its own**; that
section is currently its plan of record.
