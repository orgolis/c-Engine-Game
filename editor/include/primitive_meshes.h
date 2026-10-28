#pragma once

#include "vulkan/vulkan_scene_mesh.h"
#include "mesh_renderer_component.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace schizo::editor {

inline void gen_cube(std::vector<gws::renderer::gpu::SceneVertex>& verts,
                     std::vector<uint32_t>& idx) {
    using V = gws::renderer::gpu::SceneVertex;
    auto add_face = [&](glm::vec3 o, glm::vec3 u, glm::vec3 v, glm::vec3 n) {
        uint32_t b = (uint32_t)verts.size();
        glm::vec4 t{u.x, u.y, u.z, 1.0f};
        verts.push_back({o,         n, {0,0}, t});
        verts.push_back({o + u,     n, {1,0}, t});
        verts.push_back({o + u + v, n, {1,1}, t});
        verts.push_back({o + v,     n, {0,1}, t});
        // CCW from outside (cross(E1,E2) = declared face normal).
        idx.insert(idx.end(), {b, b+2, b+1, b, b+3, b+2});
    };
    float h = 0.5f;
    // Centred at origin, unit size. Local space is scaled by entity transform.
    add_face({-h,-h,-h},{1,0,0},{0,1,0},{0,0,-1}); // front (-Z)
    add_face({ h,-h, h},{-1,0,0},{0,1,0},{0,0,1}); // back  (+Z)
    add_face({-h,-h, h},{1,0,0},{0,0,-1},{0,-1,0});// bottom(-Y)
    add_face({-h, h,-h},{1,0,0},{0,0,1},{0,1,0});  // top   (+Y)
    // Left (-X): u/v swapped vs the other 5 faces so its (u, v, n) basis
    // is left-handed (u × v = -n). Without this swap, the universal CCW
    // index pattern in add_face would produce a CW-from-outside triangle
    // and back-face culling would hide this face.
    add_face({-h,-h,-h},{0,1,0},{0,0,1},{-1,0,0}); // left  (-X)
    add_face({ h,-h,-h},{0,0,1},{0,1,0},{1,0,0});  // right (+X)
}

inline void gen_plane(std::vector<gws::renderer::gpu::SceneVertex>& verts,
                      std::vector<uint32_t>& idx) {
    using V = gws::renderer::gpu::SceneVertex;
    glm::vec3 n{0,1,0};
    glm::vec4 t{1,0,0,1};
    // Unit plane on Y=0; entity scale handles size.
    verts.push_back({{-0.5f, 0, -0.5f}, n, {0,0}, t});
    verts.push_back({{ 0.5f, 0, -0.5f}, n, {1,0}, t});
    verts.push_back({{ 0.5f, 0,  0.5f}, n, {1,1}, t});
    verts.push_back({{-0.5f, 0,  0.5f}, n, {0,1}, t});
    // CCW from above (+Y normal).
    idx = {0,2,1, 0,3,2};
}

inline void gen_sphere(std::vector<gws::renderer::gpu::SceneVertex>& verts,
                       std::vector<uint32_t>& idx,
                       int rings = 16, int sectors = 32) {
    using V = gws::renderer::gpu::SceneVertex;
    const float pi = 3.14159265358979323846f;
    const float r  = 0.5f;
    for (int y = 0; y <= rings; ++y) {
        float v = (float)y / (float)rings;
        float phi = v * pi;
        for (int x = 0; x <= sectors; ++x) {
            float u = (float)x / (float)sectors;
            float theta = u * 2.0f * pi;
            glm::vec3 n{ std::sin(phi) * std::cos(theta),
                         std::cos(phi),
                         std::sin(phi) * std::sin(theta) };
            glm::vec3 p = n * r;
            glm::vec3 tan{ -std::sin(theta), 0.0f, std::cos(theta) };
            verts.push_back({p, n, {u, v}, glm::vec4(tan, 1.0f)});
        }
    }
    int cols = sectors + 1;
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < sectors; ++x) {
            uint32_t a = y * cols + x;
            uint32_t b = a + 1;
            uint32_t c = a + cols;
            uint32_t d = c + 1;
            // CCW from outside (radial outward normal).
            idx.insert(idx.end(), {a, b, c,  b, d, c});
        }
    }
}

