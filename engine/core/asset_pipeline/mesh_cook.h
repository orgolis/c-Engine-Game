#pragma once

// ====================
// Offline Mesh Cook  (Pillar C2 / Master Plan Stage 2.2)
// ====================
//
// Turns an imported mesh into a GPU-ready cooked blob: vertex-cache + vertex-
// fetch optimised, with a precomputed LOD chain (meshoptimizer). The runtime
// loads it with zero processing. Format-agnostic beyond "position is 3 floats
// at offset 0" — vertices are opaque stride-byte records, indices are u32.
//
// Cooked blob layout:
//   [CookedMeshHeader]
//   [CookedLod x lod_count]
//   [vertices : vertex_count * vertex_stride]
//   [indices  : total_index_count * u32]
//
// (Offline meshlet generation reuses meshopt_buildMeshlets — a follow-up;
// the runtime already builds meshlets today.)

#include <meshoptimizer.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <vector>

namespace schizo::assets {

struct ImportedMesh {
    const uint8_t*  vertices      = nullptr;  // interleaved, position @ offset 0
    uint32_t        vertex_count  = 0;
    uint32_t        vertex_stride = 0;        // bytes per vertex
    const uint32_t* indices       = nullptr;
    uint32_t        index_count   = 0;
};

inline constexpr uint32_t kCookedMeshMagic   = 0x48534D43u; // 'C','M','S','H'
inline constexpr uint32_t kCookedMeshVersion = 2u;          // 2 = + meshlets
inline constexpr uint32_t kMeshletMaxVerts   = 64u;
inline constexpr uint32_t kMeshletMaxTris    = 124u;

struct CookedMeshHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t vertex_count;
    uint32_t vertex_stride;
    uint32_t lod_count;
    uint32_t total_index_count;
    // Offline meshlet data (built from LOD0). 0 when not present.
    uint32_t meshlet_count;
    uint32_t meshlet_vertex_count;    // entries in the meshlet vertex table
    uint32_t meshlet_triangle_count;  // entries in the meshlet triangle table (bytes)
    float    bounds_min[3];
    float    bounds_max[3];
};

struct CookedLod {
    uint32_t index_offset;   // into the cooked index buffer (in indices)
    uint32_t index_count;
    float    error;          // simplification error (0 = LOD0)
};

// Mirrors meshopt_Meshlet: a cluster of <= kMeshletMaxVerts verts /
// kMeshletMaxTris triangles, indexing the meshlet vertex/triangle tables.
struct CookedMeshlet {
    uint32_t vertex_offset;
    uint32_t triangle_offset;
    uint32_t vertex_count;
    uint32_t triangle_count;
};

