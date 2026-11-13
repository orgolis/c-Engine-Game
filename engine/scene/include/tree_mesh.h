#pragma once

// ============================================================================
// Procedural trees -- a trunk that splits into limbs, branches and twigs,
// with leaves where the twigs end (broadleaf) or whorls of drooping branches
// covered in needle tufts (conifer).
// ============================================================================
//
// The foliage system's built-in Conifer and Broadleaf kinds draw these (an
// imported tree model is the Model kind). A tree is grown recursively: each
// branch is a tapering tube along a gently bending path; children leave it
// at a golden-angle spread, shorter and thinner; the last level carries the
// leaves. `detail` 0 is the full tree, 1 a lighter one for middle distance.
//
// The tree stands in a 1-tall frame: base at the origin, top full y = 1, so
// the foliage layer's size scales it. Leaves are single-sided quads meant to
// be drawn double-sided; their normals point out of the crown, which lights
// a crown like a canopy rather than like a heap of flat cards.
//
// Pure: no GPU. Checked by tools/terrainbrush_check.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace schizo::scene {

struct TreeVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct TreeMeshData {
    std::vector<TreeVertex> bark_vertices, leaf_vertices;
    std::vector<uint32_t>   bark_indices, leaf_indices;
    int branches = 0;                        // tubes grown (trunk included)
    size_t triangles() const { return (bark_indices.size() + leaf_indices.size()) / 3; }
};

namespace tree_detail {

struct Rand {
    uint32_t s;
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; }
    float range(float a, float b) { return a + (b - a) * next(); }
};

inline glm::vec3 any_perpendicular(const glm::vec3& d) {
    const glm::vec3 a = std::fabs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    return glm::normalize(glm::cross(d, a));
}

// A tapering tube through `pts`, radius r0 at the first point to r1 at the last.
inline void tube(TreeMeshData& m, const std::vector<glm::vec3>& pts, float r0, float r1, int sides) {
    const float pi = 3.14159265358979323846f;
    if (pts.size() < 2 || sides < 3) return;
    const uint32_t base = static_cast<uint32_t>(m.bark_vertices.size());
    const size_t n = pts.size();
    glm::vec3 side = any_perpendicular(glm::normalize(pts[1] - pts[0]));
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 dir = glm::normalize(i + 1 < n ? pts[i + 1] - pts[i] : pts[i] - pts[i - 1]);
        // Carry the frame along the path so the tube does not twist.
        side = glm::normalize(side - dir * glm::dot(side, dir));
        const glm::vec3 up = glm::cross(dir, side);
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        const float r = r0 + (r1 - r0) * t;
        for (int s = 0; s <= sides; ++s) {
            const float a = 2.0f * pi * static_cast<float>(s) / static_cast<float>(sides);
            const glm::vec3 nrm = side * std::cos(a) + up * std::sin(a);
            m.bark_vertices.push_back({pts[i] + nrm * r, nrm, glm::vec2(static_cast<float>(s) / sides, t * 4.0f)});
        }
    }
    const uint32_t ring = static_cast<uint32_t>(sides + 1);
    for (uint32_t i = 0; i + 1 < n; ++i)
        for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) {
            const uint32_t a = base + i * ring + s, b = a + 1, c = a + ring, d = c + 1;
            m.bark_indices.insert(m.bark_indices.end(), {a, c, b, b, c, d});
        }
    ++m.branches;
}

// One leaf (or needle tuft, when long and thin): a quad at `p` facing `face`,
// lit with `light_n` (out of the crown).
inline void leaf(TreeMeshData& m, const glm::vec3& p, const glm::vec3& face, const glm::vec3& along, float w, float h,
                 const glm::vec3& light_n) {
    const glm::vec3 a = glm::normalize(along - face * glm::dot(along, face));
    const glm::vec3 b = glm::normalize(glm::cross(face, a));
    const uint32_t i = static_cast<uint32_t>(m.leaf_vertices.size());
    m.leaf_vertices.push_back({p - b * (w * 0.5f), light_n, {0, 0}});
    m.leaf_vertices.push_back({p + b * (w * 0.5f), light_n, {1, 0}});
    m.leaf_vertices.push_back({p + b * (w * 0.5f) + a * h, light_n, {1, 1}});
    m.leaf_vertices.push_back({p - b * (w * 0.5f) + a * h, light_n, {0, 1}});
    m.leaf_indices.insert(m.leaf_indices.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
}

// A path from `start` along `dir` for `length`, bending towards `bend` (unit
// per length; up for broadleaf limbs, down for conifer branches).
inline std::vector<glm::vec3> path(const glm::vec3& start, glm::vec3 dir, float length, const glm::vec3& bend, int segments,
                                   Rand& rnd, float wobble) {
    std::vector<glm::vec3> p{start};
    glm::vec3 at = start;
    const float step = length / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        dir = glm::normalize(dir + bend * step + glm::vec3(rnd.range(-1, 1), rnd.range(-1, 1), rnd.range(-1, 1)) * wobble * step);
        at += dir * step;
        p.push_back(at);
    }
    return p;
}

