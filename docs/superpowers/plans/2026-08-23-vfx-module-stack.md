# VFX Module Stack (item 4.3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make particle effects authorable as a saved `.vfx` asset — an ordered stack of modules that the existing CPU simulation interprets — instead of 6 hardcoded fields on a component.

**Architecture:** Three stages (Spawn, Initialize, Update) each hold an ordered list of modules. Every module is a pure function of one particle given `(dt, normalised age, emitter transform, per-particle RNG)`, which is the shape a compute shader could later execute. `ParticleSystem` becomes an interpreter over a `VfxGraph` document; `EmitterConfig` survives as a façade that synthesises the default stack, so the existing verified checks keep passing unmodified.

**Tech Stack:** C++20 (`gws_vfx` target is `cxx_std_17`, keep it that way), glm, ImGui (editor only), CMake + Ninja, MinGW-UCRT g++. Tests are standalone headless executables in `tools/<name>_check/main.cpp`.

**Spec:** [`docs/superpowers/specs/2026-08-23-vfx-graph-design.md`](../specs/2026-08-23-vfx-graph-design.md)

## Global Constraints

- **Module purity is non-negotiable.** A module reads and writes one `Particle`, plus its `ModuleParams` and the `Context` it is handed. No scene access, no reads of other particles, no allocation, no state persisting across frames. This is the property that keeps GPU codegen mechanical later; breaking it once forfeits that.
- **Module order is semantic.** Stages hold ordered lists. Serialisation preserves file order and never sorts.
- **`vfx_check` and `emitter_check` must pass unmodified** at the end of every task. If either needs editing, the default stack is unfaithful and that is the bug.
- `gws_vfx` stays at `cxx_std_17` and depends only on `glm` plus header-only `engine/core` headers. It must not gain an ImGui or Vulkan dependency.
- New engine-side code lives in namespace `schizo::vfx` (matching its neighbours). `Curve`/`Gradient` come from `gws::anim` via `#include "anim/curve.h"`.
- Text asset conventions follow `engine/core/assets/material_desc.h` exactly: version stamp, every field written including defaults, unknown keys ignored, missing keys defaulted, parse always succeeds.
- Build: `cmake --build build/windows-debug --target <target>`. Run checks from the repo root.

---

### Task 1: Make size and colour particle state

Today `build_billboards` computes size and colour from `cfg_` at render time, which no compute shader could do. This task moves them into `Particle` with **no behaviour change** — the enabling refactor for everything after it.

**Files:**
- Modify: `engine/core/vfx/particle_system.h` (the `Particle` struct)
- Modify: `engine/core/vfx/particle_system.cpp` (`spawn_one`, `update`, `build_billboards`)
- Test: `tools/vfx_check/main.cpp` — **read only**, must pass unmodified

**Interfaces:**
- Consumes: nothing
- Produces: `Particle` gains `float size` and `glm::vec4 color`, written during `update()` and read by `build_billboards()`

- [ ] **Step 1: Add the two fields**

In `engine/core/vfx/particle_system.h`, extend `Particle`:

```cpp
struct Particle {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    float     age  = 0.0f;
    float     life = 1.0f;
    float     seed = 0.0f;   // per-particle [0,1) for variation

    // Size and colour are particle STATE, not render-time arithmetic. They
    // used to be computed inside build_billboards from the emitter config,
    // which a GPU simulation could not do: compute writes the buffer, the
    // vertex stage only reads it. Modules write these; the billboard builder
    // reads them.
    float     size = 0.25f;
    glm::vec4 color{1.0f};
};
```

- [ ] **Step 2: Run vfx_check to confirm it still passes before changing behaviour**

Run:
```bash
cmake --build build/windows-debug --target vfx_check && ./build/windows-debug/bin/vfx_check.exe
```
Expected: PASS — adding unused fields changes nothing. This establishes the baseline.

- [ ] **Step 3: Initialise them at spawn and write them during update**

In `particle_system.cpp`, at the end of `spawn_one()` before `particles_.push_back(p)`:

```cpp
    p.size  = cfg_.size_start;
    p.color = cfg_.color_start;
```

In `update()`, inside the integrate loop, after `p.age += dt;`:

```cpp
        // Size and colour over life. This is the arithmetic build_billboards
        // used to do at render time, moved to where a module will own it.
        const float t = p.life > 0.0f ? glm::clamp(p.age / p.life, 0.0f, 1.0f) : 1.0f;
        p.size  = glm::mix(cfg_.size_start, cfg_.size_end, t);
        p.color = glm::mix(cfg_.color_start, cfg_.color_end, t);
```

- [ ] **Step 4: Read them in build_billboards instead of recomputing**

In `build_billboards()`, replace the three lines computing `t`, `size` and `color` with:

```cpp
        const float size = p.size * 0.5f;      // half-extent; p.size is full width
        const glm::vec4 color = p.color;
```

Delete the now-unused `const float t = ...` line in that function.

- [ ] **Step 5: Run vfx_check to verify no behaviour change**

Run:
```bash
cmake --build build/windows-debug --target vfx_check && ./build/windows-debug/bin/vfx_check.exe
```
Expected: PASS, all 12 assertions, identical output to Step 2.

**If check 3 ("particles fade") fails**, the cause is ordering: `update()` must write `p.color` *after* `p.age += dt` so the same age drives it as before. A particle spawned this frame is aged once before its colour is written, which matches the old render-time behaviour where `build_billboards` saw the already-incremented age.

- [ ] **Step 6: Commit**

```bash
git add engine/core/vfx/particle_system.h engine/core/vfx/particle_system.cpp
git commit -m "refactor(vfx): size and colour become particle state, not render-time maths

build_billboards computed both from the emitter config every frame. A GPU
simulation cannot work that way -- compute writes the buffer and the vertex
stage only reads it -- so they move into Particle now, before modules need
to own them. No behaviour change: vfx_check passes unmodified.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: The module data types

Data only — no execution yet. A tagged parameter type rather than a struct per module, so a later version can replace a constant with a computed sub-graph without breaking the file format.

**Files:**
- Create: `engine/core/vfx/vfx_module.h`
- Modify: `engine/core/vfx/CMakeLists.txt` (no new .cpp — header-only, but document it)
- Test: `tools/vfxgraph_check/main.cpp` (create), `tools/CMakeLists.txt` (register)

**Interfaces:**
- Consumes: `gws::anim::Curve`, `gws::anim::Gradient` from `"anim/curve.h"`
- Produces:
  - `enum class VfxStage : uint8_t { Spawn, Init, Update }`
  - `enum class ModuleKind : uint8_t { SpawnRate, SpawnBurst, InitLifetime, InitVelocityCone, InitSize, InitColor, InitPositionShape, Gravity, Drag, SizeOverLife, ColorOverLife, VelocityOverLife }`
  - `struct VfxModule { ModuleKind kind; std::map<std::string, ParamValue> params; }`
  - `using ParamValue = std::variant<float, glm::vec3, glm::vec4, gws::anim::Curve, gws::anim::Gradient>`
  - `VfxStage stage_of(ModuleKind)`, `const char* module_kind_name(ModuleKind)`, `ModuleKind module_kind_from_name(const std::string&, bool& ok)`
  - `float VfxModule::get_float(const std::string& key, float fallback) const` and the vec3/vec4/curve/gradient equivalents

- [ ] **Step 1: Write the failing test**

Create `tools/vfxgraph_check/main.cpp`:

```cpp
// ====================
// vfxgraph_check — headless verification of the VFX module stack (item 4.3).
//
// The assertions here are about the things that fail SILENTLY. A stack that
// emits nothing looks like a broken renderer; a serialiser that reorders
// modules changes every saved effect with no error; a module that reads the
// scene compiles fine and forecloses GPU simulation. None of those show up in
// a screenshot, which is why they are asserted here.
// ====================

#include "vfx/vfx_module.h"

#include <cstdio>
#include <string>

using namespace schizo::vfx;

