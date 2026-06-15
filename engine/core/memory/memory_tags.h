#pragma once

#include <cstdint>

namespace gws::memory {

// ====================
// Allocation Tags (Pillar A1 / Master Plan Stage 0.1)
// ====================
//
// Every allocation is attributed to a category. Tags drive per-tag
// accounting in the MemoryTracker (later 0.1 increment) so leaks and
// usage can be grouped by subsystem ("live allocations at shutdown grouped
// by tag"). Kept as a small POD enum so it can be threaded cheaply through
// allocator calls.

enum class AllocationTag : uint8_t {
    Temp = 0,    // transient / frame-scoped scratch (the default)
    Render,      // draw lists, GPU upload staging, render-graph scratch
    Physics,
    Audio,
    Scene,
    Animation,
    Net,
    Asset,       // import / cook / streaming
    ECS,         // component storage, command buffers
    Editor,
    Count
};

inline const char* to_string(AllocationTag t) {
    switch (t) {
        case AllocationTag::Temp:      return "Temp";
        case AllocationTag::Render:    return "Render";
        case AllocationTag::Physics:   return "Physics";
        case AllocationTag::Audio:     return "Audio";
        case AllocationTag::Scene:     return "Scene";
        case AllocationTag::Animation: return "Animation";
        case AllocationTag::Net:       return "Net";
        case AllocationTag::Asset:     return "Asset";
        case AllocationTag::ECS:       return "ECS";
        case AllocationTag::Editor:    return "Editor";
        default:                       return "Unknown";
    }
}

}  // namespace gws::memory
