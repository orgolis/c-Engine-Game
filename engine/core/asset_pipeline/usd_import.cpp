// USD -> ImportedScene importer (Master Plan Stage 2.1). The only TU that pulls
// in tinyusdz + Tydra, so their API is isolated here.

#include "usd_import.h"
#include "obj_import.h"   // path_stem / path_dir (shared path helpers)

#include "tinyusdz.hh"
#include "tydra/render-data.hh"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

namespace schizo::assets {

namespace {
namespace tt = tinyusdz::tydra;

// USD transforms use the row-vector convention: p' = p(row) * M (row-major),
// translation in row 3. matrix4d is `double m[4][4]`.
void xform_point(const tinyusdz::value::matrix4d& M, const float in[3], float out[3]) {
    for (int c = 0; c < 3; ++c)
        out[c] = static_cast<float>(in[0] * M.m[0][c] + in[1] * M.m[1][c] +
                                    in[2] * M.m[2][c] + M.m[3][c]);
}
void xform_dir(const tinyusdz::value::matrix4d& M, const float in[3], float out[3]) {
    for (int c = 0; c < 3; ++c)
        out[c] = static_cast<float>(in[0] * M.m[0][c] + in[1] * M.m[1][c] + in[2] * M.m[2][c]);
}

// Read N components from a Tydra vertex attribute element (handles any stride).
bool read_attr(const tt::VertexAttribute& a, size_t i, int comps, float* out) {
    if (a.empty() || i >= a.vertex_count()) return false;
    const uint8_t* p = a.get_data().data() + i * a.stride_bytes();
    std::memcpy(out, p, sizeof(float) * static_cast<size_t>(comps));
    return true;
}

}  // namespace

bool import_usd(const std::string& path, ImportedScene& out) {
    // Read the file ourselves and load from memory — this dodges tinyusdz's
    // Win32 memory-mapped file path (which crashes in this MinGW build) while
    // still letting tinyusdz detect the format from the filename hint.
    std::ifstream f(std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(path.data()), path.size())),
                    std::ios::binary);   // UTF-8-safe: see obj_import.h u8p
    if (!f) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;

    tinyusdz::Stage stage;
    std::string warn, err;
    if (!tinyusdz::LoadUSDFromMemory(bytes.data(), bytes.size(), path, &stage, &warn, &err))
        return false;

    const std::string base_dir = path_dir(path);

    tt::RenderSceneConverterEnv env(stage);
    env.usd_filename = path;
    if (!base_dir.empty()) env.set_search_paths({ base_dir });
    // mesh_config defaults: triangulate = true, build_vertex_indices = true,
    // so points/normals/texcoords are vertex-aligned and faceVertexIndices()
    // returns triangles.

    tt::RenderSceneConverter converter;
    tt::RenderScene rscene;
    if (!converter.ConvertToRenderScene(env, &rscene)) return false;

    out = ImportedScene{};
    std::snprintf(out.name, sizeof(out.name), "%s", path_stem(path).c_str());

    // --- materials (UsdPreviewSurface) ---
    for (const tt::RenderMaterial& m : rscene.materials) {
        CookedMaterial cm{};
        std::snprintf(cm.name, sizeof(cm.name), "%s",
                      m.name.empty() ? "material" : m.name.c_str());
        const tt::PreviewSurfaceShader& s = m.surfaceShader;
        cm.base_color[0] = s.diffuseColor.value[0];
        cm.base_color[1] = s.diffuseColor.value[1];
        cm.base_color[2] = s.diffuseColor.value[2];
        cm.base_color[3] = s.opacity.value;
        cm.metallic  = s.metallic.value;
        cm.roughness = s.roughness.value;
        // Texture GUID linking: diffuseColor texture -> image -> resolved filename.
        if (s.diffuseColor.is_texture() &&
            s.diffuseColor.texture_id < static_cast<int>(rscene.textures.size())) {
            const int64_t img = rscene.textures[s.diffuseColor.texture_id].texture_image_id;
            if (img >= 0 && img < static_cast<int64_t>(rscene.images.size())) {
                const std::string& asset = rscene.images[img].asset_identifier;
                if (!asset.empty())
                    std::snprintf(cm.albedo_tex, sizeof(cm.albedo_tex), "%s", asset.c_str());
            }
        }
        out.materials.push_back(cm);
    }
    int default_mat = -1;
    auto ensure_default_mat = [&]() -> int {
        if (default_mat < 0) { default_mat = (int)out.materials.size(); out.materials.push_back(CookedMaterial{}); }
        return default_mat;
    };

    // Build one ImportedMesh from a Tydra RenderMesh under world transform `M`.
    auto build_mesh = [&](const tt::RenderMesh& rm, const tinyusdz::value::matrix4d& M) {
        const size_t N = rm.points.size();
        const std::vector<uint32_t>& idx = rm.faceVertexIndices();
        if (N == 0 || idx.empty()) return;

        const bool have_n = (rm.normals.vertex_count() == N);
        auto uvit = rm.texcoords.find(0);
        const bool have_uv = (uvit != rm.texcoords.end() && uvit->second.vertex_count() == N);

        std::vector<ImportedVertex> verts(N);
        for (size_t i = 0; i < N; ++i) {
            ImportedVertex& v = verts[i];
            const float lp[3] = { rm.points[i][0], rm.points[i][1], rm.points[i][2] };
            float wp[3]; xform_point(M, lp, wp);
            v.px = wp[0]; v.py = wp[1]; v.pz = wp[2];

            if (have_n) {
                float ln[3] = {0, 1, 0}; read_attr(rm.normals, i, 3, ln);
                float wn[3]; xform_dir(M, ln, wn);
                const float len = std::sqrt(wn[0]*wn[0] + wn[1]*wn[1] + wn[2]*wn[2]);
                if (len > 1e-8f) { v.nx = wn[0]/len; v.ny = wn[1]/len; v.nz = wn[2]/len; }
                else            { v.nx = 0; v.ny = 1; v.nz = 0; }
            } else { v.nx = 0; v.ny = 1; v.nz = 0; }

            if (have_uv) { float uv[2] = {0, 0}; read_attr(uvit->second, i, 2, uv); v.u = uv[0]; v.v = uv[1]; }
        }

        ImportedMeshData md;
        md.vertex_stride = static_cast<uint32_t>(sizeof(ImportedVertex));
        md.vertices.resize(verts.size() * md.vertex_stride);
        std::memcpy(md.vertices.data(), verts.data(), md.vertices.size());
        md.indices = idx;
        const int mesh_idx = static_cast<int>(out.meshes.size());
        out.meshes.push_back(std::move(md));

        CookedNode cn{};
        std::snprintf(cn.name, sizeof(cn.name), "%s_%d", out.name, mesh_idx);
        cn.mesh     = mesh_idx;
        cn.material = (rm.material_id >= 0 && rm.material_id < (int)out.materials.size())
                          ? rm.material_id : ensure_default_mat();
        cn.parent   = -1;  // positions already world-baked
        out.nodes.push_back(cn);
    };

    // Walk the Tydra node hierarchy; each Mesh node carries a world matrix.
    std::function<void(const tt::Node&)> visit = [&](const tt::Node& node) {
        if (node.nodeType == tt::NodeType::Mesh &&
            node.id >= 0 && node.id < static_cast<int>(rscene.meshes.size())) {
            build_mesh(rscene.meshes[node.id], node.global_matrix);
        }
        for (const tt::Node& c : node.children) visit(c);
    };
    for (const tt::Node& n : rscene.nodes) visit(n);

    // Fallback: meshes not reachable through a node still cook (identity xform).
    if (out.meshes.empty()) {
        for (const tt::RenderMesh& rm : rscene.meshes)
            build_mesh(rm, tinyusdz::value::matrix4d::identity());
    }

    for (const CookedMaterial& m : out.materials)
        if (m.albedo_tex[0]) out.deps.push_back(m.albedo_tex);

    return !out.meshes.empty();
}

}  // namespace schizo::assets