namespace {
int g_pass = 0, g_fail = 0;
void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what.c_str());
    if (ok) ++g_pass; else ++g_fail;
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== vfxgraph_check: VFX module stack ===\n");

    // ---- 1. Every module kind knows which stage it belongs to --------------
    check(stage_of(ModuleKind::SpawnRate)     == VfxStage::Spawn,  "SpawnRate is a Spawn module");
    check(stage_of(ModuleKind::InitLifetime)  == VfxStage::Init,   "InitLifetime is an Init module");
    check(stage_of(ModuleKind::Gravity)       == VfxStage::Update, "Gravity is an Update module");
    check(stage_of(ModuleKind::ColorOverLife) == VfxStage::Update, "ColorOverLife is an Update module");

    // ---- 2. Kind names round-trip -----------------------------------------
    // The name is what the .vfx file stores. If it does not round-trip, a
    // saved effect silently loses a module on load rather than failing.
    {
        bool all = true;
        for (int i = 0; i <= static_cast<int>(ModuleKind::VelocityOverLife); ++i) {
            const auto k = static_cast<ModuleKind>(i);
            bool ok = false;
            const ModuleKind back = module_kind_from_name(module_kind_name(k), ok);
            if (!ok || back != k) all = false;
        }
        check(all, "every ModuleKind name parses back to the same kind");
    }
    {
        bool ok = true;
        module_kind_from_name("NotAModule", ok);
        check(!ok, "an unknown module name reports failure rather than defaulting");
    }

    // ---- 3. Parameter access is typed and falls back ------------------------
    {
        VfxModule m;
        m.kind = ModuleKind::InitLifetime;
        m.params["MIN"] = 1.5f;
        check(m.get_float("MIN", 0.0f) == 1.5f, "a float parameter reads back");
        check(m.get_float("MAX", 9.0f) == 9.0f, "a missing parameter yields the fallback");
        check(m.get_float("MIN", 0.0f) == 1.5f && m.get_vec3("MIN", glm::vec3(7.0f)) == glm::vec3(7.0f),
              "reading a parameter as the wrong type yields the fallback, not garbage");
    }

    std::printf("\nvfxgraph_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("vfxgraph_check: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Register the check and run it to verify it fails**

Append to `tools/CMakeLists.txt`, after the `vfx_check` block (around line 648):

```cmake
# vfxgraph_check: VFX module-stack verification (headless, no device) — module
# stage membership, name round-trip, typed parameter access, stack execution
# order, .vfx round-trip including module order, and the validate() cases that
# otherwise fail silently.
add_executable(vfxgraph_check vfxgraph_check/main.cpp)
target_link_libraries(vfxgraph_check PRIVATE gws_vfx)
target_compile_features(vfxgraph_check PRIVATE cxx_std_20)
```

Run:
```bash
cmake --build build/windows-debug --target vfxgraph_check
```
Expected: FAIL — `vfx/vfx_module.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `engine/core/vfx/vfx_module.h`:

```cpp
#pragma once

// ============================================================================
// vfx_module — the unit of a VFX stack (item 4.3).
//
// A module is a pure function of ONE particle: it reads and writes that
// particle, its own parameters, and the Context it is handed. It may not read
// the scene, may not read other particles, may not allocate, and may not hold
// state across frames.
//
// That restriction is the point. It is exactly the shape a compute shader can
// execute, so generating one later is transcription rather than redesign.
// Breaking it once -- a module that queries physics, or keeps a running
// average -- makes GPU simulation a rewrite rather than an addition.
//
// Parameters are a TAGGED VALUE rather than a struct per module so that a
// later version can replace a constant with a computed sub-graph without
// breaking the file format. That is also why they are keyed by string: the
// .vfx file is text and hand-editable, and a positional encoding would make a
// reordered parameter silently mean something else.
// ============================================================================

#include "anim/curve.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace schizo::vfx {

/// When a module runs. Spawn decides HOW MANY particles appear this frame;
/// Init runs once per particle at birth; Update runs on every live particle
/// every frame. A module in the wrong stage runs at the wrong frequency, which
/// is a silent failure -- hence stage_of() and the validate() check that uses it.
enum class VfxStage : uint8_t { Spawn, Init, Update };

enum class ModuleKind : uint8_t {
    // Spawn
    SpawnRate,
    SpawnBurst,
    // Initialize
    InitLifetime,
    InitVelocityCone,
    InitSize,
    InitColor,
    InitPositionShape,
    // Update
    Gravity,
    Drag,
    SizeOverLife,
    ColorOverLife,
    VelocityOverLife,
};

inline VfxStage stage_of(ModuleKind k) {
    switch (k) {
        case ModuleKind::SpawnRate:
        case ModuleKind::SpawnBurst:
            return VfxStage::Spawn;
        case ModuleKind::InitLifetime:
        case ModuleKind::InitVelocityCone:
        case ModuleKind::InitSize:
        case ModuleKind::InitColor:
        case ModuleKind::InitPositionShape:
            return VfxStage::Init;
        default:
            return VfxStage::Update;
    }
}

inline const char* module_kind_name(ModuleKind k) {
    switch (k) {
        case ModuleKind::SpawnRate:         return "SpawnRate";
        case ModuleKind::SpawnBurst:        return "SpawnBurst";
        case ModuleKind::InitLifetime:      return "InitLifetime";
        case ModuleKind::InitVelocityCone:  return "InitVelocityCone";
        case ModuleKind::InitSize:          return "InitSize";
        case ModuleKind::InitColor:         return "InitColor";
        case ModuleKind::InitPositionShape: return "InitPositionShape";
        case ModuleKind::Gravity:           return "Gravity";
        case ModuleKind::Drag:              return "Drag";
        case ModuleKind::SizeOverLife:      return "SizeOverLife";
        case ModuleKind::ColorOverLife:     return "ColorOverLife";
        case ModuleKind::VelocityOverLife:  return "VelocityOverLife";
    }
    return "Unknown";
}

/// Parse a module name. `ok` is set false for an unrecognised name rather than
/// defaulting to a kind: a typo in a hand-edited .vfx must not silently become
/// a SpawnRate module, which would change the effect with no diagnostic.
inline ModuleKind module_kind_from_name(const std::string& n, bool& ok) {
    ok = true;
    for (int i = 0; i <= static_cast<int>(ModuleKind::VelocityOverLife); ++i) {
        const auto k = static_cast<ModuleKind>(i);
        if (n == module_kind_name(k)) return k;
    }
    ok = false;
    return ModuleKind::SpawnRate;
}

using ParamValue = std::variant<float, glm::vec3, glm::vec4,
                                gws::anim::Curve, gws::anim::Gradient>;

struct VfxModule {
    ModuleKind kind = ModuleKind::SpawnRate;
    /// Keyed by name, sorted: iteration order is therefore stable, which the
    /// serialiser needs to produce byte-identical output for an unchanged
    /// module.
    std::map<std::string, ParamValue> params;

    /// Typed reads. A key that is absent OR holds a different type yields the
    /// fallback. Both cases mean "this module was not configured with that",
    /// and throwing std::bad_variant_access at a hand-edited file would turn a
    /// typo into a crash.
    float get_float(const std::string& key, float fallback) const {
        auto it = params.find(key);
        if (it == params.end()) return fallback;
        if (const float* v = std::get_if<float>(&it->second)) return *v;
        return fallback;
    }
    glm::vec3 get_vec3(const std::string& key, const glm::vec3& fallback) const {
        auto it = params.find(key);
        if (it == params.end()) return fallback;
        if (const glm::vec3* v = std::get_if<glm::vec3>(&it->second)) return *v;
        return fallback;
    }
    glm::vec4 get_vec4(const std::string& key, const glm::vec4& fallback) const {
        auto it = params.find(key);
        if (it == params.end()) return fallback;
        if (const glm::vec4* v = std::get_if<glm::vec4>(&it->second)) return *v;
        return fallback;
    }
    gws::anim::Curve get_curve(const std::string& key, const gws::anim::Curve& fallback) const {
        auto it = params.find(key);
        if (it == params.end()) return fallback;
        if (const gws::anim::Curve* v = std::get_if<gws::anim::Curve>(&it->second)) return *v;
        return fallback;
    }
    gws::anim::Gradient get_gradient(const std::string& key,
                                     const gws::anim::Gradient& fallback) const {
        auto it = params.find(key);
        if (it == params.end()) return fallback;
        if (const gws::anim::Gradient* v = std::get_if<gws::anim::Gradient>(&it->second)) return *v;
        return fallback;
    }
};

}  // namespace schizo::vfx
```

- [ ] **Step 4: Run the check to verify it passes**

Run:
```bash
cmake --build build/windows-debug --target vfxgraph_check && ./build/windows-debug/bin/vfxgraph_check.exe
```
Expected: `vfxgraph_check: ALL OK`, 8 passed, 0 failed.

- [ ] **Step 5: Commit**

```bash
git add engine/core/vfx/vfx_module.h tools/vfxgraph_check/main.cpp tools/CMakeLists.txt
git commit -m "feat(vfx): module data types — kinds, stages, tagged parameters

Data only, no execution yet. Parameters are a tagged variant keyed by
string rather than a struct per module, so a constant can later become a
computed sub-graph without a format break, and so a hand-edited .vfx has
named rather than positional fields.

An unknown module name reports failure instead of defaulting to a kind: a
typo that silently becomes a SpawnRate module changes the effect with no
diagnostic. Reading a parameter as the wrong type yields the fallback for
the same reason -- bad_variant_access would turn a typo into a crash.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: The VfxGraph document, default stack, and validate()

**Files:**
- Create: `engine/core/vfx/vfx_graph.h`, `engine/core/vfx/vfx_graph.cpp`
- Modify: `engine/core/vfx/CMakeLists.txt`
- Test: `tools/vfxgraph_check/main.cpp` (extend)

**Interfaces:**
- Consumes: `VfxModule`, `ModuleKind`, `VfxStage`, `stage_of()` from Task 2
- Produces:
  - `class VfxGraph` with `std::vector<VfxModule>& stage(VfxStage)`, `const` overload, `add_module(ModuleKind)`, `remove_module(VfxStage, size_t)`, `move_module(VfxStage, size_t from, size_t to)`
  - `std::string name; uint32_t max_particles = 2048; bool world_space = true;`
  - `static VfxGraph default_stack()` — the 9 modules in spec §4.1 with `EmitterConfig`'s defaults
  - `static VfxGraph from_emitter_config(const EmitterConfig&)` — the compatibility façade
  - `std::vector<std::string> validate() const`

- [ ] **Step 1: Write the failing tests**

Append to `tools/vfxgraph_check/main.cpp`, before the summary printf, and add `#include "vfx/vfx_graph.h"` at the top:

```cpp
    // ---- 4. The default stack is the current behaviour, written down --------
    {
        const VfxGraph g = VfxGraph::default_stack();
        check(g.stage(VfxStage::Spawn).size() == 1, "default stack has one Spawn module");
        check(g.stage(VfxStage::Init).size() == 4,  "default stack has four Init modules");
        check(g.stage(VfxStage::Update).size() == 4, "default stack has four Update modules");
        // Order is semantic: the current integrator applies gravity and THEN
        // damps the result. A stack that damps first is different motion.
        check(g.stage(VfxStage::Update)[0].kind == ModuleKind::Gravity &&
              g.stage(VfxStage::Update)[1].kind == ModuleKind::Drag,
              "Gravity precedes Drag, matching the current integrator");
    }

    // ---- 5. validate() catches what fails silently -------------------------
    {
        check(VfxGraph::default_stack().validate().empty(),
              "the default stack validates clean");
    }
    {
        VfxGraph g = VfxGraph::default_stack();
        g.stage(VfxStage::Spawn).clear();
        check(!g.validate().empty(),
              "a stack with no spawn module is reported, not silently inert");
    }
    {
        VfxGraph g = VfxGraph::default_stack();
        auto& init = g.stage(VfxStage::Init);
        for (size_t i = 0; i < init.size(); ++i)
            if (init[i].kind == ModuleKind::InitColor) { init.erase(init.begin() + i); break; }
        check(!g.validate().empty(),
              "ColorOverLife with no InitColor before it is reported");
    }
    {
        VfxGraph g = VfxGraph::default_stack();
        g.max_particles = 0;
        check(!g.validate().empty(), "max_particles of 0 is reported");
    }
    {
        // A module placed in the wrong stage runs at the wrong frequency:
        // an Init module in Update re-initialises every particle every frame,
        // which reads as "the effect does not move".
        VfxGraph g = VfxGraph::default_stack();
        VfxModule stray; stray.kind = ModuleKind::InitLifetime;
        g.stage(VfxStage::Update).push_back(stray);
        check(!g.validate().empty(), "a module in the wrong stage is reported");
    }

    // ---- 6. Reordering ----------------------------------------------------
    {
        VfxGraph g = VfxGraph::default_stack();
        g.move_module(VfxStage::Update, 0, 1);
        check(g.stage(VfxStage::Update)[0].kind == ModuleKind::Drag &&
              g.stage(VfxStage::Update)[1].kind == ModuleKind::Gravity,
              "move_module reorders within a stage");
    }
```

- [ ] **Step 2: Run to verify it fails**

Run:
```bash
cmake --build build/windows-debug --target vfxgraph_check
```
Expected: FAIL — `vfx/vfx_graph.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `engine/core/vfx/vfx_graph.h`:

```cpp
#pragma once

// ============================================================================
// vfx_graph — the editable VFX document (item 4.3).
//
// Plain data describing an effect: three ordered stages of modules, plus the
// few properties that belong to the effect rather than to any module.
//
// Kept apart from ParticleSystem for the reason anim_graph is kept apart from
// the runtime state machine: this one is inert and editable, that one is live
// and mid-flight, and an editor must not poke at the second while the user
// drags in the first.
//
// Lives in engine/core rather than editor/ -- unlike AnimGraph -- because a
// SHIPPED GAME must load .vfx files. A document only the editor can read is a
// document players cannot see the effects of.
// ============================================================================

