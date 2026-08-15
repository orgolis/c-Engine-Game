// ============================================================================
// level_tools — see level_tools.h.
// ============================================================================
#include "level_tools.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace schizo::editor {

namespace {

// A small explicit PRNG (SplitMix64 -> float). Deliberately not std::rand:
// that is process-global, so a scatter's result would depend on whatever else
// called rand() first and a fixed seed would not reproduce a layout.
struct Rng {
    uint64_t state;
    explicit Rng(uint32_t seed) : state(0x9E3779B97F4A7C15ull ^ (uint64_t(seed) + 1)) {}

    uint64_t next_u64() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    /// [0,1)
    float next_float() {
        return static_cast<float>((next_u64() >> 40) * (1.0 / 16777216.0));
    }
    float range(float lo, float hi) { return lo + (hi - lo) * next_float(); }
};

}  // namespace

std::vector<Placement> build_array(const glm::vec3& origin, const ArrayParams& p) {
    std::vector<Placement> out;
    const int cx = std::max(1, p.count_x);
    const int cy = std::max(1, p.count_y);
    const int cz = std::max(1, p.count_z);
    out.reserve(static_cast<size_t>(cx) * cy * cz);

    // Centring shifts by half the total extent, so the array grows around the
    // source rather than only in +step. Half of (count-1), not count: with four
    // objects there are three gaps.
    glm::vec3 base = origin;
    if (p.center)
        base -= p.step * glm::vec3(float(cx - 1), float(cy - 1), float(cz - 1)) * 0.5f;

    for (int z = 0; z < cz; ++z)
        for (int y = 0; y < cy; ++y)
            for (int x = 0; x < cx; ++x) {
                Placement pl;
                pl.position = base + glm::vec3(p.step.x * float(x),
                                               p.step.y * float(y),
                                               p.step.z * float(z));
                out.push_back(pl);
            }
    return out;
}

std::vector<Placement> build_scatter(const glm::vec3& center, const ScatterParams& p) {
    std::vector<Placement> out;
    const int want = std::max(0, p.count);
    if (want == 0 || p.radius <= 0.0f) return out;

    Rng rng(p.seed);
    const float min_d2 = p.min_distance * p.min_distance;
    const int attempts = std::max(1, p.max_attempts);

    for (int i = 0; i < want; ++i) {
        bool placed = false;
        for (int a = 0; a < attempts && !placed; ++a) {
            // sqrt on the radius, or points bunch toward the centre: uniform r
            // gives uniform density per RING, not per unit area, and the result
            // looks like a target rather than a scatter.
            const float ang = rng.range(0.0f, 6.2831853f);
            const float r   = p.radius * std::sqrt(rng.next_float());
            const glm::vec3 pos = center + glm::vec3(std::cos(ang) * r, 0.0f,
                                                     std::sin(ang) * r);

            if (min_d2 > 0.0f) {
                bool too_close = false;
                for (const Placement& e : out) {
                    const glm::vec3 d = e.position - pos;
                    if ((d.x * d.x + d.z * d.z) < min_d2) { too_close = true; break; }
                }
                if (too_close) continue;
            }

            Placement pl;
            pl.position = pos;
            if (p.random_yaw)
                pl.rotation = glm::angleAxis(rng.range(0.0f, 6.2831853f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
            const float s = rng.range(std::min(p.scale_min, p.scale_max),
                                      std::max(p.scale_min, p.scale_max));
            pl.scale = glm::vec3(s);
            out.push_back(pl);
            placed = true;
        }
        // Not placed after `attempts`: the request is unsatisfiable at this
        // density. Returning fewer is the honest answer; overlapping them
        // anyway would quietly violate the spacing the user asked for.
    }
    return out;
}

glm::vec3 Spline::evaluate(float t) const {
    if (points_.empty()) return glm::vec3(0.0f);
    if (points_.size() == 1) return points_[0];
    if (points_.size() == 2)
        return glm::mix(points_[0], points_[1], std::clamp(t, 0.0f, 1.0f));

    t = std::clamp(t, 0.0f, 1.0f);
    const int segs = static_cast<int>(points_.size()) - 1;
    const float ft = t * float(segs);
    int   i  = std::min(static_cast<int>(ft), segs - 1);
    const float u = ft - float(i);

    // Endpoints are duplicated rather than wrapped, so the curve does not curl
    // back toward the far end of the path -- wrapping makes an open path behave
    // like a closed one at its extremes, which looks like a bug in the tool.
    auto at = [&](int k) {
        return points_[static_cast<size_t>(std::clamp(k, 0, segs))];
    };
    const glm::vec3 p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);

    const float u2 = u * u, u3 = u2 * u;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * u +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * u3);
}

float Spline::length(int samples) const {
    if (points_.size() < 2) return 0.0f;
    const int n = std::max(2, samples);
    float total = 0.0f;
    glm::vec3 prev = evaluate(0.0f);
    for (int i = 1; i <= n; ++i) {
        const glm::vec3 cur = evaluate(float(i) / float(n));
        total += glm::length(cur - prev);
        prev = cur;
    }
    return total;
}

std::vector<Placement> Spline::distribute(int count, bool align_to_tangent) const {
    std::vector<Placement> out;
    if (points_.size() < 2 || count <= 0) return out;

    // Build an arc-length table, then walk it. Spacing evenly in `t` instead
    // bunches instances where the curve is tight and stretches them on
    // straights -- most visible exactly at the corners people care about.
    constexpr int kSamples = 256;
    std::vector<float> arc(kSamples + 1, 0.0f);
    std::vector<glm::vec3> pos(kSamples + 1);
    pos[0] = evaluate(0.0f);
    for (int i = 1; i <= kSamples; ++i) {
        pos[i] = evaluate(float(i) / float(kSamples));
        arc[i] = arc[i - 1] + glm::length(pos[i] - pos[i - 1]);
    }
    const float total = arc[kSamples];
    if (total <= 1e-6f) return out;

    for (int k = 0; k < count; ++k) {
        // count == 1 lands at the start rather than dividing by zero.
        const float want = (count == 1) ? 0.0f
                                        : total * (float(k) / float(count - 1));
        int i = 0;
        while (i < kSamples && arc[i + 1] < want) ++i;

        const float seg = arc[i + 1] - arc[i];
        const float f   = seg > 1e-6f ? (want - arc[i]) / seg : 0.0f;

        Placement pl;
        pl.position = glm::mix(pos[i], pos[i + 1], f);
        if (align_to_tangent) {
            glm::vec3 tan = pos[std::min(i + 1, kSamples)] - pos[i];
            tan.y = 0.0f;
            if (glm::length(tan) > 1e-5f) {
                const float yaw = std::atan2(tan.x, tan.z);
                pl.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            }
        }
        out.push_back(pl);
    }
    return out;
}

}  // namespace schizo::editor
