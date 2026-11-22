#pragma once
// ============================================================================
// anim_graph_runtime.h -- the animation graph a character runs in Play:
//
//   - parameters (floats, bools, triggers) set by gameplay each frame;
//   - layers, each a state machine: the first poses the whole body, the others
//     a part of it (the bones under a mask bone: an upper body that aims and
//     fires while the legs walk);
//   - a state plays a clip, or a blend space of clips over one or two
//     parameters (1D by Delaunay segments, 2D by Delaunay triangles; a point
//     outside them is clamped onto their edge, radially from the origin for
//     locomotion: the way kept, and how far past said, for stride and rate);
//   - sync groups: the clips of a blend space, and states of one group, keep
//     one phase measured between their foot plants (sync markers): a walk and
//     a strafe blended put down the same foot together; walk to run goes on
//     with the same foot's step;
//   - transitions on conditions over the parameters, an exit time, from a state
//     or from any state; a change is reported with its blend time and the
//     bones it touches, and the pose layer above (the character's actor) carries
//     the difference away on a spring (inertialization: no clip sampled twice);
//   - montage slots: a one-shot (a shot, a reload, a hit, a death) played over
//     the layers on a slot's bones, from gameplay or a script.
//
// Pure: clips are reached through ClipSource (duration, sample into a pose,
// sync markers). Checked by tools/animgraph_check.
// ============================================================================
#include "skeleton.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace schizo::renderer::animgraph {

using Pose = std::vector<BoneTransform>;

// ---- blend spaces ------------------------------------------------------------------

struct BlendSample {
    int clip = -1;
    glm::vec2 at{0.0f};    // where it sits in the space (1D: x)
    float rate = 1.0f;     // its own play rate
    bool measured = false; // placed where its clip was measured to move (a document keeps that)
};
struct BlendWeight {
    int sample = -1;
    float w = 0.0f;
};

/// Samples over one or two parameters; weights at a point by the Delaunay
/// triangle (or segment) holding it.
struct BlendSpace {
    std::vector<BlendSample> samples;
    std::vector<std::array<int, 3>> tris;   // (2D) Delaunay triangles
    std::vector<int> order;                  // (1D, or all on a line) samples along the line
    bool line = false;
    glm::vec2 line_o{0.0f}, line_d{1.0f, 0.0f};

