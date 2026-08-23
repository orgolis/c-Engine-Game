# VFX authoring (workflow item 4.3) — design

**Created:** 2026-08-23
**Status:** approved, not yet implemented
**Item:** [`WORKFLOW_PLAN.md`](../../EngineMasterPlan/WORKFLOW_PLAN.md) Phase 4.3 — *"VFX graph — effects become design work rather than code"*

---

## 1. What is actually missing

The plan records 4.3 as an unbuilt node graph. Reading the code first changes the problem:

**The simulation is fixed.** [`ParticleSystem`](../../../engine/core/vfx/particle_system.h) has one `EmitterConfig` of
14 scalar fields and a hardcoded pipeline — spawn by rate, integrate under gravity and drag, lerp size and colour
over life. There is no seam a graph could attach to.

**The component is thinner than the config it feeds.** `ParticleEmitterComponent` carries 6 fields;
[`particle_emitter_cache.h`](../../../editor/include/particle_emitter_cache.h) copies exactly those six into an
`EmitterConfig` and lets the other eight — lifetime, base velocity, spread, gravity, drag, size start/end,
world-space — take library defaults. **Today an effect cannot be authored at all** beyond spawn rate and two
colours, from the inspector, a scene file, or C++ per entity.

**No Phase 4 document persists.** `MaterialGraph`, `AnimGraph` and `Sequence` have no serialisation — verified by
grep, not assumed. 4.4's own notes concede it. A VFX document would be the fifth that vanishes on editor close,
and unlike the other four it is *meaningless* unsaved: an effect is shared across many entities and outlives any
one scene.

So 4.3 is not "add a canvas". It is: make the simulation describable, make the description saveable, and make the
component point at it.

---

## 2. The decision, and the two it was taken over

**Chosen: an ordered module stack that the simulation interprets.** Stages — Spawn, Initialize, Update — each hold
an ordered list of modules. The document is a program; `ParticleSystem` becomes an interpreter over it.

**Rejected — a property graph over the fixed pipeline.** Cheap, and it would close the eight-unauthorable-fields
gap, but nodes that only produce *values* into a fixed integrator can never express "add a vortex", "orbit the
emitter axis", or "kill particles below y=0" — the second thing anyone asks for. It would meet the phase exit
criterion on a technicality and then be rewritten. This project's recurring lesson is *finish before you start*.

**Rejected for now — codegen to a GPU compute simulation.** The right ceiling eventually; wrong cost today. It
makes the existing verified CPU simulation a second implementation that must agree with a generated one, and
disagreement between two simulators is invisible — both run, both look plausible, and play mode differs from
preview. 4.1 already had to promote byte-identical determinism to a *correctness* requirement to make codegen
safe. Timing weighs too: v0.6.15–v0.6.20 were six releases spent on a GPU path failing silently, and 3.9's own
billboard pass shipped a bug that passed validation and drew garbage.

**The bet that makes the rejection reversible** is §3.

---

## 3. The execution contract

Every module is a pure function of one particle:

```cpp
void apply(Particle& p, const ModuleParams& params, const Context& ctx);
```

`Context` carries `dt`, normalised age `t`, the emitter transform, and a **per-particle** RNG. Modules may not read
the scene, may not read other particles, may not allocate, and may not hold state across frames.

That restriction is the whole architectural bet: it is exactly the shape a compute shader can execute, so
generating one later is transcription rather than redesign. Violating it once — a module that queries physics, or
keeps a running average — makes GPU simulation a rewrite. It costs nothing to honour today.

It also keeps the sim replayable, which workflow item 5.1 (deterministic replay) depends on and which the plan
warns gets harder the longer it is deferred.

### 3.1 Two consequences that follow from the current code

**`Particle` gains `size` and `color`.** Today neither is particle state:
[`build_billboards`](../../../engine/core/vfx/particle_system.cpp) computes both from `cfg_` at render time. Under
the contract they become state that Update modules write and the billboard builder only reads — which is also what
a GPU sim requires, since compute writes the buffer and the vertex stage reads it. The struct grows from 9 floats
to 14.

**Module order is semantic, not cosmetic.** The current integrator applies gravity and *then* damps the result:

```cpp
p.vel += cfg_.gravity * dt;
p.vel *= damp;
```

Reversing those gives different motion. So each stage stores an **ordered list**, reordering is an edit that
changes the effect, and the serialiser must preserve file order. A format that emitted modules in map order would
silently alter every saved effect on load — a corruption with no error and no crash.

---

## 4. The v1 module set

