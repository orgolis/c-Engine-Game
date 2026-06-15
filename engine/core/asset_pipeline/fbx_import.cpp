// FBX -> ImportedScene importer (Master Plan Stage 2.1). The only TU that pulls
// in ufbx, so its API is isolated here.

#include "fbx_import.h"
#include "obj_import.h"   // path_stem / path_dir (shared path helpers)

#include "ufbx.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::assets {

namespace {

// Weld face corners by their (position, normal, uv) triple into a properly
// indexed mesh — same idea as the OBJ importer, good for LOD simplification.
struct VKey {
    float p[3], n[3], t[2];
    bool operator==(const VKey& o) const { return std::memcmp(this, &o, sizeof(VKey)) == 0; }
};
struct VKeyHash {
    size_t operator()(const VKey& k) const {
        uint64_t h = 1469598103934665603ull;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&k);
        for (size_t i = 0; i < sizeof(VKey); ++i) { h = (h ^ b[i]) * 1099511628211ull; }
        return static_cast<size_t>(h);
    }
};

float r2f(ufbx_real v) { return static_cast<float>(v); }

// Pull a material map's vec4 (base color etc.) with a sane fallback.
void map_color(const ufbx_material_map& m, float out[4], float dr, float dg, float db, float da) {
    if (m.value_components >= 3) {
        out[0] = r2f(m.value_vec4.x); out[1] = r2f(m.value_vec4.y); out[2] = r2f(m.value_vec4.z);
        out[3] = (m.value_components >= 4) ? r2f(m.value_vec4.w) : da;
    } else {
        out[0] = dr; out[1] = dg; out[2] = db; out[3] = da;
    }
}

std::string texture_ref(const ufbx_material_map& m, const std::string& base_dir) {
    if (!m.texture) return {};
    const ufbx_string& rel = m.texture->relative_filename.length ? m.texture->relative_filename
                                                                 : m.texture->filename;
    if (!rel.length) return {};
    return base_dir + std::string(rel.data, rel.length);
}

}  // namespace

