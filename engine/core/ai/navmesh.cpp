// ============================================================================
// navmesh.cpp — triangle navmesh build + A* + funnel. See navmesh.h.
// ============================================================================

#include "ai/navmesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>

namespace schizo::ai {

namespace {

// 2D (XZ) signed area of triangle (a,b,c); >0 = c left of a->b.
float area2(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}

bool point_in_tri_xz(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b,
                     const glm::vec3& c) {
    const float d1 = area2(p, a, b);
    const float d2 = area2(p, b, c);
    const float d3 = area2(p, c, a);
    const bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);   // all same sign (or zero) → inside/on edge
}

// Quantised vertex key for edge welding.
uint64_t vkey(const glm::vec3& v, float inv_eps) {
    auto q = [&](float f) { return static_cast<int64_t>(std::llround(f * inv_eps)); };
    uint64_t h = 1469598103934665603ull;
    for (int64_t c : {q(v.x), q(v.y), q(v.z)}) {
        h ^= static_cast<uint64_t>(c); h *= 1099511628211ull;
    }
    return h;
}

float dist_xz(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

bool NavMesh::build(const std::vector<glm::vec3>& tv, float weld_eps) {
    tris_.clear();
    if (tv.size() < 3) return false;
    const size_t n = tv.size() / 3;
    tris_.reserve(n);
    for (size_t t = 0; t < n; ++t) {
        Tri tri;
        tri.v[0] = tv[t * 3 + 0];
        tri.v[1] = tv[t * 3 + 1];
        tri.v[2] = tv[t * 3 + 2];
        // Normalise to CCW-from-above (+Y up) so edge i has the interior on its
        // left → portal left/right are consistent in the funnel.
        const glm::vec3 nrm = glm::cross(tri.v[1] - tri.v[0], tri.v[2] - tri.v[0]);
        if (nrm.y < 0.0f) std::swap(tri.v[1], tri.v[2]);
        tri.centroid = (tri.v[0] + tri.v[1] + tri.v[2]) / 3.0f;
        tris_.push_back(tri);
    }

    // Adjacency: map each undirected edge (welded endpoints) → (tri, edge).
    const float inv_eps = 1.0f / std::max(weld_eps, 1e-6f);
    struct EdgeRef { int tri; int edge; };
    std::unordered_map<uint64_t, EdgeRef> edge_map;
    edge_map.reserve(tris_.size() * 3);
    for (int t = 0; t < static_cast<int>(tris_.size()); ++t) {
        for (int e = 0; e < 3; ++e) {
            const uint64_t ka = vkey(tris_[t].v[e], inv_eps);
            const uint64_t kb = vkey(tris_[t].v[(e + 1) % 3], inv_eps);
            // Order-INDEPENDENT edge key: the same physical edge is (a,b) in one
            // triangle and (b,a) in its neighbour, so combine symmetrically
            // (min/max) — an asymmetric mix would never match and leave every
            // triangle islanded (no adjacency → no path).
            const uint64_t lo = std::min(ka, kb), hi = std::max(ka, kb);
            const uint64_t key = lo * 0x9E3779B97F4A7C15ull ^ hi;
            auto it = edge_map.find(key);
            if (it == edge_map.end()) {
                edge_map[key] = {t, e};
            } else {
                tris_[t].neighbor[e] = it->second.tri;
                tris_[it->second.tri].neighbor[it->second.edge] = t;
            }
        }
    }
    build_grid();
    return true;
}

void NavMesh::build_grid() {
    cell_start_.clear();
    cell_tris_.clear();
    grid_w_ = grid_h_ = 0;
    if (tris_.empty()) return;
    glm::vec2 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
    for (const Tri& t : tris_)
        for (const glm::vec3& v : t.v) { mn = glm::min(mn, glm::vec2(v.x, v.z)); mx = glm::max(mx, glm::vec2(v.x, v.z)); }
    const glm::vec2 ext = glm::max(mx - mn, glm::vec2(1e-3f));
    // About eight triangles a cell; never more than ~4M cells.
    cell_ = std::clamp(std::sqrt(ext.x * ext.y / static_cast<float>(tris_.size()) * 8.0f), 0.25f, 256.0f);
    while ((ext.x / cell_ + 1.0f) * (ext.y / cell_ + 1.0f) > 4.0e6f) cell_ *= 1.5f;
    grid_min_ = mn;
    grid_w_ = static_cast<int>(ext.x / cell_) + 1;
    grid_h_ = static_cast<int>(ext.y / cell_) + 1;
    const size_t cells = static_cast<size_t>(grid_w_) * grid_h_;
    auto span = [&](const Tri& t, int& x0, int& z0, int& x1, int& z1) {
        float ax = t.v[0].x, bx = t.v[0].x, az = t.v[0].z, bz = t.v[0].z;
        for (int k = 1; k < 3; ++k) { ax = std::min(ax, t.v[k].x); bx = std::max(bx, t.v[k].x); az = std::min(az, t.v[k].z); bz = std::max(bz, t.v[k].z); }
        x0 = std::clamp(static_cast<int>((ax - grid_min_.x) / cell_), 0, grid_w_ - 1);
        x1 = std::clamp(static_cast<int>((bx - grid_min_.x) / cell_), 0, grid_w_ - 1);
        z0 = std::clamp(static_cast<int>((az - grid_min_.y) / cell_), 0, grid_h_ - 1);
        z1 = std::clamp(static_cast<int>((bz - grid_min_.y) / cell_), 0, grid_h_ - 1);
    };
    std::vector<uint32_t> count(cells + 1, 0);
    for (const Tri& t : tris_) {
        int x0, z0, x1, z1;
        span(t, x0, z0, x1, z1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) ++count[static_cast<size_t>(z) * grid_w_ + x];
    }
    cell_start_.assign(cells + 1, 0);
    for (size_t c = 0; c < cells; ++c) cell_start_[c + 1] = cell_start_[c] + count[c];
    cell_tris_.assign(cell_start_[cells], -1);
    std::vector<uint32_t> fill(cell_start_.begin(), cell_start_.end() - 1);
    for (int i = 0; i < static_cast<int>(tris_.size()); ++i) {
        int x0, z0, x1, z1;
        span(tris_[i], x0, z0, x1, z1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) cell_tris_[fill[static_cast<size_t>(z) * grid_w_ + x]++] = i;
    }
}

bool NavMesh::contains(const glm::vec3& p) const {
    if (grid_w_ > 0) {
        const int cx = static_cast<int>(std::floor((p.x - grid_min_.x) / cell_));
        const int cz = static_cast<int>(std::floor((p.z - grid_min_.y) / cell_));
        if (cx < 0 || cz < 0 || cx >= grid_w_ || cz >= grid_h_) return false;
        const size_t c = static_cast<size_t>(cz) * grid_w_ + cx;
        for (uint32_t k = cell_start_[c]; k < cell_start_[c + 1]; ++k) {
            const Tri& t = tris_[static_cast<size_t>(cell_tris_[k])];
            if (point_in_tri_xz(p, t.v[0], t.v[1], t.v[2])) return true;
        }
        return false;
    }
    for (const Tri& t : tris_)
        if (point_in_tri_xz(p, t.v[0], t.v[1], t.v[2])) return true;
    return false;
}

int NavMesh::nearest_tri(const glm::vec3& p) const {
    if (tris_.empty()) return -1;
    if (grid_w_ > 0) {
        const int cx = static_cast<int>(std::floor((p.x - grid_min_.x) / cell_));
        const int cz = static_cast<int>(std::floor((p.z - grid_min_.y) / cell_));
        // Containing: of those stacked here, the nearest in height.
        if (cx >= 0 && cz >= 0 && cx < grid_w_ && cz < grid_h_) {
            const size_t c = static_cast<size_t>(cz) * grid_w_ + cx;
            int best = -1;
            float bd = std::numeric_limits<float>::max();
            for (uint32_t k = cell_start_[c]; k < cell_start_[c + 1]; ++k) {
                const int t = cell_tris_[k];
                const Tri& tr = tris_[static_cast<size_t>(t)];
                if (!point_in_tri_xz(p, tr.v[0], tr.v[1], tr.v[2])) continue;
                const float d = std::fabs(height_on(t, p) - p.y);
                if (d < bd) { bd = d; best = t; }
            }
            if (best >= 0) return best;
        }
        // Nearest centroid: rings of cells outward, until no nearer cell is left.
        const int ccx = std::clamp(cx, 0, grid_w_ - 1), ccz = std::clamp(cz, 0, grid_h_ - 1);
        const float outside = std::max({grid_min_.x - p.x, p.x - (grid_min_.x + grid_w_ * cell_),
                                         grid_min_.y - p.z, p.z - (grid_min_.y + grid_h_ * cell_), 0.0f});
        int best = -1;
        float bd = std::numeric_limits<float>::max();
        const int rmax = std::max(grid_w_, grid_h_);
        for (int r = 0; r <= rmax; ++r) {
            if (best >= 0 && (static_cast<float>(r) - 1.0f) * cell_ + outside > bd) break;
            for (int z = ccz - r; z <= ccz + r; ++z) {
                if (z < 0 || z >= grid_h_) continue;
                const bool edge_row = z == ccz - r || z == ccz + r;
                for (int x = ccx - r; x <= ccx + r; x += edge_row ? 1 : 2 * std::max(r, 1)) {
                    if (x < 0 || x >= grid_w_) continue;
                    const size_t c = static_cast<size_t>(z) * grid_w_ + x;
                    for (uint32_t k = cell_start_[c]; k < cell_start_[c + 1]; ++k) {
                        const int t = cell_tris_[k];
                        const float d = glm::length(tris_[static_cast<size_t>(t)].centroid - p);
                        if (d < bd) { bd = d; best = t; }
                    }
                }
            }
        }
        if (best >= 0) return best;
    }
    // Prefer a triangle whose XZ footprint contains p.
    for (int t = 0; t < static_cast<int>(tris_.size()); ++t)
        if (point_in_tri_xz(p, tris_[t].v[0], tris_[t].v[1], tris_[t].v[2])) return t;
    // Fallback: nearest centroid (XZ + Y).
    int best = -1; float bd = std::numeric_limits<float>::max();
    for (int t = 0; t < static_cast<int>(tris_.size()); ++t) {
        const float d = glm::length(tris_[t].centroid - p);
        if (d < bd) { bd = d; best = t; }
    }
    return best;
}

float NavMesh::height_on(int tri, const glm::vec3& p) const {
    if (tri < 0 || tri >= static_cast<int>(tris_.size())) return p.y;
    const Tri& t = tris_[tri];
    // Barycentric in XZ → interpolate Y.
    const float d = area2(t.v[0], t.v[1], t.v[2]);
    if (std::abs(d) < 1e-8f) return t.centroid.y;
    const float a = area2(p, t.v[1], t.v[2]) / d;
    const float b = area2(t.v[0], p, t.v[2]) / d;
    const float c = 1.0f - a - b;
    return a * t.v[0].y + b * t.v[1].y + c * t.v[2].y;
}

bool NavMesh::astar(int s, int e, const glm::vec3& start, const glm::vec3& end,
                    std::vector<int>& out) const {
    out.clear();
    if (s < 0 || e < 0) return false;
    if (s == e) { out.push_back(s); return true; }

    const int N = static_cast<int>(tris_.size());
    std::vector<float> g(N, std::numeric_limits<float>::max());
    std::vector<int>   came(N, -1);
    std::vector<uint8_t> closed(N, 0);

    struct Node { float f; int tri; };
    struct Cmp { bool operator()(const Node& a, const Node& b) const { return a.f > b.f; } };
    std::priority_queue<Node, std::vector<Node>, Cmp> open;

    g[s] = 0.0f;
    open.push({dist_xz(start, end), s});
    while (!open.empty()) {
        const int cur = open.top().tri; open.pop();
        if (cur == e) break;
        if (closed[cur]) continue;
        closed[cur] = 1;
        for (int k = 0; k < 3; ++k) {
            const int nb = tris_[cur].neighbor[k];
            if (nb < 0 || closed[nb]) continue;
            const float step = dist_xz(tris_[cur].centroid, tris_[nb].centroid);
            const float ng = g[cur] + step;
            if (ng < g[nb]) {
                g[nb] = ng; came[nb] = cur;
                open.push({ng + dist_xz(tris_[nb].centroid, end), nb});
            }
        }
    }
    if (came[e] < 0 && s != e) return false;

    // Reconstruct.
    std::vector<int> rev;
    for (int c = e; c != -1; c = came[c]) { rev.push_back(c); if (c == s) break; }
    if (rev.empty() || rev.back() != s) return false;
    out.assign(rev.rbegin(), rev.rend());
    return true;
}

void NavMesh::funnel(const std::vector<int>& polys, const glm::vec3& start,
                     const glm::vec3& end, std::vector<glm::vec3>& out) const {
    out.clear();
    // Build the portal list: portal 0 = (start,start); one per adjacent poly
    // pair (left,right from the departing triangle's shared edge, CCW winding);
    // last = (end,end).
    std::vector<glm::vec3> left, right;
    left.push_back(start); right.push_back(start);
    for (size_t i = 0; i + 1 < polys.size(); ++i) {
        const Tri& t = tris_[polys[i]];
        int shared = -1;
        for (int e = 0; e < 3; ++e) if (t.neighbor[e] == polys[i + 1]) { shared = e; break; }
        if (shared < 0) continue;  // shouldn't happen for an A* path
        // CCW-from-above: crossing edge e, v[e] is on the right, v[e+1] the left.
        right.push_back(t.v[shared]);
        left.push_back(t.v[(shared + 1) % 3]);
    }
    left.push_back(end); right.push_back(end);

    // Simple Stupid Funnel Algorithm (Mononen), XZ plane.
    std::vector<glm::vec3> pts;
    glm::vec3 apex = start, portalL = start, portalR = start;
    size_t apexIdx = 0, leftIdx = 0, rightIdx = 0;
    pts.push_back(apex);
    for (size_t i = 1; i < left.size(); ++i) {
        const glm::vec3 l = left[i], r = right[i];
        // Right side.
        if (area2(apex, portalR, r) <= 0.0f) {
            if (apex == portalR || area2(apex, portalL, r) > 0.0f) {
                portalR = r; rightIdx = i;
            } else {
                pts.push_back(portalL);
                apex = portalL; apexIdx = leftIdx;
                portalL = apex; portalR = apex; leftIdx = apexIdx; rightIdx = apexIdx;
                i = apexIdx; continue;
            }
        }
        // Left side.
        if (area2(apex, portalL, l) >= 0.0f) {
            if (apex == portalL || area2(apex, portalR, l) < 0.0f) {
                portalL = l; leftIdx = i;
            } else {
                pts.push_back(portalR);
                apex = portalR; apexIdx = rightIdx;
                portalL = apex; portalR = apex; leftIdx = apexIdx; rightIdx = apexIdx;
                i = apexIdx; continue;
            }
        }
    }
    pts.push_back(end);

    // Drop redundant collinear corners (the grid triangulation makes the funnel
    // pin the apex to several near-collinear portal vertices along a straight
    // run). Keep a point only if it turns the path meaningfully.
    if (pts.size() > 2) {
        std::vector<glm::vec3> simp;
        simp.reserve(pts.size());
        simp.push_back(pts.front());
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            const glm::vec3& a = simp.back();
            const glm::vec3& b = pts[i];
            const glm::vec3& c = pts[i + 1];
            // Perpendicular distance of b from segment a->c (XZ); keep if it
            // bends more than ~2 cm.
            const float len = dist_xz(a, c);
            const float dev = len > 1e-4f ? std::abs(area2(a, c, b)) / len : dist_xz(a, b);
            if (dev > 0.02f) simp.push_back(b);
        }
        simp.push_back(pts.back());
        pts.swap(simp);
    }

    // Set Y for each corner by sampling the mesh at its XZ.
    for (glm::vec3& p : pts) {
        const int t = nearest_tri(p);
        p.y = (t >= 0) ? height_on(t, p) : p.y;
    }
    out = std::move(pts);
}

bool NavMesh::find_path(const glm::vec3& start, const glm::vec3& end,
                        std::vector<glm::vec3>& out_path) const {
    out_path.clear();
    if (tris_.empty()) return false;
    const int s = nearest_tri(start);
    const int e = nearest_tri(end);
    if (s < 0 || e < 0) return false;

    std::vector<int> polys;
    if (!astar(s, e, start, end, polys)) return false;

    if (polys.size() < 2) {   // same triangle → straight segment
        out_path = {start, end};
        return true;
    }
    funnel(polys, start, end, out_path);
    if (out_path.size() < 2) out_path = {start, end};
    return true;
}

} // namespace schizo::ai
