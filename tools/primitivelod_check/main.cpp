// ============================================================================
// primitivelod_check — the curved primitives' LOD tiers may only switch in
// where the switch is invisible, and the error formula that decides "where"
// must actually bound the geometry.
//
// Spheres were 1,024 triangles at any distance -- at 20k entities, 5.1M of the
// frame's 5.4M triangles. Each curved primitive now has lower-resolution tiers
// from its own generator, switched in where the tier's deviation from LOD 0
// projects to under half a pixel (primitive_lod in primitive_meshes.h). That
// "where" is only as good as the error formula, and the first version of it
// was wrong: it took the larger of the two directions' sagittas, while a UV
// quad's deepest point bulges by roughly their SUM -- tiers would have switched
// in at about half the safe distance. So this measures the real maximum
// deviation of every tier the cache builds, by sampling its triangles, and
// asserts the formula bounds it.
//
// "Invisible" also has to hold for ray tracing, which sees LOD 0 only: the
// BLAS is built from it, while shadow and AO rays start at whatever tier was
// rasterized. An inscribed coarse tier starts those rays inside the BLAS and
// the object shadows itself. So the lower tiers enclose the surface, and this
// traces the lighting pass's own rays (same offsets) from every tier against
// LOD 0 and asserts none of them hits its own object.
// ============================================================================
#include "primitive_meshes.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using gws::renderer::gpu::Mesh;
using gws::renderer::gpu::SceneVertex;
using namespace schizo::editor;
using primitive_lod::Tier;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// Signed distance to each ideal solid (negative inside), and the point or axis
// each one's curved surface is built around.
using Sdf  = float (*)(const glm::vec3&);
using Core = glm::vec3 (*)(const glm::vec3&);

float sphere_sdf(const glm::vec3& p) { return glm::length(p) - 0.5f; }
glm::vec3 sphere_core(const glm::vec3&) { return glm::vec3(0.0f); }

// Radius 0.5, height 1, capped.
float cylinder_sdf(const glm::vec3& p) {
    const glm::vec2 d(glm::length(glm::vec2(p.x, p.z)) - 0.5f, std::fabs(p.y) - 0.5f);
    return glm::length(glm::max(d, glm::vec2(0.0f))) + std::min(std::max(d.x, d.y), 0.0f);
}
glm::vec3 cylinder_core(const glm::vec3& p) { return glm::vec3(0.0f, p.y, 0.0f); }

// Radius 0.5, cylinder half-height 0.5.
glm::vec3 capsule_core(const glm::vec3& p) { return glm::vec3(0.0f, std::clamp(p.y, -0.5f, 0.5f), 0.0f); }
float capsule_sdf(const glm::vec3& p) { return glm::length(p - capsule_core(p)) - 0.5f; }

// Smallest and largest signed distance over points sampled on every triangle
// (vertices, edges and interior on a barycentric grid).
std::pair<float, float> sdf_range(const Tier& t, Sdf sdf) {
    float lo = 1e30f, hi = -1e30f;
    constexpr int N = 24;
    for (size_t k = 0; k + 2 < t.i.size(); k += 3) {
        const glm::vec3 a = t.v[t.i[k]].position, b = t.v[t.i[k + 1]].position, c = t.v[t.i[k + 2]].position;
        for (int i = 0; i <= N; ++i)
            for (int j = 0; j <= N - i; ++j) {
                const float u = float(i) / N, w = float(j) / N;
                const float d = sdf(a * (1.0f - u - w) + b * u + c * w);
                lo = std::min(lo, d); hi = std::max(hi, d);
            }
    }
    return {lo, hi};
}

// ---- the lighting pass's rays, on the CPU ----------------------------------
//
// Mirrors tools/lighting_pass.frag (pinned below): a shadow ray starts at
// worldPos + N * shadowRayEpsilon(...) and an AO ray at worldPos + N * 0.02,
// where N is the G-buffer's interpolated vertex normal; both use tMin 0.001
// and no face culling. The object sits at the origin, where the epsilon's
// distance term is smallest -- the worst case.

float shadow_ray_epsilon(const glm::vec3& world_pos, const glm::vec3& n, const glm::vec3& l) {
    const float ndotl = std::max(glm::dot(n, l), 0.0f);
    const float slope = std::sqrt(std::max(1.0f - ndotl * ndotl, 0.0f)) / std::max(ndotl, 0.05f);
    const float scale = std::max(1.0f, glm::length(world_pos) * 1e-3f);
    return std::clamp(0.02f * (1.0f + slope) * scale, 0.02f, 2.0f);
}

