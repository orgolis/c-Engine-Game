#pragma once

// ====================
// Snapshot interpolation — client-side render smoothing  (Stage 7 step 2, tail)
// ====================
//
// Snapshots arrive at a fixed but comparatively low rate (and can drop, since state
// rides the Unreliable channel). Snapping entities to the newest sample looks jerky.
// Instead the client buffers timestamped position samples per entity and RENDERS at
// a time that trails the newest sample by a small delay, interpolating between the
// two samples that bracket that render time. The delay (a couple of ticks) means
// there are almost always two samples to interpolate across, so motion stays smooth;
// a dropped snapshot just widens the gap the interpolation spans instead of causing
// a visible hitch.
//
// This is a pure client-side helper over the wire format in replication.h: after
// apply_snapshot(), record() each replicated entity's position at the snapshot tick,
// then sample() it at render time. Header-only, depends only on glm.
//
// Position only for now (the common case for remote avatars/props); rotation slerp
// is a straightforward follow-up.

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::network {

class TransformInterpolator {
public:
    // How far behind the newest sample to render, in ticks (default 2).
    void   set_delay_ticks(double d) { delay_ = d; }
    double delay_ticks() const       { return delay_; }
    // Bound on samples retained per entity (older ones are dropped).
    void   set_max_samples(size_t n) { max_samples_ = n < 2 ? 2 : n; }

    // Record a position sample for an entity at a snapshot tick. Samples are kept
    // ordered by tick; a repeat tick overwrites, an out-of-order tick is inserted.
    void record(uint64_t netid, uint64_t tick, const glm::vec3& pos) {
        auto& s = tracks_[netid];
        if (s.empty() || tick > s.back().tick) {
            s.push_back(Sample{tick, pos});
        } else {
            auto it = std::lower_bound(s.begin(), s.end(), tick,
                                       [](const Sample& a, uint64_t t) { return a.tick < t; });
            if (it != s.end() && it->tick == tick) it->pos = pos;
            else                                    s.insert(it, Sample{tick, pos});
        }
        while (s.size() > max_samples_) s.erase(s.begin());
        if (tick > newest_) newest_ = tick;
    }

    // Newest tick recorded across all entities (0 if none yet).
    uint64_t newest_tick() const { return newest_; }

    // Interpolated position at the trailing render time (newest_tick - delay).
    bool sample(uint64_t netid, glm::vec3& out) const {
        return sample_at(netid, static_cast<double>(newest_) - delay_, out);
    }

    // Interpolated position at an explicit render tick. Clamps to the first/last
    // sample outside the buffered range. Returns false if the entity is unknown.
    bool sample_at(uint64_t netid, double render_tick, glm::vec3& out) const {
        auto it = tracks_.find(netid);
        if (it == tracks_.end() || it->second.empty()) return false;
        const std::vector<Sample>& s = it->second;
        if (render_tick <= static_cast<double>(s.front().tick)) { out = s.front().pos; return true; }
        if (render_tick >= static_cast<double>(s.back().tick))  { out = s.back().pos;  return true; }
        for (size_t i = 0; i + 1 < s.size(); ++i) {
            const double t0 = static_cast<double>(s[i].tick);
            const double t1 = static_cast<double>(s[i + 1].tick);
            if (render_tick >= t0 && render_tick <= t1) {
                const double a = (t1 > t0) ? (render_tick - t0) / (t1 - t0) : 0.0;
                out = s[i].pos + static_cast<float>(a) * (s[i + 1].pos - s[i].pos);
                return true;
            }
        }
        out = s.back().pos;   // unreachable given the clamps above
        return true;
    }

    void forget(uint64_t netid) { tracks_.erase(netid); }
    void clear() { tracks_.clear(); newest_ = 0; }

private:
    struct Sample { uint64_t tick; glm::vec3 pos; };
    std::unordered_map<uint64_t, std::vector<Sample>> tracks_;  // per entity, tick-ordered
    uint64_t newest_      = 0;
    double   delay_       = 2.0;
    size_t   max_samples_ = 32;
};

}  // namespace engine::network
