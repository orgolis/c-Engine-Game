#include "skinned_gltf_loader.h"

#include "tinygltf.hpp"

#include <spdlog/spdlog.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace gws::renderer::gpu {
namespace {

using schizo::renderer::Skeleton;
using schizo::renderer::AnimationClip;
using schizo::renderer::BoneTransform;
using schizo::renderer::Bone;

// ---- stride-aware accessor readers (handle interleaved + tightly-packed) ----

// Read a FLOAT accessor into a flat float array (count * componentCount).
std::vector<float> read_floats(const tinygltf::Model& m, int accessor) {
    std::vector<float> out;
    if (accessor < 0 || accessor >= (int)m.accessors.size()) return out;
    const auto& acc = m.accessors[accessor];
    if (acc.bufferView < 0 || acc.bufferView >= (int)m.bufferViews.size()) return out;
    const auto& bv = m.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)m.buffers.size()) return out;
    const auto& buf = m.buffers[bv.buffer];
    const int comps = tinygltf::Model::GetAccessorComponentCount(acc);
    const int elem  = comps * 4;  // float
    const int stride = bv.byteStride > 0 ? bv.byteStride : elem;
    const size_t start = (size_t)bv.byteOffset + acc.byteOffset;
    out.resize((size_t)acc.count * comps);
    for (int i = 0; i < acc.count; ++i) {
        const size_t off = start + (size_t)i * stride;
        if (off + elem > buf.data.size()) { out.clear(); break; }
        std::memcpy(&out[(size_t)i * comps], buf.data.data() + off, elem);
    }
    return out;
}

// Read a 4-component unsigned accessor (JOINTS_0: u8/u16/u32) as uvec4.
std::vector<glm::uvec4> read_uvec4(const tinygltf::Model& m, int accessor) {
    std::vector<glm::uvec4> out;
    if (accessor < 0 || accessor >= (int)m.accessors.size()) return out;
    const auto& acc = m.accessors[accessor];
    if (acc.bufferView < 0 || acc.bufferView >= (int)m.bufferViews.size()) return out;
    const auto& bv = m.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)m.buffers.size()) return out;
    const auto& buf = m.buffers[bv.buffer];
    const int csize = tinygltf::Model::GetAccessorComponentSize(acc);
    const int elem  = 4 * csize;
    const int stride = bv.byteStride > 0 ? bv.byteStride : elem;
    const size_t start = (size_t)bv.byteOffset + acc.byteOffset;
    out.assign(acc.count, glm::uvec4(0));
    for (int i = 0; i < acc.count; ++i) {
        const uint8_t* p = buf.data.data() + start + (size_t)i * stride;
        if (start + (size_t)i * stride + elem > buf.data.size()) break;
        for (int k = 0; k < 4; ++k) {
            uint32_t v = 0;
            if (acc.componentType == 5121)      v = p[k];                                     // U8
            else if (acc.componentType == 5123) v = ((const uint16_t*)p)[k];                  // U16
            else if (acc.componentType == 5125) v = ((const uint32_t*)p)[k];                  // U32
            out[i][k] = v;
        }
    }
    return out;
}

// Read an index accessor (u8/u16/u32) as uint32.
std::vector<uint32_t> read_indices(const tinygltf::Model& m, int accessor) {
    std::vector<uint32_t> out;
    if (accessor < 0 || accessor >= (int)m.accessors.size()) return out;
    const auto& acc = m.accessors[accessor];
    if (acc.bufferView < 0 || acc.bufferView >= (int)m.bufferViews.size()) return out;
    const auto& bv = m.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)m.buffers.size()) return out;
    const auto& buf = m.buffers[bv.buffer];
    const int csize = tinygltf::Model::GetAccessorComponentSize(acc);
    const size_t start = (size_t)bv.byteOffset + acc.byteOffset;
    out.resize(acc.count);
    for (int i = 0; i < acc.count; ++i) {
        const uint8_t* p = buf.data.data() + start + (size_t)i * csize;
        if (start + (size_t)i * csize + csize > buf.data.size()) { out.clear(); break; }
        if (csize == 1)      out[i] = p[0];
        else if (csize == 2) out[i] = *(const uint16_t*)p;
        else                 out[i] = *(const uint32_t*)p;
    }
    return out;
}

