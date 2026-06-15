// glTF 2.0 -> ImportedScene importer (Master Plan Stage 2.1). The only TU that
// pulls in tinygltf, so the header-only implementation is emitted once here.

#include "gltf_import.h"
#include "obj_import.h"   // path_stem / path_dir (shared path helpers)

#include "tinygltf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace schizo::assets {

namespace {

// Column-major 4x4 (matches CookedNode::local_xform, read back via glm::make_mat4).
using Mat4 = std::array<float, 16>;

Mat4 identity() { return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; }

Mat4 mul(const Mat4& a, const Mat4& b) {  // a * b, both column-major
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
    return r;
}

// Compose T * R * S from a node's translation / quaternion / scale.
Mat4 compose_trs(const tinygltf::Node& n) {
    const float tx = n.translation.size() > 0 ? n.translation[0] : 0.0f;
    const float ty = n.translation.size() > 1 ? n.translation[1] : 0.0f;
    const float tz = n.translation.size() > 2 ? n.translation[2] : 0.0f;
    const float qx = n.rotation.size() > 0 ? n.rotation[0] : 0.0f;
    const float qy = n.rotation.size() > 1 ? n.rotation[1] : 0.0f;
    const float qz = n.rotation.size() > 2 ? n.rotation[2] : 0.0f;
    const float qw = n.rotation.size() > 3 ? n.rotation[3] : 1.0f;
    const float sx = n.scale.size() > 0 ? n.scale[0] : 1.0f;
    const float sy = n.scale.size() > 1 ? n.scale[1] : 1.0f;
    const float sz = n.scale.size() > 2 ? n.scale[2] : 1.0f;

    // Rotation matrix from quaternion (column-major).
    const float xx = qx*qx, yy = qy*qy, zz = qz*qz;
    const float xy = qx*qy, xz = qx*qz, yz = qy*qz;
    const float wx = qw*qx, wy = qw*qy, wz = qw*qz;
    Mat4 m = identity();
    // column 0
    m[0] = (1 - 2*(yy + zz)) * sx;
    m[1] = (2*(xy + wz))     * sx;
    m[2] = (2*(xz - wy))     * sx;
    // column 1
    m[4] = (2*(xy - wz))     * sy;
    m[5] = (1 - 2*(xx + zz)) * sy;
    m[6] = (2*(yz + wx))     * sy;
    // column 2
    m[8]  = (2*(xz + wy))    * sz;
    m[9]  = (2*(yz - wx))    * sz;
    m[10] = (1 - 2*(xx + yy))* sz;
    // column 3 = translation
    m[12] = tx; m[13] = ty; m[14] = tz;
    return m;
}

// Pointer to accessor element 0, plus the byte stride between elements.
const uint8_t* accessor_base(const tinygltf::Model& m, int acc_idx,
                             int& stride, int& comp_size, int& comp_count) {
    if (acc_idx < 0 || acc_idx >= static_cast<int>(m.accessors.size())) return nullptr;
    const tinygltf::Accessor& a = m.accessors[acc_idx];
    comp_count = tinygltf::Model::GetAccessorComponentCount(a);
    comp_size  = tinygltf::Model::GetAccessorComponentSize(a);
    if (a.bufferView < 0 || a.bufferView >= static_cast<int>(m.bufferViews.size())) return nullptr;
    const tinygltf::BufferView& bv = m.bufferViews[a.bufferView];
    if (bv.buffer < 0 || bv.buffer >= static_cast<int>(m.buffers.size())) return nullptr;
    const tinygltf::Buffer& buf = m.buffers[bv.buffer];
    stride = bv.byteStride > 0 ? bv.byteStride : comp_count * comp_size;
    const size_t start = static_cast<size_t>(bv.byteOffset) + a.byteOffset;
    const size_t need  = start + static_cast<size_t>(a.count) * stride;
    if (start >= buf.data.size() || need > buf.data.size()) return nullptr;
    return buf.data.data() + start;
}

// Read a FLOAT VECn accessor into a flat float array (count * comps).
std::vector<float> read_floats(const tinygltf::Model& m, int acc_idx, int want_comps) {
    int stride = 0, csize = 0, ccount = 0;
    const uint8_t* base = accessor_base(m, acc_idx, stride, csize, ccount);
    if (!base) return {};
    const int n = m.accessors[acc_idx].count;
    const int comps = std::min(want_comps, ccount);
    std::vector<float> out(static_cast<size_t>(n) * want_comps, 0.0f);
    for (int i = 0; i < n; ++i) {
        const uint8_t* e = base + static_cast<size_t>(i) * stride;
        for (int c = 0; c < comps; ++c)
            std::memcpy(&out[static_cast<size_t>(i) * want_comps + c], e + c * 4, 4);
    }
    return out;
}

// Read an index accessor (UNSIGNED_BYTE/SHORT/INT) into uint32.
std::vector<uint32_t> read_indices(const tinygltf::Model& m, int acc_idx) {
    int stride = 0, csize = 0, ccount = 0;
    const uint8_t* base = accessor_base(m, acc_idx, stride, csize, ccount);
    if (!base) return {};
    const tinygltf::Accessor& a = m.accessors[acc_idx];
    std::vector<uint32_t> out(a.count);
    for (int i = 0; i < a.count; ++i) {
        const uint8_t* e = base + static_cast<size_t>(i) * stride;
        uint32_t v = 0;
        switch (a.componentType) {
            case 5121: v = *e; break;                                   // UNSIGNED_BYTE
            case 5123: { uint16_t s; std::memcpy(&s, e, 2); v = s; } break; // UNSIGNED_SHORT
            case 5125: std::memcpy(&v, e, 4); break;                    // UNSIGNED_INT
            default:   std::memcpy(&v, e, std::min(csize, 4)); break;
        }
        out[i] = v;
    }
    return out;
}

// The base-color image URI for a material, made .gltf-relative (empty if none /
// embedded). Used for texture-GUID linking.
std::string base_color_uri(const tinygltf::Model& m, const tinygltf::Material& mat,
                           const std::string& base_dir) {
    const int ti = mat.pbrMetallicRoughness.baseColorTexture.index;
    if (ti < 0 || ti >= static_cast<int>(m.textures.size())) return {};
    const int src = m.textures[ti].source;
    if (src < 0 || src >= static_cast<int>(m.images.size())) return {};
    const std::string& uri = m.images[src].uri;
    if (uri.empty()) return {};   // embedded (bufferView) image — no file to link
    return base_dir + uri;
}

}  // namespace