    /// Triangulates (call after the samples change). On a line (or one axis):
    /// segments along it.
    void build() {
        tris.clear();
        order.clear();
        line = false;
        const int n = static_cast<int>(samples.size());
        if (n == 0) return;
        // On a line? The farthest pair sets it; every point near it.
        int a = 0, b = 0;
        float far = -1.0f;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) {
                const float d = glm::length(samples[static_cast<size_t>(i)].at - samples[static_cast<size_t>(j)].at);
                if (d > far) { far = d; a = i; b = j; }
            }
        line_o = samples[static_cast<size_t>(a)].at;
        line_d = far > 1e-6f ? (samples[static_cast<size_t>(b)].at - line_o) / far : glm::vec2(1.0f, 0.0f);
        bool on_line = true;
        for (const auto& s : samples) {
            const glm::vec2 r = s.at - line_o;
            if (std::abs(r.x * line_d.y - r.y * line_d.x) > 1e-3f * std::max(1.0f, far)) { on_line = false; break; }
        }
        if (n < 3 || on_line) {
            line = true;
            for (int i = 0; i < n; ++i) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](int x, int y) {
                return glm::dot(samples[static_cast<size_t>(x)].at - line_o, line_d) < glm::dot(samples[static_cast<size_t>(y)].at - line_o, line_d);
            });
            return;
        }
        // Bowyer-Watson in a super triangle.
        glm::vec2 lo(1e30f), hi(-1e30f);
        for (const auto& s : samples) { lo = glm::min(lo, s.at); hi = glm::max(hi, s.at); }
        const glm::vec2 c = (lo + hi) * 0.5f;
        const float span = std::max(hi.x - lo.x, hi.y - lo.y) * 20.0f + 1.0f;
        std::vector<glm::vec2> P;
        for (const auto& s : samples) P.push_back(s.at);
        P.push_back(c + glm::vec2(-span, -span));
        P.push_back(c + glm::vec2(span, -span));
        P.push_back(c + glm::vec2(0.0f, span));
        struct T { int v[3]; };
        std::vector<T> T_ = {{{n, n + 1, n + 2}}};
        auto in_circle = [&](const T& t, const glm::vec2& p) {
            glm::vec2 A = P[static_cast<size_t>(t.v[0])], B = P[static_cast<size_t>(t.v[1])], C = P[static_cast<size_t>(t.v[2])];
            if ((B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x) < 0.0f) std::swap(B, C);   // counter-clockwise
            const double ax = A.x - p.x, ay = A.y - p.y, bx = B.x - p.x, by = B.y - p.y, cx = C.x - p.x, cy = C.y - p.y;
            const double det = (ax * ax + ay * ay) * (bx * cy - cx * by) - (bx * bx + by * by) * (ax * cy - cx * ay) +
                               (cx * cx + cy * cy) * (ax * by - bx * ay);
            return det > 1e-12;
        };
        for (int i = 0; i < n; ++i) {
            const glm::vec2 p = P[static_cast<size_t>(i)];
            std::vector<std::array<int, 2>> edges;
            std::vector<T> keep;
            for (const T& t : T_) {
                if (in_circle(t, p)) {
                    for (int e = 0; e < 3; ++e) edges.push_back({t.v[e], t.v[(e + 1) % 3]});
                } else {
                    keep.push_back(t);
                }
            }
            // The hole's boundary: edges not shared by two removed triangles.
            for (size_t e = 0; e < edges.size(); ++e) {
                bool shared = false;
                for (size_t f = 0; f < edges.size() && !shared; ++f)
                    if (e != f && ((edges[e][0] == edges[f][1] && edges[e][1] == edges[f][0]) || (edges[e][0] == edges[f][0] && edges[e][1] == edges[f][1])))
                        shared = true;
                if (!shared) keep.push_back({{edges[e][0], edges[e][1], i}});
            }
            T_.swap(keep);
        }
        for (const T& t : T_) {
            if (t.v[0] >= n || t.v[1] >= n || t.v[2] >= n) continue;
            const glm::vec2 A = P[static_cast<size_t>(t.v[0])], B = P[static_cast<size_t>(t.v[1])], C = P[static_cast<size_t>(t.v[2])];
            if (std::abs((B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x)) < 1e-8f) continue;
            tris.push_back({t.v[0], t.v[1], t.v[2]});
        }
        if (tris.empty()) {   // (degenerate after all)
            line = true;
            for (int i = 0; i < n; ++i) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](int x, int y) {
                return glm::dot(samples[static_cast<size_t>(x)].at - line_o, line_d) < glm::dot(samples[static_cast<size_t>(y)].at - line_o, line_d);
            });
        }
    }

    /// Whether the origin is inside (or on) the triangulated area.
    bool holds_origin() const {
        if (line) return false;
        float w[3];
        for (const auto& t : tris) if (bary(t, glm::vec2(0.0f), w)) return true;
        return false;
    }

    /// The weights at `p`. Outside the samples' area the point is clamped onto its
    /// edge: `radial` (and the origin inside) along the line from the origin (the
    /// way kept; `*over` = how much farther the point is than the edge), else the
    /// nearest point. `*over` is 1 inside.
    void weights(glm::vec2 p, bool radial, std::vector<BlendWeight>& out, float* over = nullptr) const {
        out.clear();
        if (over) *over = 1.0f;
        const int n = static_cast<int>(samples.size());
        if (n == 0) return;
        if (n == 1) { out.push_back({0, 1.0f}); return; }
        if (line) {
            float t = glm::dot(p - line_o, line_d);
            const float t0 = glm::dot(samples[static_cast<size_t>(order.front())].at - line_o, line_d);
            const float t1 = glm::dot(samples[static_cast<size_t>(order.back())].at - line_o, line_d);
            if (radial && over) {
                // Measured from the origin's place on the line: how far past the end.
                const float to = glm::dot(-line_o, line_d);
                if (t > t1 && t1 - to > 1e-4f) *over = (t - to) / (t1 - to);
                else if (t < t0 && to - t0 > 1e-4f) *over = (to - t) / (to - t0);
            }
            t = std::clamp(t, t0, t1);
            for (size_t k = 0; k + 1 < order.size(); ++k) {
                const float a = glm::dot(samples[static_cast<size_t>(order[k])].at - line_o, line_d);
                const float b = glm::dot(samples[static_cast<size_t>(order[k + 1])].at - line_o, line_d);
                if (t <= b + 1e-6f || k + 2 == order.size()) {
                    const float u = b - a > 1e-6f ? std::clamp((t - a) / (b - a), 0.0f, 1.0f) : 0.0f;
                    if (1.0f - u > 1e-6f) out.push_back({order[k], 1.0f - u});
                    if (u > 1e-6f) out.push_back({order[k + 1], u});
                    return;
                }
            }
            return;
        }
        float w[3];
        for (const auto& tri : tris)
            if (bary(tri, p, w)) {
                for (int k = 0; k < 3; ++k) if (w[k] > 1e-6f) out.push_back({tri[static_cast<size_t>(k)], w[k]});
                return;
            }
        // Outside: onto the edge (the hull: edges of one triangle only).
        glm::vec2 q = p;
        int ea = -1, eb = -1;
        float eu = 0.0f;
        const bool from_origin = radial && glm::length(p) > 1e-6f && holds_origin();
        float best = 1e30f;
        for (const auto& tri : tris)
            for (int e = 0; e < 3; ++e) {
                const int i = tri[static_cast<size_t>(e)], j = tri[static_cast<size_t>((e + 1) % 3)];
                if (!hull_edge(i, j)) continue;
                const glm::vec2 A = samples[static_cast<size_t>(i)].at, B = samples[static_cast<size_t>(j)].at;
                if (from_origin) {
                    // The ray 0 + s p meets A + u (B - A): s and u.
                    const glm::vec2 d = B - A;
                    const float den = p.x * (-d.y) - p.y * (-d.x);
                    if (std::abs(den) < 1e-9f) continue;
                    const float s = (A.x * (-d.y) - A.y * (-d.x)) / den;
                    const float u = (p.x * A.y - p.y * A.x) / den;
                    if (s > 1e-6f && u >= -1e-4f && u <= 1.0f + 1e-4f && s < best) { best = s; ea = i; eb = j; eu = std::clamp(u, 0.0f, 1.0f); q = p * s; }
                } else {
                    const glm::vec2 d = B - A;
                    const float len2 = glm::dot(d, d);
                    const float u = len2 > 1e-12f ? std::clamp(glm::dot(p - A, d) / len2, 0.0f, 1.0f) : 0.0f;
                    const glm::vec2 c = A + d * u;
                    const float dist = glm::length(p - c);
                    if (dist < best) { best = dist; ea = i; eb = j; eu = u; q = c; }
                }
            }
        if (ea < 0) { out.push_back({nearest(p), 1.0f}); return; }
        if (from_origin && over && glm::length(q) > 1e-6f) *over = glm::length(p) / glm::length(q);
        if (1.0f - eu > 1e-6f) out.push_back({ea, 1.0f - eu});
        if (eu > 1e-6f) out.push_back({eb, eu});
    }

