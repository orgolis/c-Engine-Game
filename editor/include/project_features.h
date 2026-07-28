#pragma once
// ============================================================================
// Modular engine features (Editor project system).
//
// Every engine system is compiled into the editor/runtime; a *project* selects
// which of these optional systems it uses. Selection is runtime config (the
// project manifest), not a per-project recompile — so a feature can be added to
// an existing project later without rebuilding. Core systems (rendering, ECS,
// asset pipeline, editor) are always present and are NOT in this list.
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::project {

// Coarse, game-facing systems the user toggles per project.
enum class Feature : uint32_t {
    Physics = 0,      // Jolt rigid bodies + character controller
    Animation,        // skeletal animation, IK, state machine
    Audio,            // spatial mixer, occlusion
    Networking,       // multiplayer (transport / replication / prediction)
    AI,               // navmesh + behaviour trees
    Terrain,          // terrain + water authoring/rendering
    WorldStreaming,   // large-world streaming + floating origin
    VFX,              // particle / billboard effects
    Combat,           // frame-data combat (hitboxes, i-frames)
    Scripting,        // Python / C++ / C# scripts + hot reload
    GameUI,           // runtime game-UI (HUD / menus)
    Count
};

struct FeatureInfo {
    Feature     id;
    const char* key;         // stable identifier written to the manifest
    const char* name;        // human display name
    const char* desc;        // one-line description for the launcher UI
    bool        default_on;  // ticked by default in a new project
    Feature     depends_on;  // auto-enabled with this feature (Count = none)
};

// The registry of all toggleable features, in display order.
const std::vector<FeatureInfo>& feature_table();
const FeatureInfo*              feature_by_key(const std::string& key);
const FeatureInfo&              feature_info(Feature f);

// A set of enabled features (bitmask over Feature).
class FeatureSet {
public:
    bool has(Feature f) const { return (mask_ >> uint32_t(f)) & 1u; }
    void set(Feature f, bool on) {
        const uint32_t bit = 1u << uint32_t(f);
        if (on) mask_ |= bit; else mask_ &= ~bit;
    }
    void enable(Feature f) { set(f, true); }

    // Turn on every feature that an enabled feature depends on.
    void resolve_dependencies();

    uint32_t raw() const { return mask_; }
    void     set_raw(uint32_t m) { mask_ = m; }

    static FeatureSet defaults();  // the default_on subset
    static FeatureSet all();       // everything on (used by non-launcher runtime modes)
    static FeatureSet none() { return FeatureSet{}; }

private:
    uint32_t mask_ = 0;
};

} // namespace schizo::project
