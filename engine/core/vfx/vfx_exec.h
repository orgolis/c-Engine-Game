#pragma once

// ============================================================================
// vfx_exec — what a module is ALLOWED to know, as a pure function of one
// particle.
//
// Header-only and free of every engine subsystem on purpose. The signature is
// the contract: a module sees one Particle, its own parameters, and a Context
// holding dt, normalised age, the emitter position and an RNG. Nothing else is
// reachable from here, so the restriction is enforced by what this file can
// see rather than by review.
//
// That is what makes a compute-shader version a transcription later. Each
// module maps to a handful of GLSL statements with no gather, no allocation and
// no cross-invocation communication.
//
// Adding the scene, the frame number, or the particle array to Context is how
// the GPU path stops being buildable. If a module ever seems to need one of
// those, that is the signal to reconsider the module, not to widen Context.
// ============================================================================

#include "vfx/vfx_module.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace schizo::vfx {

/// Everything a module is allowed to know. Deliberately small.
struct Context {
    float     dt = 0.0f;
    float     t  = 0.0f;          // normalised age, [0,1]
    glm::vec3 emitter_pos{0.0f};
    uint32_t* rng = nullptr;      // caller-owned xorshift32 state
};

/// The same xorshift32 the system has always used, so the facade path draws
/// from an identical generator rather than a second one that merely looks alike.
inline float ctx_rand01(const Context& ctx) {
    if (!ctx.rng) return 0.5f;
    uint32_t& s = *ctx.rng;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return static_cast<float>(s & 0xFFFFFF) / static_cast<float>(0x1000000);
}

}  // namespace schizo::vfx