private:
    bool bary(const std::array<int, 3>& t, const glm::vec2& p, float* w) const {
        const glm::vec2 A = samples[static_cast<size_t>(t[0])].at, B = samples[static_cast<size_t>(t[1])].at, C = samples[static_cast<size_t>(t[2])].at;
        const float den = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
        if (std::abs(den) < 1e-12f) return false;
        w[0] = ((B.y - C.y) * (p.x - C.x) + (C.x - B.x) * (p.y - C.y)) / den;
        w[1] = ((C.y - A.y) * (p.x - C.x) + (A.x - C.x) * (p.y - C.y)) / den;
        w[2] = 1.0f - w[0] - w[1];
        const float eps = -1e-5f;
        if (w[0] < eps || w[1] < eps || w[2] < eps) return false;
        for (int k = 0; k < 3; ++k) w[k] = std::max(0.0f, w[k]);
        const float s = w[0] + w[1] + w[2];
        for (int k = 0; k < 3; ++k) w[k] /= s;
        return true;
    }
    bool hull_edge(int i, int j) const {
        int count = 0;
        for (const auto& t : tris)
            for (int e = 0; e < 3; ++e) {
                const int a = t[static_cast<size_t>(e)], b = t[static_cast<size_t>((e + 1) % 3)];
                if ((a == i && b == j) || (a == j && b == i)) ++count;
            }
        return count == 1;
    }
    int nearest(const glm::vec2& p) const {
        int best = 0;
        for (size_t i = 1; i < samples.size(); ++i)
            if (glm::length(samples[i].at - p) < glm::length(samples[static_cast<size_t>(best)].at - p)) best = static_cast<int>(i);
        return best;
    }
};

// ---- sync markers ----------------------------------------------------------------------

/// A clip's foot plants: the seconds each foot comes down (foot 0 left, 1 right),
/// alternating. Its phase runs 0..1 over a stride: 0 the left foot down, 0.5 the
/// right; a clip with two strides runs it twice. Without markers the phase is
/// the fraction of the clip.
struct SyncMarkers {
    float duration = 0.0f;
    std::vector<float> t;
    std::vector<uint8_t> foot;

    /// From the two feet's contact times; empty unless they alternate.
    static SyncMarkers from_contacts(float duration, const std::vector<float>& left, const std::vector<float>& right) {
        SyncMarkers m;
        m.duration = duration;
        struct C { float t; uint8_t f; };
        std::vector<C> all;
        for (float x : left) all.push_back({x, 0});
        for (float x : right) all.push_back({x, 1});
        std::sort(all.begin(), all.end(), [](const C& a, const C& b) { return a.t < b.t; });
        if (all.size() < 2 || all.size() % 2) return m;
        for (size_t i = 0; i < all.size(); ++i) if (all[i].f == all[(i + 1) % all.size()].f) return m;
        // Start at a left foot.
        size_t s = 0;
        while (all[s].f != 0) ++s;
        for (size_t i = 0; i < all.size(); ++i) { m.t.push_back(all[(s + i) % all.size()].t); m.foot.push_back(all[(s + i) % all.size()].f); }
        return m;
    }
    bool valid() const { return duration > 0.0f && t.size() >= 2; }
    int strides() const { return valid() ? static_cast<int>(t.size()) / 2 : 1; }

