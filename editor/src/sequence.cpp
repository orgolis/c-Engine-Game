// ============================================================================
// sequence — see sequence.h.
// ============================================================================
#include "sequence.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace schizo::editor {

Track& Sequence::add_track(uint32_t entity_id, TrackTarget target) {
    for (Track& t : tracks_)
        if (t.entity_id == entity_id && t.target == target) return t;
    Track t;
    t.entity_id = entity_id;
    t.target    = target;
    tracks_.push_back(std::move(t));
    return tracks_.back();
}

void Sequence::remove_track(size_t index) {
    if (index < tracks_.size()) tracks_.erase(tracks_.begin() + static_cast<long>(index));
}

std::vector<TrackValue> Sequence::evaluate(float time) const {
    std::vector<TrackValue> out;
    out.reserve(tracks_.size());

    const float t = std::clamp(time, 0.0f, duration_);
    for (const Track& tr : tracks_) {
        if (!tr.enabled) continue;
        // An empty track is one the user made but has not authored yet.
        // Contributing 0 would snap the object to the origin the moment a track
        // appears, which reads as the timeline breaking the scene.
        if (tr.curve.empty()) continue;
        out.push_back({tr.entity_id, tr.target, tr.curve.evaluate(t)});
    }
    return out;
}

float Sequence::advance(float time, float dt, bool loop) const {
    float t = time + dt;
    if (t <= duration_) return t < 0.0f ? 0.0f : t;

    if (!loop) return duration_;
    // fmod rather than subtracting the duration once: a long dt (a stalled
    // frame, or a debugger pause) would otherwise leave the playhead beyond the
    // end and the sequence would appear frozen.
    const float wrapped = std::fmod(t, duration_);
    return wrapped < 0.0f ? wrapped + duration_ : wrapped;
}

}  // namespace schizo::editor
