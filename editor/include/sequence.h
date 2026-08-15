#pragma once
// ============================================================================
// sequence — keyframed tracks over time (4.4).
//
// Cutscenes, camera moves and ability choreography. Without one, anything that
// has to happen ON A SCHEDULE gets written in C++ with hand-tuned timers, which
// is why "the door opens two seconds after the lever" is currently a code
// change rather than an edit.
//
// Built on gws::anim::Curve from 4.5 rather than a second keyframe type. That
// is not tidiness: a sequence with its own interpolation would ease differently
// from a curve editor showing the same data, and the editor would be lying
// about what playback will do.
//
// Evaluation is a PURE FUNCTION of time -- no accumulated state, no "advance by
// dt". Scrubbing backwards must give exactly what playing forwards gave, or the
// preview disagrees with the result and nobody can trust either. It also means
// this file can be tested without a scene.
// ============================================================================

#include "anim/curve.h"

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::editor {

/// What a track drives. Deliberately a small fixed set of scalar channels
/// rather than "any reflected field": every one of these maps to something the
/// transform already exposes, so a track can never reference a property that
/// stopped existing.
enum class TrackTarget : uint8_t {
    PositionX, PositionY, PositionZ,
    RotationYaw, RotationPitch, RotationRoll,
    ScaleUniform,
    Count
};

inline const char* track_target_name(TrackTarget t) {
    switch (t) {
        case TrackTarget::PositionX:     return "Position X";
        case TrackTarget::PositionY:     return "Position Y";
        case TrackTarget::PositionZ:     return "Position Z";
        case TrackTarget::RotationYaw:   return "Rotation Yaw";
        case TrackTarget::RotationPitch: return "Rotation Pitch";
        case TrackTarget::RotationRoll:  return "Rotation Roll";
        case TrackTarget::ScaleUniform:  return "Scale";
        default:                         return "?";
    }
}

struct Track {
    uint32_t          entity_id = 0;
    TrackTarget       target    = TrackTarget::PositionX;
    gws::anim::Curve  curve;
    bool              enabled   = true;
};

/// One evaluated channel, handed to the caller to apply. Returned rather than
/// applied here so the model stays free of the scene -- and so a test can check
/// what WOULD be applied without building one.
struct TrackValue {
    uint32_t    entity_id;
    TrackTarget target;
    float       value;
};

class Sequence {
public:
    float duration() const { return duration_; }
    void  set_duration(float d) { duration_ = d > 0.01f ? d : 0.01f; }

    size_t track_count() const { return tracks_.size(); }
    std::vector<Track>&       tracks()       { return tracks_; }
    const std::vector<Track>& tracks() const { return tracks_; }

    /// Add a track, or return the existing one for this entity+target. One
    /// channel cannot have two curves: whichever won would be arbitrary, and
    /// the loser would be invisible in the UI.
    Track& add_track(uint32_t entity_id, TrackTarget target);

    void remove_track(size_t index);

    /// Every enabled track's value at `time`, clamped to [0, duration].
    ///
    /// Tracks with no keys are SKIPPED rather than contributing 0 -- an empty
    /// track is one the user has created but not authored, and snapping the
    /// object to the origin the moment a track appears would be alarming.
    std::vector<TrackValue> evaluate(float time) const;

    /// Playhead advance with looping. Returns the new time. Separate from
    /// evaluate() so that scrubbing and playing share exactly one evaluation
    /// path.
    float advance(float time, float dt, bool loop) const;

private:
    std::vector<Track> tracks_;
    float duration_ = 5.0f;
};

}  // namespace schizo::editor