    float phase_of(float time) const {
        if (duration <= 0.0f) return 0.0f;
        const float tt = wrap(time, duration);
        if (!valid()) return tt / duration;
        const int n = static_cast<int>(t.size());
        // The last marker at or before tt (the list starts at a left foot, wrapping).
        int k = n - 1;
        float best = 1e30f;
        for (int i = 0; i < n; ++i) {
            const float back = wrap(tt - t[static_cast<size_t>(i)], duration);
            if (back < best) { best = back; k = i; }
        }
        const float seg = seg_len(k);
        const float u = seg > 1e-6f ? std::clamp(best / seg, 0.0f, 1.0f) : 0.0f;
        return std::fmod((static_cast<float>(k) + u) * 0.5f, 1.0f);   // two steps a stride
    }
    /// The time at `phase`: of the clip's strides, the one at or just ahead of `near`.
    float time_of(float phase, float near) const {
        if (duration <= 0.0f) return 0.0f;
        phase = wrap(phase, 1.0f);
        if (!valid()) return phase * duration;
        const int n = static_cast<int>(t.size());
        float best_t = 0.0f, best_d = 1e30f;
        for (int s = 0; s < strides(); ++s) {
            const float steps = phase * 2.0f + 2.0f * static_cast<float>(s);
            const int k = std::min(static_cast<int>(steps), n - 1);
            const float u = steps - static_cast<float>(k);
            const float tt = wrap(t[static_cast<size_t>(k)] + u * seg_len(k), duration);
            float d = wrap(tt - near, duration);
            if (d > duration * 0.5f) d = duration - d;
            if (d < best_d) { best_d = d; best_t = tt; }
        }
        return best_t;
    }
    static float wrap(float x, float m) { return m > 0.0f ? x - m * std::floor(x / m) : 0.0f; }

private:
    float seg_len(int k) const {
        const int n = static_cast<int>(t.size());
        return wrap(t[static_cast<size_t>((k + 1) % n)] - t[static_cast<size_t>(k)], duration) + (n == 1 ? duration : 0.0f);
    }
};

// ---- the graph ---------------------------------------------------------------------------

enum class ParamType : uint8_t { Float, Bool, Trigger };
struct ParamDef {
    std::string name;
    ParamType type = ParamType::Float;
    float init = 0.0f;
};
enum class Op : uint8_t { Greater, GreaterEqual, Less, LessEqual, Equals, NotEquals, True, False, Triggered };
struct Condition {
    int param = -1;
    Op op = Op::True;
    float threshold = 0.0f;
};
enum class NodeKind : uint8_t { None, Clip, Blend1D, Blend2D };
struct StateDef {
    std::string name;
    NodeKind kind = NodeKind::Clip;
    int clip = -1;
    bool loop = true;
    float rate = 1.0f;
    int rate_param = -1;              // multiplies the rate (a float parameter)
    BlendSpace space;
    int param_x = -1, param_y = -1;   // the blend space's parameters
    bool radial = false;              // clamp outside the space from the origin (locomotion)
    int sync = -1;                    // sync group (an index; -1 none)
};
struct TransitionDef {
    int from = -1;                    // -1: from any state (not from itself)
    int to = 0;
    std::vector<Condition> when;      // all
    float blend = 0.2f;
    float exit = -1.0f;               // >= 0: only past this fraction of the state's clip (a once clip: to its end at 1)
};
struct LayerDef {
    std::string name;
    std::string mask_bone;            // empty: the whole body
    std::vector<StateDef> states;
    std::vector<TransitionDef> transitions;
    int entry = 0;
};
struct SlotDef {
    std::string name;
    std::string mask_bone;            // empty: the whole body
};
struct GraphDef {
    std::vector<ParamDef> params;
    std::vector<LayerDef> layers;
    std::vector<SlotDef> slots;
    std::vector<std::string> sync_groups;

    int param(const std::string& n) const {
        for (size_t i = 0; i < params.size(); ++i) if (params[i].name == n) return static_cast<int>(i);
        return -1;
    }
    int add_param(const std::string& n, ParamType t, float init = 0.0f) {
        if (const int i = param(n); i >= 0) return i;
        params.push_back({n, t, init});
        return static_cast<int>(params.size()) - 1;
    }
    int slot(const std::string& n) const {
        for (size_t i = 0; i < slots.size(); ++i) if (slots[i].name == n) return static_cast<int>(i);
        return -1;
    }
    int sync_group(const std::string& n) {
        for (size_t i = 0; i < sync_groups.size(); ++i) if (sync_groups[i] == n) return static_cast<int>(i);
        sync_groups.push_back(n);
        return static_cast<int>(sync_groups.size()) - 1;
    }
    static int state(const LayerDef& l, const std::string& n) {
        for (size_t i = 0; i < l.states.size(); ++i) if (l.states[i].name == n) return static_cast<int>(i);
        return -1;
    }
};

