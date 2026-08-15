// ============================================================================
// sequence_check — the timeline evaluates the same way forwards and backwards.
//
// The property a sequencer lives or dies on: scrubbing to t must give exactly
// what playing to t gave. If it does not, the preview disagrees with the result
// and neither can be trusted — and the disagreement is invisible until someone
// compares a scrub against a playback frame by frame.
//
// That is why evaluation here is a pure function of time with no accumulated
// state. These tests exist to keep it that way.
// ============================================================================
#include "sequence.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace schizo::editor;
using gws::anim::CurveKey;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }

float value_of(const Sequence& s, float t, uint32_t entity, TrackTarget target) {
    for (const auto& v : s.evaluate(t))
        if (v.entity_id == entity && v.target == target) return v.value;
    return 1e9f;      // a sentinel no test expects, so a miss is obvious
}

}  // namespace

int main() {
    std::printf("=== sequence_check ===\n\n");

    // ---- 1. a track evaluates its curve --------------------------------------
    {
        Sequence s;
        s.set_duration(4.0f);
        Track& t = s.add_track(7, TrackTarget::PositionY);
        t.curve.add({0.0f, 0.0f});
        t.curve.add({4.0f, 10.0f});

        check(near(value_of(s, 0.0f, 7, TrackTarget::PositionY), 0.0f), "at t=0 it is the first key");
        check(near(value_of(s, 4.0f, 7, TrackTarget::PositionY), 10.0f), "at the end it is the last");
        check(near(value_of(s, 2.0f, 7, TrackTarget::PositionY), 5.0f),
              "and the midpoint eases through halfway");
    }

    // ---- 2. SCRUBBING BACKWARDS MATCHES PLAYING FORWARDS ---------------------
    // The assertion this whole design exists for. Any accumulated state would
    // break it, and nothing else would notice.
    {
        Sequence s;
        s.set_duration(3.0f);
        Track& t = s.add_track(1, TrackTarget::PositionX);
        t.curve.add({0.0f, -5.0f});
        t.curve.add({1.5f, 12.0f});
        t.curve.add({3.0f, 2.0f});

        // Sample forwards, then the same times backwards.
        float fwd[31], bwd[31];
        for (int i = 0; i <= 30; ++i)
            fwd[i] = value_of(s, 0.1f * float(i), 1, TrackTarget::PositionX);
        for (int i = 30; i >= 0; --i)
            bwd[i] = value_of(s, 0.1f * float(i), 1, TrackTarget::PositionX);

        bool same = true;
        for (int i = 0; i <= 30; ++i) if (!near(fwd[i], bwd[i])) same = false;
        check(same, "sampling backwards gives byte-identical values to sampling forwards");

        // And evaluating the same instant twice cannot drift.
        check(near(value_of(s, 1.234f, 1, TrackTarget::PositionX),
                   value_of(s, 1.234f, 1, TrackTarget::PositionX)),
              "and evaluating one instant twice gives the same answer");
    }

    // ---- 3. time is clamped to the sequence ----------------------------------
    // A playhead past the end must hold the final pose, not extrapolate a
    // camera off into space.
    {
        Sequence s;
        s.set_duration(2.0f);
        Track& t = s.add_track(3, TrackTarget::ScaleUniform);
        t.curve.add({0.0f, 1.0f});
        t.curve.add({2.0f, 3.0f});

        check(near(value_of(s, -10.0f, 3, TrackTarget::ScaleUniform), 1.0f),
              "before the start it holds the first value");
        check(near(value_of(s, 99.0f, 3, TrackTarget::ScaleUniform), 3.0f),
              "and past the end it holds the last, rather than extrapolating");
    }

    // ---- 4. an EMPTY track contributes nothing --------------------------------
    // Contributing 0 would snap the object to the origin the moment a track is
    // created, which reads as the timeline breaking the scene.
    {
        Sequence s;
        s.add_track(5, TrackTarget::PositionX);         // no keys
        check(s.evaluate(1.0f).empty(),
              "a track with no keyframes yields no value at all");

        Track& t = s.add_track(5, TrackTarget::PositionY);
        t.curve.add({0.0f, 4.0f});
        check(s.evaluate(1.0f).size() == 1,
              "and an authored track beside it still evaluates");
    }

    // ---- 5. disabled tracks are skipped ---------------------------------------
    {
        Sequence s;
        Track& t = s.add_track(9, TrackTarget::PositionZ);
        t.curve.add({0.0f, 1.0f});
        check(s.evaluate(0.0f).size() == 1, "an enabled track evaluates");
        s.tracks()[0].enabled = false;
        check(s.evaluate(0.0f).empty(), "a disabled one does not");
    }

    // ---- 6. one channel cannot have two curves --------------------------------
    // Whichever won would be arbitrary, and the loser would be invisible in the
    // UI while still occupying a row.
    {
        Sequence s;
        s.add_track(2, TrackTarget::RotationYaw).curve.add({0.0f, 1.0f});
        s.add_track(2, TrackTarget::RotationYaw).curve.add({1.0f, 2.0f});
        check(s.track_count() == 1, "adding the same entity+target twice reuses the track");
        check(s.tracks()[0].curve.size() == 2, "and both keys land on that one curve");

        s.add_track(2, TrackTarget::RotationPitch);
        check(s.track_count() == 2, "but a different channel is its own track");
        s.add_track(3, TrackTarget::RotationYaw);
        check(s.track_count() == 3, "and so is the same channel on another entity");
    }

    // ---- 7. looping wraps correctly, even after a long stall -------------------
    // A stalled frame or a debugger pause produces a huge dt. Subtracting the
    // duration once would leave the playhead past the end and the sequence
    // would look frozen.
    {
        Sequence s;
        s.set_duration(2.0f);
        check(near(s.advance(1.5f, 1.0f, true), 0.5f), "advancing past the end wraps");
        check(near(s.advance(1.5f, 1.0f, false), 2.0f), "or stops at the end when not looping");
        const float after_stall = s.advance(0.0f, 7.3f, true);
        check(after_stall >= 0.0f && after_stall <= 2.0f,
              "and a 7.3s stall on a 2s sequence still lands inside the range");
        check(near(after_stall, 1.3f, 1e-3f), "at the right point in the cycle");
    }

    // ---- 8. duration cannot be zero -------------------------------------------
    // Zero would divide by zero in the wrap, and a UI showing a zero-length
    // timeline has nowhere to put a key.
    {
        Sequence s;
        s.set_duration(0.0f);
        check(s.duration() > 0.0f, "a zero duration is refused");
        s.set_duration(-5.0f);
        check(s.duration() > 0.0f, "and so is a negative one");
    }

    // ---- 9. removing a track keeps the rest intact -----------------------------
    {
        Sequence s;
        s.add_track(1, TrackTarget::PositionX).curve.add({0.0f, 1.0f});
        s.add_track(2, TrackTarget::PositionY).curve.add({0.0f, 2.0f});
        s.add_track(3, TrackTarget::PositionZ).curve.add({0.0f, 3.0f});
        s.remove_track(1);
        check(s.track_count() == 2, "removing one leaves the others");
        check(near(value_of(s, 0.0f, 3, TrackTarget::PositionZ), 3.0f),
              "and the survivors still evaluate correctly");
        s.remove_track(99);
        check(s.track_count() == 2, "an out-of-range remove does nothing");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL sequence_check\n");
    return g_fail ? 1 : 0;
}