| Stage | Modules | What it replaces |
|---|---|---|
| **Spawn** — how many particles this frame | `SpawnRate`, `SpawnBurst` | the `spawn_accum_` loop; `emit_burst()` |
| **Initialize** — once per particle, at birth | `InitLifetime`, `InitVelocityCone`, `InitSize`, `InitColor`, `InitPositionShape` (point / sphere / box) | `spawn_one()`'s five RNG draws — plus emitter *shape*, which does not exist today |
| **Update** — every particle, every frame | `Gravity`, `Drag`, `SizeOverLife`, `ColorOverLife`, `VelocityOverLife` | the integrator, and the two render-time `mix()` calls |

`SizeOverLife` takes a `gws::anim::Curve`; `ColorOverLife` takes a `Gradient`. This retires the two-colour storage
limit that 4.5 had to surface in the UI rather than silently drop a dragged stop, and gives `Curve` the second real
consumer its notes were waiting on.

### 4.1 The default stack

`SpawnRate` · `InitLifetime` · `InitVelocityCone` · `InitSize` · `InitColor` · `Gravity` · `Drag` ·
`SizeOverLife` · `ColorOverLife`, in that order, carrying today's `EmitterConfig` defaults.

An emitter that has never been edited therefore behaves as it does now. That is the regression gate: `vfx_check`
and `emitter_check` must pass **unmodified**.

**`EmitterConfig` is retained as a compatibility façade, not deleted.** `ParticleSystem(const EmitterConfig&)`
keeps working by synthesising the default stack from those 14 fields. This is load-bearing for the regression gate:
`vfx_check` constructs a `ParticleSystem` from an `EmitterConfig` in every one of its six groups, so removing the
constructor would force the check to be rewritten — and a rewritten regression test proves nothing about the
rewrite it is meant to police. The same synthesis serves old scene files (§7), so one mechanism covers both.

**What is deliberately not claimed: an identical RNG stream.** Reordering the draws changes *which* particles you
get, though not their distribution. The existing checks tolerate this — check 1 accepts 99–101 particles, check 2
sets `velocity_spread = 0` precisely so its assertion is exact without randomness. A *saved* effect would not
tolerate it, which is the second reason module order belongs in the format.

### 4.2 Explicitly out of scope for v1

Sub-emitters, GPU simulation, particle collision, ribbons / trails / mesh particles, and event or data interfaces.
Each is a real feature; none is required by the phase's exit criterion (*"author a particle effect without opening
a C++ file"*), and ribbons in particular need a second render path when only the billboard pass exists.

---

## 5. The document

`VfxGraph` — plain data, ImGui-free, renderer-free. The same document/runtime split
[`anim_graph.h`](../../../editor/include/anim_graph.h) argues for: the document is editable and inert, the
`ParticleSystem` is live and mid-flight, and an editor should not poke at the second while the user drags in the
first.

**Placed in `engine/core/vfx/`, not `editor/`** — differing from `AnimGraph` on purpose, because a shipped game
must load `.vfx` files and therefore the document cannot live editor-side.

`validate()` returns human-readable problems, because a VFX stack fails silently exactly as a state machine does:

- no `SpawnRate` and no `SpawnBurst` — emits nothing, reads as a broken renderer
- `ColorOverLife` with no `InitColor` before it — not an error, just invisibly the wrong colour
- an Update module in the Initialize stage, or the reverse — runs at the wrong frequency
- `max_particles` of 0 — silently draws nothing

Parameters are held as a small tagged value type (`float` / `vec3` / `vec4` / `Curve` / `Gradient`) rather than a
raw struct per module, so a later version can replace a constant parameter with a computed sub-graph without
breaking the file format.

---

## 6. The `.vfx` text format

Mirrors [`material_desc.h`](../../../engine/core/assets/material_desc.h) rather than inventing conventions:

- `kVfxExtension = ".vfx"`, `kVfxFormatVersion` stamped into every file
- **every field written, including defaults** — so "did the editor fail to save this, or was it just default?" is
  answerable from the file alone
- unknown keys ignored, missing keys defaulted — additive fields need no version bump
- `vfx_from_text()` always succeeds, yielding defaults for garbage; only `load_vfx()` distinguishes *absent* from
  *unparseable*, because conflating them is how a missing asset becomes a silently invisible effect
- text, not binary — the position settled in 3.7, because assets live in version control and a binary blob makes
  every edit an unreviewable diff

The one addition the material format does not need: modules are an ordered sequence, written as indexed blocks and
read back **in file order, never sorted**.