/// What a graph needs of a model's clips.
class ClipSource {
public:
    virtual ~ClipSource() = default;
    virtual int clip_count() const = 0;
    virtual float duration(int clip) const = 0;
    /// Sample into `pose` (already holding the bind pose for bones the clip leaves).
    virtual void sample(int clip, float time, bool loop, Pose& pose) const = 0;
    virtual const SyncMarkers& markers(int clip) const = 0;
    virtual const Pose& bind() const = 0;
};

/// Bones under `root` (by id; the bone itself included), for a skeleton's parents.
inline std::vector<uint8_t> mask_under(const std::vector<int>& parent, int root) {
    std::vector<uint8_t> m(parent.size(), 0);
    if (root < 0) { std::fill(m.begin(), m.end(), 1); return m; }
    for (size_t b = 0; b < parent.size(); ++b)
        for (int p = static_cast<int>(b); p >= 0; p = parent[static_cast<size_t>(p)])
            if (p == root) { m[b] = 1; break; }
    return m;
}

/// One character's run of a graph.
class GraphInstance {
public:
    /// A change the pose layer above smooths away: its blend time, and the bones
    /// (a mask; nullptr: all).
    struct Change {
        float blend = 0.2f;
        const std::vector<uint8_t>* mask = nullptr;
    };
    /// A clip being played (for its events, its locomotion): its time before and
    /// after this update, its weight, from its start (fresh).
    struct Active {
        int clip = -1;
        float was = 0.0f, now = 0.0f;
        bool loop = true, fresh = false;
        float weight = 1.0f;
        int layer = -1, slot = -1;
    };

    GraphInstance() = default;
    /// `parent`: each bone's parent id (for the masks), `find_bone`: a bone by name.
    template <class FindBone>
    void init(std::shared_ptr<const GraphDef> def, const std::vector<int>& parent, FindBone&& find_bone) {
        def_ = std::move(def);
        values_.clear();
        for (const auto& p : def_->params) values_.push_back(p.init);
        triggered_.assign(def_->params.size(), 0);
        layers_.assign(def_->layers.size(), {});
        layer_masks_.clear();
        for (size_t l = 0; l < def_->layers.size(); ++l) {
            const auto& ld = def_->layers[l];
            layer_masks_.push_back(ld.mask_bone.empty() ? std::vector<uint8_t>{} : mask_under(parent, find_bone(ld.mask_bone)));
            layers_[l].state = std::clamp(ld.entry, 0, std::max(0, static_cast<int>(ld.states.size()) - 1));
            layers_[l].fresh = true;
        }
        slot_masks_.clear();
        slots_.assign(def_->slots.size(), {});
        for (const auto& s : def_->slots) slot_masks_.push_back(s.mask_bone.empty() ? std::vector<uint8_t>{} : mask_under(parent, find_bone(s.mask_bone)));
        bones_ = parent.size();
    }
    bool ready() const { return def_ != nullptr; }
    const GraphDef& def() const { return *def_; }

    // ---- parameters ----
    void set(int p, float v) { if (p >= 0 && static_cast<size_t>(p) < values_.size()) values_[static_cast<size_t>(p)] = v; }
    void set_bool(int p, bool v) { set(p, v ? 1.0f : 0.0f); }
    void trigger(int p) { if (p >= 0 && static_cast<size_t>(p) < triggered_.size()) triggered_[static_cast<size_t>(p)] = 1; }
    float get(int p) const { return p >= 0 && static_cast<size_t>(p) < values_.size() ? values_[static_cast<size_t>(p)] : 0.0f; }
    void set(const std::string& n, float v) { set(def_ ? def_->param(n) : -1, v); }

    // ---- montages ----
    /// Plays `clip` once (or looping) on slot `s`'s bones over the layers, coming in
    /// over `blend_in`; at its end (or stop_montage) it goes over `blend_out`.
    void play_montage(int s, int clip, float rate = 1.0f, float blend_in = 0.1f, float blend_out = 0.2f, bool loop = false) {
        if (s < 0 || static_cast<size_t>(s) >= slots_.size() || clip < 0) return;
        Slot& m = slots_[static_cast<size_t>(s)];
        m.clip = clip;
        m.time = 0.0f;
        m.rate = rate;
        m.loop = loop;
        m.blend_out = blend_out;
        m.fresh = true;
        changes_.push_back({blend_in, mask_of_slot(s)});
    }
    void stop_montage(int s, float blend_out = -1.0f) {
        if (s < 0 || static_cast<size_t>(s) >= slots_.size() || slots_[static_cast<size_t>(s)].clip < 0) return;
        Slot& m = slots_[static_cast<size_t>(s)];
        changes_.push_back({blend_out >= 0.0f ? blend_out : m.blend_out, mask_of_slot(s)});
        m.clip = -1;
    }
    int montage_clip(int s) const { return s >= 0 && static_cast<size_t>(s) < slots_.size() ? slots_[static_cast<size_t>(s)].clip : -1; }
    float montage_time(int s) const { return s >= 0 && static_cast<size_t>(s) < slots_.size() ? slots_[static_cast<size_t>(s)].time : 0.0f; }

