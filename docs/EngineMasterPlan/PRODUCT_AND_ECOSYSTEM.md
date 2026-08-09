# Product & Ecosystem — everything around the engine

**Created:** 2026-08-09
**Companion to:** [`AAA_ENGINE_MASTER_PLAN.md`](AAA_ENGINE_MASTER_PLAN.md) (the 16-stage runtime roadmap) ·
[`DEVELOPER_EXPERIENCE.md`](DEVELOPER_EXPERIENCE.md) (the engine as a tool) ·
[`REMAINING_WORK.md`](REMAINING_WORK.md) (verified done/remaining)

> **Why this document exists.** Assume every system in the 16-stage plan is finished and shipping. A developer
> still cannot make a game with it — because **most of what Unity, Unreal and Godot sell is not the runtime.**
> Unity's paid platform is almost entirely backend services; Unreal's is almost entirely build infrastructure and
> content supply. Both spent a decade building the surround, not the renderer.
>
> This document covers that surround. It explicitly includes the **Hub** (`WorldShaper-Hub`), which the engine
> roadmap deliberately excludes — the Hub is where most of this work actually lands.

**Status:** ✅ built + verified · 🟡 partial · 🔴 planned

---

## 0. Strategic read

We will never beat Unity on breadth or Unreal on fidelity, and do not need to. Both are vulnerable in the same
place: the surrounding developer experience is bolted on, resented, or monetised. The engine has three structural
advantages that are hard to copy quickly:

1. **Everything is text and data** (scenes, `.logic`, `.gameplay`, `.items`, prefabs, three scripting languages)
2. **The simulation is deterministic** (Jolt `CROSS_PLATFORM_DETERMINISTIC`, verified bit-identical; seeded RNG)
3. **The Hub already owns the install and project lifecycle** (versioned installs, per-project modules, self-update)

Every item below was chosen because it compounds on one of those three.

---

## 1. What the big three actually ship

| Capability | Unity | Unreal | Godot | Us today |
|---|---|---|---|---|
| Version control built for binaries | Unity VCS | Perforce + UGS | bring your own | 🔴 none |
| Distributed / cloud build | Build Automation | Horde | — | 🟡 CI exists (currently red — see [`REMAINING_WORK.md`](REMAINING_WORK.md) §Repo health) |
| Shared derived-data cache | — | Cloud DDC | — | 🟡 cook DB is local-only |
| Asset marketplace | Asset Store | Fab | Asset Library | 🔴 none |
| Package / plugin manager | UPM | Plugins | community `gd-plug` | 🔴 none |
| Backend services (auth, save, matchmaking, lobby, voice) | UGS, full stack | EOS | third party | 🟡 transport + authoritative server only |
| Analytics & LiveOps | Analytics, remote config | partial | — | 🔴 none |
| **Crash reporting** | Cloud Diagnostics | Crash Reporter | — | ✅ **`gws_diagnostics`** |
| Project templates | Hub templates + custom | templates | basic | 🟡 module toggles only |
| In-editor AI assistant | Unity AI (2026 beta) | scattered | — | 🔴 none |
| Console / store shipping | platform modules | first-party | community | 🔴 Windows only |
| Localisation & accessibility | packages | built in | built in | 🔴 none |
| Modding / UGC pipeline | Unity UGC | editor ships to players | — | 🔴 none |

One row is already green for us and red for Godot: **crash reporting** — built because we needed it. The same
logic points at the rest of this list.

---

## 2. The developer lifecycle — stage by stage

Feature lists hide gaps; a lifecycle exposes them.