#include "vfx/vfx_module.h"

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::vfx {

struct EmitterConfig;   // particle_system.h

class VfxGraph {
public:
    std::string name;
    uint32_t    max_particles = 2048;
    bool        world_space   = true;

    std::vector<VfxModule>&       stage(VfxStage s);
    const std::vector<VfxModule>& stage(VfxStage s) const;

    /// Append a module to the stage its kind belongs to. Returns its index
    /// within that stage.
    size_t add_module(ModuleKind kind);
    void   remove_module(VfxStage s, size_t index);
    /// Reorder within a stage. Out-of-range indices are ignored rather than
    /// clamped: clamping a bad drag silently moves the wrong module.
    void   move_module(VfxStage s, size_t from, size_t to);

    /// The stack that reproduces the pre-4.3 hardcoded pipeline, carrying
    /// EmitterConfig's defaults. An emitter nobody has edited behaves exactly
    /// as it did before this item existed -- which is the regression gate.
    static VfxGraph default_stack();

    /// The compatibility facade. Old scene files and the existing checks both
    /// arrive holding an EmitterConfig; this is the one place that turns one
    /// into a stack, so there is a single definition of "what the old
    /// behaviour was".
    static VfxGraph from_emitter_config(const EmitterConfig& cfg);

    /// Human-readable problems. Empty means sound.
    ///
    /// Every case here fails SILENTLY at runtime -- no exception, no log, just
    /// an effect that is invisible, motionless, or the wrong colour, which
    /// reads to the user as "the renderer is broken" rather than as a
    /// misconfigured stack.
    std::vector<std::string> validate() const;

private:
    std::vector<VfxModule> spawn_, init_, update_;
};

}  // namespace schizo::vfx
```

- [ ] **Step 4: Write the implementation**

Create `engine/core/vfx/vfx_graph.cpp`:

```cpp
// ============================================================================
// vfx_graph.cpp — the VFX document. See header.
// ============================================================================

#include "vfx/vfx_graph.h"
#include "vfx/particle_system.h"

#include <algorithm>

namespace schizo::vfx {

std::vector<VfxModule>& VfxGraph::stage(VfxStage s) {
    switch (s) {
        case VfxStage::Spawn: return spawn_;
        case VfxStage::Init:  return init_;
        default:              return update_;
    }
}

const std::vector<VfxModule>& VfxGraph::stage(VfxStage s) const {
    switch (s) {
        case VfxStage::Spawn: return spawn_;
        case VfxStage::Init:  return init_;
        default:              return update_;
    }
}

size_t VfxGraph::add_module(ModuleKind kind) {
    auto& v = stage(stage_of(kind));
    VfxModule m;
    m.kind = kind;
    v.push_back(m);
    return v.size() - 1;
}

void VfxGraph::remove_module(VfxStage s, size_t index) {
    auto& v = stage(s);
    if (index < v.size()) v.erase(v.begin() + static_cast<long>(index));
}

void VfxGraph::move_module(VfxStage s, size_t from, size_t to) {
    auto& v = stage(s);
    if (from >= v.size() || to >= v.size() || from == to) return;
    VfxModule m = v[from];
    v.erase(v.begin() + static_cast<long>(from));
    v.insert(v.begin() + static_cast<long>(to), m);
}

VfxGraph VfxGraph::default_stack() {
    EmitterConfig defaults;          // the pre-4.3 defaults, unchanged
    return from_emitter_config(defaults);
}

VfxGraph VfxGraph::from_emitter_config(const EmitterConfig& cfg) {
    VfxGraph g;
    g.max_particles = cfg.max_particles;
    g.world_space   = cfg.world_space;

    // Spawn
    {
        VfxModule m; m.kind = ModuleKind::SpawnRate;
        m.params["RATE"] = cfg.spawn_rate;
        g.spawn_.push_back(m);
    }
    // Initialize -- in the order spawn_one() drew them.
    {
        VfxModule m; m.kind = ModuleKind::InitLifetime;
        m.params["MIN"] = cfg.life_min;
        m.params["MAX"] = cfg.life_max;
        g.init_.push_back(m);
    }
    {
        VfxModule m; m.kind = ModuleKind::InitVelocityCone;
        m.params["BASE"]   = cfg.base_velocity;
        m.params["SPREAD"] = cfg.velocity_spread;
        g.init_.push_back(m);
    }
    {
        VfxModule m; m.kind = ModuleKind::InitSize;
        m.params["SIZE"] = cfg.size_start;
        g.init_.push_back(m);
    }
    {
        VfxModule m; m.kind = ModuleKind::InitColor;
        m.params["COLOR"] = cfg.color_start;
        g.init_.push_back(m);
    }
    // Update -- gravity BEFORE drag, matching the integrator. Reversing these
    // is different motion, not a cosmetic difference.
    {
        VfxModule m; m.kind = ModuleKind::Gravity;
        m.params["GRAVITY"] = cfg.gravity;
        g.update_.push_back(m);
    }
    {
        VfxModule m; m.kind = ModuleKind::Drag;
        m.params["DRAG"] = cfg.drag;
        g.update_.push_back(m);
    }
    {
        VfxModule m; m.kind = ModuleKind::SizeOverLife;
        // A two-key linear curve is the start/end pair the old config held.
        gws::anim::Curve c({{0.0f, cfg.size_start, 0.0f, 0.0f},
                            {1.0f, cfg.size_end,   0.0f, 0.0f}});
        m.params["CURVE"] = c;
        g.update_.push_back(m);
    }
    {
        VfxModule m; m.kind = ModuleKind::ColorOverLife;
        m.params["GRADIENT"] = gws::anim::Gradient::two_stop(cfg.color_start, cfg.color_end);
        g.update_.push_back(m);
    }
    return g;
}

std::vector<std::string> VfxGraph::validate() const {
    std::vector<std::string> problems;

    if (spawn_.empty())
        problems.push_back("No spawn module: this effect emits nothing.");

    if (max_particles == 0)
        problems.push_back("max_particles is 0: no particle can ever exist.");

    // A module in the wrong stage runs at the wrong frequency. An Init module
    // in Update re-initialises every particle every frame, which reads as "the
    // effect does not move" rather than as a misplaced module.
    const VfxStage stages[3] = {VfxStage::Spawn, VfxStage::Init, VfxStage::Update};
    for (VfxStage s : stages) {
        for (const VfxModule& m : stage(s)) {
            if (stage_of(m.kind) != s) {
                problems.push_back(std::string(module_kind_name(m.kind)) +
                                   " is in the wrong stage and will run at the wrong time.");
            }
        }
    }

    // ColorOverLife/SizeOverLife overwrite what Init set. Without the matching
    // Init module the particle starts from a default the author never chose --
    // not an error, just invisibly the wrong colour or size.
    const auto has_init = [&](ModuleKind k) {
        return std::any_of(init_.begin(), init_.end(),
                           [&](const VfxModule& m) { return m.kind == k; });
    };
    const auto has_update = [&](ModuleKind k) {
        return std::any_of(update_.begin(), update_.end(),
                           [&](const VfxModule& m) { return m.kind == k; });
    };
    if (has_update(ModuleKind::ColorOverLife) && !has_init(ModuleKind::InitColor))
        problems.push_back("ColorOverLife with no InitColor: particles start at a default colour.");
    if (has_update(ModuleKind::SizeOverLife) && !has_init(ModuleKind::InitSize))
        problems.push_back("SizeOverLife with no InitSize: particles start at a default size.");

    return problems;
}

}  // namespace schizo::vfx
```

- [ ] **Step 5: Add the source to the build**

In `engine/core/vfx/CMakeLists.txt`, change the `add_library` call to:

```cmake
add_library(gws_vfx STATIC
    particle_system.cpp
    vfx_graph.cpp)
```

- [ ] **Step 6: Run the check to verify it passes**

Run:
```bash
cmake --build build/windows-debug --target vfxgraph_check && ./build/windows-debug/bin/vfxgraph_check.exe
```
Expected: `vfxgraph_check: ALL OK`, 17 passed, 0 failed.

**If the `CurveKey` aggregate initialiser fails to compile**, check `engine/core/anim/curve.h` for the real field order of `CurveKey` and match it — the plan assumes `{time, value, in_tangent, out_tangent}`.

- [ ] **Step 7: Verify the existing checks are untouched**

Run:
```bash
cmake --build build/windows-debug --target vfx_check --target emitter_check \
  && ./build/windows-debug/bin/vfx_check.exe && ./build/windows-debug/bin/emitter_check.exe
```
Expected: both PASS. Neither file has been edited.

- [ ] **Step 8: Commit**

```bash
git add engine/core/vfx/vfx_graph.h engine/core/vfx/vfx_graph.cpp \
        engine/core/vfx/CMakeLists.txt tools/vfxgraph_check/main.cpp
git commit -m "feat(vfx): the VfxGraph document, default stack and validate()

Inert, editable data kept apart from the live ParticleSystem, the same
split anim_graph makes -- but placed in engine/core rather than editor/,
because a shipped game must load .vfx files.

from_emitter_config() is the single definition of 'what the old behaviour
was': default_stack() is just that function applied to EmitterConfig's
defaults, so there is one place to be wrong rather than two.

validate() covers only failures that are SILENT at runtime -- no spawn
module, max_particles 0, a module in the wrong stage, ColorOverLife with
no InitColor. Each produces an effect that is invisible, motionless or
the wrong colour, which reads as a broken renderer rather than as a
misconfigured stack.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: The interpreter — ParticleSystem executes a stack

**Files:**
- Modify: `engine/core/vfx/particle_system.h` (add graph storage + `Context`)
- Modify: `engine/core/vfx/particle_system.cpp` (`spawn_one`, `update` execute modules)
- Create: `engine/core/vfx/vfx_exec.h` (the module bodies, header-only, one function)
- Test: `tools/vfxgraph_check/main.cpp` (extend), `tools/vfx_check/main.cpp` (read only)

**Interfaces:**
- Consumes: `VfxGraph`, `VfxModule` from Task 3
- Produces:
  - `struct Context { float dt; float t; glm::vec3 emitter_pos; uint32_t* rng; }`
  - `void apply_module(const VfxModule&, Particle&, const Context&)`
  - `ParticleSystem::set_graph(const VfxGraph&)`, `const VfxGraph& graph() const`
  - `ParticleSystem(const EmitterConfig&)` unchanged in signature, now building a graph internally

- [ ] **Step 1: Write the failing test**

Append to `tools/vfxgraph_check/main.cpp` (add `#include "vfx/particle_system.h"`):