inline void gen_cylinder(std::vector<gws::renderer::gpu::SceneVertex>& verts,
                         std::vector<uint32_t>& idx,
                         int segments = 24) {
    using V = gws::renderer::gpu::SceneVertex;
    const float pi = 3.14159265358979323846f;
    const float r  = 0.5f;
    const float h  = 0.5f;  // half-height; total height = 1.0
    // Side wall (two rings of verts)
    uint32_t base = (uint32_t)verts.size();
    for (int i = 0; i <= segments; ++i) {
        float a = (float)i / (float)segments * 2.0f * pi;
        glm::vec3 n{ std::cos(a), 0.0f, std::sin(a) };
        glm::vec3 tan{ -std::sin(a), 0.0f, std::cos(a) };
        glm::vec4 t4(tan, 1.0f);
        verts.push_back({{n.x*r, -h, n.z*r}, n, {(float)i/segments, 0}, t4});
        verts.push_back({{n.x*r,  h, n.z*r}, n, {(float)i/segments, 1}, t4});
    }
    for (int i = 0; i < segments; ++i) {
        uint32_t a = base + i*2;
        uint32_t b = a + 1;
        uint32_t c = a + 2;
        uint32_t d = a + 3;
        idx.insert(idx.end(), {a, b, c,  b, d, c});
    }
    // Top cap (fan)
    uint32_t top_center = (uint32_t)verts.size();
    verts.push_back({{0, h, 0}, {0,1,0}, {0.5f, 0.5f}, {1,0,0,1}});
    uint32_t top_start = (uint32_t)verts.size();
    for (int i = 0; i <= segments; ++i) {
        float a = (float)i / (float)segments * 2.0f * pi;
        glm::vec3 p{ std::cos(a)*r, h, std::sin(a)*r };
        verts.push_back({p, {0,1,0}, {(std::cos(a)+1)*0.5f, (std::sin(a)+1)*0.5f}, {1,0,0,1}});
    }
    // Top cap CCW from above (+Y normal).
    for (int i = 0; i < segments; ++i)
        idx.insert(idx.end(), {top_center, top_start + i + 1, top_start + i});
    // Bottom cap (fan)
    uint32_t bot_center = (uint32_t)verts.size();
    verts.push_back({{0, -h, 0}, {0,-1,0}, {0.5f, 0.5f}, {1,0,0,1}});
    uint32_t bot_start = (uint32_t)verts.size();
    for (int i = 0; i <= segments; ++i) {
        float a = (float)i / (float)segments * 2.0f * pi;
        glm::vec3 p{ std::cos(a)*r, -h, std::sin(a)*r };
        verts.push_back({p, {0,-1,0}, {(std::cos(a)+1)*0.5f, (std::sin(a)+1)*0.5f}, {1,0,0,1}});
    }
    // Bottom cap CCW from below (-Y normal).
    for (int i = 0; i < segments; ++i)
        idx.insert(idx.end(), {bot_center, bot_start + i, bot_start + i + 1});
}

