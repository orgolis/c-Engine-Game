// ============================================================================
// skinned_import_check — headless verification of the rigged-glTF importer.
//
// Generates a minimal rig (2 bones, a 4-vertex quad skinned bottom->bone0 /
// top->bone1, and a 1s "bend the tip 90 deg about X" animation) as a .gltf +
// .bin, loads it through gws::renderer::gpu::load_skinned_gltf, and checks the
// skeleton hierarchy, joint remap, inverse-bind, and animation sampling.
//
// Usage: skinned_import_check [output.gltf]
//   With a path, the rig is written there and kept (so the editor can Import
//   it). Without, it goes to a temp file and is deleted after the check.
// ============================================================================
#include "vulkan/skinned_gltf_loader.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace gws::renderer::gpu;

// ---- tiny binary buffer builder ----
struct Bin {
    std::vector<uint8_t> data;
    size_t put(const void* p, size_t n) {
        size_t off = data.size();
        data.insert(data.end(), (const uint8_t*)p, (const uint8_t*)p + n);
        return off;
    }
    void f(float v)    { put(&v, 4); }
    void u16(uint16_t v){ put(&v, 2); }
};

// Write the test rig to `gltf_path` (+ a sibling .bin).
static void write_test_rig(const std::string& gltf_path) {
    const fs::path gp(gltf_path);
    const std::string bin_name = gp.stem().string() + ".bin";
    const fs::path bp = gp.parent_path() / bin_name;

    Bin b;
    // POSITION (4x vec3): a vertical quad, bottom at y=0, top at y=2.
    const size_t off_pos = b.data.size();
    float pos[4][3] = {{-0.2f,0,0},{0.2f,0,0},{-0.2f,2,0},{0.2f,2,0}};
    for (auto& v : pos) { b.f(v[0]); b.f(v[1]); b.f(v[2]); }
    // NORMAL (4x vec3): +Z.
    const size_t off_nrm = b.data.size();
    for (int i=0;i<4;++i){ b.f(0); b.f(0); b.f(1); }
    // JOINTS_0 (4x u16vec4): bottom->joint0, top->joint1.
    const size_t off_jnt = b.data.size();
    uint16_t jn[4][4] = {{0,0,0,0},{0,0,0,0},{1,0,0,0},{1,0,0,0}};
    for (auto& v : jn) for (int k=0;k<4;++k) b.u16(v[k]);
    // WEIGHTS_0 (4x vec4): full weight on the first joint.
    const size_t off_wgt = b.data.size();
    for (int i=0;i<4;++i){ b.f(1); b.f(0); b.f(0); b.f(0); }
    // indices (6x u16).
    const size_t off_idx = b.data.size();
    uint16_t idx[6] = {0,1,2, 2,1,3};
    for (uint16_t v : idx) b.u16(v);
    // inverseBindMatrices (2x mat4, column-major): bone0=I, bone1=translate(0,-1,0).
    const size_t off_ibm = b.data.size();
    float I[16]   = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float Tm1[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,1};
    for (float v : I)   b.f(v);
    for (float v : Tm1) b.f(v);
    // anim input times (2x scalar): 0, 1.
    const size_t off_tin = b.data.size();
    b.f(0.0f); b.f(1.0f);
    // anim output rotations (2x vec4 xyzw): identity, 90deg about X.
    const size_t off_rot = b.data.size();
    b.f(0); b.f(0); b.f(0); b.f(1);                         // t=0 identity
    b.f(0.70710678f); b.f(0); b.f(0); b.f(0.70710678f);     // t=1 90deg X
    const size_t total = b.data.size();

    std::ofstream bin_out(bp, std::ios::binary);
    bin_out.write((const char*)b.data.data(), (std::streamsize)b.data.size());
    bin_out.close();

    auto bv = [](size_t off, size_t len) {
        return "{\"buffer\":0,\"byteOffset\":" + std::to_string(off) +
               ",\"byteLength\":" + std::to_string(len) + "}";
    };
    std::string json =
    "{\n"
    "\"asset\":{\"version\":\"2.0\"},\n"
    "\"scene\":0,\n"
    "\"scenes\":[{\"nodes\":[0,2]}],\n"
    "\"nodes\":[\n"
    "  {\"name\":\"root\",\"translation\":[0,0,0],\"children\":[1]},\n"
    "  {\"name\":\"tip\",\"translation\":[0,1,0]},\n"
    "  {\"name\":\"mesh\",\"mesh\":0,\"skin\":0}\n"
    "],\n"
    "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":5}],\n"
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"JOINTS_0\":2,\"WEIGHTS_0\":3},\"indices\":4}]}],\n"
    "\"animations\":[{\"name\":\"bend\",\"samplers\":[{\"input\":6,\"output\":7,\"interpolation\":\"LINEAR\"}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"rotation\"}}]}],\n"
    "\"accessors\":[\n"
    "  {\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},\n"
    "  {\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},\n"
    "  {\"bufferView\":2,\"componentType\":5123,\"count\":4,\"type\":\"VEC4\"},\n"
    "  {\"bufferView\":3,\"componentType\":5126,\"count\":4,\"type\":\"VEC4\"},\n"
    "  {\"bufferView\":4,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},\n"
    "  {\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},\n"
    "  {\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},\n"
    "  {\"bufferView\":7,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"}\n"
    "],\n"
    "\"bufferViews\":[\n"
    "  " + bv(off_pos,48) + ",\n"
    "  " + bv(off_nrm,48) + ",\n"
    "  " + bv(off_jnt,32) + ",\n"
    "  " + bv(off_wgt,64) + ",\n"
    "  " + bv(off_idx,12) + ",\n"
    "  " + bv(off_ibm,128) + ",\n"
    "  " + bv(off_tin,8) + ",\n"
    "  " + bv(off_rot,32) + "\n"
    "],\n"
    "\"buffers\":[{\"uri\":\"" + bin_name + "\",\"byteLength\":" + std::to_string(total) + "}]\n"
    "}\n";

    std::ofstream(gp) << json;
}

