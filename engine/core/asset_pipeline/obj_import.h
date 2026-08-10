#pragma once

// ====================
// OBJ Importer  (Pillar C1 / Master Plan Stage 2.1)
// ====================
//
// Parses a Wavefront .obj into the neutral ImportedScene the cooker consumes —
// the one importer we can ship without vendoring (FBX/USD need assimp/tinyusdz).
// Welds face corners by their (position, uv, normal) triple into a properly
// indexed mesh (good LOD simplification downstream), interleaved as
// position(3) + normal(3) + uv(2) = 32-byte vertices, position @ offset 0.
//
//   ImportedScene scene;
//   if (import_obj("foo.obj", scene)) { auto pak = cook_scene(scene); ... }

#include "imported_scene.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::assets {

namespace detail {

// The interleaved record this importer welds face-corners into — the engine's
// canonical 32-byte pos/normal/uv vertex (see imported_scene.h). Using the
// shared type makes the cooked vertex layout the runtime loader expects an
// explicit, single-source-of-truth contract rather than a coincidence.
using ObjVertex = schizo::assets::ImportedVertex;  // 32 bytes, pos @ 0

struct ObjCorner {
    int v = 0, t = 0, n = 0;
    bool operator==(const ObjCorner& o) const { return v == o.v && t == o.t && n == o.n; }
};
struct ObjCornerHash {
    size_t operator()(const ObjCorner& c) const {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](int x) { h = (h ^ static_cast<uint32_t>(x)) * 1099511628211ull; };
        mix(c.v); mix(c.t); mix(c.n);
        return static_cast<size_t>(h);
    }
};

// Parse one face token ("v", "v/t", "v//n", "v/t/n"; 1-based, negatives are
// relative to current count). Resolves to 0-based indices (-1 if absent).
inline ObjCorner parse_corner(const std::string& tok,
                              int pos_count, int uv_count, int nrm_count) {
    ObjCorner c{-1, -1, -1};
    int part = 0;
    size_t start = 0;
    for (size_t i = 0; i <= tok.size(); ++i) {
        if (i == tok.size() || tok[i] == '/') {
            if (i > start) {
                int idx = std::atoi(tok.substr(start, i - start).c_str());
                int resolved;
                if (idx > 0)       resolved = idx - 1;
                else if (idx < 0)  resolved = (part == 0 ? pos_count : part == 1 ? uv_count : nrm_count) + idx;
                else               resolved = -1;
                if (part == 0) c.v = resolved;
                else if (part == 1) c.t = resolved;
                else if (part == 2) c.n = resolved;
            }
            ++part;
            start = i + 1;
        }
    }
    return c;
}

}  // namespace detail

inline std::string path_stem(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    size_t dot   = path.find_last_of('.');
    size_t begin = (slash == std::string::npos) ? 0 : slash + 1;
    size_t end   = (dot == std::string::npos || dot < begin) ? path.size() : dot;
    return path.substr(begin, end - begin);
}