inline void gen_capsule(std::vector<gws::renderer::gpu::SceneVertex>& verts,
                        std::vector<uint32_t>& idx,
                        int segments = 24, int rings = 8) {
    using V = gws::renderer::gpu::SceneVertex;
    const float pi = 3.14159265358979323846f;
    const float r  = 0.5f;
    const float h  = 0.5f;  // half-height of cylinder portion
    // Top hemisphere
    uint32_t top_base = (uint32_t)verts.size();
    for (int y = 0; y <= rings; ++y) {
        float v = (float)y / (float)rings;
        float phi = v * 0.5f * pi;  // 0..pi/2
        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / (float)segments;
            float theta = u * 2.0f * pi;
            glm::vec3 n{ std::sin(phi) * std::cos(theta),
                         std::cos(phi),
                         std::sin(phi) * std::sin(theta) };
            glm::vec3 p = n * r + glm::vec3(0, h, 0);
            glm::vec3 tan{ -std::sin(theta), 0.0f, std::cos(theta) };
            verts.push_back({p, n, {u, v}, glm::vec4(tan, 1.0f)});
        }
    }
    int cols = segments + 1;
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t a = top_base + y * cols + x;
            uint32_t b = a + 1;
            uint32_t c = a + cols;
            uint32_t d = c + 1;
            // CCW from outside (radial outward normal).
            idx.insert(idx.end(), {a, b, c,  b, d, c});
        }
    }
    // Cylinder body (two rings of verts). NOTE: vertex layout differs from
    // gen_cylinder — here `a` is the TOP ring vertex (+h) and `b` is the
    // BOTTOM ring (-h), so the CCW order differs accordingly.
    uint32_t cyl_base = (uint32_t)verts.size();
    for (int i = 0; i <= segments; ++i) {
        float a = (float)i / (float)segments * 2.0f * pi;
        glm::vec3 n{ std::cos(a), 0.0f, std::sin(a) };
        glm::vec3 tan{ -std::sin(a), 0.0f, std::cos(a) };
        glm::vec4 t4(tan, 1.0f);
        verts.push_back({{n.x*r,  h, n.z*r}, n, {(float)i/segments, 0}, t4});
        verts.push_back({{n.x*r, -h, n.z*r}, n, {(float)i/segments, 1}, t4});
    }
    for (int i = 0; i < segments; ++i) {
        uint32_t a = cyl_base + i*2;
        uint32_t b = a + 1;
        uint32_t c = a + 2;
        uint32_t d = a + 3;
        // CCW from outside (radial outward normal).
        idx.insert(idx.end(), {a, c, b,  b, c, d});
    }
    // Bottom hemisphere
    uint32_t bot_base = (uint32_t)verts.size();
    for (int y = 0; y <= rings; ++y) {
        float v = (float)y / (float)rings;
        float phi = 0.5f * pi + v * 0.5f * pi;  // pi/2..pi
        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / (float)segments;
            float theta = u * 2.0f * pi;
            glm::vec3 n{ std::sin(phi) * std::cos(theta),
                         std::cos(phi),
                         std::sin(phi) * std::sin(theta) };
            glm::vec3 p = n * r + glm::vec3(0, -h, 0);
            glm::vec3 tan{ -std::sin(theta), 0.0f, std::cos(theta) };
            verts.push_back({p, n, {u, v}, glm::vec4(tan, 1.0f)});
        }
    }
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t a = bot_base + y * cols + x;
            uint32_t b = a + 1;
            uint32_t c = a + cols;
            uint32_t d = c + 1;
            // CCW from outside (radial outward normal).
            idx.insert(idx.end(), {a, b, c,  b, d, c});
        }
    }
}

inline void gen_pyramid(std::vector<gws::renderer::gpu::SceneVertex>& verts,
                        std::vector<uint32_t>& idx) {
    using V = gws::renderer::gpu::SceneVertex;
    const float h = 0.5f;
    glm::vec3 apex{0, h, 0};
    glm::vec3 c0{-h, -h, -h};
    glm::vec3 c1{ h, -h, -h};
    glm::vec3 c2{ h, -h,  h};
    glm::vec3 c3{-h, -h,  h};
    auto tri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        glm::vec4 t{1,0,0,1};
        uint32_t i0 = (uint32_t)verts.size();
        verts.push_back({a, n, {0.5f, 1}, t});
        verts.push_back({b, n, {0, 0}, t});
        verts.push_back({c, n, {1, 0}, t});
        idx.insert(idx.end(), {i0, i0+1, i0+2});
    };
    // Four side faces (apex + base edge). Argument order swapped so the
    // cross(b-a, c-a) normal points outward from each slant face.
    tri(apex, c1, c0);
    tri(apex, c2, c1);
    tri(apex, c3, c2);
    tri(apex, c0, c3);
    // Base (two triangles, downward normal). CCW from below.
    {
        glm::vec3 n{0, -1, 0};
        glm::vec4 t{1,0,0,1};
        uint32_t b = (uint32_t)verts.size();
        verts.push_back({c0, n, {0,0}, t});
        verts.push_back({c1, n, {1,0}, t});
        verts.push_back({c2, n, {1,1}, t});
        verts.push_back({c3, n, {0,1}, t});
        idx.insert(idx.end(), {b, b+1, b+2, b, b+2, b+3});
    }
}

