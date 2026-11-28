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
#include "clip_library.h"
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
    // A 1x1 PNG, RGBA (200, 100, 50, 255): the material's base colour image.
    const size_t off_png = b.data.size();
    static const uint8_t png[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x38, 0x91, 0x62, 0xF4, 0x1F, 0x00, 0x05, 0xB4, 0x02, 0x5E, 0x1F, 0x3A, 0xFB, 0xCB, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    for (uint8_t v : png) b.data.push_back(v);
    while (b.data.size() % 4) b.data.push_back(0);
    // "steps": the tip's rotation, STEP, keys at 0, 0.5, 1 (identity, 90 X, identity).
    const size_t off_tin3 = b.data.size();
    b.f(0.0f); b.f(0.5f); b.f(1.0f);
    const size_t off_rot3 = b.data.size();
    b.f(0); b.f(0); b.f(0); b.f(1);
    b.f(0.70710678f); b.f(0); b.f(0); b.f(0.70710678f);
    b.f(0); b.f(0); b.f(0); b.f(1);
    // "spline": the root's translation, CUBICSPLINE, keys at 0 and 1 (input 6):
    // (in-tangent, value, out-tangent) per key -- 0 to (2,0,0), flat tangents.
    const size_t off_cub = b.data.size();
    for (float v : {0.f,0.f,0.f, 0.f,0.f,0.f, 0.f,0.f,0.f,   0.f,0.f,0.f, 2.f,0.f,0.f, 0.f,0.f,0.f}) b.f(v);
    // "split": the root's translation on keys at 0 and 2 -- (0,0,0) to (0,3,0) --
    // while the tip's rotation runs 0..1 (input 6, output 7).
    const size_t off_tin2 = b.data.size();
    b.f(0.0f); b.f(2.0f);
    const size_t off_tr2 = b.data.size();
    for (float v : {0.f,0.f,0.f, 0.f,3.f,0.f}) b.f(v);
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
    "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"JOINTS_0\":2,\"WEIGHTS_0\":3},\"indices\":4,\"material\":0}]}],\n"
    "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.25,0.125,1],\"metallicFactor\":0.2,"
        "\"roughnessFactor\":0.7,\"baseColorTexture\":{\"index\":0}}}],\n"
    "\"textures\":[{\"source\":0}],\n"
    "\"images\":[{\"bufferView\":8,\"mimeType\":\"image/png\"}],\n"
    "\"animations\":[{\"name\":\"bend\",\"samplers\":[{\"input\":6,\"output\":7,\"interpolation\":\"LINEAR\"}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"rotation\"}}]},\n"
    " {\"name\":\"steps\",\"samplers\":[{\"input\":8,\"output\":9,\"interpolation\":\"STEP\"}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"rotation\"}}]},\n"
    " {\"name\":\"spline\",\"samplers\":[{\"input\":6,\"output\":10,\"interpolation\":\"CUBICSPLINE\"}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}]},\n"
    " {\"name\":\"split\",\"samplers\":[{\"input\":6,\"output\":7,\"interpolation\":\"LINEAR\"},{\"input\":11,\"output\":12,\"interpolation\":\"LINEAR\"}],"
        "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"rotation\"}},{\"sampler\":1,\"target\":{\"node\":0,\"path\":\"translation\"}}]}],\n"
    "\"accessors\":[\n"
    "  {\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},\n"
    "  {\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},\n"
    "  {\"bufferView\":2,\"componentType\":5123,\"count\":4,\"type\":\"VEC4\"},\n"
    "  {\"bufferView\":3,\"componentType\":5126,\"count\":4,\"type\":\"VEC4\"},\n"
    "  {\"bufferView\":4,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"},\n"
    "  {\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},\n"
    "  {\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},\n"
    "  {\"bufferView\":7,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},\n"
    "  {\"bufferView\":9,\"componentType\":5126,\"count\":3,\"type\":\"SCALAR\"},\n"
    "  {\"bufferView\":10,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},\n"
    "  {\"bufferView\":11,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"},\n"
    "  {\"bufferView\":12,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},\n"
    "  {\"bufferView\":13,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}\n"
    "],\n"
    "\"bufferViews\":[\n"
    "  " + bv(off_pos,48) + ",\n"
    "  " + bv(off_nrm,48) + ",\n"
    "  " + bv(off_jnt,32) + ",\n"
    "  " + bv(off_wgt,64) + ",\n"
    "  " + bv(off_idx,12) + ",\n"
    "  " + bv(off_ibm,128) + ",\n"
    "  " + bv(off_tin,8) + ",\n"
    "  " + bv(off_rot,32) + ",\n"
    "  " + bv(off_png, sizeof(png)) + ",\n"
    "  " + bv(off_tin3, 12) + ",\n"
    "  " + bv(off_rot3, 48) + ",\n"
    "  " + bv(off_cub, 72) + ",\n"
    "  " + bv(off_tin2, 8) + ",\n"
    "  " + bv(off_tr2, 24) + "\n"
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
    // Validate an existing production asset without overwriting it with the
    // fixture. This also exercises the loader's real bone remapping and poses.
    if (argc == 3 && std::string(argv[1]) == "--load") {
        auto r = load_skinned_gltf(argv[2]);
        check("asset imported", r.valid);
        if (!r.valid) { std::cout << r.error << "\n"; return 1; }
        check("skinned geometry", !r.vertices.empty() && !r.indices.empty());
        check("embedded texture decoded", r.material.present && !r.material.base_color.rgba.empty());
        check("animation clips", !r.animations.empty());
        for (const auto& clip : r.animations) {
            bool sane = clip->GetDuration() > 0.0f;
            for (float fraction : {0.0f, 0.5f, 1.0f}) {
                clip->Sample(*r.skeleton, clip->GetDuration() * fraction, false);
                r.skeleton->UpdateWorldTransforms();
                for (const auto& v : r.vertices) {
                    glm::vec4 posed(0.0f);
                    for (int k = 0; k < 4; ++k) {
                        if (v.joints[k] >= r.inverse_bind.size()) { sane = false; continue; }
                        posed += v.weights[k] * r.skeleton->GetWorldMatrix(static_cast<uint16_t>(v.joints[k])) *
                                 r.inverse_bind[v.joints[k]] * glm::vec4(v.position, 1.0f);
                    }
                    sane = sane && std::isfinite(posed.x) && std::isfinite(posed.y) && std::isfinite(posed.z) &&
                           glm::length(glm::vec3(posed)) < 10000.0f;
                }
            }
            check(("pose " + clip->GetName()).c_str(), sane);
        }
        std::cout << "asset: " << r.vertices.size() << " vertices, " << r.skeleton->GetBoneCount()
                  << " bones, " << r.animations.size() << " clips\n";
        return g_fail ? 1 : 0;
    }
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
    // The material comes with it: factors, and the base colour image decoded.
    check("the material: its factors", R.material.present && approx(R.material.base_color_factor.y, 0.25f) &&
                                       approx(R.material.metallic_factor, 0.2f) && approx(R.material.roughness_factor, 0.7f));
    check("and its base colour image, decoded (an embedded PNG)",
          R.material.base_color.width == 1 && R.material.base_color.height == 1 && R.material.base_color.rgba.size() == 4 &&
              R.material.base_color.rgba[0] == 200 && R.material.base_color.rgba[1] == 100 && R.material.base_color.rgba[2] == 50);
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

    check("4 animations", R.animations.size() == 4);
    if (R.animations.size() == 4 && tip >= 0 && root >= 0) {
        auto& clip = R.animations[0];
        const auto* track = clip->GetBoneAnimation((uint16_t)tip);
        check("tip has an animation track", track != nullptr);
        check("its rotation keeps its 2 evenly spaced keys (1 a second); the unanimated position is one value",
              track && track->HasChannels() && track->RotationChannel().values.size() == 2 &&
                  approx(track->RotationChannel().rate, 1.0f) && track->PositionChannel().values.size() == 1);
        // Sampled straight into a pose (from the bind pose), the other clips:
        std::vector<schizo::renderer::BoneTransform> bind_pose, pose;
        for (const auto& bone : R.skeleton->GetAllBones()) bind_pose.push_back(bone.local_transform);
        auto at = [&](int clip_index, float t) { pose = bind_pose; R.animations[clip_index]->SampleLocal(t, false, pose); };
        at(1, 0.25f);
        const bool held = approx(pose[tip].rotation.w, 1.0f);
        at(1, 0.75f);
        check("STEP: a key is held until the next (identity at 0.25 s, 90 X at 0.75 s)",
              held && approx(pose[tip].rotation.x, 0.70710678f) && approx(pose[tip].rotation.w, 0.70710678f));
        at(2, 0.5f);
        const float mid = pose[root].position.x;
        at(2, 0.25f);
        check("CUBICSPLINE: the Hermite curve through the keys, not its tangents (x 1.0 at 0.5 s, 0.3125 at 0.25 s)",
              approx(mid, 1.0f, 2e-3f) && approx(pose[root].position.x, 0.3125f, 4e-3f));
        check("a channel lasts as long as its own keys: the clip as long as the longest (2 s)",
              approx(R.animations[3]->GetDuration(), 2.0f));
        at(3, 1.5f);
        check("...the shorter one holds its last value, the longer goes on (90 X held, y 2.25)",
              approx(pose[tip].rotation.x, 0.70710678f) && approx(pose[root].position.y, 2.25f));
        // Sample at t=1s: tip should be rotated ~90deg about X (quat w,x ~ 0.707).
        clip->Sample(*R.skeleton, 1.0f, false);
        const auto* tb = R.skeleton->GetBone((uint16_t)tip);
        glm::quat q = tb ? tb->local_transform.rotation : glm::quat(1,0,0,0);
        check("tip animated to 90deg about X at t=1",
              approx(q.w, 0.70710678f) && approx(q.x, 0.70710678f));
    }

    // Clip libraries and root motion (clip_library.h, 0.9.1): clips made on a rig
    // with its bones in another order play on the model by bone NAME; a clip's root
    // travel over a stretch of time, wrapping round a loop.
    {
        using namespace schizo::renderer;
        Skeleton model, lib;
        model.AddBone("root");
        model.AddBone("pelvis", "root");
        model.AddBone("hand", "pelvis");
        lib.AddBone("root");
        lib.AddBone("hand", "root");     // (another order, one bone fewer...)
        lib.AddBone("tail", "root");     // (...and one the model lacks)
        auto c = std::make_shared<AnimationClip>("Swing");
        auto* rb = c->AddBoneAnimation(lib.GetBone("root")->id);
        BoneTransform k0, k1;
        k1.position = glm::vec3(0.0f, 0.0f, 2.0f);   // the root carried 2 m forward over a second
        rb->AddKeyframe(0.0f, k0);
        rb->AddKeyframe(1.0f, k1);
        auto* hb = c->AddBoneAnimation(lib.GetBone("hand")->id);
        BoneTransform h0, h1;
        h1.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));
        hb->AddKeyframe(0.0f, h0);
        hb->AddKeyframe(1.0f, h1);
        c->AddBoneAnimation(lib.GetBone("tail")->id)->AddKeyframe(0.0f, h1);
        c->UpdateDuration();
        std::vector<std::shared_ptr<AnimationClip>> clips;
        clips.push_back(std::make_shared<AnimationClip>("Swing"));   // an older one of the name: replaced
        clips.push_back(std::make_shared<AnimationClip>("Idle"));
        const int added = merge_clips(clips, model, lib, {c});
        const auto& sw = clips[0];
        std::vector<BoneTransform> pose(3);
        sw->SampleLocal(1.0f, false, pose);
        const uint16_t hand = model.GetBone("hand")->id;
        check("clip library: a clip made on a rig in another order plays on the model by bone name (the hand turned "
              "90, the root moved), a bone the model lacks left out, one of the same name replaced",
              added == 1 && clips.size() == 2 && sw->GetBoneAnimations().size() == 2 && approx(pose[hand].rotation.x, 0.70710678f) &&
                  approx(pose[model.GetBone("root")->id].position.z, 2.0f));
        const uint16_t root = model.GetBone("root")->id;
        const glm::vec3 half = root_travel(*sw, root, 0.25f, 0.75f, false);
        const glm::vec3 held = root_travel(*sw, root, 0.9f, 1.4f, false);
        const glm::vec3 wrap = root_travel(*sw, root, 0.9f, 1.1f, true);
        check("root motion: 1 m between 0.25 and 0.75 s; a once clip held at its end (0.2 m from 0.9); a loop wrapping "
              "round adds the lap (0.2 + 0.2 m)",
              approx(half.z, 1.0f) && approx(held.z, 0.2f) && approx(wrap.z, 0.4f));
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
