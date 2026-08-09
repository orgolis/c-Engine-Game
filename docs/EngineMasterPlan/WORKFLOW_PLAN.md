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
| 0.2 | **Resolve the default branch** — merge the working branch into `main` or change the default; handle `main`'s 16 divergent commits | engine | GitHub currently serves an engine 117 commits out of date |
| 0.3 | Run the `*_check` suite on pull requests | engine | 20 binaries already exist; this is CI configuration, not new tests |
| 0.4 | Publish unstripped binaries per release (symbol server) | engine | Crash reports only symbolize against the same-version unstripped build — without this the crash system is half-useless |

**Exit criteria:** a fresh `git clone --recurse-submodules` configures and builds; CI is green; a crash report
from a released build can be symbolized.

---

## Phase 1 · Automation foundation — 🟠 High · 1–2 weeks

Everything in later phases assumes these exist. The CLI in particular is the single highest-leverage item
in this document.

| # | Item | Repo | Notes |
|---|---|---|---|
| 1.1 | **Engine CLI** — `cook`, `test`, `build`, `run --headless`, `screenshot`, `validate` | engine | Unlocks CI, automation and agent workflows in one move |
| 1.2 | Machine-readable project protocol — query/modify entities and components, run checks, read logs | engine | Makes the engine agent-drivable; edits arrive as diffs through undo + VCS |
| 1.3 | **Generated documentation** from the authorable registry + script API table | engine | Docs that cannot drift from code. Uniquely cheap here |
| 1.4 | Crash report → GitHub issue, grouped by stack signature | engine | Closes the loop from player crash to tracked bug |
| 1.5 | **Repo scaffolding on project creation** — `.gitattributes`, `.gitignore`, LFS tracking correct from commit one | hub | The single mistake that ruins game repos, made impossible |

**Exit criteria:** CI runs the full check suite through the CLI; a new project from the Hub is a correctly
configured Git repo; API docs regenerate from source.

---

## Phase 2 · Inner loop — 🟠 High · 2–4 weeks

The wait between a change and seeing it is most of what a developer will remember about this engine.
Protect it before the project is large enough to make it slow.

| # | Item | Repo | Notes |
|---|---|---|---|
| 2.1 | **`innerloop_check` + published budgets + CI gate** | engine | Edit→play < 1 s, script reload < 500 ms, asset→viewport < 2 s. Fail the build on regression |
| 2.2 | **Precompile every shader pipeline**, warm the cache at load | engine | Removes the defining stutter of this console generation. Inventory every runtime `compile_glsl` site first |
| 2.3 | "PSO compiled during gameplay" counter in the overlay | engine | Should read zero in a shipped build |
| 2.4 | **Hot reload everything else** — shaders, textures, meshes, scenes, component definitions | engine | Principle: treat every required restart as a bug |
| 2.5 | **Keep changes made while playing** | engine | Diff the play world against the authored world. Unity structurally cannot do this; we can |
| 2.6 | Never block the editor thread — import/cook/bake on `gws_jobs` with progress + cancel | engine | |
| 2.7 | Undo covering gameplay components, terrain, logic graph, script/agent edits | engine | Partial undo teaches people not to rely on it |

**Exit criteria:** the budget table in `DEVELOPER_EXPERIENCE.md` §1.1 is measured, met, and enforced in CI.

---

## Phase 3 · Collect the ceilings already paid for — 🟠 High · 2–4 weeks

Every item here finishes something already built and verified. Highest value per hour in the whole plan.

| # | Item | Repo | Notes |
|---|---|---|---|
| 3.1 | **Adopt `parallel_for`** in culling, LOD and animation sampling | engine | Verified job system, currently idle while these run single-threaded |
| 3.2 | **Enable the GPU-driven path** — `gpu_driven_enabled` is false and nothing sets it true | engine | The indirect draw code already exists and is referenced by the HZB culler |
| 3.3 | **World-streaming integration** — camera feed, async load on jobs, origin rebase on the live scene | engine | 21 verified assertions, zero editor references |
| 3.4 | **AI: bake from real geometry** — feed terrain chunks + static colliders to `NavMeshBuilder` | engine | Builder and follower already work; the demo bakes a synthetic grid |
| 3.5 | AI perception + NPC behaviour trees + combat AI (target → ability → execute) | engine | Unblocks G8 encounters and G16 stealth |
| 3.6 | **ECS authority flip** — gizmo/inspector/save write ECS; retire the OOP shadow | engine | Simplifies save, network and gameplay at once |
| 3.7 | Editor inspector + scene save onto core reflection/serialization | engine | The remaining Stage-0 integration debt |
| 3.8 | **`SkinnedMeshComponent`** + drive the player character | engine | The gap between a working animation stack and a visible animated character |
| 3.9 | VFX GPU billboard draw | engine | Unblocks G12 gunfeel |
| 3.10 | Remove or salvage ~7,000 lines of dead pre-ECS code | engine | Salvage the character library's input-buffer + locomotion into G1 |

**Exit criteria:** no verified library is unreferenced by a running scene; the engine renders an animated
character walking a streamed world with an AI agent pathing on baked geometry.

---

## Phase 4 · Make it authorable — 🟠 High · 4–8 weeks

Until these exist, using the engine means writing C++. This is the phase that turns it into a tool other
people can use.

| # | Item | Repo | Notes |
|---|---|---|---|
| 4.1 | **Material / shader graph** | engine | **The most impactful missing editor.** Without it every game made here looks identical. Prerequisite for non-photoreal rendering |
| 4.2 | **Command palette + universal search** | engine | Days of work, disproportionate effect — and one uniform entry point for agents |
| 4.3 | VFX graph | engine | Effects become design work rather than code |
| 4.4 | Timeline / sequencer | engine | Cutscenes, camera moves, ability choreography |
| 4.5 | Curve + gradient editors | engine | Small, used constantly; their absence forces magic numbers into data files |
| 4.6 | Animation state-machine graph editor | engine | Port the logic-graph node canvas — the hard half is solved |
| 4.7 | Level-design toolkit — snapping, arrays, splines, scatter, in-editor greybox | engine | |
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