**Indexing is explicit.** `STAGE=` is a delimiter: every `MODULE.n.*` line belongs to the stage most recently
named. `n` is a whole-file counter that only groups one module's keys together — it is *not* a position, and the
loader ignores its value entirely, appending modules to the current stage in the order the `KIND` lines appear.
Two modules sharing an index, or indices that skip or descend, therefore load correctly rather than reordering an
effect. The counter exists only so a human editing the file can tell which keys belong together.

```
# GameWorldshaper VFX
VFX_VERSION=1
NAME=campfire_embers
MAX_PARTICLES=2048
WORLD_SPACE=1
STAGE=SPAWN
MODULE.0.KIND=SpawnRate
MODULE.0.RATE=50
STAGE=INIT
MODULE.1.KIND=InitLifetime
MODULE.1.MIN=1
MODULE.1.MAX=2
...
```

---

## 7. The component / asset boundary

```cpp
struct ParticleEmitterComponent {
    std::string vfx_path;         // the .vfx asset
    bool  enabled  = false;       // "this entity is an emitter" — per instance
    bool  emitting = true;        // pauses spawning, keeps live particles
    float rate_scale = 1.0f;      // per-instance override
};
```

The eight silently-unauthorable fields stop being a problem by ceasing to exist on the component — the effect owns
them.

`rate_scale` multiplies the count the Spawn stage produces for that instance, applied after the stage runs and
before particles are created; it does not reach into a module's parameters, which stay owned by the shared asset.
`0.0` means "spawn nothing" and is distinct from `emitting = false`, which also stops spawning but is the
per-frame toggle rather than a saved property of the placement.

**Old scenes still load.** The existing keys (`spawn_rate`, `max_particles`, `color_start`, `color_end`) parse into
a synthesised default stack held in memory, matching how `apply_reflected_line` already declines lines it does not
own rather than consuming them (3.7). No scene file needs editing, and none is rewritten until saved.

`EditorParticleEmitters` keeps its shape and its rebase contract. It builds its `ParticleSystem` from a loaded
`VfxGraph` instead of six copied fields, and registers with the shared `AssetWatcher` so editing a `.vfx` hot-reloads
it — consistent with 2.4, which settles the file before firing.

---

## 8. Editor surface

**A per-stage module stack is a list, not a graph — and the v1 UI is a stack panel, not `NodeCanvas`.**

The module model was chosen on its merits, but it does not want a canvas: ordered rows with drag-to-reorder and a
parameter block per module is the correct UI for it. Unity's Shuriken is a stack; Niagara is a stack whose
*modules* are internally graphs — the graph earns its place one level down, later, when a module's parameters
become computed rather than constant. Forcing the canvas into v1 to justify the word "graph" in the item title
would be building the wrong UI for a good architecture.

Consequence: [`node_canvas.h`](../../../editor/include/node_canvas.h)'s claim that *"four Phase 4 items need one of
these"* is wrong about 4.3, and the comment is corrected rather than satisfied. The canvas keeps its two real
consumers (4.1, 4.6).

Panel contents: the stage list, an "add module" menu per stage filtered to modules legal in that stage, drag to
reorder, per-module parameter widgets drawing `Curve` and `Gradient` through the existing 4.5 editors, `validate()`
output shown inline, and a live particle/vertex count so "the simulation is running" is visible rather than taken
on faith.

---

## 9. Testing

New `vfxgraph_check`, headless, in the house style — assertions about the properties that fail *silently*:

1. Module order changes the result — `Gravity·Drag` differs numerically from `Drag·Gravity`
2. The default stack matches the pre-rewrite integrator, over multiple steps
3. `.vfx` round-trips, **including module order**
4. A file with modules written out of index order loads in file order, not sorted order
5. A hand-mangled / truncated file yields defaults, not garbage
6. `load_vfx()` distinguishes absent from unparseable
7. `validate()` catches each silent-failure case in §5
8. A module cannot observe another particle — asserted structurally by the `Context` it is handed
9. Two entities sharing one `.vfx` still get independent streams (the existing per-entity seeding)
10. `Curve`/`Gradient` parameters survive the round trip with their key/stop counts intact

**Regression gate:** `vfx_check` (12 assertions) and `emitter_check` pass **unmodified**. If either needs editing,
the default stack is not faithful and that is the bug.

---

## 10. Out of scope, recorded so it is not re-litigated

- GPU compute simulation — enabled by §3, deliberately not built here
- Sub-emitters, collision, ribbons/trails/mesh particles, event & data interfaces
- Retrofitting `.vfx`-style persistence onto the other four Phase 4 documents — the helpers are written so it is
  possible, but doing it is its own item
- A node canvas for module internals — §8, revisit when parameters become computed