    // ---- what plays ----
    int state(int layer) const { return layer >= 0 && static_cast<size_t>(layer) < layers_.size() ? layers_[static_cast<size_t>(layer)].state : -1; }
    const std::string& state_name(int layer) const {
        static const std::string none;
        const int s = state(layer);
        return s >= 0 ? def_->layers[static_cast<size_t>(layer)].states[static_cast<size_t>(s)].name : none;
    }
    float state_time(int layer) const { return layer >= 0 && static_cast<size_t>(layer) < layers_.size() ? layers_[static_cast<size_t>(layer)].time : 0.0f; }
    /// The current state's blend weights (its last update), and how far past its
    /// edge the point was (1: inside).
    const std::vector<BlendWeight>& weights(int layer, float* over = nullptr) const {
        static const std::vector<BlendWeight> none;
        if (layer < 0 || static_cast<size_t>(layer) >= layers_.size()) return none;
        if (over) *over = layers_[static_cast<size_t>(layer)].over;
        return layers_[static_cast<size_t>(layer)].w;
    }

    /// Advance by `dt`: times and phases, then transitions (one per layer), then
    /// triggers cleared. Changes are queued (take_changes).
    void update(float dt, const ClipSource& clips) {
        if (!def_) return;
        active_.clear();
        for (size_t l = 0; l < layers_.size(); ++l) advance_layer(static_cast<int>(l), dt, clips);
        for (size_t l = 0; l < layers_.size(); ++l) evaluate_transitions(static_cast<int>(l), clips);
        for (size_t s = 0; s < slots_.size(); ++s) {
            Slot& m = slots_[s];
            if (m.clip < 0) continue;
            const float was = m.fresh ? -1e-4f : m.time;
            m.time += dt * m.rate;
            const float dur = clips.duration(m.clip);
            active_.push_back({m.clip, was, m.time, m.loop, m.fresh, 1.0f, -1, static_cast<int>(s)});
            m.fresh = false;
            if (!m.loop && m.time >= dur) stop_montage(static_cast<int>(s));
        }
        std::fill(triggered_.begin(), triggered_.end(), 0);
    }
    std::vector<Change> take_changes() { std::vector<Change> c; c.swap(changes_); return c; }
    const std::vector<Active>& active() const { return active_; }

    /// The pose: the layers, each over the one below on its mask, then the slots.
    void evaluate(const ClipSource& clips, Pose& out) {
        out = clips.bind();
        out.resize(bones_);
        for (size_t l = 0; l < layers_.size(); ++l) {
            const StateDef& sd = def_->layers[l].states[static_cast<size_t>(layers_[l].state)];
            if (sd.kind == NodeKind::None) continue;
            const bool whole = layer_masks_[l].empty();
            Pose& dst = whole ? out : tmp_;
            if (!whole) { tmp_ = clips.bind(); tmp_.resize(bones_); }
            pose_state(static_cast<int>(l), clips, dst);
            if (!whole)
                for (size_t b = 0; b < bones_ && b < layer_masks_[l].size(); ++b) if (layer_masks_[l][b]) out[b] = tmp_[b];
        }
        for (size_t s = 0; s < slots_.size(); ++s) {
            const Slot& m = slots_[s];
            if (m.clip < 0) continue;
            tmp_ = clips.bind();
            tmp_.resize(bones_);
            const float dur = clips.duration(m.clip);
            clips.sample(m.clip, m.loop ? m.time : std::min(m.time, std::max(0.0f, dur - 1e-4f)), m.loop, tmp_);
            const auto& mask = slot_masks_[s];
            for (size_t b = 0; b < bones_; ++b) if (mask.empty() || (b < mask.size() && mask[b])) out[b] = tmp_[b];
        }
    }

private:
    struct Layer {
        int state = 0;
        float time = 0.0f;      // a clip state's time
        float phase = 0.0f;     // a blend state's phase (0..1 a stride)
        bool fresh = true;
        std::vector<BlendWeight> w;
        float over = 1.0f;
        std::vector<float> sample_t;   // each sample's time (a blend state)
    };
    struct Slot {
        int clip = -1;
        float time = 0.0f, rate = 1.0f, blend_out = 0.2f;
        bool loop = false, fresh = false;
    };

    const std::vector<uint8_t>* mask_of_layer(int l) const {
        return layer_masks_[static_cast<size_t>(l)].empty() ? nullptr : &layer_masks_[static_cast<size_t>(l)];
    }
    const std::vector<uint8_t>* mask_of_slot(int s) const {
        return slot_masks_[static_cast<size_t>(s)].empty() ? nullptr : &slot_masks_[static_cast<size_t>(s)];
    }

    float rate_of(const StateDef& sd) const { return sd.rate * (sd.rate_param >= 0 ? get(sd.rate_param) : 1.0f); }

