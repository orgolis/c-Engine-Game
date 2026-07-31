# Remaining Work — verified current state

**Last verified:** 2026-07-30
**Companion to:** [`AAA_ENGINE_MASTER_PLAN.md`](AAA_ENGINE_MASTER_PLAN.md) (its §1 has the quick
16-stage status table; this file is the detailed done/remaining breakdown).

> "Verified" below means a headless `tools/*_check` **or** a live editor session confirms it — not
> that a doc claims it. The master plan's older per-stage STATUS blocks predate ~6 weeks of work; this
> file supersedes them.

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
- **Stage 7 — Networking** 🟢 core + multiplayer live in-editor + **headless dedicated server** (H3).
  Transport + replication + prediction; `NetSession` + PIE launcher; `DedicatedServer` (authoritative ECS
  world + input ingest at a fixed tick, no renderer/audio) + a deployable `tools/dedicated_server` binary —
  verified `mp_check`, **`server_check` (12)**, + live 2-process.
  **Remaining:** **interest management / AOI** (H4); delta-encoding + snapshot interpolation; stepping Jolt
  physics inside the server tick.
- **Stage 8 — World** 🟡 core lib verified (grid partition + streaming manager + floating origin, `world_check` 21/21),
  **but not integrated.**
  **Remaining:** wire to the editor camera + `gws_jobs` async load; apply origin-rebase to the live scene; HLOD;
  material/anim LOD; procedural terrain/biomes/scatter; authored streamable content.
- **Stage 9 — AI** 🟡 nav (A\*+funnel) + behavior trees built + verified (`ai_check` 21/21) — "Stage 9 start."
  **Remaining:** utility AI (J3), GOAP (J4), crowds (J5); integration — bake navmesh from the scene, agent path-follow,
  perception, NPC BTs; local avoidance (RVO/ORCA); dynamic navmesh updates.
- **Stage 10 — Gameplay** 🟡 partial — frame-data combat, abilities, character controller, VFX, game-UI framework exist.
  **Remaining:** loot/itemization, vehicles, quests/progression; full server-authoritative wiring.
- **Stage 11 — Editor & tooling** 🟢 strong — panels/gizmo/inspector/undo/asset-browser/terminal + **terrain editor** +
  the project **Hub** (now its own repo, `WorldShaper-Hub`).
  **Remaining:** animation state-machine graph editor, visual scripting, richer debug visualization (navmesh/AI/froxels).
- **Stage 12 — Scripting** 🟢 done — **exceeds plan**: Python + C++ + C# backends all built + verified (`script_check`)
  with hot reload (plan only asked for Lua).
- **Stage 13 — Platform** 🔴 Windows-only. **Remaining:** Linux (+ the headless server), consoles, mobile.
- **Stage 14 — Performance infrastructure** 🟢 all 5 pillars built + in the unified overlay (`profiler_check`).
  **Remaining:** captured-baseline regression gating in CI.
- **Stage 15 — Modern AAA** 🔴 mostly not started. DDGI is a step toward Lumen; **remaining:** Nanite-like virtual geometry,
  Lumen-grade GI, crowd sim, world sim, destruction.

---

## Prioritized remaining work (for the target game: open-world MP action-RPG)

**Tier 1 — unblocks the actual game**
- Finish the **ECS migration** (make ECS authoritative; retire the OOP shadow) — cleans up gameplay/save/net.
- **GPU-driven rendering completion** (3.1) — the open-world instance-count enabler.
- **Networking: interest management (AOI)** + delta-encoding — the remaining scale pieces (the headless
  dedicated server is done + verified; step physics inside its tick next).
- **World-streaming integration** (wire the verified `gws_world` lib into the editor/world) + **AI integration**
  (bake navmesh, agent path-follow, perception) — makes the open world + enemies real.

**Tier 2 — depth**
- Gameplay: loot/inventory, vehicles, quests/progression.
- Audio: bus graph, reverb, DSP, streaming.
- Animation: motion matching; anim state-machine editor; player hookup.

**Tier 3 — fidelity / scale / reach**
- Virtual-texture runtime + Nanite-like virtual geometry; virtual shadow maps; hair/skin/material layering.
- Destruction/cloth/ropes; crowd + world simulation.
- Platform layer (Linux + headless server, then consoles/mobile).
- Foundation adoption cleanup (editor inspector/save onto core reflection/serialization; all hot systems on `parallel_for`).

---

## Editor-as-product track (separate from the engine roadmap) — DONE

The Hub is a shipped product in its own repo (`WorldShaper-Hub`): standalone launcher, per-project sandbox,
CPack/NSIS installer, and engine-version install/update pulled from the engine repo's GitHub Releases (private
repo via a GitHub token). It is intentionally NOT part of this engine repo or the 16-stage roadmap.
