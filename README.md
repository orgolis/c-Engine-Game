# GameWorldshaper

A Vulkan game engine and editor for Windows, written in C++20.

Deferred renderer, ECS gameplay layer, Jolt physics, spatial audio, and an editor
you can extend in Python, C++ or C# without rebuilding it. Projects are launched and
kept up to date through the [GameWorldshaper Hub](https://github.com/orgolis/WorldShaper-Hub).

> **Licence:** source-available, not open source. Free to use, learn from and ship
> with; attribution required; a 5% royalty applies only above USD 100,000 lifetime
> revenue per product. See [Licence](#licence).

---

## Status

**v0.8.4.** Usable and under active development by one person. Windows-only today —
Linux presets exist and configure, but nothing verifies them ([#66](https://github.com/orgolis/c-Engine-Game/issues/66)).

| | |
|---|---|
| Renderer | Vulkan 1.3, deferred, PBR, optional hardware ray tracing |
| Physics | Jolt |
| Audio | miniaudio, spatialised, with a mix-bus mixer |
| Scripting | Python (pocketpy), C++ (compiled to a DLL), C# (.NET) |
| Tests | 67 headless check binaries, run through `gws test` |

Development is tracked in [`docs/EngineMasterPlan/`](docs/EngineMasterPlan/) —
[`WORKFLOW_PLAN.md`](docs/EngineMasterPlan/WORKFLOW_PLAN.md) is the ordering,
[`REMAINING_WORK.md`](docs/EngineMasterPlan/REMAINING_WORK.md) is the verified state.
Phases 0–4 are complete; Phase 5 is next.

---

## What is in it

**Rendering** — deferred G-buffer, clustered lighting, cascaded and ray-traced
shadows, SSAO/GTAO, screen-space reflections, DDGI probe GI, volumetric fog and
light shafts, raymarched clouds, bloom, TAA, ACES tonemapping, bindless textures,
GPU-driven indirect draws, HZB occlusion culling.

**Editor** — docked panels, transform gizmos, undo/redo covering gameplay
components and terrain, an asset browser with drag-and-drop, terrain sculpting, a
node-based material graph that compiles to SPIR-V, a VFX module stack, a timeline
sequencer, an animation state-machine graph, curve and gradient editors,
level-design tools (snapping, arrays, splines, scatter), a command palette, and an
audio mixer.

**Gameplay** — an ECS with attributes, tags, effects, abilities, damage, inventory,
crafting, quests, factions, NPCs and save/load, plus genre modules for shooters,
vehicles, building, survival and stealth.

**Tooling** — `gws`, a CLI covering `test`, `cook`, `validate`, `docs`, `project`,
`run`, `build`, `crash`, `shaders` and `screenshot`. Crash reports symbolise against
published debug symbols.

---

## Getting started

### Just want to use it

Install the [Hub](https://github.com/orgolis/WorldShaper-Hub), then
**Engine Versions → Check for Updates**. It downloads a release, manages versions and
launches the editor. You do not need to build anything.

### Building from source

**Requirements**

- Windows 10/11
- [Vulkan SDK](https://vulkan.lunarg.com/) 1.3+
- CMake 3.20+ and Ninja
- A C++20 compiler. The canonical toolchain is **Strawberry Perl's MinGW g++**, which
  the presets pin by absolute path so the build cannot silently fall back to MSVC.

```sh
git clone --recurse-submodules https://github.com/orgolis/c-Engine-Game.git
cd c-Engine-Game
cmake --preset windows-debug
cmake --build --preset windows-debug
```

`--recurse-submodules` is not optional — ten submodules must populate or configure
fails.

**Run the editor**

```sh
./build/windows-debug/bin/editor.exe
```

**Run the checks**

```sh
./build/windows-debug/bin/gws.exe test
```

Two checks are currently red on `main` and excluded from CI by name
(`shadersource_check`, `terrainmat_check`); three more need a GPU and are skipped on
CI runners.

---

## Performance on weaker hardware

If the editor is slow, open **Post-Processing → Quality** and pick **Low**. It
disables the passes whose cost does not depend on scene complexity — ray-traced
shadows, SSAO, SSR, clouds and volumetric light — and halves the render scale. On
integrated graphics the first launch selects Low automatically.

**Window → Performance (Stage 14) → GPU (N2)** shows per-pass GPU timings sorted by
cost, with a **Copy breakdown** button that puts the numbers on the clipboard. If you
report a performance problem, that paste is the useful thing to include.

The full analysis — what costs what, and why a near-empty scene can still be slow —
is in [`PERFORMANCE_AUDIT.md`](docs/EngineMasterPlan/PERFORMANCE_AUDIT.md).

---

## Extending the editor

Drop a script in `<project>/editor_scripts/` and it adds commands to the editor:

```python
import engine

def on_start(e):
    engine.register_command("Count Entities", "Script", "count_entities")

def count_entities():
    engine.set_status("This scene has " + str(engine.entity_count()) + " entities")
```

Commands appear in the palette (Ctrl+P) and in **Window → Extensions**, and the file
hot-reloads when you save it. The same works in C++ and C#. Use
**Window → Extensions → New Extension…** for a working starting point.

---

## Contributing

Issues and pull requests are welcome. Contributions are licensed to the Licensor
under the terms in [LICENSE](LICENSE) section 9(d) — read it before submitting, since
this is not an open-source project and that clause matters.

Two conventions worth knowing:

- **Every behavioural change ships with a check.** There are 67 of them and they run
  headlessly; a fix without one tends to come back.
- **Assertions target the failure mode, not the happy path.** The bugs that survive
  here are the quiet ones — a pass that reports a plausible wrong number, a document
  that saves without one field. Several checks exist specifically because a green
  test once certified something broken.

---

## Licence

**Source-available. Not open source.** See [LICENSE](LICENSE) for the terms; in short:

- Free to read, build, modify and ship products with, commercially or otherwise.
- **Attribution required** — "Made with GameWorldshaper" and "Engine by orgolis" in a
  splash, credits or About screen of any publicly distributed product.
- **5% royalty** on gross revenue **above USD 100,000 lifetime, per product**. The
  first $100,000 is always free, and non-commercial use always is.
- You may not sell the Engine itself, or a fork of it, as an engine or tool.

Third-party components keep their own licences —
see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), which must be distributed with
the Engine or with anything embedding it. Nothing bundled is copyleft.

Licensing questions and royalty waivers: orgolis123@gmail.com