// A direction `angle` away from `d`, turned `azimuth` around it.
inline glm::vec3 spread(const glm::vec3& d, float angle, float azimuth) {
    const glm::vec3 side = any_perpendicular(d);
    const glm::vec3 off = glm::angleAxis(azimuth, d) * side;
    return glm::normalize(d * std::cos(angle) + off * std::sin(angle));
}

inline glm::vec3 point_on(const std::vector<glm::vec3>& p, float t) {
    const float f = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(p.size() - 1);
    const size_t i = std::min(static_cast<size_t>(f), p.size() - 2);
    return glm::mix(p[i], p[i + 1], f - static_cast<float>(i));
}

}  // namespace tree_detail

/// A broadleaf tree: the trunk splits into limbs, then branches, then twigs
/// with a cluster of leaves on each.
inline TreeMeshData generate_broadleaf_tree(uint32_t seed, int detail) {
    using namespace tree_detail;
    TreeMeshData m;
    Rand rnd{seed * 747796405u + 2891336453u};
    const float golden = 2.39996323f;
    const bool full = detail == 0;
    const int sides0 = full ? 7 : 5;
    const glm::vec3 crown_centre(0.0f, 0.68f, 0.0f);
    // Trunk: up to where it splits, leaning a little.
    const glm::vec3 lean(rnd.range(-0.08f, 0.08f), 1.0f, rnd.range(-0.08f, 0.08f));
    const float split = rnd.range(0.36f, 0.46f);
    const auto trunk = path(glm::vec3(0.0f), glm::normalize(lean), split, glm::vec3(0.0f), full ? 5 : 3, rnd, 0.25f);
    const float r_trunk = 0.034f;
    tube(m, trunk, r_trunk, r_trunk * 0.72f, sides0);
    const glm::vec3 top = trunk.back();

    auto leaf_cluster = [&](const glm::vec3& at, float radius, int count, float size) {
        for (int k = 0; k < count; ++k) {
            const glm::vec3 off = glm::vec3(rnd.range(-1, 1), rnd.range(-0.7f, 1), rnd.range(-1, 1)) * radius;
            const glm::vec3 p = at + off;
            const glm::vec3 out = glm::normalize(p - crown_centre + glm::vec3(0, 0.15f, 0));
            const glm::vec3 face = glm::normalize(out + glm::vec3(rnd.range(-0.6f, 0.6f), rnd.range(-0.3f, 0.6f), rnd.range(-0.6f, 0.6f)));
            const glm::vec3 along = glm::normalize(glm::vec3(rnd.range(-1, 1), rnd.range(-0.2f, 1), rnd.range(-1, 1)));
            leaf(m, p, face, along, size * rnd.range(0.7f, 1.1f), size * rnd.range(0.9f, 1.3f), out);
        }
    };

    const int limbs = full ? 4 + static_cast<int>(rnd.next() * 2.0f) : 4;
    float az = rnd.range(0.0f, 6.28f);
    for (int i = 0; i < limbs; ++i, az += golden) {
        const glm::vec3 d = spread(glm::vec3(0, 1, 0), rnd.range(0.55f, 0.95f), az);
        const float len = rnd.range(0.30f, 0.40f);
        const auto limb = path(top, d, len, glm::vec3(0, 0.9f, 0), full ? 4 : 3, rnd, 0.5f);
        tube(m, limb, r_trunk * 0.62f, r_trunk * 0.3f, full ? 5 : 4);
        const int branches = full ? 4 : 3;
        float az2 = rnd.range(0.0f, 6.28f);
        for (int j = 0; j < branches; ++j, az2 += golden) {
            const float t = rnd.range(0.35f, 0.95f);
            const glm::vec3 at = point_on(limb, t);
            const glm::vec3 pd = glm::normalize(limb.back() - limb.front());
            const glm::vec3 bd = spread(pd, rnd.range(0.5f, 0.9f), az2);
            const float blen = len * rnd.range(0.45f, 0.6f);
            const auto br = path(at, bd, blen, glm::vec3(0, 0.6f, 0), full ? 3 : 2, rnd, 0.6f);
            tube(m, br, r_trunk * 0.3f, r_trunk * 0.12f, full ? 4 : 3);
            if (!full) {
                leaf_cluster(br.back(), 0.075f, 9, 0.085f);
                leaf_cluster(point_on(br, 0.5f), 0.06f, 5, 0.08f);
                continue;
            }
            float az3 = rnd.range(0.0f, 6.28f);
            for (int k = 0; k < 3; ++k, az3 += golden) {
                const glm::vec3 tat = point_on(br, rnd.range(0.4f, 1.0f));
                const glm::vec3 td = spread(glm::normalize(br.back() - br.front()), rnd.range(0.5f, 1.0f), az3);
                const auto twig = path(tat, td, blen * rnd.range(0.35f, 0.5f), glm::vec3(0, 0.4f, 0), 2, rnd, 0.6f);
                tube(m, twig, r_trunk * 0.1f, r_trunk * 0.05f, 3);
                leaf_cluster(twig.back(), 0.055f, 14, 0.045f);
            }
            leaf_cluster(br.back(), 0.05f, 10, 0.045f);
        }
    }
    return m;
}

