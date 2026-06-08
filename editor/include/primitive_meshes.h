#pragma once

#include "vulkan/vulkan_scene_mesh.h"
#include "mesh_renderer_component.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <cmath>

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
        sphere   = [&]{
            std::vector<SceneVertex> v; std::vector<uint32_t> i;
            gen_sphere(v, i);
            Submesh sm; sm.material_index = 0;
            sm.lods.push_back({0, (uint32_t)i.size(), 0.0f});
            return Mesh::create(device, v, i, {sm});
        }();
        cylinder = [&]{
            std::vector<SceneVertex> v; std::vector<uint32_t> i;
            gen_cylinder(v, i);
            Submesh sm; sm.material_index = 0;
            sm.lods.push_back({0, (uint32_t)i.size(), 0.0f});
            return Mesh::create(device, v, i, {sm});
        }();
        capsule = [&]{
            std::vector<SceneVertex> v; std::vector<uint32_t> i;
            gen_capsule(v, i);
            Submesh sm; sm.material_index = 0;
            sm.lods.push_back({0, (uint32_t)i.size(), 0.0f});
            return Mesh::create(device, v, i, {sm});
        }();
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
