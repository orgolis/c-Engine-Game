#pragma once

// ====================
// ImportedScene — neutral import intermediate  (Pillar C1 / Stage 2.1)
// ====================
//
// The format every importer (glTF/FBX/OBJ/USD) emits and the cooker consumes.
// The runtime never sees source formats — only the cooked bundle. In-memory
// only (holds std::vector), so it's the *input* side; the cooked POD outputs
// (CookedMesh, CookedMaterial, CookedNode) are what get serialized.

#include "reflection/reflection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::assets {

// Canonical interleaved vertex layout the standard importers (OBJ/glTF) emit
// and the cooker preserves byte-for-byte (position @ offset 0). The cooked
// mesh blob stores opaque stride-byte records; when its stride equals
// sizeof(ImportedVertex) the runtime loader knows the semantic layout and can
// expand records into renderer vertices with NO reparse of the source file —
// this is the contract that lets the cook->runtime loop skip importers at load.
struct ImportedVertex {
    float px, py, pz;   // position
    float nx, ny, nz;   // normal
    float u,  v;        // uv0
};
static_assert(sizeof(ImportedVertex) == 32, "ImportedVertex layout drift");

// Raw geometry for one mesh (interleaved vertices, position @ offset 0).
struct ImportedMeshData {
    std::vector<uint8_t>  vertices;
    uint32_t              vertex_stride = 0;
    std::vector<uint32_t> indices;
};

// POD material/node — reflected so they serialize into the cooked manifest.
struct CookedMaterial {
    char  name[64]     = {0};
    float base_color[4]= {1, 1, 1, 1};
    float metallic     = 0.0f;
    float roughness    = 1.0f;
    char  albedo_tex[128] = {0};   // path/guid reference
};

struct CookedNode {
    char  name[64]      = {0};
    int   parent        = -1;      // index into nodes, -1 = root
    int   mesh          = -1;      // index into meshes, -1 = none
    int   material      = -1;      // index into materials, -1 = none
    float local_xform[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};

// In-memory scene as produced by an importer.
struct ImportedScene {
    char                          name[64] = {0};
    std::vector<ImportedMeshData> meshes;
    std::vector<CookedMaterial>   materials;
    std::vector<CookedNode>       nodes;
    // Source files this scene was built from BESIDES the main source itself —
    // .mtl libraries, external .bin buffers, referenced textures. The cooker
    // records these so an incremental build re-cooks this scene when any
    // dependency changes (the dependency-graph acceptance). In-memory only.
    std::vector<std::string>      deps;
};

}  // namespace schizo::assets

GWS_REFLECT_BEGIN(schizo::assets::CookedMaterial)
    GWS_REFLECT_FIELD(name)
    GWS_REFLECT_FIELD(base_color)
    GWS_REFLECT_FIELD(metallic)
    GWS_REFLECT_FIELD(roughness)
    GWS_REFLECT_FIELD(albedo_tex)
GWS_REFLECT_END()

GWS_REFLECT_BEGIN(schizo::assets::CookedNode)
    GWS_REFLECT_FIELD(name)
    GWS_REFLECT_FIELD(parent)
    GWS_REFLECT_FIELD(mesh)
    GWS_REFLECT_FIELD(material)
    GWS_REFLECT_FIELD(local_xform)
GWS_REFLECT_END()
