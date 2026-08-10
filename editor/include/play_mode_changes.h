#pragma once

// ============================================================================
// Play-mode change tracking — keep what you tuned while playing.
//
// THE PROBLEM. Every engine with a play mode has to answer: what happens to the
// changes you made while playing? Unity's answer is to throw them away, and it
// is the single most-cited papercut in the editor — you tune a boss's health
// across ten attempts, find the value that feels right, press stop, and it is
// gone. The workaround the community settled on is to tint the whole UI red so
// you remember not to trust your own edits.
//
// Discarding is the RIGHT DEFAULT — play must not corrupt the authored scene.
// The mistake is making it the only option. This module keeps the default and
// adds the missing half: a diff of what play actually changed, so the developer
// decides what survives instead of the editor deciding for them.
//
// HOW IT FITS. Deliberately does NOT modify ScenePlaybackManager, whose restore
// path is already correct. The sequence is:
//
//     Capture(...)     at play start   — baseline
//     Diff(...)        BEFORE Stop     — while the play-end values still exist
//     StopPlayback()                   — the existing restore; discards
//     Apply(kept, ...) after the stop  — re-applies only what was chosen
//
// So "discard everything" is exactly today's behaviour with an empty keep-list,
// and the risky direction (writing to the authored scene) is opt-in per row.
//
// SCOPE, HONESTLY. Covers transforms and every component in the authorable
// registry — so any gameplay component added with one make_authorable<T>() line
// is diffable here for free, no per-component code. NOT covered: entities
// spawned or destroyed during play (structural changes need scene-graph merge,
// not value merge), and OOP components outside the authorable registry. Both
// are reported as untracked counts rather than silently ignored, because a diff
// that quietly omits things is worse than no diff.
//
// Keyed by Entity::GetId() rather than list index: ids are stable when play
// spawns or destroys entities, indices are not.
// ============================================================================

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace schizo::scene { class Scene; }

namespace schizo::editor {

class EcsSceneBridge;   // ecs_bridge.h

// One value that play changed and the developer may want to keep.
struct PlayChange {
    uint32_t    entity_id = 0;
    std::string entity_name;
    std::string component;      // "Transform", or an authorable component name
    std::string summary;        // human-readable, e.g. "Health 100 -> 42"
    bool        keep = false;   // the developer's choice; default is discard

    // Play-end payload, applied verbatim when kept.
    bool                 is_transform = false;
    glm::vec3            position{0.0f};
    glm::quat            rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3            scale{1.0f};
    std::vector<uint8_t> bytes;     // serialized authorable component
};

// What a diff found, including the parts it cannot represent.
struct PlayChangeReport {
    std::vector<PlayChange> changes;
    size_t entities_spawned   = 0;   // present at stop, absent at start
    size_t entities_destroyed = 0;   // present at start, absent at stop
    bool empty() const {
        return changes.empty() && entities_spawned == 0 && entities_destroyed == 0;
    }
};

class PlayModeChanges {
public:
    // Baseline the authored state. Call immediately BEFORE StartPlayback.
    void Capture(const std::shared_ptr<schizo::scene::Scene>& scene,
                 EcsSceneBridge* bridge);

    // Compare live state against the baseline. Call BEFORE StopPlayback, while
    // the play-end values still exist.
    PlayChangeReport Diff(const std::shared_ptr<schizo::scene::Scene>& scene,
                          EcsSceneBridge* bridge) const;

    // Write the chosen changes into the authored scene. Call AFTER StopPlayback
    // so the restore does not undo them. Rows with keep == false are skipped.
    // Returns how many were applied.
    static size_t Apply(const std::vector<PlayChange>& changes,
                        const std::shared_ptr<schizo::scene::Scene>& scene,
                        EcsSceneBridge* bridge);

    // Read the CURRENT value of each given row, marked keep, so applying the
    // result restores exactly what those rows are about to overwrite. That is
    // what makes "Keep selected" undoable: the inverse of an apply is another
    // apply, so no second code path can disagree with the first.
    static std::vector<PlayChange> SnapshotCurrent(
        const std::vector<PlayChange>& rows,
        const std::shared_ptr<schizo::scene::Scene>& scene,
        EcsSceneBridge* bridge);

    void Clear() { baseline_.clear(); has_baseline_ = false; }
    bool has_baseline() const { return has_baseline_; }

private:
    struct Baseline {
        std::string entity_name;
        glm::vec3   position{0.0f};
        glm::quat   rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3   scale{1.0f};
        // authorable component name -> serialized bytes at play start
        std::unordered_map<std::string, std::vector<uint8_t>> components;
    };

    std::unordered_map<uint32_t, Baseline> baseline_;
    bool has_baseline_ = false;
};

}  // namespace schizo::editor