std::vector<glm::mat4> read_mat4s(const tinygltf::Model& m, int accessor) {
    std::vector<glm::mat4> out;
    std::vector<float> f = read_floats(m, accessor);
    const size_t n = f.size() / 16;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = glm::make_mat4(&f[i * 16]);
    return out;
}

// glTF node local transform → BoneTransform (TRS, or decomposed matrix).
BoneTransform node_local(const tinygltf::Node& n) {
    BoneTransform t;
    if (n.matrix.size() == 16) {
        glm::mat4 M = glm::make_mat4(n.matrix.data());
        glm::vec3 s(glm::length(glm::vec3(M[0])),
                    glm::length(glm::vec3(M[1])),
                    glm::length(glm::vec3(M[2])));
        t.position = glm::vec3(M[3]);
        glm::mat3 R(glm::vec3(M[0]) / (s.x > 1e-6f ? s.x : 1.0f),
                    glm::vec3(M[1]) / (s.y > 1e-6f ? s.y : 1.0f),
                    glm::vec3(M[2]) / (s.z > 1e-6f ? s.z : 1.0f));
        t.rotation = glm::normalize(glm::quat_cast(R));
        t.scale    = s;
    } else {
        t.position = glm::vec3(n.translation[0], n.translation[1], n.translation[2]);
        // glm::quat ctor is (w, x, y, z); glTF stores (x, y, z, w).
        t.rotation = glm::quat(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]);
        t.scale    = glm::vec3(n.scale[0], n.scale[1], n.scale[2]);
    }
    return t;
}

// Sample a VEC3 animation sampler at time t (LINEAR).
glm::vec3 sample_vec3(const tinygltf::Model& m, const tinygltf::AnimationSampler& s,
                      float t, glm::vec3 fallback) {
    std::vector<float> in = read_floats(m, s.input);
    std::vector<float> out = read_floats(m, s.output);
    if (in.empty() || out.size() < in.size() * 3) return fallback;
    auto V = [&](size_t k) { return glm::vec3(out[k*3], out[k*3+1], out[k*3+2]); };
    if (t <= in.front()) return V(0);
    if (t >= in.back())  return V(in.size() - 1);
    for (size_t i = 0; i + 1 < in.size(); ++i)
        if (t >= in[i] && t <= in[i+1]) {
            float a = (in[i+1] > in[i]) ? (t - in[i]) / (in[i+1] - in[i]) : 0.0f;
            return glm::mix(V(i), V(i+1), a);
        }
    return fallback;
}

// Sample a VEC4 quaternion animation sampler at time t (SLERP).
glm::quat sample_quat(const tinygltf::Model& m, const tinygltf::AnimationSampler& s,
                      float t, glm::quat fallback) {
    std::vector<float> in = read_floats(m, s.input);
    std::vector<float> out = read_floats(m, s.output);
    if (in.empty() || out.size() < in.size() * 4) return fallback;
    auto Q = [&](size_t k) { return glm::quat(out[k*4+3], out[k*4+0], out[k*4+1], out[k*4+2]); };
    if (t <= in.front()) return Q(0);
    if (t >= in.back())  return Q(in.size() - 1);
    for (size_t i = 0; i + 1 < in.size(); ++i)
        if (t >= in[i] && t <= in[i+1]) {
            float a = (in[i+1] > in[i]) ? (t - in[i]) / (in[i+1] - in[i]) : 0.0f;
            return glm::normalize(glm::slerp(Q(i), Q(i+1), a));
        }
    return fallback;
}

} // namespace