```cpp
    // ---- 7. Module ORDER changes the result --------------------------------
    // Gravity-then-Drag damps the gravity contribution of this frame;
    // Drag-then-Gravity does not. The difference is small per frame and
    // compounds, which is exactly the kind of bug a screenshot cannot show.
    {
        auto run = [](bool gravity_first) {
            VfxGraph g = VfxGraph::default_stack();
            auto& up = g.stage(VfxStage::Update);
            if (!gravity_first) std::swap(up[0], up[1]);
            // Remove randomness so the comparison is exact.
            auto& init = g.stage(VfxStage::Init);
            for (auto& m : init)
                if (m.kind == ModuleKind::InitVelocityCone) m.params["SPREAD"] = 0.0f;
            for (auto& m : g.stage(VfxStage::Update))
                if (m.kind == ModuleKind::Drag) m.params["DRAG"] = 2.0f;

            ParticleSystem ps;
            ps.set_graph(g);
            ps.set_seed(7);
            ps.emit_burst(1);
            for (int i = 0; i < 10; ++i) ps.update(0.016f);
            return ps.particles().empty() ? 0.0f : ps.particles()[0].pos.y;
        };
        const float a = run(true), b = run(false);
        check(std::fabs(a - b) > 1e-4f,
              "Gravity-then-Drag differs numerically from Drag-then-Gravity");
    }

    // ---- 8. The default stack matches the pre-4.3 integrator ---------------
    // The regression gate in one assertion: a stack-driven system and a
    // config-driven one must agree, or every saved effect changed meaning.
    {
        EmitterConfig cfg;
        cfg.spawn_rate      = 0.0f;
        cfg.base_velocity   = glm::vec3(0.0f, 10.0f, 0.0f);
        cfg.velocity_spread = 0.0f;
        cfg.gravity         = glm::vec3(0.0f, -10.0f, 0.0f);
        cfg.drag            = 0.5f;

        ParticleSystem ps(cfg);               // the facade path
        ps.set_seed(3);
        ps.emit_burst(1);

        // Hand-rolled reference using the OLD arithmetic, exactly.
        glm::vec3 vel(0.0f, 10.0f, 0.0f), pos(0.0f);
        const float dt = 0.016f;
        for (int i = 0; i < 20; ++i) {
            ps.update(dt);
            const float damp = std::max(0.0f, 1.0f - 0.5f * dt);
            vel += glm::vec3(0.0f, -10.0f, 0.0f) * dt;
            vel *= damp;
            pos += vel * dt;
        }
        const bool ok = !ps.particles().empty() &&
                        std::fabs(ps.particles()[0].pos.y - pos.y) < 1e-4f;
        check(ok, "the default stack reproduces the pre-4.3 integrator exactly");
    }
```

Add `#include <cmath>` and `#include <algorithm>` to the check's includes.

- [ ] **Step 2: Run to verify it fails**

Run:
```bash
cmake --build build/windows-debug --target vfxgraph_check
```
Expected: FAIL — `'class schizo::vfx::ParticleSystem' has no member named 'set_graph'`.

- [ ] **Step 3: Write the module bodies**

Create `engine/core/vfx/vfx_exec.h`:

```cpp
#pragma once

// ============================================================================
// vfx_exec — what each module DOES, as a pure function of one particle.
//
// Header-only and free of every engine subsystem on purpose. The signature is
// the contract: a module sees one Particle, its own parameters, and a Context
// holding dt, normalised age, the emitter position and an RNG. Nothing else is
// reachable from here, so the restriction is enforced by what this file can
// see rather than by review.
//
// That is what makes a compute-shader version a transcription later. Each case
// below maps to a handful of GLSL statements with no gather, no allocation and
// no cross-invocation communication.
// ============================================================================

#include "vfx/vfx_module.h"

#include <glm/glm.hpp>
#include <algorithm>

namespace schizo::vfx {

struct Particle;   // particle_system.h

/// Everything a module is allowed to know. Deliberately small -- adding the
/// scene, the frame number, or the particle array to this struct is how the
/// GPU path stops being buildable.
struct Context {
    float     dt = 0.0f;
    float     t  = 0.0f;          // normalised age, [0,1]
    glm::vec3 emitter_pos{0.0f};
    uint32_t* rng = nullptr;      // caller-owned xorshift32 state
};

/// The same xorshift32 the system has always used, so the facade path draws
/// from an identical generator.
inline float ctx_rand01(const Context& ctx) {
    if (!ctx.rng) return 0.5f;
    uint32_t& s = *ctx.rng;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return static_cast<float>(s & 0xFFFFFF) / static_cast<float>(0x1000000);
}

}  // namespace schizo::vfx
```

The `apply_module` body needs the complete `Particle` type, so it is defined in `particle_system.cpp` (Step 5) rather than here.

- [ ] **Step 4: Add graph storage to the system**

In `engine/core/vfx/particle_system.h`, add after the `set_seed` declaration:

```cpp
    /// Drive the simulation from a VFX document. Replaces the config-driven
    /// path; ParticleSystem(EmitterConfig) simply builds one of these.
    void set_graph(const VfxGraph& g);
    const VfxGraph& graph() const { return graph_; }
```

Add to the private section:

```cpp
    VfxGraph graph_;
```

And at the top of the file, after the existing includes:

```cpp
#include "vfx/vfx_graph.h"
```

Change the `EmitterConfig` constructor declaration to:

```cpp
    /// Compatibility facade: builds the default stack from `cfg`. Kept because
    /// vfx_check and old scene files both arrive holding an EmitterConfig, and
    /// a rewritten regression test proves nothing about the rewrite it polices.
    explicit ParticleSystem(const EmitterConfig& cfg);
```

- [ ] **Step 5: Implement the interpreter**

In `engine/core/vfx/particle_system.cpp`, add `#include "vfx/vfx_exec.h"` and replace `spawn_one()` and the body of `update()`:

```cpp
ParticleSystem::ParticleSystem(const EmitterConfig& cfg) : cfg_(cfg) {
    graph_ = VfxGraph::from_emitter_config(cfg);
}

void ParticleSystem::set_graph(const VfxGraph& g) { graph_ = g; }

namespace {

/// One module, applied to one particle. Every case is local arithmetic on `p`.
void apply_module(const VfxModule& m, Particle& p, const Context& ctx) {
    switch (m.kind) {
        case ModuleKind::InitLifetime: {
            const float lo = m.get_float("MIN", 1.0f), hi = m.get_float("MAX", 2.0f);
            p.life = lo + (hi - lo) * ctx_rand01(ctx);
            break;
        }
        case ModuleKind::InitVelocityCone: {
            const glm::vec3 base = m.get_vec3("BASE", glm::vec3(0.0f, 3.0f, 0.0f));
            const float spread   = m.get_float("SPREAD", 1.0f);
            const glm::vec3 r(ctx_rand01(ctx) * 2.0f - 1.0f,
                              ctx_rand01(ctx) * 2.0f - 1.0f,
                              ctx_rand01(ctx) * 2.0f - 1.0f);
            p.vel = base + r * spread;
            break;
        }
        case ModuleKind::InitSize:
            p.size = m.get_float("SIZE", 0.25f);
            break;
        case ModuleKind::InitColor:
            p.color = m.get_vec4("COLOR", glm::vec4(1.0f));
            break;
        case ModuleKind::InitPositionShape: {
            // 0 = point, 1 = sphere, 2 = box. Point is the pre-4.3 behaviour.
            const float shape  = m.get_float("SHAPE", 0.0f);
            const float radius = m.get_float("RADIUS", 1.0f);
            if (shape >= 0.5f) {
                const glm::vec3 r(ctx_rand01(ctx) * 2.0f - 1.0f,
                                  ctx_rand01(ctx) * 2.0f - 1.0f,
                                  ctx_rand01(ctx) * 2.0f - 1.0f);
                // Sphere and box differ only in whether the offset is
                // normalised; both stay inside `radius`.
                p.pos += (shape >= 1.5f) ? r * radius
                                         : (glm::length(r) > 1e-5f
                                                ? glm::normalize(r) * radius * ctx_rand01(ctx)
                                                : glm::vec3(0.0f));
            }
            break;
        }
        case ModuleKind::Gravity:
            p.vel += m.get_vec3("GRAVITY", glm::vec3(0.0f, -9.8f, 0.0f)) * ctx.dt;
            break;
        case ModuleKind::Drag:
            p.vel *= std::max(0.0f, 1.0f - m.get_float("DRAG", 0.1f) * ctx.dt);
            break;
        case ModuleKind::VelocityOverLife:
            p.vel += m.get_vec3("ACCEL", glm::vec3(0.0f)) * ctx.dt;
            break;
        case ModuleKind::SizeOverLife: {
            const gws::anim::Curve fallback({{0.0f, 0.25f, 0.0f, 0.0f},
                                             {1.0f, 0.0f,  0.0f, 0.0f}});
            p.size = m.get_curve("CURVE", fallback).evaluate(ctx.t);
            break;
        }
        case ModuleKind::ColorOverLife: {
            const gws::anim::Gradient fallback =
                gws::anim::Gradient::two_stop(glm::vec4(1.0f), glm::vec4(0.0f));
            p.color = m.get_gradient("GRADIENT", fallback).sample(ctx.t);
            break;
        }
        case ModuleKind::SpawnRate:
        case ModuleKind::SpawnBurst:
            break;   // Spawn modules produce counts, not particle edits.
    }
}

}  // namespace

void ParticleSystem::spawn_one() {
    if (particles_.size() >= graph_.max_particles) return;
    Particle p;
    p.pos = position_;
    p.age = 0.0f;
    Context ctx;
    ctx.dt = 0.0f;
    ctx.t  = 0.0f;
    ctx.emitter_pos = position_;
    ctx.rng = &rng_;
    for (const VfxModule& m : graph_.stage(VfxStage::Init)) apply_module(m, p, ctx);
    p.seed = ctx_rand01(ctx);
    particles_.push_back(p);
}

void ParticleSystem::update(float dt) {
    if (dt <= 0.0f) return;

    // ---- Spawn stage: how many particles this frame ----
    if (emitting_) {
        for (const VfxModule& m : graph_.stage(VfxStage::Spawn)) {
            if (m.kind == ModuleKind::SpawnRate) {
                spawn_accum_ += m.get_float("RATE", 50.0f) * dt;
                while (spawn_accum_ >= 1.0f) { spawn_one(); spawn_accum_ -= 1.0f; }
            } else if (m.kind == ModuleKind::SpawnBurst) {
                // Fires once, when the emitter first runs past the burst time.
                const float at = m.get_float("TIME", 0.0f);
                const float before = elapsed_, after = elapsed_ + dt;
                if (before <= at && at < after) {
                    const int n = static_cast<int>(m.get_float("COUNT", 10.0f));
                    for (int i = 0; i < n; ++i) spawn_one();
                }
            }
        }
    }
    elapsed_ += dt;

    // ---- Update stage ----
    const auto& update_modules = graph_.stage(VfxStage::Update);
    for (auto& p : particles_) {
        p.age += dt;
        Context ctx;
        ctx.dt = dt;
        ctx.t  = p.life > 0.0f ? glm::clamp(p.age / p.life, 0.0f, 1.0f) : 1.0f;
        ctx.emitter_pos = position_;
        ctx.rng = &rng_;
        for (const VfxModule& m : update_modules) apply_module(m, p, ctx);
        p.pos += p.vel * dt;
    }

    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [](const Particle& p) { return p.age >= p.life; }),
        particles_.end());
}
```

