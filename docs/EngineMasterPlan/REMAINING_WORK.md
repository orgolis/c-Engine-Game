# Remaining Work — corrected current state

**Last updated:** 2026-07-27
**Companion to:** [`AAA_ENGINE_MASTER_PLAN.md`](AAA_ENGINE_MASTER_PLAN.md)

> The master plan's §1 "Current-state snapshot" is the **2026-06-12 baseline** and is now
> superseded — it lists Audio / AI / World / Scripting as absent, but the tree has shipped real
> modules for all of them since. This file is the honest current status and the prioritized
> remaining-work tree. Verify against `*.cpp`, not against ✅ marks.

---

## Done / solid (was "missing" in the June snapshot)

- **Core foundation** — memory, jobs, reflection, serialization, logging, profiler (Pillar A, N)
- **ECS** (D) · **Asset import + cook pipeline** (C1, C2)
- **Renderer** — deferred + PBR + shadows + IBL + sky + SSR + SSAO(HBAO/GTAO/VXAO/RT) +
  volumetrics + **clouds** + **DDGI (realtime GI)** + water + terrain + post-FX
  (tonemap/bloom/FXAA/TAA/auto-exposure) + RT (shadows/AO/reflections) + culling (frustum/meshlet/HZB)
- **Physics** — Jolt (E) · **Audio** core — mixer/spatial/occlusion (G)
- **Networking** — transport/replication/prediction + in-editor multiplayer (H)
- **World** — streaming + floating-origin (I) · **AI** — navmesh + behavior trees (J)
- **Animation** — IK + state machine (F, partial — see blocker below)
- **Scripting** — Python / C++ / C# (L) · **VFX** + **Combat** frame-data · **Game-UI** framework
- **Editor** — panels / gizmo / undo-redo / asset browser / terminal / terrain editor (K)

Roughly the whole pillar map has a real module; what remains is *completion, integration, and reach*.

---

## Remaining work tree

### Tier 1 — blocks the target game (open-world MP action-RPG / looter-shooter)

- **GPU skinning in the Vulkan path** — *the* blocker: skeletal meshes don't render, so animated
  characters aren't visible. Prerequisite for everything character-facing. (F)
- **Networking completion** — headless dedicated-server target, interest management (AOI),
  delta-encoding + snapshot interpolation. (H)
- **World streaming integration** — wire to editor camera + async job load, HLOD, apply
  origin-rebase to camera/entities, author streamable content. (I)
- **AI completion** — bake navmesh from scene, agent path-follow, perception, NPC behavior trees. (J)

### Tier 2 — depth the game needs

- **Gameplay (Stage 10)** — loot/inventory, vehicles, quest/progression (combat + abilities exist).
- **Audio** — bus graph, reverb/DSP, streaming, procedural. (G)
- **Animation** — motion matching; animation editor UI. (F/K)
- **GPU-driven rendering** — indirect draw / mesh-shader completion. (B7)

### Tier 3 — fidelity / scale / reach

- **Virtual-texture runtime** (Stage 8) + **Nanite-like virtual geometry**. (C3 / O1)
- **Virtual shadow maps** (B5); **hair / skin / material layering** (B6); lightmaps / path tracer (B4)
- **Destruction / cloth / ropes** (E4); **crowd + world-sim** (O)
- **Platform layer** — Linux / console / mobile (currently Windows-only). (M)
- **Editor** — project launcher (**in progress, 2026-07-27**), visual scripting. (K)

---

## Editor usability track (active)

**Project launcher + modular feature selection** — make the editor a real program: on launch it
shows a project interface (create new / open recent), and a new project selects which engine
features it uses (modular, runtime-config: all systems compiled in, the project manifest enables
them). Features can be added later from Project Settings. See the launcher work in `editor/`
(`project.h`, `feature_registry.h`, `project_launcher.*`) and [[reference_build_system]] to build.