LoadedSkinnedModel load_skinned_gltf(const std::string& path) {
    LoadedSkinnedModel R;

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    std::string ext;
    if (auto dot = path.find_last_of('.'); dot != std::string::npos) ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });

    bool ok = (ext == ".glb")
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!ok) { R.error = err.empty() ? ("cannot load " + path) : err; return R; }

    if (model.skins.empty())        { R.error = "no skin in glTF (not a rigged asset)"; return R; }
    const tinygltf::Skin& skin = model.skins[0];
    if (skin.joints.empty())        { R.error = "skin has no joints"; return R; }

    // The mesh node references this skin. Fall back to any mesh node.
    int mesh_index = -1;
    for (const auto& n : model.nodes) if (n.skin == 0 && n.mesh >= 0) { mesh_index = n.mesh; break; }
    if (mesh_index < 0)
        for (const auto& n : model.nodes) if (n.mesh >= 0) { mesh_index = n.mesh; break; }
    if (mesh_index < 0 || mesh_index >= (int)model.meshes.size()) { R.error = "no mesh for skin"; return R; }

    // ---- Skeleton from skin.joints (parents added before children) ----
    R.skeleton = std::make_shared<Skeleton>();
    std::vector<int> node_parent(model.nodes.size(), -1);
    for (int ni = 0; ni < (int)model.nodes.size(); ++ni)
        for (int c : model.nodes[ni].children)
            if (c >= 0 && c < (int)node_parent.size()) node_parent[c] = ni;

    std::set<int> joint_set(skin.joints.begin(), skin.joints.end());
    std::map<int, int> node2bone;  // glTF node index -> engine bone id

    auto uname = [&](int node_idx) {
        const std::string& base = model.nodes[node_idx].name;
        return (base.empty() ? std::string("joint") : base) + "__" + std::to_string(node_idx);
    };
    auto add_joint = [&](int node_idx) {
        int pn = node_parent[node_idx];
        std::string parent_name = (pn >= 0 && joint_set.count(pn)) ? uname(pn) : "";
        uint16_t id = R.skeleton->AddBone(uname(node_idx), parent_name, node_local(model.nodes[node_idx]));
        node2bone[node_idx] = id;
    };

    std::set<int> added;
    bool progress = true;
    while (added.size() < joint_set.size() && progress) {
        progress = false;
        for (int jn : skin.joints) {
            if (added.count(jn)) continue;
            int pn = node_parent[jn];
            const bool parent_ready = (pn < 0) || !joint_set.count(pn) || added.count(pn);
            if (parent_ready) { add_joint(jn); added.insert(jn); progress = true; }
        }
    }
    if (node2bone.size() != joint_set.size()) {  // cycle guard — add leftovers as roots
        for (int jn : skin.joints)
            if (!node2bone.count(jn)) { uint16_t id = R.skeleton->AddBone(uname(jn), "", node_local(model.nodes[jn])); node2bone[jn] = id; }
    }
    R.skeleton->UpdateWorldTransforms();

    // ---- inverse-bind matrices (per bone) ----
    R.inverse_bind.assign(R.skeleton->GetBoneCount(), glm::mat4(1.0f));
    std::vector<glm::mat4> ibm;
    if (skin.inverseBindMatrices >= 0) ibm = read_mat4s(model, skin.inverseBindMatrices);
    std::vector<int> jointpos2bone(skin.joints.size(), 0);  // JOINTS_0 value -> bone id
    for (size_t p = 0; p < skin.joints.size(); ++p) {
        auto it = node2bone.find(skin.joints[p]);
        int bone = (it != node2bone.end()) ? it->second : 0;
        jointpos2bone[p] = bone;
        if (p < ibm.size()) R.inverse_bind[bone] = ibm[p];
        else                R.inverse_bind[bone] = glm::inverse(R.skeleton->GetWorldMatrix((uint16_t)bone));
    }

    // ---- vertices (all primitives of the mesh, concatenated) ----
    const tinygltf::Mesh& mesh = model.meshes[mesh_index];
    for (const auto& prim : mesh.primitives) {
        auto find = [&](const char* k) -> int {
            auto it = prim.attributes.find(k);
            return it != prim.attributes.end() ? it->second : -1;
        };
        std::vector<float> pos = read_floats(model, find("POSITION"));
        if (pos.empty()) continue;
        const size_t vcount = pos.size() / 3;
        std::vector<float>      nrm = read_floats(model, find("NORMAL"));
        std::vector<float>      uv  = read_floats(model, find("TEXCOORD_0"));
        std::vector<float>      tan = read_floats(model, find("TANGENT"));
        std::vector<glm::uvec4> jnt = read_uvec4(model, find("JOINTS_0"));
        std::vector<float>      wgt = read_floats(model, find("WEIGHTS_0"));

        const uint32_t base = (uint32_t)R.vertices.size();
        for (size_t i = 0; i < vcount; ++i) {
            SkinnedVertex v;
            v.position = glm::vec3(pos[i*3], pos[i*3+1], pos[i*3+2]);
            v.normal   = (nrm.size() >= (i+1)*3) ? glm::vec3(nrm[i*3], nrm[i*3+1], nrm[i*3+2]) : glm::vec3(0,1,0);
            v.uv       = (uv.size()  >= (i+1)*2) ? glm::vec2(uv[i*2], uv[i*2+1]) : glm::vec2(0);
            v.tangent  = (tan.size() >= (i+1)*4) ? glm::vec4(tan[i*4], tan[i*4+1], tan[i*4+2], tan[i*4+3]) : glm::vec4(1,0,0,1);

            const glm::uvec4 j = (i < jnt.size()) ? jnt[i] : glm::uvec4(0);
            for (int k = 0; k < 4; ++k)
                v.joints[k] = (j[k] < jointpos2bone.size()) ? (uint32_t)jointpos2bone[j[k]] : 0u;

            glm::vec4 w = (wgt.size() >= (i+1)*4) ? glm::vec4(wgt[i*4], wgt[i*4+1], wgt[i*4+2], wgt[i*4+3])
                                                  : glm::vec4(1,0,0,0);
            const float ws = w.x + w.y + w.z + w.w;
            v.weights = (ws > 1e-6f) ? (w / ws) : glm::vec4(1,0,0,0);
            R.vertices.push_back(v);
        }

        std::vector<uint32_t> pidx = read_indices(model, prim.indices);
        if (pidx.empty()) for (uint32_t i = 0; i < vcount; ++i) R.indices.push_back(base + i);
        else              for (uint32_t i : pidx)               R.indices.push_back(base + i);
    }
    if (R.vertices.empty()) { R.error = "skinned mesh has no vertices"; return R; }

    // ---- animations (glTF samplers/channels → AnimationClip per animation) ----
    for (const auto& ga : model.animations) {
        auto clip = std::make_shared<AnimationClip>(ga.name.empty() ? "clip" : ga.name, 30.0f);
        struct Chans { int t = -1, r = -1, s = -1; };
        std::map<int, Chans> per_bone;  // bone id -> its T/R/S samplers
        for (const auto& ch : ga.channels) {
            auto it = node2bone.find(ch.target.node);
            if (it == node2bone.end()) continue;                    // not a joint
            if (ch.sampler < 0 || ch.sampler >= (int)ga.samplers.size()) continue;
            Chans& c = per_bone[it->second];
            if      (ch.target.path == "translation") c.t = ch.sampler;
            else if (ch.target.path == "rotation")    c.r = ch.sampler;
            else if (ch.target.path == "scale")       c.s = ch.sampler;
        }
        for (auto& [bone, c] : per_bone) {
            const Bone* bb = R.skeleton->GetBone((uint16_t)bone);
            const BoneTransform bind = bb ? bb->local_transform : BoneTransform{};
            std::set<float> times;
            auto add_times = [&](int samp) {
                if (samp < 0) return;
                for (float t : read_floats(model, ga.samplers[samp].input)) times.insert(t);
            };
            add_times(c.t); add_times(c.r); add_times(c.s);
            if (times.empty()) continue;
            auto* track = clip->AddBoneAnimation((uint16_t)bone);
            for (float t : times) {
                BoneTransform bt = bind;
                if (c.t >= 0) bt.position = sample_vec3(model, ga.samplers[c.t], t, bind.position);
                if (c.r >= 0) bt.rotation = sample_quat(model, ga.samplers[c.r], t, bind.rotation);
                if (c.s >= 0) bt.scale    = sample_vec3(model, ga.samplers[c.s], t, bind.scale);
                track->AddKeyframe(t, bt);
            }
        }
        clip->SetLooping(true);
        clip->UpdateDuration();
        R.animations.push_back(clip);
    }

    R.valid = true;
    spdlog::info("[skinned-gltf] '{}' loaded: {} verts, {} bones, {} anim(s)",
                 path, R.vertices.size(), R.skeleton->GetBoneCount(), R.animations.size());
    return R;
}

} // namespace gws::renderer::gpu
