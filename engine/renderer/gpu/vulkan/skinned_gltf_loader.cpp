#include "skinned_gltf_loader.h"

#include "tinygltf.hpp"
#include <stb_image.h>

#include <spdlog/spdlog.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <cstdint>

namespace gws::renderer::gpu {
namespace {

using schizo::renderer::Skeleton;
using schizo::renderer::AnimationClip;
using schizo::renderer::BoneTransform;
using schizo::renderer::Bone;
using schizo::renderer::UniformTrack;
using schizo::renderer::Keyframe;

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

// A glTF animation sampler evaluated exactly at time t, as the spec has it:
// STEP holds a key, LINEAR interpolates (slerp for rotations), CUBICSPLINE is a
// Hermite curve through the keys with their in- and out-tangents (the output
// holds in-tangent, value, out-tangent per key). `comps` 3 (a vector) or 4 (a
// quaternion, x y z w). Returns false when the data does not fit the sampler.
enum class SamplerMode { Linear, Step, Cubic };
bool eval_sampler(const std::vector<float>& in, const std::vector<float>& out, SamplerMode mode, int comps, float t,
                  glm::vec4& result) {
    const size_t n = in.size();
    const size_t stride = mode == SamplerMode::Cubic ? static_cast<size_t>(comps) * 3 : static_cast<size_t>(comps);
    const size_t voff = mode == SamplerMode::Cubic ? static_cast<size_t>(comps) : 0;
    if (n == 0 || out.size() < n * stride) return false;
    auto get = [&](size_t k, size_t off) {
        glm::vec4 v(0.0f, 0.0f, 0.0f, comps == 4 ? 1.0f : 0.0f);
        for (int c = 0; c < comps; ++c) v[c] = out[k * stride + off + static_cast<size_t>(c)];
        return v;
    };
    if (t <= in.front() || n == 1) { result = get(0, voff); return true; }
    if (t >= in.back()) { result = get(n - 1, voff); return true; }
    const size_t i = static_cast<size_t>(std::upper_bound(in.begin(), in.end(), t) - in.begin()) - 1;
    const float dt = in[i + 1] - in[i];
    const float s = dt > 1e-9f ? (t - in[i]) / dt : 0.0f;
    if (mode == SamplerMode::Step) { result = get(i, voff); return true; }
    if (mode == SamplerMode::Linear) {
        const glm::vec4 a = get(i, voff), b = get(i + 1, voff);
        if (comps == 4) {
            const glm::quat q = glm::slerp(glm::quat(a.w, a.x, a.y, a.z), glm::quat(b.w, b.x, b.y, b.z), s);
            result = glm::vec4(q.x, q.y, q.z, q.w);
        } else {
            result = a + (b - a) * s;
        }
        return true;
    }
    const glm::vec4 v0 = get(i, voff), b0 = get(i, voff + static_cast<size_t>(comps)), v1 = get(i + 1, voff), a1 = get(i + 1, 0);
    const float s2 = s * s, s3 = s2 * s;
    result = (2 * s3 - 3 * s2 + 1) * v0 + (s3 - 2 * s2 + s) * dt * b0 + (-2 * s3 + 3 * s2) * v1 + (s3 - s2) * dt * a1;
    return true;
}
SamplerMode sampler_mode(const std::string& interpolation) {
    if (interpolation == "STEP") return SamplerMode::Step;
    if (interpolation == "CUBICSPLINE") return SamplerMode::Cubic;
    return SamplerMode::Linear;
}
// The rate a channel is kept at: its own when its keys are evenly spaced at 60 a
// second or fewer and it is not a spline (then its keys are kept exactly), else 60.
float channel_rate(const std::vector<float>& in, SamplerMode mode) {
    constexpr float kMax = 60.0f;
    if (in.size() < 2 || mode == SamplerMode::Cubic) return kMax;
    const float h = (in.back() - in.front()) / static_cast<float>(in.size() - 1);
    if (h < 1.0f / kMax - 1e-5f || h <= 0.0f) return kMax;
    if (std::abs(in.front() - std::round(in.front() / h) * h) > h * 0.01f) return kMax;   // keys on the rate's grid from 0
    for (size_t k = 1; k < in.size(); ++k)
        if (std::abs((in[k] - in[k - 1]) - h) > h * 0.01f) return kMax;
    return 1.0f / h;
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
    // An accessor is shared across many keyframes and often across channels.
    // Decode each one once for this import instead of copying it at every sample.
    std::map<int,std::vector<float>> samples;
    auto values = [&](int accessor) -> const std::vector<float>& {
        auto [it, inserted] = samples.try_emplace(accessor);
        if (inserted) it->second = read_floats(model, accessor);
        return it->second;
    };
    size_t clip_bytes_new = 0, clip_bytes_old = 0;
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
            // Each channel on its own: resampled exactly onto a uniform rate (its
            // own when its keys already are), a channel that never moves kept as
            // one value, a missing one as the bind pose.
            auto vec_channel = [&](int samp, const glm::vec3& rest) {
                UniformTrack<glm::vec3> tr;
                tr.values.push_back(rest);
                if (samp < 0) return tr;
                const auto& sp = ga.samplers[samp];
                const auto& in = values(sp.input);
                const auto& out = values(sp.output);
                const SamplerMode mode = sampler_mode(sp.interpolation);
                if (in.empty()) return tr;
                tr.rate = channel_rate(in, mode);
                tr.step = mode == SamplerMode::Step;
                const size_t n = static_cast<size_t>(std::lround(in.back() * tr.rate)) + 1;
                std::vector<glm::vec3> v;
                v.reserve(n);
                glm::vec4 r;
                for (size_t k = 0; k < n; ++k)
                    v.push_back(eval_sampler(in, out, mode, 3, static_cast<float>(k) / tr.rate, r) ? glm::vec3(r) : rest);
                bool constant = true;
                for (const auto& x : v) constant = constant && glm::length(x - v[0]) <= 1e-6f * (1.0f + glm::length(v[0]));
                tr.values = constant ? std::vector<glm::vec3>{v[0]} : std::move(v);
                return tr;
            };
            UniformTrack<glm::quat> rot;
            rot.values.push_back(bind.rotation);
            if (c.r >= 0) {
                const auto& sp = ga.samplers[c.r];
                const auto& in = values(sp.input);
                const auto& out = values(sp.output);
                const SamplerMode mode = sampler_mode(sp.interpolation);
                if (!in.empty()) {
                    rot.rate = channel_rate(in, mode);
                    rot.step = mode == SamplerMode::Step;
                    const size_t n = static_cast<size_t>(std::lround(in.back() * rot.rate)) + 1;
                    std::vector<glm::quat> v;
                    v.reserve(n);
                    glm::vec4 r;
                    for (size_t k = 0; k < n; ++k) {
                        glm::quat q = eval_sampler(in, out, mode, 4, static_cast<float>(k) / rot.rate, r)
                                          ? glm::normalize(glm::quat(r.w, r.x, r.y, r.z)) : bind.rotation;
                        if (!v.empty() && glm::dot(q, v.back()) < 0.0f) q = -q;   // one hemisphere: no flips between samples
                        v.push_back(q);
                    }
                    bool constant = true;
                    for (const auto& q : v) constant = constant && std::abs(glm::dot(q, v[0])) >= 1.0f - 1e-7f;
                    rot.values = constant ? std::vector<glm::quat>{v[0]} : std::move(v);
                }
            }
            auto* track = clip->AddBoneAnimation((uint16_t)bone);
            track->SetChannels(vec_channel(c.t, bind.position), std::move(rot), vec_channel(c.s, bind.scale));
            clip_bytes_new += track->DataBytes();
            // (what the keys merged over every channel took, for the report)
            std::set<float> times;
            for (int samp : {c.t, c.r, c.s}) if (samp >= 0) for (float t : values(ga.samplers[samp].input)) times.insert(t);
            clip_bytes_old += times.size() * sizeof(Keyframe);
        }
        clip->UpdateDuration();
        // The clip lasts as long as its longest channel's keys (a channel that ends
        // early holds its last value).
        {
            float longest = clip->GetDuration();
            for (const auto& ch : ga.channels)
                if (ch.sampler >= 0 && ch.sampler < (int)ga.samplers.size()) {
                    const auto& in = values(ga.samplers[ch.sampler].input);
                    if (!in.empty()) longest = std::max(longest, in.back());
                }
            clip->SetDuration(longest);
        }
        clip->SetLooping(true);
        R.animations.push_back(clip);
    }