// Directory portion of a path, including the trailing separator (empty if the
// path has none). Used to resolve .mtl / map_Kd references relative to the .obj.
inline std::string path_dir(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

namespace detail {
// Map a Wavefront specular exponent Ns (Phong, ~0..1000) to a PBR roughness in
// [0.04, 1]. Glossier (higher Ns) -> smoother (lower roughness).
inline float ns_to_roughness(float ns) {
    if (ns <= 0.0f) return 1.0f;
    float r = std::sqrt(2.0f / (ns + 2.0f));
    return r < 0.04f ? 0.04f : (r > 1.0f ? 1.0f : r);
}
}  // namespace detail

// Parse a Wavefront .mtl into CookedMaterials. `name_to_index` maps each
// `newmtl` name -> its index in `out`. `base_dir` is the .obj's directory, used
// to make `map_Kd` references .obj-relative so the cooker can resolve+cook the
// texture and the runtime can recover its GUID via asset_id_from_path().
// Paths are UTF-8. A narrow std::string handed to ifstream is interpreted in the
// ANSI codepage on Windows, so any model whose name is not pure ASCII fails to
// open — and the caller can only report "FAIL import", with no clue why.
inline std::filesystem::path u8p(const std::string& s) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

inline bool import_mtl(const std::string& mtl_path, const std::string& base_dir,
                       std::vector<CookedMaterial>& out,
                       std::unordered_map<std::string, int>& name_to_index) {
    std::ifstream f(u8p(mtl_path));
    if (!f) return false;
    int cur = -1;  // index into `out`, NOT a pointer (out reallocates on push)
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line);
        std::string key; s >> key;
        if (key == "newmtl") {
            std::string name; s >> name;
            name_to_index[name] = static_cast<int>(out.size());
            CookedMaterial m{};
            std::snprintf(m.name, sizeof(m.name), "%s", name.c_str());
            out.push_back(m);
            cur = static_cast<int>(out.size()) - 1;
        } else if (cur < 0) {
            continue;
        } else if (key == "Kd") {
            float r = 1, g = 1, b = 1; s >> r >> g >> b;
            out[cur].base_color[0] = r; out[cur].base_color[1] = g; out[cur].base_color[2] = b;
        } else if (key == "d") {
            float a = 1; s >> a; out[cur].base_color[3] = a;
        } else if (key == "Tr") {              // transparency: d == 1 - Tr
            float t = 0; s >> t; out[cur].base_color[3] = 1.0f - t;
        } else if (key == "Ns") {
            float ns = 0; s >> ns; out[cur].roughness = detail::ns_to_roughness(ns);
        } else if (key == "Pr") {              // PBR roughness (exporter extension)
            float pr = 1; s >> pr; out[cur].roughness = pr;
        } else if (key == "Pm") {              // PBR metallic (exporter extension)
            float pm = 0; s >> pm; out[cur].metallic = pm;
        } else if (key == "map_Kd") {
            // Take the last whitespace-token as the path (skips any map options
            // like `-s 1 1 1`); covers the overwhelmingly common simple form.
            std::string tok, last;
            while (s >> tok) last = tok;
            if (!last.empty()) {
                const std::string ref = base_dir + last;
                std::snprintf(out[cur].albedo_tex, sizeof(out[cur].albedo_tex), "%s", ref.c_str());
            }
        }
    }
    return !out.empty();
}

