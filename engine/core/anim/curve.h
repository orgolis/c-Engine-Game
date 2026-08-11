#pragma once
// ============================================================================
// curve — keyed float curves and colour gradients.
//
// Their absence is why values that vary over time end up as magic numbers in
// data files: a particle's colour is a start and an end because there is no
// type that can say "this, then this, then that". Fading in over the first 10%
// and out over the last 30% is not expressible at all, so effects get written
// in C++ or not written.
//
// Evaluation lives here, apart from the editor, because it is the half that can
// be wrong in ways nobody sees. A gradient that reads its last stop slightly
// early, or a curve that steps instead of easing, looks plausible in a
// screenshot and wrong in motion.
//
// Keys are kept sorted by time. Inserting is O(n) and evaluating is a linear
// scan: curves here have a handful of keys, and a binary search would be more
// code defending against a case that does not arise.
// ============================================================================

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace gws::anim {

// ---------------------------------------------------------------- Curve ----

struct CurveKey {
    float time  = 0.0f;
    float value = 0.0f;
    /// Tangents in value-per-unit-time. Both zero gives a flat-in/flat-out
    /// ease, which is the shape people expect from dragging two points.
    float in_tangent  = 0.0f;
    float out_tangent = 0.0f;
};

class Curve {
public:
    Curve() = default;
    explicit Curve(std::vector<CurveKey> keys) : keys_(std::move(keys)) { sort(); }

    /// Insert a key and return its index. Sorted on insert so evaluation never
    /// has to care how it was built -- an editor drags keys past each other
    /// constantly, and an unsorted curve evaluates to nonsense rather than
    /// failing.
    size_t add(const CurveKey& k) {
        keys_.push_back(k);
        sort();
        for (size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i].time == k.time && keys_[i].value == k.value) return i;
        return keys_.size() - 1;
    }

    void   remove(size_t i)  { if (i < keys_.size()) keys_.erase(keys_.begin() + i); }
    void   clear()           { keys_.clear(); }
    size_t size()      const { return keys_.size(); }
    bool   empty()     const { return keys_.empty(); }

    const std::vector<CurveKey>& keys() const { return keys_; }
    std::vector<CurveKey>&       keys()       { return keys_; }
    void sort() {
        std::stable_sort(keys_.begin(), keys_.end(),
                         [](const CurveKey& a, const CurveKey& b) { return a.time < b.time; });
    }

    /// Value at `t`, cubic Hermite between the surrounding keys.
    ///
    /// CLAMPED outside the key range rather than extrapolated. Extrapolating a
    /// cubic runs away fast, and a fade that overshoots into negative alpha
    /// because a particle lived a frame longer than the curve is a bug that
    /// only shows up at the edges.
    float evaluate(float t) const {
        if (keys_.empty()) return 0.0f;
        if (t <= keys_.front().time) return keys_.front().value;
        if (t >= keys_.back().time)  return keys_.back().value;

        for (size_t i = 0; i + 1 < keys_.size(); ++i) {
            const CurveKey& a = keys_[i];
            const CurveKey& b = keys_[i + 1];
            if (t < a.time || t > b.time) continue;

            const float dt = b.time - a.time;
            if (dt <= 0.0f) return b.value;      // duplicate times: take the later key

            const float u  = (t - a.time) / dt;
            const float u2 = u * u;
            const float u3 = u2 * u;

            // Hermite basis. Tangents are scaled by dt so a curve keeps its
            // shape when its keys are moved apart -- otherwise stretching a
            // curve in time also changes how it eases.
            const float h00 =  2.0f * u3 - 3.0f * u2 + 1.0f;
            const float h10 =         u3 - 2.0f * u2 + u;
            const float h01 = -2.0f * u3 + 3.0f * u2;
            const float h11 =         u3 -        u2;

            return h00 * a.value + h10 * dt * a.out_tangent +
                   h01 * b.value + h11 * dt * b.in_tangent;
        }
        return keys_.back().value;
    }

private:
    std::vector<CurveKey> keys_;
};

// ------------------------------------------------------------- Gradient ----

struct GradientStop {
    float     time  = 0.0f;
    glm::vec4 color{1.0f};
};

class Gradient {
public:
    Gradient() = default;
    explicit Gradient(std::vector<GradientStop> stops) : stops_(std::move(stops)) { sort(); }

    /// A two-stop ramp, the shape everything currently hard-codes as a start
    /// and an end colour. Having it as a named constructor makes converting an
    /// existing pair into a gradient a one-liner rather than a decision.
    static Gradient two_stop(const glm::vec4& a, const glm::vec4& b) {
        return Gradient({{0.0f, a}, {1.0f, b}});
    }

    size_t add(const GradientStop& s) {
        stops_.push_back(s);
        sort();
        for (size_t i = 0; i < stops_.size(); ++i)
            if (stops_[i].time == s.time) return i;
        return stops_.size() - 1;
    }

    void   remove(size_t i)  { if (i < stops_.size()) stops_.erase(stops_.begin() + i); }
    size_t size()      const { return stops_.size(); }
    bool   empty()     const { return stops_.empty(); }

    const std::vector<GradientStop>& stops() const { return stops_; }
    std::vector<GradientStop>&       stops()       { return stops_; }
    void sort() {
        std::stable_sort(stops_.begin(), stops_.end(),
                         [](const GradientStop& a, const GradientStop& b) { return a.time < b.time; });
    }

    /// Colour at `t`, linear between stops, clamped at both ends.
    ///
    /// Alpha interpolates with the rest. It is the channel that carries fades,
    /// so treating it separately -- or forgetting it -- is the difference
    /// between an effect that dissolves and one that pops out of existence.
    glm::vec4 sample(float t) const {
        if (stops_.empty()) return glm::vec4(1.0f);
        if (t <= stops_.front().time) return stops_.front().color;
        if (t >= stops_.back().time)  return stops_.back().color;

        for (size_t i = 0; i + 1 < stops_.size(); ++i) {
            const GradientStop& a = stops_[i];
            const GradientStop& b = stops_[i + 1];
            if (t < a.time || t > b.time) continue;

            const float dt = b.time - a.time;
            if (dt <= 0.0f) return b.color;      // coincident stops: a hard edge
            const float u = (t - a.time) / dt;
            return a.color + (b.color - a.color) * u;
        }
        return stops_.back().color;
    }

private:
    std::vector<GradientStop> stops_;
};

}  // namespace gws::anim