    // ---- material: the first primitive's, with its images decoded ----
    {
        const tinygltf::Mesh& mm = model.meshes[mesh_index];
        const int mi = mm.primitives.empty() ? -1 : mm.primitives[0].material;
        if (mi >= 0 && mi < static_cast<int>(model.materials.size())) {
            const tinygltf::Material& gm = model.materials[mi];
            LoadedSkinnedMaterial& M = R.material;
            M.present = true;
            const auto& f = gm.pbrMetallicRoughness.baseColorFactor;
            if (f.size() == 4) M.base_color_factor = glm::vec4(f[0], f[1], f[2], f[3]);
            M.metallic_factor  = static_cast<float>(gm.pbrMetallicRoughness.metallicFactor);
            M.roughness_factor = static_cast<float>(gm.pbrMetallicRoughness.roughnessFactor);
            auto decode = [&](int tex_index, LoadedImage& out) {
                if (tex_index < 0 || tex_index >= static_cast<int>(model.textures.size())) return;
                const int src = model.textures[tex_index].source;
                if (src < 0 || src >= static_cast<int>(model.images.size())) return;
                const tinygltf::Image& img = model.images[src];
                if (img.bufferView < 0 || img.bufferView >= static_cast<int>(model.bufferViews.size())) return;
                const auto& view = model.bufferViews[img.bufferView];
                if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) return;
                const auto& buf = model.buffers[view.buffer];
                if (view.byteOffset + view.byteLength > buf.data.size()) return;
                int w = 0, h = 0, comp = 0;
                stbi_uc* px = stbi_load_from_memory(buf.data.data() + view.byteOffset, static_cast<int>(view.byteLength),
                                                    &w, &h, &comp, STBI_rgb_alpha);
                if (!px) return;
                out.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
                out.width = static_cast<uint32_t>(w);
                out.height = static_cast<uint32_t>(h);
                stbi_image_free(px);
            };
            decode(gm.pbrMetallicRoughness.baseColorTexture.index, M.base_color);
            decode(gm.pbrMetallicRoughness.metallicRoughnessTexture.index, M.metallic_roughness);
            decode(gm.normalTexture.index, M.normal);
            decode(gm.emissiveTexture.index, M.emissive);
            if (gm.emissiveFactor.size() == 3)
                M.emissive_factor = glm::vec3(gm.emissiveFactor[0], gm.emissiveFactor[1], gm.emissiveFactor[2]);
        }
    }

    R.valid = true;
    spdlog::info("[skinned-gltf] '{}' loaded: {} verts, {} bones, {} anim(s) ({} KB of motion; {} KB as merged keys), material {}{}",
                 path, R.vertices.size(), R.skeleton->GetBoneCount(), R.animations.size(), (clip_bytes_new + 1023) / 1024,
                 (clip_bytes_old + 1023) / 1024, R.material.present ? "yes" : "no", R.material.base_color.empty() ? "" : " (textured)");
    return R;
}

} // namespace gws::renderer::gpu