bool import_gltf(const std::string& path, ImportedScene& out) {
    std::string ext;
    {
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos) { ext = path.substr(dot); for (char& c : ext) c = (char)::tolower(c); }
    }

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    const bool ok = (ext == ".glb")
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!ok || model.meshes.empty()) return false;

    out = ImportedScene{};
    std::snprintf(out.name, sizeof(out.name), "%s", path_stem(path).c_str());
    const std::string base_dir = path_dir(path);

    // --- materials ---
    for (const tinygltf::Material& mat : model.materials) {
        CookedMaterial cm{};
        std::snprintf(cm.name, sizeof(cm.name), "%s",
                      mat.name.empty() ? "material" : mat.name.c_str());
        const auto& bcf = mat.pbrMetallicRoughness.baseColorFactor;
        for (int i = 0; i < 4 && i < static_cast<int>(bcf.size()); ++i) cm.base_color[i] = bcf[i];
        cm.metallic  = mat.pbrMetallicRoughness.metallicFactor;
        cm.roughness = mat.pbrMetallicRoughness.roughnessFactor;
        const std::string tex = base_color_uri(model, mat, base_dir);
        if (!tex.empty()) std::snprintf(cm.albedo_tex, sizeof(cm.albedo_tex), "%s", tex.c_str());
        out.materials.push_back(cm);
    }
    int default_mat = -1;
    auto ensure_default_mat = [&]() -> int {
        if (default_mat < 0) { default_mat = (int)out.materials.size(); out.materials.push_back(CookedMaterial{}); }
        return default_mat;
    };
    if (out.materials.empty()) ensure_default_mat();

    // --- geometry + baked node hierarchy ---
    // Build a primitive (POSITION + optional NORMAL/TEXCOORD_0 + indices) into a
    // 32-byte ImportedVertex mesh; returns the new mesh index or -1.
    auto build_primitive = [&](const tinygltf::Primitive& prim) -> int {
        auto pit = prim.attributes.find("POSITION");
        if (pit == prim.attributes.end()) return -1;
        std::vector<float> pos = read_floats(model, pit->second, 3);
        if (pos.empty()) return -1;
        const size_t vcount = pos.size() / 3;

        std::vector<float> nrm, uv0;
        auto nit = prim.attributes.find("NORMAL");
        if (nit != prim.attributes.end()) nrm = read_floats(model, nit->second, 3);
        auto tit = prim.attributes.find("TEXCOORD_0");
        if (tit != prim.attributes.end()) uv0 = read_floats(model, tit->second, 2);

        std::vector<ImportedVertex> verts(vcount);
        for (size_t i = 0; i < vcount; ++i) {
            ImportedVertex& v = verts[i];
            v.px = pos[i*3+0]; v.py = pos[i*3+1]; v.pz = pos[i*3+2];
            if (i*3+2 < nrm.size()) { v.nx = nrm[i*3+0]; v.ny = nrm[i*3+1]; v.nz = nrm[i*3+2]; }
            else                    { v.nx = 0; v.ny = 1; v.nz = 0; }
            if (i*2+1 < uv0.size()) { v.u = uv0[i*2+0]; v.v = uv0[i*2+1]; }
        }

        std::vector<uint32_t> idx;
        if (prim.indices >= 0) idx = read_indices(model, prim.indices);
        if (idx.empty()) {                       // non-indexed: sequential
            idx.resize(vcount);
            for (uint32_t i = 0; i < vcount; ++i) idx[i] = i;
        }
        if (idx.empty()) return -1;

        ImportedMeshData md;
        md.vertex_stride = static_cast<uint32_t>(sizeof(ImportedVertex));
        md.vertices.resize(verts.size() * md.vertex_stride);
        std::memcpy(md.vertices.data(), verts.data(), md.vertices.size());
        md.indices = std::move(idx);
        const int mesh_idx = static_cast<int>(out.meshes.size());
        out.meshes.push_back(std::move(md));
        return mesh_idx;
    };

    // Recursively walk the node graph, baking each node's world transform into
    // the emitted CookedNode (parent = -1; the runtime composes from local).
    std::function<void(int, const Mat4&)> visit = [&](int node_idx, const Mat4& parent_world) {
        if (node_idx < 0 || node_idx >= static_cast<int>(model.nodes.size())) return;
        const tinygltf::Node& node = model.nodes[node_idx];
        const Mat4 world = mul(parent_world, compose_trs(node));
        if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
            for (const tinygltf::Primitive& prim : model.meshes[node.mesh].primitives) {
                const int mesh_idx = build_primitive(prim);
                if (mesh_idx < 0) continue;
                CookedNode cn{};
                std::snprintf(cn.name, sizeof(cn.name), "%s_%d", out.name, mesh_idx);
                cn.mesh     = mesh_idx;
                cn.material = (prim.material >= 0 && prim.material < (int)out.materials.size())
                                  ? prim.material : ensure_default_mat();
                cn.parent   = -1;
                std::memcpy(cn.local_xform, world.data(), sizeof(cn.local_xform));
                out.nodes.push_back(cn);
            }
        }
        for (int child : node.children) visit(child, world);
    };

    // Roots: the active scene's nodes, else nodes that aren't anyone's child.
    std::vector<int> roots;
    if (!model.scenes.empty()) {
        const int si = (model.scene >= 0 && model.scene < (int)model.scenes.size()) ? model.scene : 0;
        roots = model.scenes[si].nodes;
    }
    if (roots.empty()) {
        std::vector<bool> is_child(model.nodes.size(), false);
        for (const auto& n : model.nodes)
            for (int c : n.children) if (c >= 0 && c < (int)is_child.size()) is_child[c] = true;
        for (int i = 0; i < (int)model.nodes.size(); ++i) if (!is_child[i]) roots.push_back(i);
    }
    for (int r : roots) visit(r, identity());

    // Dependencies: external .bin buffers + referenced textures, so an
    // incremental cook re-cooks this scene when any of them change.
    for (const tinygltf::Buffer& b : model.buffers)
        if (!b.uri.empty() && b.uri.find("data:") == std::string::npos)
            out.deps.push_back(base_dir + b.uri);
    for (const CookedMaterial& m : out.materials)
        if (m.albedo_tex[0]) out.deps.push_back(m.albedo_tex);

    return !out.meshes.empty();
}

}  // namespace schizo::assets
