# Project Schizo / Game-Worldshaper — Documentation Index

**Last updated:** 2026-07-27

This folder holds all design, architecture, and planning documentation for the engine
(`gws::` / `schizo::` namespaces, Vulkan renderer under `gws::renderer::gpu::`). Start
with the source-of-truth roadmap below, then drill into the folder that matches your task.

---

## 🧭 Start here — the source of truth

**[`EngineMasterPlan/AAA_ENGINE_MASTER_PLAN.md`](EngineMasterPlan/AAA_ENGINE_MASTER_PLAN.md)**
is the single, dependency-ordered roadmap. It maps the 15-pillar AAA engine plan onto the
*actual current state of the codebase* and re-orders it into **16 stages** by hard dependency.
When any other doc disagrees with reality, this one wins — and it verifies claims against
`*.cpp`, not against status docs.

It has two **companion** plans (still current, narrower scope):

| Companion | Scope |
|-----------|-------|
| [`EngineTechnicalRework/EXECUTION_CHECKLIST.md`](EngineTechnicalRework/EXECUTION_CHECKLIST.md) | Renderer rework, the OpenGL→Vulkan migration (Phases 0–7) |
| [`phase-6-planning/IMPLEMENTATION-ROADMAP.md`](phase-6-planning/IMPLEMENTATION-ROADMAP.md) | Gameplay systems (character controller, abilities, networking) |

> **Historical note:** the Phase 0–7 renderer plan and the Phase-6 gameplay plan predate the
> master plan. They remain accurate for their subsystems and are referenced by the master plan,
> but they are *not* the overall project status. Treat their "current phase" wording as historical.

---

## 📂 Folder map

### `EngineMasterPlan/` — current roadmap
- `AAA_ENGINE_MASTER_PLAN.md` — **the 16-stage living roadmap (read this first)**

### `EngineTechnicalRework/` — renderer rework planning (Phases 0–7)
- `EXECUTION_CHECKLIST.md` — phase/week checklist for the Vulkan migration
- `REWORK_MASTER_PLAN.md` — renderer redesign strategy
- `DEVELOPMENT_PLAN.md` — renderer development roadmap
- `DEPENDENCIES_AND_LIBRARIES.md` — build dependencies & Vulkan SDK setup
- `CLEANUP_GUIDE.md` — file removal/archival guidance for the migration
- `DOCUMENTATION_PACKAGE_SUMMARY.md` — index of the rework doc package

### `architecture/` — technical system design
- `ENGINE_ARCHITECTURE_OVERVIEW.md` — overall engine architecture
- `GRAPHICS-ABSTRACTION-LAYER.md` — render API abstraction
- `PHASE_4_DEFERRED_PIPELINE.md` — deferred rendering pipeline
- `PHASE_5_CULLING.md` — frustum culling
- `HZB_OCCLUSION_CULLING.md` — hierarchical-Z occlusion culling
- `PHASE_6_EDITOR_AND_HZB_CULLING.md`, `PHASE_6_EDITOR_COMPLETE.md` — editor + HZB
- `PHASE_7_GPU_PROFILER.md`, `PHASE_7_GPU_PROFILER_COMPLETE.md` — GPU profiling
- `physics_module.md` — physics (Jolt) system

### `systems/` — engine subsystem deep-dives
`DEFERRED-RENDERING-SYSTEM.md` · `LIGHTING-SYSTEM.md` · `MATERIAL-AND-PBR-SYSTEM.md` ·
`MESH-AND-GEOMETRY-SYSTEM.md` · `POST-PROCESSING-SYSTEM.md` · `SCENE-MANAGEMENT-SYSTEM.md`

### `references/` — quick-reference cheat sheets
One `*-QUICK-REFERENCE.md` per system (deferred rendering, graphics, lighting, material,
mesh, post-processing, scene management) plus `WEEK-5-6-QUICK-REFERENCE.md`.

### `testing/` — testing & debugging guides
Camera debug/diagnostic guides (`CAMERA_*.md`), `PHYSICS-TESTING.md`, `TESTING_GAMEPLAY.md`,
`QUICK_START_CAMERA_TEST.md`, `IN_GAME_CAMERA_TEST.md`.

### `phase-6-planning/` — gameplay systems planning
Detailed specs and process docs: `IMPLEMENTATION-ROADMAP.md` (companion plan above),
`CHARACTER-CONTROLLER-SPECIFICATION.md`, `ABILITY-SYSTEM-SPECIFICATION.md`,
`NETWORKING-SPECIFICATION.md`, `SYSTEM-INTERACTIONS-DETAILED.md`, `TESTING-STRATEGY.md`,
`CODE-REVIEW-CHECKLIST.md`, `INTEGRATION-CHECKPOINTS.md`, `BUILD-COMPILATION-GUIDE.md`,
`RISK-CONTINGENCY-PLAN.md`, `ARCHITECTURE-COMPLETE-SYSTEMS.md`, and the
`DOCUMENTATION-NAVIGATION-GUIDE.md` / `COMPLETE-DOCS-OVERVIEW.md` overviews.

### `design/` & `game-design/` — game design
- `design/GAME-DESIGN.md` — game design document
- `game-design/project-schizo.md` — *Project Schizo* concept (open-world multiplayer action-RPG)

### `archive/` — completed & historical work
Finished phase summaries and superseded planning docs (Phase 1–3 status, Phase 6 week-by-week,
legacy roadmap, early pre-planning). Kept for "how we did X before" reference only — not current.

---

## Quick navigation

| I want to… | Go to |
|------------|-------|
| Know what to build next | `EngineMasterPlan/AAA_ENGINE_MASTER_PLAN.md` |
| Understand the renderer | `architecture/` + `systems/` |
| Look something up fast | `references/` |
| Debug / test a system | `testing/` |
| See the game vision | `design/GAME-DESIGN.md`, `game-design/project-schizo.md` |
| Find old completed work | `archive/` |

---

## Maintenance rules

- **One source of truth:** keep overall status in `AAA_ENGINE_MASTER_PLAN.md`. Don't create new
  top-level "status" or "reorganization" docs at the repo root — they drift out of date fast.
- **Archive, don't scatter:** move completed phase/week docs into `archive/`.
- **Keep this index current:** when you add a top-level folder or a companion plan, update the map above.
- **Verify before trusting a ✅:** status ticks reflect intent; confirm against source.