    void blend_weights(const StateDef& sd, Layer& L) {
        const glm::vec2 p(get(sd.param_x), sd.kind == NodeKind::Blend2D ? get(sd.param_y) : 0.0f);
        sd.space.weights(p, sd.radial, L.w, &L.over);
    }

    /// The phase of a layer's state (for its sync group).
    float phase_now(int l, const ClipSource& clips) const {
        const Layer& L = layers_[static_cast<size_t>(l)];
        const StateDef& sd = def_->layers[static_cast<size_t>(l)].states[static_cast<size_t>(L.state)];
        if (sd.kind == NodeKind::Clip) return sd.clip >= 0 ? clips.markers(sd.clip).phase_of(L.time) : 0.0f;
        return L.phase;
    }

    void advance_layer(int l, float dt, const ClipSource& clips) {
        Layer& L = layers_[static_cast<size_t>(l)];
        const StateDef& sd = def_->layers[static_cast<size_t>(l)].states[static_cast<size_t>(L.state)];
        const float rate = rate_of(sd);
        if (sd.kind == NodeKind::Clip) {
            if (sd.clip < 0) return;
            const float was = L.fresh ? L.time - 1e-4f : L.time;
            L.time += dt * rate;
            active_.push_back({sd.clip, was, L.time, sd.loop, L.fresh, 1.0f, l, -1});
            L.fresh = false;
            return;
        }
        if (sd.kind == NodeKind::None) return;
        blend_weights(sd, L);
        L.sample_t.resize(sd.space.samples.size(), 0.0f);
        // The phase moves at the weighted pace of its clips' strides.
        float pace = 0.0f;
        for (const auto& bw : L.w) {
            const BlendSample& s = sd.space.samples[static_cast<size_t>(bw.sample)];
            const float dur = s.clip >= 0 ? clips.duration(s.clip) : 0.0f;
            if (dur > 1e-4f) pace += bw.w * s.rate * static_cast<float>(clips.markers(s.clip).strides()) / dur;
        }
        const float was_phase = L.phase;
        L.phase = SyncMarkers::wrap(L.phase + dt * rate * pace, 1.0f);
        // Each clip at the phase; the heaviest one's events.
        int top = -1;
        float top_w = -1.0f;
        for (const auto& bw : L.w) if (bw.w > top_w) { top_w = bw.w; top = bw.sample; }
        for (const auto& bw : L.w) {
            const BlendSample& s = sd.space.samples[static_cast<size_t>(bw.sample)];
            if (s.clip < 0) continue;
            const SyncMarkers& m = clips.markers(s.clip);
            float& st = L.sample_t[static_cast<size_t>(bw.sample)];
            st = m.time_of(L.phase, st);
            if (bw.sample == top) {
                // Its time at the phase before (the same stride), and now (past its end: wrapped).
                const float was = L.fresh ? st - 1e-4f : m.time_of(was_phase, st);
                const float dur = m.duration > 0.0f ? m.duration : clips.duration(s.clip);
                float now = st;
                if (!L.fresh && dur > 0.0f && now < was) now += dur;
                active_.push_back({s.clip, was, now, true, L.fresh, bw.w, l, -1});
            }
        }
        L.fresh = false;
    }

    bool holds(const Condition& c) const {
        const float v = get(c.param);
        switch (c.op) {
            case Op::Greater: return v > c.threshold;
            case Op::GreaterEqual: return v >= c.threshold;
            case Op::Less: return v < c.threshold;
            case Op::LessEqual: return v <= c.threshold;
            case Op::Equals: return std::abs(v - c.threshold) < 1e-4f;
            case Op::NotEquals: return std::abs(v - c.threshold) >= 1e-4f;
            case Op::True: return v > 0.5f;
            case Op::False: return v <= 0.5f;
            case Op::Triggered: return c.param >= 0 && static_cast<size_t>(c.param) < triggered_.size() && triggered_[static_cast<size_t>(c.param)];
        }
        return false;
    }

    /// How far through its clip the layer's state is (0..1; a loop: this cycle).
    float progress(int l, const ClipSource& clips) const {
        const Layer& L = layers_[static_cast<size_t>(l)];
        const StateDef& sd = def_->layers[static_cast<size_t>(l)].states[static_cast<size_t>(L.state)];
        if (sd.kind == NodeKind::Clip && sd.clip >= 0) {
            const float dur = clips.duration(sd.clip);
            if (dur <= 1e-5f) return 1.0f;
            return sd.loop ? SyncMarkers::wrap(L.time, dur) / dur : std::min(1.0f, L.time / dur);
        }
        return L.phase;
    }