// Where a lower-resolution tier of a curved primitive may replace LOD 0.
namespace primitive_lod {
// Screen pixels per world unit at 1 m for a conservative view: 1440 px tall at
// a 45-degree vertical FOV (1440 / (2 tan 22.5 deg) ~= 1738). A smaller screen
// or a wider FOV magnifies less, so the editor's defaults sit inside the bound.
inline constexpr float kPixelsPerUnitAt1m = 1738.0f;
// The largest on-screen deviation a tier switch may cause.
inline constexpr float kMaxErrorPx = 0.5f;

/// How far a radius-`r` circle bulges past its inscribed `segments`-gon: the
/// sagitta of one segment, r (1 - cos(pi / segments)).
inline float chord_error(float r, int segments) {
    return r * (1.0f - std::cos(3.14159265358979323846f / static_cast<float>(segments)));
}

/// How far a radius-`r` sphere bulges past a UV tessellation with `around`
/// segments around and `meridian` segments over a FULL circle pole to pole and
/// back (so 2 * rings for a sphere). The deepest point of each quad is near its
/// centre, where both curvatures add: r (1 - cos(pi/around) cos(pi/meridian)).
/// Taking only the larger of the two directions' sagittas -- the first version
/// of this -- underestimates it by up to 2x.
inline float uv_sphere_error(float r, int around, int meridian) {
    const float pi = 3.14159265358979323846f;
    return r * (1.0f - std::cos(pi / static_cast<float>(around)) *
                       std::cos(pi / static_cast<float>(meridian)));
}

/// The distance (in units of the mesh's own size) beyond which a surface
/// deviation of `err` projects to under kMaxErrorPx.
inline float switch_distance(float err) {
    return err * kPixelsPerUnitAt1m / kMaxErrorPx;
}

/// One LOD tier of a curved primitive: the SAME generator at lower resolution,
/// so every tier is exactly round with exact normals and UVs (decimating LOD 0
/// would only approximate that), plus `err`, the furthest its surface gets from
/// the ideal one.
///
/// LOD 0 is the generator's mesh as it always was: inscribed, vertices on the
/// surface. The lower tiers are ENCLOSING instead -- pushed out until their
/// flattest face just touches the surface. That is for ray tracing: the BLAS is
/// built from LOD 0 only, and a shadow or AO ray starts at the RASTERIZED
/// surface. An inscribed coarse tier puts that start up to 0.068 x size inside
/// the BLAS (the 64-triangle sphere's face centres), past the shadow ray's
/// 0.02-unit offset, so the ray would hit the sphere's own inner wall: a sunlit
/// sphere shadowing itself. An enclosing tier keeps every start point on or
/// outside the surface, LOD 0 is inside it, and a ray leaving along the normal
/// side only gets further away -- it cannot hit its own object at all.
struct Tier {
    std::vector<gws::renderer::gpu::SceneVertex> v;
    std::vector<uint32_t> i;
    float err = 0.0f;
};

/// The factor that pushes a UV tessellation out until it encloses its
/// radius-r surface: its flattest face sits at r cos(pi/around) cos(pi/meridian)
/// (meridian over a full circle; 0 for a straight side, which does not curve
/// that way). The tier's vertices then sit at r k, its furthest from the surface.
inline float enclosing_scale(int around, int meridian) {
    const float pi = 3.14159265358979323846f;
    float c = std::cos(pi / static_cast<float>(around));
    if (meridian > 0) c *= std::cos(pi / static_cast<float>(meridian));
    return 1.0f / c;
}

/// Scale `t` by `k` away from the primitive's core: its centre point (sphere),
/// axis (cylinder) or axis segment |y| <= half_axis (capsule; the cylinder's
/// half_axis is unbounded). Normals stay radial: the vertices all still lie at
/// one distance from the core, so interpolated normals point straight out.
inline void enclose(Tier& t, float k, float half_axis) {
    for (auto& vert : t.v) {
        const glm::vec3 core(0.0f, std::clamp(vert.position.y, -half_axis, half_axis), 0.0f);
        vert.position = core + (vert.position - core) * k;
    }
}

inline Tier sphere_tier(int rings, int sectors, bool lod0) {
    Tier t; gen_sphere(t.v, t.i, rings, sectors);
    if (lod0) { t.err = uv_sphere_error(0.5f, sectors, 2 * rings); return t; }
    const float k = enclosing_scale(sectors, 2 * rings);
    enclose(t, k, 0.0f);
    t.err = 0.5f * (k - 1.0f);
    return t;
}
inline Tier cylinder_tier(int segments, bool lod0) {
    Tier t; gen_cylinder(t.v, t.i, segments);
    // Only the side curves; the caps are flat (an enclosing tier's caps just
    // grow with the rim).
    if (lod0) { t.err = chord_error(0.5f, segments); return t; }
    const float k = enclosing_scale(segments, 0);
    enclose(t, k, 1e30f);
    t.err = 0.5f * (k - 1.0f);
    return t;
}
inline Tier capsule_tier(int segments, int rings, bool lod0) {
    Tier t; gen_capsule(t.v, t.i, segments, rings);
    // Hemisphere caps: rings span a quarter turn, so 4 * rings a full one.
    // The cylinder band between them is flat along the axis: chord_error.
    if (lod0) {
        t.err = std::max(uv_sphere_error(0.5f, segments, 4 * rings), chord_error(0.5f, segments));
        return t;
    }
    // One factor for the whole capsule: the band's rings ARE the hemispheres'
    // equators, so scaling them differently would open a crack.
    const float k = enclosing_scale(segments, 4 * rings);
    enclose(t, k, 0.5f);
    t.err = 0.5f * (k - 1.0f);
    return t;
}

// The tiers each curved primitive is built with; LOD 0 only when `lods` is false.
inline std::vector<Tier> sphere_tiers(bool lods) {
    if (!lods) return {sphere_tier(16, 32, true)};
    return {sphere_tier(16, 32, true), sphere_tier(8, 16, false), sphere_tier(4, 8, false)};
}
inline std::vector<Tier> cylinder_tiers(bool lods) {
    if (!lods) return {cylinder_tier(24, true)};
    return {cylinder_tier(24, true), cylinder_tier(12, false), cylinder_tier(6, false)};
}
inline std::vector<Tier> capsule_tiers(bool lods) {
    if (!lods) return {capsule_tier(24, 8, true)};
    return {capsule_tier(24, 8, true), capsule_tier(12, 4, false), capsule_tier(6, 2, false)};
}

/// Where each tier switches in (0 for LOD 0). A tier's deviation from LOD 0 is
/// at most its own error plus LOD 0's (LOD 0 lies inside the surface, the
/// lower tiers outside it).
inline std::vector<float> switch_distances(const std::vector<Tier>& tiers) {
    std::vector<float> d;
    for (size_t t = 0; t < tiers.size(); ++t)
        d.push_back(t == 0 ? 0.0f : switch_distance(tiers[t].err + tiers[0].err));
    return d;
}
}  // namespace primitive_lod

