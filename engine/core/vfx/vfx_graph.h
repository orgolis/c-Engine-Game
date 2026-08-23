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
    /// EmitterConfig's defaults. An emitter nobody has edited behaves exactly as
    /// it did before this item existed -- which is the regression gate.
    static VfxGraph default_stack();

    /// The compatibility facade. Old scene files and the existing checks both
    /// arrive holding an EmitterConfig; this is the one place that turns one
    /// into a stack, so there is a single definition of "what the old behaviour
    /// was" rather than two that can drift.
    static VfxGraph from_emitter_config(const EmitterConfig& cfg);

    /// Human-readable problems. Empty means sound.
    ///
    /// Every case here fails SILENTLY at runtime -- no exception, no log, just
    /// an effect that is invisible, motionless, or the wrong colour, which reads
    /// to the user as "the renderer is broken" rather than as a misconfigured
    /// stack.
    std::vector<std::string> validate() const;

private:
    std::vector<VfxModule> spawn_, init_, update_;
};

}  // namespace schizo::vfx