Add `float elapsed_ = 0.0f;` to the private members in the header, and change `spawn_one`'s `cfg_.max_particles` guard as shown.

- [ ] **Step 6: Run vfxgraph_check to verify it passes**

Run:
```bash
cmake --build build/windows-debug --target vfxgraph_check && ./build/windows-debug/bin/vfxgraph_check.exe
```
Expected: `ALL OK`, 19 passed.

**If assertion 8 fails by a small amount**, the cause is the order of `p.age += dt` versus `p.pos += p.vel * dt`. The old code aged the particle *after* integrating position; the new code ages first so `ctx.t` is correct for the Update modules. Position integration must still happen after the velocity modules run. Compare against the reference loop step by step before changing the reference.

- [ ] **Step 7: Verify the regression gate**

Run:
```bash
cmake --build build/windows-debug --target vfx_check --target emitter_check \
  && ./build/windows-debug/bin/vfx_check.exe && ./build/windows-debug/bin/emitter_check.exe
```
Expected: both PASS, **with neither file edited**. Confirm with `git status` that `tools/vfx_check/main.cpp` and `tools/emitter_check/main.cpp` are unmodified.

- [ ] **Step 8: Commit**

```bash
git add engine/core/vfx/particle_system.h engine/core/vfx/particle_system.cpp \
        engine/core/vfx/vfx_exec.h tools/vfxgraph_check/main.cpp
git commit -m "feat(vfx): ParticleSystem interprets a module stack

The integrator is now a loop over ordered modules rather than fixed
arithmetic. vfx_exec.h holds the module bodies and can see nothing but
one Particle, its parameters and a Context of dt/age/emitter/RNG -- the
restriction is enforced by what the file can reach, not by review, which
is what keeps a compute-shader version a transcription later.

Asserted rather than asserted-of: Gravity-then-Drag differs numerically
from Drag-then-Gravity, so order is proven semantic; and the default
stack reproduces the pre-4.3 integrator to 1e-4 over 20 steps against a
hand-rolled reference using the old arithmetic.

vfx_check and emitter_check pass unmodified.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: The `.vfx` text format

**Files:**
- Create: `engine/core/vfx/vfx_io.h`, `engine/core/vfx/vfx_io.cpp`
- Modify: `engine/core/vfx/CMakeLists.txt`
- Test: `tools/vfxgraph_check/main.cpp` (extend)

**Interfaces:**
- Consumes: `VfxGraph` from Task 3
- Produces:
  - `inline constexpr const char* kVfxExtension = ".vfx";`
  - `inline constexpr int kVfxFormatVersion = 1;`
  - `std::string vfx_to_text(const VfxGraph&)`
  - `VfxGraph vfx_from_text(const std::string&)` — always succeeds
  - `bool load_vfx(const std::string& path, VfxGraph& out)` — false only if the file is absent/unreadable
  - `bool save_vfx(const std::string& path, const VfxGraph&)`

- [ ] **Step 1: Write the failing tests**

Append to `tools/vfxgraph_check/main.cpp` (add `#include "vfx/vfx_io.h"`):

```cpp
    // ---- 9. Round-trip, including module ORDER -----------------------------
    {
        VfxGraph g = VfxGraph::default_stack();
        g.name = "campfire_embers";
        g.move_module(VfxStage::Update, 0, 1);          // Drag now precedes Gravity
        const VfxGraph back = vfx_from_text(vfx_to_text(g));
        check(back.name == "campfire_embers", "name round-trips");
        check(back.stage(VfxStage::Update).size() == 4, "module count round-trips");
        check(back.stage(VfxStage::Update)[0].kind == ModuleKind::Drag &&
              back.stage(VfxStage::Update)[1].kind == ModuleKind::Gravity,
              "module ORDER round-trips -- a sorted reload would change the effect");
    }

    // ---- 10. Serialisation is deterministic --------------------------------
    {
        const VfxGraph g = VfxGraph::default_stack();
        check(vfx_to_text(g) == vfx_to_text(g), "the same graph serialises byte-identically");
    }

    // ---- 11. Curve and Gradient parameters survive -------------------------
    {
        VfxGraph g = VfxGraph::default_stack();
        for (auto& m : g.stage(VfxStage::Update)) {
            if (m.kind != ModuleKind::ColorOverLife) continue;
            gws::anim::Gradient grad;
            grad.add({0.0f, glm::vec4(1, 0, 0, 1)});
            grad.add({0.5f, glm::vec4(0, 1, 0, 1)});
            grad.add({1.0f, glm::vec4(0, 0, 1, 0)});
            m.params["GRADIENT"] = grad;
        }
        const VfxGraph back = vfx_from_text(vfx_to_text(g));
        size_t stops = 0;
        for (const auto& m : back.stage(VfxStage::Update))
            if (m.kind == ModuleKind::ColorOverLife)
                stops = m.get_gradient("GRADIENT", gws::anim::Gradient()).size();
        check(stops == 3, "a three-stop gradient round-trips with all three stops");
    }

    // ---- 12. Indices are grouping, not position ----------------------------
    // A hand-edited file with descending or duplicated indices must load in
    // FILE order. Treating the index as a position would reorder the effect.
    {
        const std::string text =
            "VFX_VERSION=1\nNAME=x\nMAX_PARTICLES=64\nWORLD_SPACE=1\n"
            "STAGE=UPDATE\n"
            "MODULE.9.KIND=Drag\nMODULE.9.DRAG=0.5\n"
            "MODULE.2.KIND=Gravity\nMODULE.2.GRAVITY=0,-9.8,0\n";
        const VfxGraph g = vfx_from_text(text);
        check(g.stage(VfxStage::Update).size() == 2, "both modules load");
        check(g.stage(VfxStage::Update)[0].kind == ModuleKind::Drag,
              "descending indices load in file order, not sorted order");
    }

    // ---- 13. Garbage yields defaults, absence is distinguishable -----------
    {
        const VfxGraph g = vfx_from_text("this is not a vfx file\n\x01\x02\n");
        check(g.stage(VfxStage::Update).empty() && g.max_particles == 2048,
              "an unparseable string yields an empty, default graph rather than garbage");
    }
    {
        VfxGraph g;
        check(!load_vfx("definitely/not/here.vfx", g),
              "load_vfx reports a missing file -- absent and unparseable are different");
    }
    {
        // An unknown module KIND is skipped, not defaulted to SpawnRate.
        const std::string text =
            "VFX_VERSION=1\nSTAGE=UPDATE\n"
            "MODULE.0.KIND=NotAModule\nMODULE.0.X=1\n"
            "MODULE.1.KIND=Gravity\nMODULE.1.GRAVITY=0,-1,0\n";
        const VfxGraph g = vfx_from_text(text);
        check(g.stage(VfxStage::Update).size() == 1 &&
              g.stage(VfxStage::Update)[0].kind == ModuleKind::Gravity,
              "an unknown module kind is skipped, not silently turned into another module");
    }
```

- [ ] **Step 2: Run to verify it fails**

Run:
```bash
cmake --build build/windows-debug --target vfxgraph_check
```
Expected: FAIL — `vfx/vfx_io.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `engine/core/vfx/vfx_io.h`:

```cpp
#pragma once

// ============================================================================
// vfx_io — the .vfx text format (item 4.3).
//
// Conventions are taken wholesale from assets/material_desc.h rather than
// invented: a version stamp, every field written including defaults, unknown
// keys ignored, missing keys defaulted, and a parse that always succeeds.
// Writing defaults matters because a partially-written file makes "did the
// editor fail to save this, or was it just default?" unanswerable from the
// file alone.
//
// Text rather than binary, per the position settled in 3.7: assets live in
// version control and a binary blob makes every edit an unreviewable diff.
//
// THE ONE ADDITION material_desc does not need: modules are an ORDERED
// sequence. STAGE= is a delimiter, and MODULE.n.* belongs to the stage most
// recently named. `n` groups one module's keys together for a human reading
// the file -- it is NOT a position, and the loader ignores its value, appending
// in the order KIND lines appear. Duplicated, skipped or descending indices
// therefore load correctly instead of silently reordering an effect.
// ============================================================================

#include "vfx/vfx_graph.h"

#include <string>

namespace schizo::vfx {

inline constexpr const char* kVfxExtension     = ".vfx";
inline constexpr int         kVfxFormatVersion = 1;

std::string vfx_to_text(const VfxGraph& g);

/// Always succeeds -- garbage yields a default graph. The caller that needs to
/// know whether a FILE existed is load_vfx(), and conflating "unparseable" with
/// "absent" is how a missing asset becomes a silently invisible effect.
VfxGraph vfx_from_text(const std::string& text);

/// False only when the file cannot be read.
bool load_vfx(const std::string& path, VfxGraph& out);
bool save_vfx(const std::string& path, const VfxGraph& g);

}  // namespace schizo::vfx
```

- [ ] **Step 4: Write the implementation**

Create `engine/core/vfx/vfx_io.cpp`:

```cpp
// ============================================================================
// vfx_io.cpp — .vfx text serialisation. See header.
// ============================================================================

#include "vfx/vfx_io.h"

#include <fstream>
#include <sstream>

namespace schizo::vfx {
namespace {

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

float parse_float(const std::string& s, float fallback) {
    try { return std::stof(s); } catch (...) { return fallback; }
}

std::vector<float> parse_floats(const std::string& s) {
    std::vector<float> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) out.push_back(parse_float(trim(part), 0.0f));
    return out;
}

const char* stage_key(VfxStage s) {
    switch (s) {
        case VfxStage::Spawn: return "SPAWN";
        case VfxStage::Init:  return "INIT";
        default:              return "UPDATE";
    }
}

/// Curves and gradients are written as flat comma lists because the format is
/// line-based and a nested block would need a parser rather than a split.
/// Curve: time,value,in,out per key. Gradient: time,r,g,b,a per stop.
std::string curve_to_text(const gws::anim::Curve& c) {
    std::ostringstream o;
    for (size_t i = 0; i < c.keys().size(); ++i) {
        const auto& k = c.keys()[i];
        if (i) o << ",";
        o << k.time << "," << k.value << "," << k.in_tangent << "," << k.out_tangent;
    }
    return o.str();
}

std::string gradient_to_text(const gws::anim::Gradient& g) {
    std::ostringstream o;
    for (size_t i = 0; i < g.stops().size(); ++i) {
        const auto& s = g.stops()[i];
        if (i) o << ",";
        o << s.time << "," << s.color.r << "," << s.color.g << ","
          << s.color.b << "," << s.color.a;
    }
    return o.str();
}

void write_param(std::ostringstream& o, size_t index, const std::string& key,
                 const ParamValue& v) {
    o << "MODULE." << index << "." << key << "=";
    if (const float* f = std::get_if<float>(&v))                 o << *f;
    else if (const glm::vec3* p = std::get_if<glm::vec3>(&v))    o << p->x << "," << p->y << "," << p->z;
    else if (const glm::vec4* p = std::get_if<glm::vec4>(&v))    o << p->x << "," << p->y << "," << p->z << "," << p->w;
    else if (const auto* c = std::get_if<gws::anim::Curve>(&v))  o << "curve:" << curve_to_text(*c);
    else if (const auto* g = std::get_if<gws::anim::Gradient>(&v)) o << "grad:" << gradient_to_text(*g);
    o << "\n";
}

ParamValue parse_param(const std::string& val) {
    if (val.rfind("curve:", 0) == 0) {
        const std::vector<float> f = parse_floats(val.substr(6));
        std::vector<gws::anim::CurveKey> keys;
        for (size_t i = 0; i + 3 < f.size(); i += 4)
            keys.push_back({f[i], f[i + 1], f[i + 2], f[i + 3]});
        return gws::anim::Curve(keys);
    }
    if (val.rfind("grad:", 0) == 0) {
        const std::vector<float> f = parse_floats(val.substr(5));
        std::vector<gws::anim::GradientStop> stops;
        for (size_t i = 0; i + 4 < f.size(); i += 5)
            stops.push_back({f[i], glm::vec4(f[i + 1], f[i + 2], f[i + 3], f[i + 4])});
        return gws::anim::Gradient(stops);
    }
    const std::vector<float> f = parse_floats(val);
    if (f.size() >= 4) return glm::vec4(f[0], f[1], f[2], f[3]);
    if (f.size() == 3) return glm::vec3(f[0], f[1], f[2]);
    return f.empty() ? 0.0f : f[0];
}

}  // namespace