    void evaluate_transitions(int l, const ClipSource& clips) {
        const LayerDef& ld = def_->layers[static_cast<size_t>(l)];
        Layer& L = layers_[static_cast<size_t>(l)];
        auto fires = [&](const TransitionDef& t) {
            if (t.from >= 0 && t.from != L.state) return false;
            if (t.from < 0 && t.to == L.state) return false;
            if (t.exit >= 0.0f && progress(l, clips) < t.exit - 1e-5f) return false;
            for (const auto& c : t.when) if (!holds(c)) return false;
            return true;
        };
        const TransitionDef* go = nullptr;
        for (const auto& t : ld.transitions) if (t.from < 0 && fires(t)) { go = &t; break; }    // any state first
        if (!go) for (const auto& t : ld.transitions) if (t.from >= 0 && fires(t)) { go = &t; break; }
        if (!go || go->to < 0 || static_cast<size_t>(go->to) >= ld.states.size()) return;
        for (const auto& c : go->when) if (c.op == Op::Triggered && c.param >= 0) triggered_[static_cast<size_t>(c.param)] = 0;   // used
        const StateDef& from = ld.states[static_cast<size_t>(L.state)];
        const StateDef& to = ld.states[static_cast<size_t>(go->to)];
        const bool in_step = from.sync >= 0 && from.sync == to.sync;
        const float phase = in_step ? phase_now(l, clips) : 0.0f;
        L.state = go->to;
        L.fresh = true;
        L.time = 0.0f;
        L.phase = 0.0f;
        L.w.clear();   // (the last state's samples)
        L.over = 1.0f;
        L.sample_t.assign(to.space.samples.size(), 0.0f);
        if (in_step) {
            // The same point of the same foot's step.
            if (to.kind == NodeKind::Clip && to.clip >= 0) {
                L.time = clips.markers(to.clip).time_of(phase, 0.0f);
                L.fresh = false;
            } else {
                L.phase = phase;
                blend_weights(to, L);
                for (size_t i = 0; i < to.space.samples.size(); ++i)
                    if (to.space.samples[i].clip >= 0) L.sample_t[i] = clips.markers(to.space.samples[i].clip).time_of(phase, 0.0f);
                L.fresh = false;
            }
        }
        changes_.push_back({go->blend, mask_of_layer(l)});
    }

    void pose_state(int l, const ClipSource& clips, Pose& dst) {
        Layer& L = layers_[static_cast<size_t>(l)];
        const StateDef& sd = def_->layers[static_cast<size_t>(l)].states[static_cast<size_t>(L.state)];
        if (sd.kind == NodeKind::Clip) {
            if (sd.clip < 0) return;
            const float dur = clips.duration(sd.clip);
            clips.sample(sd.clip, sd.loop ? L.time : std::min(L.time, std::max(0.0f, dur - 1e-4f)), sd.loop, dst);
            return;
        }
        if (L.w.empty()) blend_weights(sd, L);
        if (L.sample_t.size() != sd.space.samples.size()) L.sample_t.assign(sd.space.samples.size(), 0.0f);
        // Weighted: positions and scales summed, rotations by a weighted nlerp
        // (each turned to the first's hemisphere).
        bool first = true;
        float total = 0.0f;
        for (const auto& bw : L.w) {
            const BlendSample& s = sd.space.samples[static_cast<size_t>(bw.sample)];
            if (s.clip < 0 || bw.w <= 0.0f) continue;
            one_ = clips.bind();
            one_.resize(bones_);
            clips.sample(s.clip, L.sample_t[static_cast<size_t>(bw.sample)], true, one_);
            if (first) {
                acc_.assign(bones_, BoneTransform{});
                for (size_t b = 0; b < bones_; ++b) {
                    acc_[b].position = one_[b].position * bw.w;
                    acc_[b].scale = one_[b].scale * bw.w;
                    acc_[b].rotation = one_[b].rotation * bw.w;
                }
                first = false;
            } else {
                for (size_t b = 0; b < bones_; ++b) {
                    acc_[b].position += one_[b].position * bw.w;
                    acc_[b].scale += one_[b].scale * bw.w;
                    glm::quat q = one_[b].rotation;
                    if (glm::dot(q, acc_[b].rotation) < 0.0f) q = -q;
                    acc_[b].rotation = acc_[b].rotation + q * bw.w;
                }
            }
            total += bw.w;
        }
        if (first || total <= 1e-6f) return;
        for (size_t b = 0; b < bones_; ++b) {
            dst[b].position = acc_[b].position / total;
            dst[b].scale = acc_[b].scale / total;
            const float len = glm::length(acc_[b].rotation);
            dst[b].rotation = len > 1e-8f ? acc_[b].rotation / len : dst[b].rotation;
        }
    }

    std::shared_ptr<const GraphDef> def_;
    std::vector<float> values_;
    std::vector<uint8_t> triggered_;
    std::vector<Layer> layers_;
    std::vector<Slot> slots_;
    std::vector<std::vector<uint8_t>> layer_masks_, slot_masks_;
    std::vector<Change> changes_;
    std::vector<Active> active_;
    size_t bones_ = 0;
    Pose tmp_, one_, acc_;
};

}  // namespace schizo::renderer::animgraph