### 2.1 Discover and start
| Item | Status | Notes |
|---|---|---|
| **Genre starter templates** | 🟡 | The [feature catalog](ENGINE_FEATURE_CATALOG.md#target-genres--module-coverage) already specifies module sets for 8 reference games. Ship them as one-click templates that open a project which *already runs*. **Cheapest credibility available — the design work is done.** |
| Sample projects that are real games | 🔴 | Not a cube on a plane. One small complete game per genre, with source. Doubles as a regression suite: if a sample breaks on a new engine version, so will a user's project. |
| **Generated documentation** | 🔴 | Every authorable component self-registers via reflection and the script API is one shared table — both can emit reference docs mechanically, so docs cannot drift from code. **Uniquely cheap for this architecture.** |
| In-Hub learning path | 🔴 | Tutorials that open the project they describe. |

### 2.2 Set up and collaborate
| Item | Status | Notes |
|---|---|---|
| Version control understanding binaries | 🔴 | **The deepest gap.** Binary assets cannot merge — whoever commits second silently wins. Git has no native locking; teams past ~15 people / 50 GB migrate to Perforce for exclusive checkout. **Integrate; never reimplement.** |
| **Correct repo scaffolding automatically** | 🔴 | Git LFS must be configured *before* the first commit or the repo bloats within weeks. A new project from the Hub should be born with correct `.gitattributes`, `.gitignore` and LFS tracking. **An afternoon of work; saves users months.** |
| **Semantic diffs for scenes** | 🔴 | Our scenes and sidecars are text, so a diff can say "2 entities added, 1 material changed" instead of 400 changed lines. Unity and Unreal struggle here because their formats fight them. **Structural advantage — exploit it.** |
| Asset locking for artists | 🔴 | LFS supports locks. Surface them as a button in the asset browser, not a command line — the reason artists hate Git is that every workflow assumes a terminal. |

### 2.3 Build content
| Item | Status | Notes |
|---|---|---|
| **Package format + registry** | 🔴 | See §3.5. |
| DCC round-tripping | 🟡 | Importers exist; the watched-folder re-import loop preserving material bindings does not. Blender and Substance live in every real pipeline. |
| **Asset provenance + licensing** | 🔴 | Track where every asset came from (marketplace / AI-generated / in-house) and its licence. Increasingly a legal requirement; **nobody does it well.** `cook_db` already stores content hashes. |
| Shared derived-data cache | 🔴 | Cooked assets + compiled shaders shared across machines and CI so nothing cooks twice. Unreal treats this as essential at scale; we already have content addressing and a dependency graph. |

### 2.4 Iterate and debug
| Item | Status | Notes |
|---|---|---|
| Crash reporting | ✅ | Ahead of Godot. Missing the back half: **symbol server** keeping unstripped binaries per release (we already know reports only symbolize against the same-version unstripped build), signature grouping, and reports arriving somewhere visible. |
| **Deterministic replay** | 🔴 | See §3.1 — the strongest card we hold. |
| Remote profiling on device | 🔴 | The overlay reads five pillars locally; attaching to a running build over the network is what makes it useful on console, phone, or a player's machine. |
| Automated play testing | 🟡 | 20 check binaries prove subsystems headlessly. The missing tier is a scripted play session asserting game-level outcomes — what catches "the quest became uncompletable". |

### 2.5 Ship
| Item | Status | Notes |
|---|---|---|
| **A "Ship" tab in the Hub** | 🔴 | Build targets, icons, version strings, code signing, one-click Steam depot upload. For an indie, Steamworks is the only platform that matters first — achievements, cloud saves, workshop. **The lifecycle currently has no ending.** |
| Patching / delta updates | 🔴 | A 6 GB game means 6 GB patches unless the pak format supports binary deltas. Our container is TOC-indexed and content-addressed, so this is closer than it looks. |
| **Localisation** | 🔴 | String tables, font atlases incl. CJK, right-to-left, subtitle timing. Painful to retrofit once UI code has literals baked in — **which ours does not yet. That window is open now.** |
| Accessibility | 🔴 | Remappable input (input actions are the hook), colourblind-safe palettes, text scaling, screen-reader hooks for menus. Increasingly a store requirement. |

### 2.6 Operate a live game
| Item | Status | Notes |
|---|---|---|
| Telemetry and funnels | 🔴 | Where players quit, which build regressed, session length. Most of what Unity's paid platform is. |
| Remote config / feature flags | 🔴 | Change tuning without shipping a patch. **Our gameplay is entirely data-driven, so values are already separated from code — the hard part is done.** |
| Backend services | 🔴 | Accounts, cloud saves, matchmaking, lobbies. We have transport + an authoritative server; we do not have hosted services. **Integrate rather than build.** |
| Modding / UGC | 🔴 | See §3.6. |

---

## 3. Proposals worth building

Ordered by how much they compound on existing advantages.

### 3.1 Deterministic replay + time-travel debugging 🔴 — *nobody else has this*

Record every session as an **input stream plus a seed**, not a video. Replay reconstructs it frame-exact because
the simulation is deterministic. Scrub backwards, pause on any frame, attach the profiler and inspector to that
past frame as if it were live.

**Why us:** this is normally a multi-year retrofit that fails, because determinism cannot be added after the fact.
We already have it — Jolt compiled cross-platform deterministic and verified bit-identical, prediction re-simulates
unacked input, item rolls use a seedable RNG explicitly so they are net-safe. **The hard prerequisite is done and
paid for.**

- **Killer application:** `gws_diagnostics` already ships a minidump and log ring buffer. **Ship the replay too** —
  a bug report becomes "press play and watch it happen on your machine."
- **Automated testing:** a recorded session becomes a regression test; replay against a new engine version and diff
  the final world state.
- **Marketing:** demos in 15 seconds; no competitor can answer it quickly.
- **Free follow-on:** photo mode (§Variety in [`DEVELOPER_EXPERIENCE.md`](DEVELOPER_EXPERIENCE.md)).

*Leans on:* determinism · `gws_diagnostics` · `snapshot.h`

### 3.2 The Hub as an operations console, not a launcher 🟡 — *where we are already ahead*

Unity Hub installs editors and lists projects, and users find it dull. Ours already does more. Push it toward
being where a developer starts their day.

**The feature that sells it — upgrade preflight.** Engine upgrades terrify developers because breakage is
discovered *after* committing. We have 20 headless check binaries and versioned side-by-side installs. So:
install the new version alongside, run the project's tests and cooks against it in a sandbox, and report exactly
what breaks **before** the developer switches. **No engine offers this**; it is possible for us only because both
halves already exist.

- **Project health:** last build status, test results, crash reports, cook cache size, days since last commit.
- **Template gallery:** the 8 genre presets from our own catalog as real starting points.
- **Ship tab:** targets, signing, Steam upload (§2.5).

*Leans on:* versioned installs · the 20 `*_check` tools · `project.schizo`

### 3.3 An agent-ready engine, not a chat box 🔴

The value is **not** the chat window. It is that our project format is text a model can actually author — scenes,
logic graphs, gameplay sidecars, item definitions, prefabs, and scripts in three languages. Unity and Unreal
assistants are stuck generating snippets because their project state is opaque binary. Ours is not.

**Build the interface, not the assistant.** Ship a headless CLI and a machine-readable protocol over the project:
list/query entities, read/write components, spawn a prefab, run the checks, cook, launch headless, screenshot,
read the log. Then **any** assistant can drive the engine and we never maintain a model. An agent that can run the
game, read the failure and try again is worth far more than one that autocompletes.

- **Safety by construction:** every agent edit arrives as a diff, routed through the existing undo system and
  visible in version control. Nothing mutates the project invisibly.
- **The natural loop:** "the boss fight is too easy" → agent reads the attribute set, adjusts, runs a headless play
  session, reports the result. Needs no new AI capability, only the CLI.
- **Second-order benefit:** everything making the engine agent-drivable also makes it CI-drivable and scriptable.
  **We want the CLI regardless** — it is item 1 in the sequencing below.
- **Where an in-editor chat panel does help:** answering "how do I do X in this engine" against *generated* docs
  (§2.1), so answers stay correct.

*Leans on:* text formats · shared script API · headless mode

### 3.4 GitHub integration, aimed at the people who hate Git 🔴

Programmers already have Git. The underserved are artists and designers, and the reason is consistent: every Git
workflow for binary assets assumes a terminal, and binary files cannot merge.

**Ship the boring 80% first** — a new project from the Hub creates the repo with LFS configured correctly from the
first commit, making the one mistake that ruins game repos impossible. Then an in-editor panel showing **semantic**
changes with lock/unlock buttons on assets.

- **Crash report → issue:** group by stack signature and open a GitHub issue automatically, with the replay
  attached (§3.1). Closes the loop from player crash to tracked bug.
- **Checks on pull requests:** the 20 binaries are a ready-made PR gate — no new test infrastructure.
- **Releases as the update feed:** already how the Hub installs engine versions; the same pattern extends to
  publishing a project's own builds.
- **Do this first:** our own engine CI has never passed and `main` is 117 commits stale
  (see [`REMAINING_WORK.md`](REMAINING_WORK.md) §Repo health). Fixing that is the cheapest possible proof the
  integration works.

*Leans on:* `gws_diagnostics` · `*_check` tools · the Hub release feed

### 3.5 A module registry that works because components self-register 🔴

Third-party packages are painful in Unity because a package must hand-write inspector UI, handle its own
serialization, and solve networking itself. Here, a single `make_authorable<T>` line gives a component inspector
UI, save support and replication at once — so an installed module can be a genuine first-class citizen.

**Do not build a storefront.** Build the format (`module.schizo`), dependency resolution, and install from a Git
URL or release. A marketplace with payments is a business, not a feature. Free and open modules first; commerce
only if a community forms.

*Leans on:* the authorable registry · existing per-project module toggles

### 3.6 Ship the editor to players 🔴 — longer horizon

Modding is usually retrofitted and therefore bad. Our editor is small, projects are sandboxed
(`project_paths` chdir), gameplay is data-driven, and scripting has three hot-reloadable backends. A game built on
this engine could ship a restricted editor as its mod tool, with mods as signed module packages using the §3.5
format.

**Why it matters:** UGC extends a game's life by years and is the clearest reason a studio picks one engine over
another when their game has a community. Not urgent — but decisions made now in the module format either enable it
or foreclose it.

---

## 4. Sequencing

**First — cheap and compounding**
1. **The engine CLI** (§3.3) — cook, test, build, run headless, screenshot. Unlocks CI, agents and automation at once; everything else assumes it exists.
2. **Repo scaffolding on project creation** (§2.2) — LFS correct from commit one. An afternoon.
3. **Genre templates** (§2.1) — the design work is already written down.
4. **Generated documentation** (§2.1) — docs that cannot drift.
5. **Finish the crash loop** (§2.4) — symbol server, signature grouping, auto-filed issues.

**Then — differentiating**
6. **Deterministic replay** (§3.1) — the strongest card, and it gets harder the longer systems accumulate non-deterministic state.
7. **Upgrade preflight in the Hub** (§3.2) — nobody else can offer it; both halves exist.
8. **Module format + registry** (§3.5) — format and resolution only, no storefront.
9. **Ship tab with Steam upload** (§2.5).
10. **Localisation** (§2.5) — only cheap while the UI has no baked-in strings.

**Don't — not for one person**
- **Our own version control system.** Perforce and Git took decades. Integrate; never reimplement.
- **Hosted backend services.** Auth, matchmaking and cloud saves are an operations business with on-call attached.
- **A paid marketplace.** Payments, tax, fraud, disputes, curation. A company, not a feature.
- **Console ports.** NDA SDKs and certification; only with a funded title behind it.
- **Training our own models.** Unity tried with Muse, deprecated it, and moved to third-party frontier models. Skip to the ending.
- **Real-time collaborative editing.** Enormous, and the wrong problem while the audience is small teams.

---

## Sources (researched 2026-08-09)

1. [Unity Gaming Services documentation](https://docs.unity.com/ugs/en-us/manual/overview/manual/unity-gaming-services-home) — service catalogue
2. [Unity Gaming Services overview](https://unity.com/solutions/gaming-services) — LiveOps and DevOps positioning
3. [Horde in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/horde-in-unreal-engine) — build automation, remote execution
4. [Unreal Cloud DDC](https://learn.microsoft.com/en-us/gaming/azure/unreal-cloud-ddc/integrate-with-vs) — shared derived-data cache
5. [Version control for game development](https://www.anchorpoint.app/blog/version-control-for-game-development) — binary assets, locking, Git LFS vs Perforce
6. [Unity Hub project creation and templates](https://docs.unity.com/en-us/hub/project-create)
7. [Unity AI 2026](https://iconpolls.com/blogs/unity-ai-review-2026-assistant-download-plugin-gdc-muse-user-experience-and-faqs) — Muse deprecated, replaced with third-party models
8. [Supporting mods and UGC](https://mod.io/blog/how-to-support-mods-and-ugc-in-your-next-game) — cross-platform modding
9. [Godot `gd-plug`](https://godotengine.org/asset-library/asset/962) — community plugin management
