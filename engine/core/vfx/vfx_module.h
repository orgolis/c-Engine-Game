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
// Parameters are a TAGGED VALUE rather than a struct per module so that a later
// version can replace a constant with a computed sub-graph without breaking the
// file format. That is also why they are keyed by string: the .vfx file is text
// and hand-editable, and a positional encoding would make a reordered parameter
// silently mean something else.
// ============================================================================

#include "anim/curve.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace schizo::vfx {

/// When a module runs. Spawn decides HOW MANY particles appear this frame; Init
/// runs once per particle at birth; Update runs on every live particle every
/// frame. A module in the wrong stage runs at the wrong frequency, which is a
/// silent failure -- hence stage_of() and the validate() check that uses it.
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
/// defaulting to a kind: a typo in a hand-edited .vfx must not silently become a
/// SpawnRate module, which would change the effect with no diagnostic.
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
    /// serialiser needs to produce byte-identical output for an unchanged module.
    std::map<std::string, ParamValue> params;

    /// Typed reads. A key that is absent OR holds a different type yields the
    /// fallback. Both cases mean "this module was not configured with that", and
    /// throwing std::bad_variant_access at a hand-edited file would turn a typo
    /// into a crash.
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
    gws::anim::Curve get_curve(const std::string& key,
                               const gws::anim::Curve& fallback) const {
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