bool import_fbx(const std::string& path, ImportedScene& out) {
    ufbx_load_opts opts{};
    opts.target_axes            = ufbx_axes_right_handed_y_up;  // match OBJ/glTF
    opts.target_unit_meters     = 1.0f;
    opts.generate_missing_normals = true;

    ufbx_error err{};
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &err);
    if (!scene) return false;

    out = ImportedScene{};
    std::snprintf(out.name, sizeof(out.name), "%s", path_stem(path).c_str());
    const std::string base_dir = path_dir(path);

    // --- materials (global list; map ufbx_material* -> index) ---
    std::unordered_map<const ufbx_material*, int> mat_index;
    for (size_t i = 0; i < scene->materials.count; ++i) {
        const ufbx_material* m = scene->materials.data[i];
        CookedMaterial cm{};
        std::snprintf(cm.name, sizeof(cm.name), "%s",
                      m->name.length ? std::string(m->name.data, m->name.length).c_str() : "material");
        // ufbx normalises every shading model into `pbr`, so base_color is set
        // even for classic Phong FBX (mapped from diffuse).
        map_color(m->pbr.base_color, cm.base_color, 1, 1, 1, 1);
        cm.metallic  = r2f(m->pbr.metalness.value_real);
        cm.roughness = r2f(m->pbr.roughness.value_real);
        const std::string tex = texture_ref(m->pbr.base_color, base_dir);
        if (!tex.empty()) std::snprintf(cm.albedo_tex, sizeof(cm.albedo_tex), "%s", tex.c_str());
        mat_index[m] = static_cast<int>(out.materials.size());
        out.materials.push_back(cm);
    }
    int default_mat = -1;
    auto ensure_default_mat = [&]() -> int {
        if (default_mat < 0) { default_mat = (int)out.materials.size(); out.materials.push_back(CookedMaterial{}); }
        return default_mat;
    };

    std::vector<uint32_t> tri_scratch;

    // --- geometry: one ImportedMesh per (node, material-part), world-baked ---
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node* node = scene->nodes.data[ni];
        if (node->is_root || !node->mesh) continue;
        const ufbx_mesh* mesh = node->mesh;
        const ufbx_matrix geo = node->geometry_to_world;

        for (size_t pi = 0; pi < mesh->material_parts.count; ++pi) {
            const ufbx_mesh_part& part = mesh->material_parts.data[pi];
            if (part.num_triangles == 0) continue;

            // Resolve this part's material to a global index.
            int mat_idx = -1;
            if (part.index < mesh->materials.count && mesh->materials.data[part.index]) {
                auto it = mat_index.find(mesh->materials.data[part.index]);
                mat_idx = (it != mat_index.end()) ? it->second : ensure_default_mat();
            } else {
                mat_idx = ensure_default_mat();
            }

            std::vector<ImportedVertex> verts;
            std::vector<uint32_t>       indices;
            std::unordered_map<VKey, uint32_t, VKeyHash> dedup;

            for (size_t fi = 0; fi < part.num_faces; ++fi) {
                const ufbx_face face = mesh->faces.data[part.face_indices.data[fi]];
                if (face.num_indices < 3) continue;
                tri_scratch.resize((face.num_indices - 2) * 3);
                const uint32_t ntris = ufbx_triangulate_face(
                    tri_scratch.data(), tri_scratch.size(), mesh, face);

                for (uint32_t t = 0; t < ntris * 3; ++t) {
                    const uint32_t corner = tri_scratch[t];
                    ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, corner);
                    p = ufbx_transform_position(&geo, p);
                    ufbx_vec3 n{0, 1, 0};
                    if (mesh->vertex_normal.exists) {
                        n = ufbx_get_vertex_vec3(&mesh->vertex_normal, corner);
                        n = ufbx_transform_direction(&geo, n);
                    }
                    ufbx_vec2 uv{0, 0};
                    if (mesh->vertex_uv.exists) uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, corner);

                    VKey key{};
                    key.p[0] = r2f(p.x); key.p[1] = r2f(p.y); key.p[2] = r2f(p.z);
                    key.n[0] = r2f(n.x); key.n[1] = r2f(n.y); key.n[2] = r2f(n.z);
                    key.t[0] = r2f(uv.x); key.t[1] = r2f(uv.y);

                    auto it = dedup.find(key);
                    uint32_t vid;
                    if (it != dedup.end()) {
                        vid = it->second;
                    } else {
                        vid = static_cast<uint32_t>(verts.size());
                        ImportedVertex iv{ key.p[0], key.p[1], key.p[2],
                                           key.n[0], key.n[1], key.n[2],
                                           key.t[0], key.t[1] };
                        verts.push_back(iv);
                        dedup.emplace(key, vid);
                    }
                    indices.push_back(vid);
                }
            }

            if (verts.empty() || indices.empty()) continue;

            ImportedMeshData md;
            md.vertex_stride = static_cast<uint32_t>(sizeof(ImportedVertex));
            md.vertices.resize(verts.size() * md.vertex_stride);
            std::memcpy(md.vertices.data(), verts.data(), md.vertices.size());
            md.indices = std::move(indices);
            const int mesh_idx = static_cast<int>(out.meshes.size());
            out.meshes.push_back(std::move(md));

            CookedNode cn{};
            std::snprintf(cn.name, sizeof(cn.name), "%s_%d", out.name, mesh_idx);
            cn.mesh     = mesh_idx;
            cn.material = mat_idx;
            cn.parent   = -1;        // positions already in world space
            out.nodes.push_back(cn);
        }
    }

    // Dependencies: referenced textures (the .fbx itself is the source).
    for (const CookedMaterial& m : out.materials)
        if (m.albedo_tex[0]) out.deps.push_back(m.albedo_tex);

    ufbx_free_scene(scene);
    return !out.meshes.empty();
}

}  // namespace schizo::assets