/// A conifer: a straight, tapering trunk to the top, with whorls of drooping
/// branches that shorten towards the top, covered in needle tufts.
inline TreeMeshData generate_conifer_tree(uint32_t seed, int detail) {
    using namespace tree_detail;
    TreeMeshData m;
    Rand rnd{seed * 2246822519u + 3266489917u};
    const bool full = detail == 0;
    const float golden = 2.39996323f;
    const auto trunk = path(glm::vec3(0.0f), glm::vec3(0, 1, 0), 1.0f, glm::vec3(0.0f), full ? 7 : 4, rnd, 0.04f);
    tube(m, trunk, 0.026f, 0.003f, full ? 6 : 5);
    const int whorls = full ? 11 : 7;
    float az = rnd.range(0.0f, 6.28f);
    for (int w = 0; w < whorls; ++w) {
        const float h = 0.16f + 0.8f * (static_cast<float>(w) + rnd.range(-0.2f, 0.2f)) / static_cast<float>(whorls);
        const float reach = 0.05f + 0.38f * std::pow(std::max(0.0f, 1.0f - h), 0.9f);
        const int count = full ? 7 : 5;
        for (int b = 0; b < count; ++b, az += golden) {
            const glm::vec3 at = point_on(trunk, h);
            const glm::vec3 d = spread(glm::vec3(0, 1, 0), rnd.range(1.25f, 1.5f), az);   // full horizontal
            const auto br = path(at, d, reach * rnd.range(0.85f, 1.1f), glm::vec3(0, -0.9f, 0), full ? 3 : 2, rnd, 0.3f);
            tube(m, br, 0.008f * (1.0f - h * 0.5f), 0.002f, full ? 4 : 3);
            // Needle sprays along it: narrow strips fanned out on both sides
            // and a little up, like a fir's flat sprays. Near: many fine ones;
            // middle distance: fewer, wider, so the crown still reads as full.
            const int tufts = full ? 7 : 4;
            const int per_side = full ? 3 : 1;
            const glm::vec3 bd = glm::normalize(br.back() - br.front());
            const glm::vec3 across = glm::normalize(glm::cross(bd, glm::vec3(0, 1, 0)));
            for (int k = 0; k < tufts; ++k) {
                const float t = (static_cast<float>(k) + 0.5f) / static_cast<float>(tufts);
                const glm::vec3 p = point_on(br, t);
                const glm::vec3 out = glm::normalize(glm::vec3(p.x, 0.35f, p.z) + glm::vec3(0, 0.2f, 0));
                const float sz = (full ? 0.04f : 0.075f) * (1.2f - h * 0.6f) * (1.1f - 0.4f * t);
                for (int s = -1; s <= 1; s += 2)
                    for (int q = 0; q < per_side; ++q) {
                        const float fan = 0.25f + 0.3f * static_cast<float>(q) + rnd.range(-0.1f, 0.1f);
                        const glm::vec3 dir = glm::normalize(bd * (1.0f - fan) + across * (static_cast<float>(s) * fan) +
                                                             glm::vec3(0, rnd.range(-0.15f, 0.1f), 0));
                        const glm::vec3 face = glm::normalize(glm::vec3(0, 1, 0) + across * (static_cast<float>(s) * 0.35f));
                        leaf(m, p, face, dir, sz * (full ? 0.55f : 1.0f), sz * 2.6f, out);
                    }
            }
        }
    }
    // Small sprays round the leader at the very top.
    const glm::vec3 tip = trunk.back();
    for (int k = 0; k < 5; ++k) {
        const glm::vec3 d = spread(glm::vec3(0, 1, 0), 0.6f, static_cast<float>(k) * golden);
        leaf(m, tip - glm::vec3(0, 0.07f, 0), glm::normalize(glm::vec3(d.x, 0.4f, d.z)), d, 0.018f, 0.06f,
             glm::normalize(glm::vec3(d.x, 0.6f, d.z)));
    }
    return m;
}

}  // namespace schizo::scene