inline bool import_obj(const std::string& path, ImportedScene& out) {
    std::ifstream f(u8p(path));
    if (!f) return false;

    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> uvs;
    std::vector<detail::ObjVertex>    verts;
    std::unordered_map<detail::ObjCorner, uint32_t, detail::ObjCornerHash> dedup;

    // Materials parsed from referenced .mtl libraries, and the per-material
    // index buckets (key -1 = faces before any `usemtl`). One ImportedMesh is
    // emitted per non-empty bucket so each carries its own material.
    const std::string                 base_dir = path_dir(path);
    std::vector<CookedMaterial>       materials;
    std::unordered_map<std::string, int> mat_name_to_index;
    std::map<int, std::vector<uint32_t>> mat_indices;
    int                               current_mat = -1;
    std::vector<std::string>          deps;  // .mtl + textures (for incremental cook)

    auto resolve = [&](const detail::ObjCorner& c) -> uint32_t {
        auto it = dedup.find(c);
        if (it != dedup.end()) return it->second;
        detail::ObjVertex vert{};
        if (c.v >= 0 && c.v < (int)positions.size()) {
            vert.px = positions[c.v][0]; vert.py = positions[c.v][1]; vert.pz = positions[c.v][2];
        }
        if (c.n >= 0 && c.n < (int)normals.size()) {
            vert.nx = normals[c.n][0]; vert.ny = normals[c.n][1]; vert.nz = normals[c.n][2];
        }
        if (c.t >= 0 && c.t < (int)uvs.size()) {
            vert.u = uvs[c.t][0]; vert.v = uvs[c.t][1];
        }
        uint32_t idx = static_cast<uint32_t>(verts.size());
        verts.push_back(vert);
        dedup.emplace(c, idx);
        return idx;
    };

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0] == 'v' && line[1] == ' ') {
            std::istringstream s(line.substr(2)); float x = 0, y = 0, z = 0; s >> x >> y >> z;
            positions.push_back({x, y, z});
        } else if (line[0] == 'v' && line[1] == 't') {
            std::istringstream s(line.substr(3)); float u = 0, v = 0; s >> u >> v;
            uvs.push_back({u, v});
        } else if (line[0] == 'v' && line[1] == 'n') {
            std::istringstream s(line.substr(3)); float x = 0, y = 0, z = 0; s >> x >> y >> z;
            normals.push_back({x, y, z});
        } else if (line.rfind("mtllib", 0) == 0) {
            std::istringstream s(line); std::string key, name; s >> key >> name;
            if (!name.empty()) {
                deps.push_back(base_dir + name);   // the .mtl is a dependency
                import_mtl(base_dir + name, base_dir, materials, mat_name_to_index);
            }
        } else if (line.rfind("usemtl", 0) == 0) {
            std::istringstream s(line); std::string key, name; s >> key >> name;
            auto it = mat_name_to_index.find(name);
            current_mat = (it != mat_name_to_index.end()) ? it->second : -1;
        } else if (line[0] == 'f' && line[1] == ' ') {
            std::istringstream s(line.substr(2)); std::string tok;
            std::vector<uint32_t> face;
            while (s >> tok) {
                detail::ObjCorner c = detail::parse_corner(
                    tok, (int)positions.size(), (int)uvs.size(), (int)normals.size());
                if (c.v >= 0) face.push_back(resolve(c));
            }
            std::vector<uint32_t>& bucket = mat_indices[current_mat];
            for (size_t k = 2; k < face.size(); ++k) {     // triangle fan
                bucket.push_back(face[0]);
                bucket.push_back(face[k - 1]);
                bucket.push_back(face[k]);
            }
        }
    }

    bool any_indices = false;
    for (const auto& kv : mat_indices) if (!kv.second.empty()) { any_indices = true; break; }
    if (verts.empty() || !any_indices) return false;

    out = ImportedScene{};
    const std::string stem = path_stem(path);
    std::snprintf(out.name, sizeof(out.name), "%s", stem.c_str());

    // Materials: those parsed from the .mtl, or a single default when the OBJ
    // shipped none. The default sits at the end so real materials keep their
    // .mtl indices.
    out.materials = std::move(materials);
    int default_mat = -1;
    auto ensure_default_mat = [&]() -> int {
        if (default_mat < 0) {
            CookedMaterial m{};
            std::snprintf(m.name, sizeof(m.name), "%s_mat", stem.c_str());
            default_mat = static_cast<int>(out.materials.size());
            out.materials.push_back(m);
        }
        return default_mat;
    };
    if (out.materials.empty()) ensure_default_mat();

    // One mesh + one node per non-empty material bucket. The full (shared)
    // vertex pool is handed to every bucket; the offline cook drops the unused
    // verts per mesh (meshopt vertex-fetch remap), so this isn't wasteful.
    const uint32_t stride = static_cast<uint32_t>(sizeof(detail::ObjVertex));  // 32
    for (auto& kv : mat_indices) {
        if (kv.second.empty()) continue;
        const int mat_idx = (kv.first >= 0) ? kv.first : ensure_default_mat();

        ImportedMeshData mesh;
        mesh.vertex_stride = stride;
        mesh.vertices.resize(verts.size() * stride);
        std::memcpy(mesh.vertices.data(), verts.data(), mesh.vertices.size());
        mesh.indices = std::move(kv.second);
        const int mesh_idx = static_cast<int>(out.meshes.size());
        out.meshes.push_back(std::move(mesh));

        CookedNode node{};
        std::snprintf(node.name, sizeof(node.name), "%s_%d", stem.c_str(), mesh_idx);
        node.mesh = mesh_idx;
        node.material = mat_idx;
        out.nodes.push_back(node);
    }

    // Record dependencies (the .mtl libraries + every referenced texture) so an
    // incremental cook re-cooks this scene when any of them change.
    out.deps = std::move(deps);
    for (const CookedMaterial& m : out.materials)
        if (m.albedo_tex[0]) out.deps.push_back(m.albedo_tex);

    return true;
}

}  // namespace schizo::assets