std::string vfx_to_text(const VfxGraph& g) {
    std::ostringstream o;
    o << "# GameWorldshaper VFX\n";
    o << "VFX_VERSION=" << kVfxFormatVersion << "\n";
    o << "NAME=" << g.name << "\n";
    o << "MAX_PARTICLES=" << g.max_particles << "\n";
    o << "WORLD_SPACE=" << (g.world_space ? 1 : 0) << "\n";

    size_t index = 0;
    const VfxStage stages[3] = {VfxStage::Spawn, VfxStage::Init, VfxStage::Update};
    for (VfxStage s : stages) {
        o << "STAGE=" << stage_key(s) << "\n";
        for (const VfxModule& m : g.stage(s)) {
            o << "MODULE." << index << ".KIND=" << module_kind_name(m.kind) << "\n";
            // std::map iterates sorted by key, so this is deterministic.
            for (const auto& kv : m.params) write_param(o, index, kv.first, kv.second);
            ++index;
        }
    }
    return o.str();
}

VfxGraph vfx_from_text(const std::string& text) {
    VfxGraph g;
    std::istringstream in(text);
    std::string line;

    VfxStage current = VfxStage::Spawn;
    // Modules are appended in FILE order. `pending` accumulates the keys of the
    // module currently being read; a new KIND line flushes the previous one.
    bool     have_pending = false;
    VfxModule pending;
    VfxStage  pending_stage = VfxStage::Spawn;

    const auto flush = [&]() {
        if (!have_pending) return;
        g.stage(pending_stage).push_back(pending);
        have_pending = false;
        pending = VfxModule();
    };

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));

        if (key == "NAME")               { g.name = val; continue; }
        if (key == "MAX_PARTICLES")      { g.max_particles = static_cast<uint32_t>(parse_float(val, 2048.0f)); continue; }
        if (key == "WORLD_SPACE")        { g.world_space = parse_float(val, 1.0f) != 0.0f; continue; }
        if (key == "STAGE") {
            flush();
            current = val == "SPAWN" ? VfxStage::Spawn
                    : val == "INIT"  ? VfxStage::Init
                                     : VfxStage::Update;
            continue;
        }
        if (key.rfind("MODULE.", 0) != 0) continue;   // VFX_VERSION, unknown keys

        // MODULE.<n>.<FIELD> — n is ignored on purpose (see header).
        const size_t dot = key.find('.', 7);
        if (dot == std::string::npos) continue;
        const std::string field = key.substr(dot + 1);

        if (field == "KIND") {
            flush();
            bool ok = false;
            const ModuleKind k = module_kind_from_name(val, ok);
            if (!ok) continue;          // unknown kind: skip it and its params
            pending = VfxModule();
            pending.kind  = k;
            pending_stage = current;
            have_pending  = true;
        } else if (have_pending) {
            pending.params[field] = parse_param(val);
        }
    }
    flush();
    return g;
}

bool load_vfx(const std::string& path, VfxGraph& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = vfx_from_text(ss.str());
    return true;
}

bool save_vfx(const std::string& path, const VfxGraph& g) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << vfx_to_text(g);
    return f.good();
}

}  // namespace schizo::vfx
```

- [ ] **Step 5: Add the source to the build**

In `engine/core/vfx/CMakeLists.txt`:

```cmake
add_library(gws_vfx STATIC
    particle_system.cpp
    vfx_graph.cpp
    vfx_io.cpp)
```

- [ ] **Step 6: Run the check to verify it passes**

Run:
```bash
cmake --build build/windows-debug --target vfxgraph_check && ./build/windows-debug/bin/vfxgraph_check.exe
```
Expected: `ALL OK`, 29 passed, 0 failed.

**If test 12 fails** ("descending indices load in file order"), the parser is keying modules by their index instead of flushing on `KIND`. The index must never be parsed as a number.

- [ ] **Step 7: Commit**

```bash
git add engine/core/vfx/vfx_io.h engine/core/vfx/vfx_io.cpp \
        engine/core/vfx/CMakeLists.txt tools/vfxgraph_check/main.cpp
git commit -m "feat(vfx): the .vfx text format

Conventions lifted from material_desc.h rather than invented: version
stamp, every field written including defaults, unknown keys ignored,
parse always succeeds, and only load_vfx distinguishes absent from
unparseable.

The one addition that format does not need is ordered modules. STAGE= is
a delimiter and MODULE.n is GROUPING, not position -- the loader ignores
n entirely and appends in the order KIND lines appear, so a hand-edited
file with duplicated or descending indices loads correctly instead of
silently reordering the effect. Asserted directly.

An unknown module kind is skipped rather than defaulted, matching the
same decision in module_kind_from_name.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Component and editor wiring

**Files:**
- Modify: `engine/scene/include/particle_emitter_component.h`
- Modify: `editor/include/particle_emitter_cache.h`
- Test: `tools/emitter_check/main.cpp` — **read only**, must pass unmodified

**Interfaces:**
- Consumes: `VfxGraph`, `load_vfx` from Tasks 3 and 5
- Produces: `ParticleEmitterComponent{ vfx_path, enabled, emitting, rate_scale }`; `EditorParticleEmitters` resolving `vfx_path` to a `VfxGraph`

- [ ] **Step 1: Change the component**

Replace the field block in `engine/scene/include/particle_emitter_component.h`:

```cpp
struct ParticleEmitterComponent {
    /// Off means "this entity is not an emitter" — the check the render path
    /// uses. Distinct from `emitting`, which pauses spawning while KEEPING the
    /// particles already alive, so a burst can finish rather than vanish.
    bool enabled  = false;
    bool emitting = true;

    /// The .vfx asset this entity plays. Empty means the built-in default
    /// stack, which is what every emitter authored before 4.3 gets.
    std::string vfx_path;

    /// Per-instance spawn multiplier, applied to the count the Spawn stage
    /// produces. It deliberately does NOT reach into module parameters: those
    /// belong to the shared asset, and an instance editing them would make
    /// "the same effect" mean different things in different places.
    /// 0.0 spawns nothing — distinct from `emitting = false`, which is the
    /// per-frame toggle rather than a saved property of this placement.
    float rate_scale = 1.0f;

    bool active() const { return enabled; }
};
```

Add `#include <string>` to that header. Remove the now-unused `spawn_rate`, `max_particles`, `color_start`, `color_end` fields and the `<glm/glm.hpp>` include if nothing else needs it.

- [ ] **Step 2: Build the editor to find every reader of the removed fields**

Run:
```bash
cmake --build build/windows-debug --target editor 2>&1 | grep -E "spawn_rate|max_particles|color_start|color_end" | sort -u
```
Expected: a list of call sites. Each must be updated in Step 3 — do not guess which exist.

- [ ] **Step 3: Resolve the asset in the cache**

In `editor/include/particle_emitter_cache.h`, add `#include "vfx/vfx_io.h"` and replace the construction block inside `update()`:

```cpp
            auto it = systems_.find(id);
            if (it == systems_.end()) {
                // An empty vfx_path is the pre-4.3 emitter: the built-in
                // default stack, which behaves exactly as it always did.
                schizo::vfx::VfxGraph graph;
                if (pe->vfx_path.empty() || !schizo::vfx::load_vfx(pe->vfx_path, graph))
                    graph = schizo::vfx::VfxGraph::default_stack();

                it = systems_.emplace(id, std::make_unique<schizo::vfx::ParticleSystem>()).first;
                it->second->set_graph(graph);
                // Deterministic per entity: two emitters with the same settings
                // should not produce identical particle streams, and the same
                // emitter should look the same across a reload.
                it->second->set_seed(id * 2654435761u + 1u);
            }
```

- [ ] **Step 4: Apply the per-instance scale**

Immediately after `sys.set_emitting(pe->emitting);` in the same function:

```cpp
            sys.set_rate_scale(pe->rate_scale);
```

And in `engine/core/vfx/particle_system.h`, add:

```cpp
    /// Per-instance multiplier on the Spawn stage's output. See
    /// ParticleEmitterComponent::rate_scale.
    void set_rate_scale(float s) { rate_scale_ = s < 0.0f ? 0.0f : s; }
```

with `float rate_scale_ = 1.0f;` in the private section, and in `update()` change the SpawnRate accumulation to:

```cpp
                spawn_accum_ += m.get_float("RATE", 50.0f) * rate_scale_ * dt;
```

and the burst count to `static_cast<int>(m.get_float("COUNT", 10.0f) * rate_scale_)`.

- [ ] **Step 5: Build and run the emitter check**

Run:
```bash
cmake --build build/windows-debug --target emitter_check && ./build/windows-debug/bin/emitter_check.exe
```
Expected: PASS, **file unmodified** — it only ever touched `enabled` and `emitting`, both of which survive. Confirm with `git status`.

- [ ] **Step 6: Build the whole editor**

Run:
```bash
cmake --build build/windows-debug --target editor
```
Expected: clean build. Any remaining reference to a removed field is a call site Step 2 listed.

- [ ] **Step 7: Commit**