inline std::vector<uint8_t> cook_mesh(
    const ImportedMesh& in,
    const std::vector<float>& lod_ratios = {1.0f, 0.5f, 0.25f}) {

    const uint32_t vcount = in.vertex_count;
    const uint32_t stride = in.vertex_stride;
    const uint32_t icount = in.index_count;

    // 1) Vertex-cache optimisation (reorder indices, transform-cache friendly).
    std::vector<uint32_t> idx(in.indices, in.indices + icount);
    meshopt_optimizeVertexCache(idx.data(), idx.data(), icount, vcount);

    // 2) Vertex-fetch optimisation (reorder vertices for locality + drop
    //    unused ones, remap indices).
    std::vector<uint32_t> remap(vcount);
    const size_t new_vcount =
        meshopt_optimizeVertexFetchRemap(remap.data(), idx.data(), icount, vcount);
    std::vector<uint8_t> verts(new_vcount * stride);
    meshopt_remapVertexBuffer(verts.data(), in.vertices, vcount, stride, remap.data());
    meshopt_remapIndexBuffer(idx.data(), idx.data(), icount, remap.data());

    // 3) LOD chain via simplification.
    std::vector<CookedLod>  lods;
    std::vector<uint32_t>   all_indices;
    const float* positions = reinterpret_cast<const float*>(verts.data());

    for (size_t li = 0; li < lod_ratios.size(); ++li) {
        const float ratio = lod_ratios[li];
        if (li == 0 || ratio >= 0.999f) {
            lods.push_back({static_cast<uint32_t>(all_indices.size()), icount, 0.0f});
            all_indices.insert(all_indices.end(), idx.begin(), idx.end());
            continue;
        }
        size_t target = static_cast<size_t>(icount * ratio);
        target = (target / 3) * 3;
        if (target < 3) target = 3;

        std::vector<uint32_t> lod_idx(icount);
        float error = 0.0f;
        const size_t result = meshopt_simplify(
            lod_idx.data(), idx.data(), icount,
            positions, new_vcount, stride,
            target, /*target_error*/ 0.05f, /*options*/ 0, &error);
        lod_idx.resize(result);
        lods.push_back({static_cast<uint32_t>(all_indices.size()),
                        static_cast<uint32_t>(result), error});
        all_indices.insert(all_indices.end(), lod_idx.begin(), lod_idx.end());
    }

    // 4) Bounds from positions.
    float bmin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float bmax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (size_t v = 0; v < new_vcount; ++v) {
        const float* p = reinterpret_cast<const float*>(verts.data() + v * stride);
        for (int c = 0; c < 3; ++c) { bmin[c] = std::min(bmin[c], p[c]); bmax[c] = std::max(bmax[c], p[c]); }
    }

    // 5) Offline meshlet generation from LOD0 (cluster culling at runtime). The
    //    runtime can build these live too, but precomputing removes that cost.
    std::vector<CookedMeshlet> meshlets;
    std::vector<uint32_t>      meshlet_verts;
    std::vector<uint8_t>       meshlet_tris;
    if (new_vcount > 0 && !idx.empty()) {
        const size_t max_meshlets =
            meshopt_buildMeshletsBound(idx.size(), kMeshletMaxVerts, kMeshletMaxTris);
        std::vector<meshopt_Meshlet> mo(max_meshlets);
        meshlet_verts.resize(max_meshlets * kMeshletMaxVerts);
        meshlet_tris.resize(max_meshlets * kMeshletMaxTris * 3);
        const size_t count = meshopt_buildMeshlets(
            mo.data(), meshlet_verts.data(), meshlet_tris.data(),
            idx.data(), idx.size(), positions, new_vcount, stride,
            kMeshletMaxVerts, kMeshletMaxTris, /*cone_weight*/ 0.0f);
        // Trim to what the last meshlet actually used.
        if (count > 0) {
            const meshopt_Meshlet& last = mo[count - 1];
            meshlet_verts.resize(last.vertex_offset + last.vertex_count);
            meshlet_tris.resize(last.triangle_offset +
                                ((last.triangle_count * 3 + 3) & ~3u));
        } else {
            meshlet_verts.clear();
            meshlet_tris.clear();
        }
        mo.resize(count);
        meshlets.reserve(count);
        for (const meshopt_Meshlet& m : mo)
            meshlets.push_back({m.vertex_offset, m.triangle_offset,
                                m.vertex_count, m.triangle_count});
    }

    // 6) Assemble.
    CookedMeshHeader h{};
    h.magic                  = kCookedMeshMagic;
    h.version                = kCookedMeshVersion;
    h.vertex_count           = static_cast<uint32_t>(new_vcount);
    h.vertex_stride          = stride;
    h.lod_count              = static_cast<uint32_t>(lods.size());
    h.total_index_count      = static_cast<uint32_t>(all_indices.size());
    h.meshlet_count          = static_cast<uint32_t>(meshlets.size());
    h.meshlet_vertex_count   = static_cast<uint32_t>(meshlet_verts.size());
    h.meshlet_triangle_count = static_cast<uint32_t>(meshlet_tris.size());
    std::memcpy(h.bounds_min, bmin, sizeof(bmin));
    std::memcpy(h.bounds_max, bmax, sizeof(bmax));

    std::vector<uint8_t> out;
    auto put = [&](const void* d, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(d);
        out.insert(out.end(), p, p + n);
    };
    put(&h, sizeof(h));
    put(lods.data(), lods.size() * sizeof(CookedLod));
    put(verts.data(), new_vcount * stride);
    put(all_indices.data(), all_indices.size() * sizeof(uint32_t));
    put(meshlets.data(), meshlets.size() * sizeof(CookedMeshlet));
    put(meshlet_verts.data(), meshlet_verts.size() * sizeof(uint32_t));
    put(meshlet_tris.data(), meshlet_tris.size() * sizeof(uint8_t));
    return out;
}

// Zero-copy views into a cooked-mesh blob.
struct CookedMeshView {
    const CookedMeshHeader* header            = nullptr;
    const CookedLod*        lods              = nullptr;
    const uint8_t*          vertices          = nullptr;
    const uint32_t*         indices           = nullptr;
    const CookedMeshlet*    meshlets          = nullptr;
    const uint32_t*         meshlet_vertices  = nullptr;  // table indexed by meshlet
    const uint8_t*          meshlet_triangles = nullptr;  // 3 local indices / tri

    bool open(const void* data, size_t n) {
        if (n < sizeof(CookedMeshHeader)) return false;
        header = static_cast<const CookedMeshHeader*>(data);
        if (header->magic != kCookedMeshMagic) return false;
        const uint8_t* p = static_cast<const uint8_t*>(data) + sizeof(CookedMeshHeader);
        lods     = reinterpret_cast<const CookedLod*>(p);
        p       += static_cast<size_t>(header->lod_count) * sizeof(CookedLod);
        vertices = p;
        p       += static_cast<size_t>(header->vertex_count) * header->vertex_stride;
        indices  = reinterpret_cast<const uint32_t*>(p);
        p       += static_cast<size_t>(header->total_index_count) * sizeof(uint32_t);
        meshlets = reinterpret_cast<const CookedMeshlet*>(p);
        p       += static_cast<size_t>(header->meshlet_count) * sizeof(CookedMeshlet);
        meshlet_vertices = reinterpret_cast<const uint32_t*>(p);
        p       += static_cast<size_t>(header->meshlet_vertex_count) * sizeof(uint32_t);
        meshlet_triangles = p;
        p       += static_cast<size_t>(header->meshlet_triangle_count);
        const size_t need = static_cast<size_t>(p - static_cast<const uint8_t*>(data));
        return need <= n;
    }
};

}  // namespace schizo::assets