// Moller-Trumbore, both faces.
bool hits_triangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b,
                   const glm::vec3& c) {
    const glm::vec3 e1 = b - a, e2 = c - a, p = glm::cross(d, e2);
    const float det = glm::dot(e1, p);
    if (std::fabs(det) < 1e-12f) return false;
    const float inv = 1.0f / det;
    const glm::vec3 s = o - a;
    const float u = glm::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = glm::dot(e2, q) * inv;
    return t > 0.001f && t < 1000.0f;
}

struct Blas {
    std::vector<glm::vec3> tri;   // 3 per triangle, already scaled
    float bound = 0.0f;           // radius of a sphere at the origin holding every vertex
    bool hit(const glm::vec3& o, const glm::vec3& d) const {
        // Exact early-out: a ray that misses the bounding sphere misses every triangle.
        const float b = glm::dot(o, d), c = glm::dot(o, o) - bound * bound;
        if (c > 0.0f && (b > 0.0f || b * b < c)) return false;
        for (size_t k = 0; k < tri.size(); k += 3)
            if (hits_triangle(o, d, tri[k], tri[k + 1], tri[k + 2])) return true;
        return false;
    }
};

Blas make_blas(const Tier& lod0, float s) {
    Blas b;
    for (uint32_t x : lod0.i) {
        b.tri.push_back(lod0.v[x].position * s);
        b.bound = std::max(b.bound, glm::length(lod0.v[x].position * s) * 1.0001f);
    }
    return b;
}

std::vector<glm::vec3> sphere_dirs(int n) {   // golden spiral
    std::vector<glm::vec3> d;
    for (int i = 0; i < n; ++i) {
        const float y = 1.0f - 2.0f * (i + 0.5f) / n, r = std::sqrt(1.0f - y * y);
        const float a = 2.39996323f * i;
        d.emplace_back(r * std::cos(a), y, r * std::sin(a));
    }
    return d;
}

struct SelfHits { int shadow = 0, ao = 0, rays = 0; };

// Rays traced from `raster` (scaled by s) that hit `blas`: shadow rays toward
// every light that visibly lights the point (N.L >= 0.1), and -- with `ao` --
// AO rays over the normal's hemisphere down to 1 degree above grazing.
SelfHits self_hits(const Tier& raster, const Blas& blas, float s, bool ao, bool centroid_only) {
    static const std::vector<glm::vec3> lights = sphere_dirs(32);
    SelfHits h;
    for (size_t k = 0; k + 2 < raster.i.size(); k += 3) {
        const SceneVertex* v[3] = {&raster.v[raster.i[k]], &raster.v[raster.i[k + 1]], &raster.v[raster.i[k + 2]]};
        const glm::vec3 bary[4] = {{1 / 3.f, 1 / 3.f, 1 / 3.f}, {2 / 3.f, 1 / 6.f, 1 / 6.f},
                                   {1 / 6.f, 2 / 3.f, 1 / 6.f}, {1 / 6.f, 1 / 6.f, 2 / 3.f}};
        for (int sidx = 0; sidx < (centroid_only ? 1 : 4); ++sidx) {
            const glm::vec3 w = bary[sidx];
            const glm::vec3 P = (v[0]->position * w.x + v[1]->position * w.y + v[2]->position * w.z) * s;
            const glm::vec3 N = glm::normalize(v[0]->normal * w.x + v[1]->normal * w.y + v[2]->normal * w.z);
            for (const glm::vec3& L : lights) {
                if (glm::dot(N, L) < 0.1f) continue;
                ++h.rays;
                if (blas.hit(P + N * shadow_ray_epsilon(P, N, L), L)) ++h.shadow;
            }
            if (!ao) continue;
            const glm::vec3 up = std::fabs(N.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 T = glm::normalize(glm::cross(up, N)), B = glm::cross(N, T);
            for (float polar : {0.0f, 0.8f, 1.4f, 1.5533f})          // up to 89 degrees
                for (int a = 0; a < 6; ++a) {
                    const float az = a * 1.0471976f;
                    const glm::vec3 d = glm::normalize(std::sin(polar) * (std::cos(az) * T + std::sin(az) * B) +
                                                       std::cos(polar) * N);
                    ++h.rays;
                    if (blas.hit(P + N * 0.02f, d)) ++h.ao;
                }
        }
    }
    return h;
}

struct Primitive {
    const char* name;
    std::vector<Tier> tiers;      // as the cache builds them
    std::vector<Tier> lod0_only;  // as GWS_NO_PRIMITIVE_LOD builds them
    size_t default_index_count;   // the generator's long-standing default mesh
    Sdf sdf;
    Core core;
};

template <typename Gen>
size_t default_indices(Gen gen) {
    std::vector<SceneVertex> v; std::vector<uint32_t> i;
    gen(v, i);
    return i.size();
}

fs::path find_repo_root(const fs::path& exe_dir) {
    std::error_code ec;
    for (fs::path d = exe_dir; !d.empty(); d = d.parent_path()) {
        if (fs::exists(d / "tools" / "lighting_pass.frag", ec)) return d;
        if (d == d.parent_path()) break;
    }
    return {};
}

}  // namespace