```bash
git add engine/scene/include/particle_emitter_component.h \
        editor/include/particle_emitter_cache.h engine/core/vfx/particle_system.h
git commit -m "feat(vfx): the emitter component points at a .vfx asset

The component carried 6 of EmitterConfig's 14 fields and the cache copied
exactly those six, so the other 8 -- lifetime, velocity, spread, gravity,
drag, size -- silently took library defaults and could not be authored
from anywhere. That stops being a problem by ceasing to exist on the
component: the effect owns them.

An empty vfx_path is the pre-4.3 emitter and gets the built-in default
stack, so no scene file needs editing and none is rewritten until saved.

rate_scale multiplies the Spawn stage's output and deliberately cannot
reach module parameters -- those belong to the shared asset, and an
instance editing them would make 'the same effect' mean different things
in different places.

emitter_check passes unmodified.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Hot-reload a `.vfx` while the editor runs

Spec §7 requires this and Task 6 does not provide it — the cache loads a `.vfx` once at first sight of an entity and never re-reads it. 2.4 already built the shared `AssetWatcher` that settles a file before firing, so this is wiring, not new machinery.

**Files:**
- Modify: `editor/include/particle_emitter_cache.h`
- Test: `tools/vfxgraph_check/main.cpp` (extend — the reload path, driven without a filesystem race)

**Interfaces:**
- Consumes: `gws::assets::AssetWatcher::watch(std::string, Callback)` / `unwatch(uint64_t)` / `poll(double)`; `load_vfx` from Task 5
- Produces: `EditorParticleEmitters::set_watcher(gws::assets::AssetWatcher*)`; reloading a `.vfx` replaces the graph on every live system using it **without** resetting their particles

- [ ] **Step 1: Write the failing test**

Append to `tools/vfxgraph_check/main.cpp`:

```cpp
    // ---- 14. A reloaded graph replaces settings without killing particles ---
    // The distinction that matters: hot reload must not clear live particles,
    // or every edit makes the effect vanish and restart, which reads as a
    // broken editor rather than as a reload.
    {
        ParticleSystem ps;
        ps.set_graph(VfxGraph::default_stack());
        ps.set_seed(11);
        ps.emit_burst(20);
        const size_t before = ps.alive();

        VfxGraph edited = VfxGraph::default_stack();
        for (auto& m : edited.stage(VfxStage::Update))
            if (m.kind == ModuleKind::Gravity) m.params["GRAVITY"] = glm::vec3(0.0f, 99.0f, 0.0f);
        ps.set_graph(edited);

        check(ps.alive() == before, "set_graph keeps the particles already alive");
        ps.update(0.016f);
        check(!ps.particles().empty() && ps.particles()[0].vel.y > 0.0f,
              "and the new gravity applies to them on the next frame");
    }
```

- [ ] **Step 2: Run to verify it fails or passes**

Run:
```bash
cmake --build build/windows-debug --target vfxgraph_check && ./build/windows-debug/bin/vfxgraph_check.exe
```
Expected: PASS — `set_graph` from Task 4 already assigns without touching `particles_`. This assertion pins that property so a later change cannot quietly clear the array. If it FAILS, `set_graph` is clearing particles and must not.

- [ ] **Step 3: Wire the watcher into the cache**

In `editor/include/particle_emitter_cache.h`, add `#include "assets/asset_watcher.h"` and these members:

```cpp
    /// Hot reload. Without a watcher the cache still works — it simply never
    /// re-reads a .vfx, which is the pre-4.3 behaviour.
    void set_watcher(gws::assets::AssetWatcher* w) { watcher_ = w; }

private:
    gws::assets::AssetWatcher* watcher_ = nullptr;
    /// path -> watch token, so a path is watched once no matter how many
    /// entities play it, and unwatched when the last one goes.
    std::unordered_map<std::string, uint64_t> watched_;
    /// Paths whose file changed since the last update(); applied at the top of
    /// update() rather than inside the callback, because the callback fires
    /// from poll() on the editor thread mid-frame and reloading there would
    /// swap a graph out from under the loop that is iterating it.
    std::vector<std::string> dirty_paths_;
```

- [ ] **Step 4: Register the watch when a system is created**

Inside `update()`, immediately after the `systems_.emplace(...)` block from Task 6 Step 3:

```cpp
                if (watcher_ && !pe->vfx_path.empty() && !watched_.count(pe->vfx_path)) {
                    const std::string p = pe->vfx_path;
                    watched_[p] = watcher_->watch(p, [this, p](const std::string&) {
                        dirty_paths_.push_back(p);
                    });
                }
```

- [ ] **Step 5: Apply reloads at the top of update()**

At the start of `update()`, before the entity loop:

```cpp
        // Apply any .vfx edits that settled since last frame. Done here, not in
        // the watcher callback: that fires from poll() mid-frame, and swapping
        // a graph while the emitter loop iterates it is a use-after-free
        // waiting to happen.
        if (!dirty_paths_.empty()) {
            for (const std::string& p : dirty_paths_) {
                schizo::vfx::VfxGraph g;
                if (!schizo::vfx::load_vfx(p, g)) continue;
                for (const auto& ent : scene ? scene->GetEntities()
                                             : std::vector<std::shared_ptr<schizo::scene::Entity>>{}) {
                    if (!ent) continue;
                    auto* c = ent->GetParticleEmitterComponent();
                    if (!c || c->vfx_path != p) continue;
                    auto it = systems_.find(ent->GetId());
                    if (it != systems_.end()) it->second->set_graph(g);
                }
            }
            dirty_paths_.clear();
        }
```

- [ ] **Step 6: Unwatch paths nothing plays any more**

In the existing reap loop that erases dead systems, after it, add:

```cpp
        // Drop watches for paths no live emitter references, so a scene that
        // stops using an effect stops paying to stat its file every poll.
        if (watcher_) {
            for (auto it = watched_.begin(); it != watched_.end(); ) {
                bool still_used = false;
                if (scene) {
                    for (const auto& ent : scene->GetEntities()) {
                        auto* c = ent ? ent->GetParticleEmitterComponent() : nullptr;
                        if (c && c->enabled && c->vfx_path == it->first) { still_used = true; break; }
                    }
                }
                if (still_used) { ++it; continue; }
                watcher_->unwatch(it->second);
                it = watched_.erase(it);
            }
        }
```

- [ ] **Step 7: Connect the editor's watcher**

In `editor/src/main.cpp`, find the existing shared `AssetWatcher` instance (the one 2.4 introduced, used by textures, meshes, shaders and scenes) and call `g_particle_emitters.set_watcher(&<that watcher>);` once during startup, beside the other `watch(...)` registrations. Match the exact variable name in use.

- [ ] **Step 8: Build and verify by hand**

Run:
```bash
cmake --build build/windows-debug --target editor
```

Then: launch the editor, assign a `.vfx` to an emitter, edit the file in a text editor (change a gravity value), save. Expected: the effect changes within ~0.5 s without the particles vanishing and restarting.

**This step needs a human** — a watcher that fires and a graph that visibly changes are different claims.

- [ ] **Step 9: Commit**

```bash
git add editor/include/particle_emitter_cache.h editor/src/main.cpp tools/vfxgraph_check/main.cpp
git commit -m "feat(vfx): hot-reload .vfx edits, on the shared AssetWatcher

Rides 2.4's watcher, which settles a file before firing, so a .vfx still
being written is never read half-parsed.

Two things done deliberately. Reloads are applied at the TOP of update()
rather than in the watcher callback: the callback fires from poll()
mid-frame, and swapping a graph while the emitter loop iterates it is a
use-after-free waiting to happen. And set_graph keeps the particles
already alive -- clearing them would make every edit vanish and restart
the effect, which reads as a broken editor rather than as a reload. That
property is now pinned by an assertion so a later change cannot quietly
drop it.

A path is watched once however many entities play it, and unwatched when
the last one stops.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: The editor stack panel

A module stack is a **list**, so this is a stack panel, not `NodeCanvas`. The correction to `node_canvas.h`'s claim ships with it, because the claim is only wrong once this decision is made.

**Files:**
- Create: `editor/include/vfx_stack_panel.h`, `editor/src/vfx_stack_panel.cpp`
- Modify: `editor/src/main.cpp` (register the panel and its palette command)
- Modify: `editor/include/node_canvas.h` (correct the four-items claim)
- Modify: `editor/CMakeLists.txt`

**Interfaces:**
- Consumes: `VfxGraph`, `vfx_to_text`/`save_vfx`, `CurveEditor`/`GradientEditor` from 4.5
- Produces: `class VfxStackPanel { void draw(bool* open); VfxGraph& graph(); void set_graph(const VfxGraph&); }`

- [ ] **Step 1: Write the panel header**

Create `editor/include/vfx_stack_panel.h`:

```cpp
#pragma once
// ============================================================================
// vfx_stack_panel — the editor surface for a VFX document (item 4.3).
//
// A per-stage module stack is a LIST, not a graph, so this is an ordered-row
// panel rather than a NodeCanvas. Unity's Shuriken is a stack; Niagara is a
// stack whose MODULES are internally graphs. The canvas earns its place one
// level down, later, when a module's parameters become computed rather than
// constant -- forcing it in now would be the wrong UI for the architecture.
//
// Order is an EDIT, not a display preference: reordering Gravity and Drag is
// different motion. The panel therefore shows explicit up/down controls rather
// than relying on a drag whose result is ambiguous.
// ============================================================================

#include "vfx/vfx_graph.h"

#include <string>

namespace schizo::editor {

class VfxStackPanel {
public:
    void draw(bool* open);

    schizo::vfx::VfxGraph&       graph()       { return graph_; }
    const schizo::vfx::VfxGraph& graph() const { return graph_; }
    void set_graph(const schizo::vfx::VfxGraph& g) { graph_ = g; dirty_ = false; }

    const std::string& path() const { return path_; }
    void set_path(const std::string& p) { path_ = p; }

private:
    void draw_stage(schizo::vfx::VfxStage stage, const char* label);
    void draw_module_params(schizo::vfx::VfxModule& m);

    schizo::vfx::VfxGraph graph_ = schizo::vfx::VfxGraph::default_stack();
    std::string           path_;
    bool                  dirty_ = false;
};

}  // namespace schizo::editor
```

- [ ] **Step 2: Write the panel implementation**

Create `editor/src/vfx_stack_panel.cpp`:

```cpp
// ============================================================================
// vfx_stack_panel.cpp — see header.
// ============================================================================

#include "vfx_stack_panel.h"
#include "curve_editor.h"
#include "vfx/vfx_io.h"

#include <imgui.h>