struct PrimitiveMeshCache {
    std::unique_ptr<gws::renderer::gpu::Mesh> cube;
    std::unique_ptr<gws::renderer::gpu::Mesh> plane;
    std::unique_ptr<gws::renderer::gpu::Mesh> sphere;
    std::unique_ptr<gws::renderer::gpu::Mesh> cylinder;
    std::unique_ptr<gws::renderer::gpu::Mesh> capsule;
    std::unique_ptr<gws::renderer::gpu::Mesh> pyramid;

    void initialize(gws::renderer::gpu::VulkanDevice* device) {
        using namespace gws::renderer::gpu;
        auto build = [&](void(*gen)(std::vector<SceneVertex>&, std::vector<uint32_t>&))
            -> std::unique_ptr<Mesh> {
            std::vector<SceneVertex> v;
            std::vector<uint32_t> i;
            gen(v, i);
            Submesh sm; sm.material_index = 0;
            sm.lods.push_back({0, (uint32_t)i.size(), 0.0f});
            return Mesh::create(device, v, i, {sm});
        };
        cube     = build(gen_cube);
        plane    = build(gen_plane);

        // The curved primitives get LOD tiers (primitive_lod::Tier). Each
        // tier's vertices and indices are appended to one buffer pair; LOD 0
        // stays first, so the BLAS (built from LOD 0 only) is unchanged.
        //
        // A sphere was 1,024 triangles at any distance -- at 20k entities,
        // ~5,000 spheres were 5.1M of the frame's 5.4M triangles, nearly all of
        // them covering a few dozen pixels. A tier switches in only where its
        // deviation from the full-resolution surface projects to under half a
        // pixel (see primitive_lod), in units of the mesh's own size -- these
        // meshes are lod_error_bounded, so a sphere scaled x10 keeps detail ten
        // times further out, and the shadow pass does not go a tier coarser.
        auto build_tiered = [&](const std::vector<primitive_lod::Tier>& tiers) -> std::unique_ptr<Mesh> {
            const std::vector<float> dist = primitive_lod::switch_distances(tiers);
            std::vector<SceneVertex> v;
            std::vector<uint32_t> i;
            Submesh sm; sm.material_index = 0;
            for (size_t t = 0; t < tiers.size(); ++t) {
                const uint32_t vbase = static_cast<uint32_t>(v.size());
                const uint32_t ibase = static_cast<uint32_t>(i.size());
                v.insert(v.end(), tiers[t].v.begin(), tiers[t].v.end());
                for (uint32_t x : tiers[t].i) i.push_back(x + vbase);   // generators index from 0
                sm.lods.push_back({ibase, static_cast<uint32_t>(tiers[t].i.size()), dist[t]});
            }
            auto m = Mesh::create(device, v, i, {sm});
            if (m) m->set_lod_error_bounded(true);
            return m;
        };
        // GWS_NO_PRIMITIVE_LOD=1 builds LOD 0 only: an A/B in one binary, and
        // the full-resolution look back if anyone wants it.
        const bool lods = std::getenv("GWS_NO_PRIMITIVE_LOD") == nullptr;
        sphere   = build_tiered(primitive_lod::sphere_tiers(lods));
        cylinder = build_tiered(primitive_lod::cylinder_tiers(lods));
        capsule  = build_tiered(primitive_lod::capsule_tiers(lods));
        pyramid  = build(gen_pyramid);
    }

    gws::renderer::gpu::Mesh* get(schizo::scene::MeshType type) const {
        switch (type) {
            case schizo::scene::MeshType::Cube:     return cube.get();
            case schizo::scene::MeshType::Sphere:   return sphere.get();
            case schizo::scene::MeshType::Cylinder: return cylinder.get();
            case schizo::scene::MeshType::Plane:    return plane.get();
            case schizo::scene::MeshType::Pyramid:  return pyramid.get();
            case schizo::scene::MeshType::Capsule:  return capsule.get();
        }
        return cube.get();
    }
};

} // namespace schizo::editor
