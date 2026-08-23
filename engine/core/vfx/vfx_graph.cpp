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
    // Update -- gravity BEFORE drag, matching the integrator. Reversing these is
    // different motion, not a cosmetic difference.
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
        gws::anim::Curve c(std::vector<gws::anim::CurveKey>{
            {0.0f, cfg.size_start, 0.0f, 0.0f},
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

    // A module in the wrong stage runs at the wrong frequency. An Init module in
    // Update re-initialises every particle every frame, which reads as "the
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