int main(int, char** argv) {
    std::printf("=== primitivelod_check ===\n\n");
    using namespace primitive_lod;

    std::vector<Primitive> prims;
    prims.push_back({"sphere", sphere_tiers(true), sphere_tiers(false),
                     default_indices([](auto& v, auto& i) { gen_sphere(v, i); }), sphere_sdf, sphere_core});
    prims.push_back({"cylinder", cylinder_tiers(true), cylinder_tiers(false),
                     default_indices([](auto& v, auto& i) { gen_cylinder(v, i); }), cylinder_sdf, cylinder_core});
    prims.push_back({"capsule", capsule_tiers(true), capsule_tiers(false),
                     default_indices([](auto& v, auto& i) { gen_capsule(v, i); }), capsule_sdf, capsule_core});

    for (const Primitive& p : prims) {
        std::printf("%s (%zu tiers):\n", p.name, p.tiers.size());
        char buf[220];

        // LOD 0 is the mesh primitives always had: the BLAS is built from it,
        // and GWS_NO_PRIMITIVE_LOD must give exactly the old look back.
        check(p.tiers.size() >= 2, std::string(p.name) + ": has lower tiers at all");
        check(p.tiers[0].i.size() == p.default_index_count,
              std::string(p.name) + ": LOD 0 is the generator's default mesh");
        check(p.lod0_only.size() == 1 && p.lod0_only[0].i.size() == p.tiers[0].i.size(),
              std::string(p.name) + ": GWS_NO_PRIMITIVE_LOD builds LOD 0 alone");

        std::vector<float> measured;
        for (size_t t = 0; t < p.tiers.size(); ++t) {
            const Tier& tier = p.tiers[t];
            const auto [lo, hi] = sdf_range(tier, p.sdf);
            const float m = std::max(-lo, hi);
            measured.push_back(m);
            std::snprintf(buf, sizeof buf, "%s tier %zu (%zu tris): measured %.5f <= formula %.5f",
                          p.name, t, tier.i.size() / 3, m, tier.err);
            check(m <= tier.err * 1.001f, buf);
            // Not wildly loose either: a formula far above the geometry keeps
            // tiers from switching in, silently throwing the win away.
            std::snprintf(buf, sizeof buf, "%s tier %zu: formula within 1.5x of measured", p.name, t);
            check(tier.err <= m * 1.5f, buf);
            if (t == 0) {
                std::snprintf(buf, sizeof buf, "%s LOD 0: inscribed (surface %.6f .. %.6f, never outside)",
                              p.name, lo, hi);
                check(hi <= 1e-5f, buf);
            } else {
                std::snprintf(buf, sizeof buf, "%s tier %zu: ENCLOSES the surface (%.6f .. %.6f, never inside)",
                              p.name, t, lo, hi);
                check(lo >= -1e-5f, buf);
                std::snprintf(buf, sizeof buf, "%s tier %zu: fewer triangles than tier %zu", p.name, t, t - 1);
                check(tier.i.size() < p.tiers[t - 1].i.size(), buf);
            }
            // Normals point straight out from the core (cap normals excepted),
            // which is what keeps an enclosing tier's rays away from LOD 0.
            bool radial = true;
            for (const auto& x : tier.v) {
                const glm::vec3 out = x.position - p.core(x.position);
                if (glm::length(out) < 1e-6f) continue;                       // cap centre
                if (std::fabs(x.normal.y) > 0.999f && std::fabs(std::fabs(x.position.y) - 0.5f) < 1e-5f &&
                    p.sdf == cylinder_sdf) continue;                           // flat cap
                radial = radial && glm::dot(glm::normalize(out), x.normal) > 1.0f - 1e-4f;
            }
            std::snprintf(buf, sizeof buf, "%s tier %zu: normals radial", p.name, t);
            check(radial, buf);
        }

        const std::vector<float> d = switch_distances(p.tiers);
        bool ascending = d[0] == 0.0f;
        for (size_t t = 1; t < d.size(); ++t) ascending = ascending && d[t] > d[t - 1];
        check(ascending, std::string(p.name) + ": switch distances start at 0 and ascend");
        for (size_t t = 1; t < d.size(); ++t) {
            // At the switch distance, the worst deviation between this tier and
            // LOD 0 -- at most both measured deviations from the ideal surface
            // -- projects to within the budget.
            const float px = (measured[t] + measured[0]) * kPixelsPerUnitAt1m / d[t];
            std::snprintf(buf, sizeof buf, "%s tier %zu at %.1f sizes: %.3f px <= %.2f px",
                          p.name, t, d[t], px, kMaxErrorPx);
            check(px <= kMaxErrorPx * 1.001f, buf);
        }

        // Ray tracing sees LOD 0 whatever was rasterized. LOD 0 against itself
        // is the baseline the shader's offsets were tuned for.
        for (float s : {1.0f, 4.0f, 16.0f}) {
            const Blas blas = make_blas(p.tiers[0], s);
            for (size_t t = 0; t < p.tiers.size(); ++t) {
                if (t == 0 && s == 4.0f) continue;
                const SelfHits h = self_hits(p.tiers[t], blas, s, t > 0, t == 0);
                std::snprintf(buf, sizeof buf,
                              "%s tier %zu at scale %g: no ray hits its own object (%d rays; shadow %d, AO %d)",
                              p.name, t, s, h.rays, h.shadow, h.ao);
                check(h.shadow == 0 && h.ao == 0, buf);
            }
        }
        std::printf("\n");
    }

    std::printf("The bugs this guards against are real:\n");
    {
        Tier inscribed; gen_sphere(inscribed.v, inscribed.i, 8, 16);
        const auto [lo, hi] = sdf_range(inscribed, sphere_sdf);
        const float larger_sagitta = std::max(chord_error(0.5f, 16), chord_error(0.5f, 16));
        char buf[220];
        std::snprintf(buf, sizeof buf, "larger-sagitta formula %.5f < real bulge %.5f of an 8x16 sphere "
                      "(would switch in too early)", larger_sagitta, std::max(-lo, hi));
        check(larger_sagitta < std::max(-lo, hi), buf);

        // The same coarse tiers left inscribed shadow themselves.
        Tier coarse; gen_sphere(coarse.v, coarse.i, 4, 8);
        const SelfHits h = self_hits(coarse, make_blas(prims[0].tiers[0], 1.0f), 1.0f, false, false);
        std::snprintf(buf, sizeof buf, "an INSCRIBED 4x8 sphere tier self-shadows: %d of %d shadow rays hit "
                      "its own LOD 0", h.shadow, h.rays);
        check(h.shadow > 0, buf);
    }

    std::printf("\nThe shader lines the ray model mirrors:\n");
    {
        const fs::path root = find_repo_root(fs::absolute(fs::path(argv[0])).parent_path());
        if (root.empty()) {
            std::printf("  SKIP needs a source checkout (tools/lighting_pass.frag)\n");
        } else {
            std::ifstream f(root / "tools" / "lighting_pass.frag");
            std::stringstream ss; ss << f.rdbuf();
            const std::string src = ss.str();
            for (const char* line : {
                     "float slope = sqrt(max(1.0 - ndotl * ndotl, 0.0)) / max(ndotl, 0.05);",
                     "float scale = max(1.0, length(worldPos) * 1e-3);",
                     "return clamp(0.02 * (1.0 + slope) * scale, 0.02, 2.0);",
                     "vec3 origin = worldPos + normal * shadowRayEpsilon(worldPos, normal, ray_dir);",
                     "vec3  origin = worldPos + N * 0.02;",
                     "0xFF, origin, 0.001, ray_dir, t_max);"}) {
                check(src.find(line) != std::string::npos,
                      std::string("lighting_pass.frag still has: ") + line);
            }
        }
    }

    std::printf("\nError-bounded thresholds are in units of the mesh's own size:\n");
    {
        Mesh m;
        const glm::mat4 x10 = glm::scale(glm::mat4(1.0f), glm::vec3(10.0f));
        const glm::mat4 tall = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 4.0f, 2.0f));
        check(m.lod_distance(100.0f, x10) == 100.0f, "off by default: world units (glTF)");
        check(!m.lod_error_bounded(), "off by default: the shadow pass may go a tier coarser");
        m.set_lod_error_bounded(true);
        check(std::fabs(m.lod_distance(100.0f, x10) - 10.0f) < 1e-4f,
              "error-bounded: a sphere scaled x10 is 'as near' as one at a tenth the distance");
        check(std::fabs(m.lod_distance(100.0f, tall) - 25.0f) < 1e-4f,
              "non-uniform scale: the LARGEST axis decides (detail kept where it shows)");
        check(m.lod_distance(100.0f, glm::mat4(0.0f)) == 100.0f, "zero scale: distance passes through");
        Mesh moved(std::move(m));
        check(std::fabs(moved.lod_distance(100.0f, x10) - 10.0f) < 1e-4f && moved.lod_error_bounded(),
              "moving a mesh keeps it error-bounded (the shadow pass keeps its tier)");
        Mesh assigned;
        assigned = std::move(moved);
        check(std::fabs(assigned.lod_distance(100.0f, x10) - 10.0f) < 1e-4f && assigned.lod_error_bounded(),
              "move-assigning a mesh keeps it error-bounded");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail)
        std::printf("FAIL primitivelod_check — a primitive LOD tier can switch in visibly.\n");
    return g_fail ? 1 : 0;
}