static int g_fail = 0;
static void check(const char* what, bool ok) {
    std::cout << (ok ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!ok) ++g_fail;
}
static bool approx(float a, float b, float eps = 1e-3f) { return std::abs(a - b) < eps; }

int main(int argc, char** argv) {
    std::string gltf_path;
    bool keep = false;
    if (argc > 1) { gltf_path = argv[1]; keep = true; }
    else {
        gltf_path = (fs::temp_directory_path() / "gws_test_rig.gltf").string();
    }
    fs::create_directories(fs::path(gltf_path).parent_path());

    std::cout << "skinned_import_check: writing rig -> " << gltf_path << "\n";
    write_test_rig(gltf_path);

    LoadedSkinnedModel R = load_skinned_gltf(gltf_path);

    check("import succeeded", R.valid);
    if (!R.valid) { std::cout << "  error: " << R.error << "\n"; return 1; }

    check("2 bones", R.skeleton && R.skeleton->GetBoneCount() == 2);
    int root = R.skeleton->GetBoneId("root__0");
    int tip  = R.skeleton->GetBoneId("tip__1");
    check("named bones present", root >= 0 && tip >= 0);
    if (root >= 0 && tip >= 0) {
        const auto* tb = R.skeleton->GetBone((uint16_t)tip);
        check("tip parented to root", tb && tb->parent_id == root);
    }

    check("4 vertices", R.vertices.size() == 4);
    check("6 indices",  R.indices.size() == 6);
    if (R.vertices.size() == 4 && root >= 0 && tip >= 0) {
        check("bottom verts weighted to root bone",
              R.vertices[0].joints.x == (uint32_t)root && R.vertices[1].joints.x == (uint32_t)root);
        check("top verts weighted to tip bone",
              R.vertices[2].joints.x == (uint32_t)tip && R.vertices[3].joints.x == (uint32_t)tip);
    }

    if (tip >= 0 && (size_t)tip < R.inverse_bind.size()) {
        // inverse-bind of tip should be translate(0,-1,0): column 3, row y = -1.
        check("tip inverse-bind = translate(0,-1,0)", approx(R.inverse_bind[tip][3].y, -1.0f));
    }

    check("1 animation", R.animations.size() == 1);
    if (!R.animations.empty() && tip >= 0) {
        auto& clip = R.animations[0];
        const auto* track = clip->GetBoneAnimation((uint16_t)tip);
        check("tip has an animation track", track != nullptr);
        check("track has 2 keyframes", track && track->GetKeyframes().size() == 2);
        // Sample at t=1s: tip should be rotated ~90deg about X (quat w,x ~ 0.707).
        clip->Sample(*R.skeleton, 1.0f, false);
        const auto* tb = R.skeleton->GetBone((uint16_t)tip);
        glm::quat q = tb ? tb->local_transform.rotation : glm::quat(1,0,0,0);
        check("tip animated to 90deg about X at t=1",
              approx(q.w, 0.70710678f) && approx(q.x, 0.70710678f));
    }

    if (!keep) {
        std::error_code ec;
        fs::remove(gltf_path, ec);
        fs::remove((fs::path(gltf_path).parent_path() / (fs::path(gltf_path).stem().string() + ".bin")), ec);
    }

    if (g_fail == 0) { std::cout << "skinned_import_check: ALL OK\n"; return 0; }
    std::cout << "skinned_import_check: " << g_fail << " FAILED\n";
    return 1;
}