namespace schizo::editor {

using namespace schizo::vfx;

void VfxStackPanel::draw_module_params(VfxModule& m) {
    for (auto& kv : m.params) {
        const std::string& key = kv.first;
        ParamValue& v = kv.second;
        ImGui::PushID(key.c_str());
        if (float* f = std::get_if<float>(&v)) {
            if (ImGui::DragFloat(key.c_str(), f, 0.01f)) dirty_ = true;
        } else if (glm::vec3* p = std::get_if<glm::vec3>(&v)) {
            if (ImGui::DragFloat3(key.c_str(), &p->x, 0.01f)) dirty_ = true;
        } else if (glm::vec4* p = std::get_if<glm::vec4>(&v)) {
            if (ImGui::ColorEdit4(key.c_str(), &p->x)) dirty_ = true;
        } else if (auto* c = std::get_if<gws::anim::Curve>(&v)) {
            // Drawn through 4.5's editor, which draws from the REAL evaluate():
            // straight lines between keys would show a curve that eases
            // nothing, an editor lying about the runtime.
            if (draw_curve_editor(key.c_str(), *c)) dirty_ = true;
        } else if (auto* g = std::get_if<gws::anim::Gradient>(&v)) {
            if (draw_gradient_editor(key.c_str(), *g)) dirty_ = true;
        }
        ImGui::PopID();
    }
}

void VfxStackPanel::draw_stage(VfxStage stage, const char* label) {
    auto& mods = graph_.stage(stage);
    ImGui::SeparatorText(label);

    for (size_t i = 0; i < mods.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNodeEx(module_kind_name(mods[i].kind),
                                            ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90.0f);
        // Explicit reorder controls: order is an edit, and a drag whose drop
        // target is ambiguous silently changes the effect.
        if (ImGui::SmallButton("^") && i > 0)               { graph_.move_module(stage, i, i - 1); dirty_ = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("v") && i + 1 < mods.size()) { graph_.move_module(stage, i, i + 1); dirty_ = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { graph_.remove_module(stage, i); dirty_ = true; if (open) ImGui::TreePop(); ImGui::PopID(); break; }
        if (open) { draw_module_params(mods[i]); ImGui::TreePop(); }
        ImGui::PopID();
    }

    // The add menu is filtered to modules legal in THIS stage — offering an
    // Update module under Spawn invites a stack that runs at the wrong
    // frequency, which validate() would then have to report.
    if (ImGui::Button((std::string("+ Add##") + label).c_str()))
        ImGui::OpenPopup((std::string("add_") + label).c_str());
    if (ImGui::BeginPopup((std::string("add_") + label).c_str())) {
        for (int i = 0; i <= static_cast<int>(ModuleKind::VelocityOverLife); ++i) {
            const auto k = static_cast<ModuleKind>(i);
            if (stage_of(k) != stage) continue;
            if (ImGui::MenuItem(module_kind_name(k))) { graph_.add_module(k); dirty_ = true; }
        }
        ImGui::EndPopup();
    }
}

void VfxStackPanel::draw(bool* open) {
    if (!ImGui::Begin("VFX Stack", open)) { ImGui::End(); return; }

    char name[128];
    std::snprintf(name, sizeof(name), "%s", graph_.name.c_str());
    if (ImGui::InputText("Name", name, sizeof(name))) { graph_.name = name; dirty_ = true; }

    int maxp = static_cast<int>(graph_.max_particles);
    if (ImGui::DragInt("Max particles", &maxp, 1.0f, 0, 1 << 20)) {
        graph_.max_particles = static_cast<uint32_t>(maxp < 0 ? 0 : maxp);
        dirty_ = true;
    }
    if (ImGui::Checkbox("World space", &graph_.world_space)) dirty_ = true;

    draw_stage(VfxStage::Spawn,  "Spawn");
    draw_stage(VfxStage::Init,   "Initialize");
    draw_stage(VfxStage::Update, "Update");

    // Problems are shown inline rather than on save: every one of them
    // produces an effect that is invisible or motionless at runtime with no
    // error, so the panel is the only place they can be seen.
    const std::vector<std::string> problems = graph_.validate();
    if (!problems.empty()) {
        ImGui::SeparatorText("Problems");
        for (const std::string& p : problems)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", p.c_str());
    }

    ImGui::Separator();
    ImGui::BeginDisabled(path_.empty());
    if (ImGui::Button("Save")) { if (schizo::vfx::save_vfx(path_, graph_)) dirty_ = false; }
    ImGui::EndDisabled();
    if (dirty_) { ImGui::SameLine(); ImGui::TextUnformatted("(unsaved)"); }

    ImGui::End();
}

}  // namespace schizo::editor
```

**Before building**, confirm the real signatures of the 4.5 curve/gradient editors in `editor/include/curve_editor.h` and adjust the two calls in `draw_module_params` to match — the plan assumes `bool draw_curve_editor(const char*, Curve&)` and `bool draw_gradient_editor(const char*, Gradient&)`.

- [ ] **Step 3: Register the panel**

In `editor/CMakeLists.txt`, add `src/vfx_stack_panel.cpp` to the editor's source list.

In `editor/src/main.cpp`, beside the other panel instances add `schizo::editor::VfxStackPanel g_vfx_stack;` and `bool g_show_vfx_stack = false;`, call `g_vfx_stack.draw(&g_show_vfx_stack);` in the panel draw block, and register the palette command beside the other 15:

```cpp
    palette.register_command("VFX: Open Stack Editor",
                             [] { g_show_vfx_stack = true; });
```

Match the exact `register_command` signature used by the existing 15 registrations.

- [ ] **Step 4: Correct the node_canvas claim**

In `editor/include/node_canvas.h`, replace the sentence beginning *"Four Phase 4 items need one of these"* with:

```
// Three Phase 4 items need one of these: the material graph (4.1), the timeline
// (4.4) and the animation state machine (4.6). The VFX stack (4.3) was expected
// to be a fourth and is NOT: a per-stage module stack is an ordered list, so it
// is drawn as a stack panel. A canvas would earn its place there only one level
// down, if module parameters ever become computed rather than constant.
```

- [ ] **Step 5: Build and run the editor**

Run:
```bash
cmake --build build/windows-debug --target editor
```
Expected: clean build.

Then launch the editor, press Ctrl+P, type `vfx`, press Enter. Expected: the VFX Stack panel opens showing 1 Spawn, 4 Initialize and 4 Update modules with no problems listed. Reorder Gravity and Drag with the arrows and confirm the row order changes.

**This step needs a human.** A panel that draws is not a panel that works, and the repo has a documented instance of exactly that gap (3.9's billboard pass).

- [ ] **Step 6: Commit**

```bash
git add editor/include/vfx_stack_panel.h editor/src/vfx_stack_panel.cpp \
        editor/src/main.cpp editor/include/node_canvas.h editor/CMakeLists.txt
git commit -m "feat(vfx): the stack panel — and node_canvas's claim corrected

A per-stage module stack is an ordered list, so this is a stack panel and
not a NodeCanvas. node_canvas.h claimed four Phase 4 items needed a
canvas; three do. The claim is corrected rather than satisfied, because
forcing the canvas in to justify the word 'graph' in the item title would
be the wrong UI for the architecture.

Reordering is explicit up/down buttons rather than a drag: order is an
edit that changes the motion, and a drop target that is ambiguous
silently changes the effect.

The add menu is filtered per stage, so a stack that runs modules at the
wrong frequency cannot be built by clicking. validate() output is shown
inline, because every case it reports is invisible at runtime.

Curve and gradient parameters draw through 4.5's editors, which evaluate
the real curve rather than drawing straight lines between keys.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: Close the item

**Files:**
- Modify: `docs/EngineMasterPlan/WORKFLOW_PLAN.md` (the 4.3 row and the Phase 4 header)
- Modify: `CMakeLists.txt` (version bump)
- Modify: `.github/workflows/ci.yml` if checks are enumerated there rather than globbed

- [ ] **Step 1: Confirm the whole check suite passes**

Run:
```bash
cmake --build build/windows-debug && ctest --test-dir build/windows-debug --output-on-failure
```
Expected: all green. If `vfxgraph_check` is not in the CTest list, add it the same way `vfx_check` is registered.

- [ ] **Step 2: Update the workflow plan**

In `docs/EngineMasterPlan/WORKFLOW_PLAN.md`, replace the 4.3 row's Notes cell with a completion note in the house style — what was built, what the item got wrong (it is a stack, not a graph), what was measured, and what is explicitly left (GPU simulation, sub-emitters, collision, ribbons, events).

Also correct the stale Phase 4 header, which still reads *"🟠 In progress (4.2 done)"* while 4.1, 4.2, 4.4, 4.5, 4.6, 4.7 and now 4.3 are done. Remaining: 4.8, 4.9.

- [ ] **Step 3: Bump the version**

In `CMakeLists.txt` line 3, bump `VERSION 0.6.20` to `0.7.0` — a new authoring subsystem and a changed component schema is a minor bump, not a patch.

- [ ] **Step 4: Commit**

```bash
git add docs/EngineMasterPlan/WORKFLOW_PLAN.md CMakeLists.txt
git commit -m "release: v0.7.0 — VFX effects are authorable and saveable

4.3 done, and the item was wrong about its own shape: a per-stage module
stack is an ordered list, so it is a stack panel rather than the node
graph the plan assumed. node_canvas.h's 'four items need a canvas' is
corrected to three.

What the item did not say, and what actually blocked authoring: the
component exposed 6 of EmitterConfig's 14 fields and the cache copied
exactly those six, so lifetime, velocity, spread, gravity, drag and size
took library defaults and could not be set from anywhere. And no Phase 4
document persisted -- MaterialGraph, AnimGraph and Sequence still do not.
.vfx is the first, and its helpers are written so the other three could
follow.

Left, and named: GPU simulation (enabled by the purity contract, not
built), sub-emitters, collision, ribbons/trails/mesh particles, events.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Self-review

**Spec coverage.** §3 execution contract → Task 4 (`vfx_exec.h`, enforced by what the file can reach). §3.1 size/colour as state → Task 1; module order semantic → Tasks 3, 4 (assertion 7), 5 (assertion 12), 8 (explicit reorder). §4 module set → Task 2 (kinds) + Task 4 (bodies). §4.1 default stack + `EmitterConfig` façade → Task 3. §4.2 out-of-scope → nothing implements them; recorded in Task 9's commit. §5 document + `validate()` → Task 3. §6 `.vfx` format → Task 5. §7 component/asset boundary → Task 6, hot-reload → Task 7. §8 stack panel + `node_canvas` correction → Task 8. §9 all ten test items → Tasks 2, 3, 4, 5 assertions; regression gate verified in Tasks 1, 3, 4, 6.

**One spec item with weaker coverage, stated rather than hidden:** §9 item 9 (two entities sharing one `.vfx` get independent streams) relies on the per-entity seeding preserved in Task 6 Step 3, but no new assertion proves it — `emitter_check` covers the emitter-count half only. Worth an assertion in Task 6 if the implementer has one to spare; not a blocker, since the seeding line is unchanged from code that already shipped.

**Placeholder scan.** No TBD/TODO. Four steps deliberately say *verify the real signature before building* (Task 3 Step 6 `CurveKey`, Task 7 Step 7 the watcher variable, Task 8 Step 2 curve/gradient editors, Task 8 Step 3 `register_command`) — these are instructions to check a specific named symbol in a specific named file, not deferred decisions.

**Type consistency.** `stage()`/`add_module()`/`remove_module()`/`move_module()` are used identically in Tasks 3, 5, 7 and 8. `get_float`/`get_vec3`/`get_vec4`/`get_curve`/`get_gradient` defined in Task 2 and used unchanged in Tasks 4, 5, 7, 8. `Context{dt, t, emitter_pos, rng}` defined in Task 4 and used only there. `set_graph` defined in Task 4, used in Tasks 6 and 7; `set_rate_scale` defined and used in Task 6. `vfx_to_text`/`vfx_from_text`/`load_vfx`/`save_vfx` defined in Task 5, used in Tasks 5, 6, 7, 8.
