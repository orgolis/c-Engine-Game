/**
 * @file vulkan_gltf_loader.cpp
 * @brief glTF loader implementation using the in-tree mini-tinygltf and
 *        stb_image. Common-case glTF only (static meshes, separate
 *        accessors, uint16/uint32 indices, no skinning/animation, no
 *        sparse accessors, no data-URI images).
 */

#include "vulkan_gltf_loader.h"
#include "vulkan_device.h"
#include "vulkan_scene_mesh.h"
#include "vulkan_scene_material.h"
#include "vulkan_texture.h"
#include "vulkan_texture_manager.h"

#include "tinygltf.hpp"
#include <stb_image.h>

#include <spdlog/spdlog.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace gws::renderer::gpu {

Scene::~Scene() = default;

namespace {

// glTF componentType constants
constexpr int kCompUnsignedShort = 5123;
constexpr int kCompUnsignedInt   = 5125;
constexpr int kCompFloat         = 5126;

// Pull a typed pointer to the start of an accessor's data, given the
// already-extracted bytes from `Model::GetAccessorData`.
template <typename T>
const T* as(const std::vector<uint8_t>& bytes) {
    return reinterpret_cast<const T*>(bytes.data());
}

// Given a glTF image entry, decode its pixels via stb_image and return
// (rgba_pixels, width, height). On success, caller MUST `stbi_image_free`.
struct DecodedImage {
    stbi_uc* pixels = nullptr;
    int      width  = 0;
    int      height = 0;
};

DecodedImage decode_glb_image(const tinygltf::Model& model,
                              const tinygltf::Image& img,
                              const std::string& gltf_dir) {
    DecodedImage out;
    int comp = 0;

    if (img.bufferView >= 0 && img.bufferView < (int)model.bufferViews.size()) {
        // Embedded in a glTF buffer view (common with .glb).
        const auto& view = model.bufferViews[img.bufferView];
        if (view.buffer < 0 || view.buffer >= (int)model.buffers.size()) return out;
        const auto& buf  = model.buffers[view.buffer];
        if (view.byteOffset + view.byteLength > (int)buf.data.size())   return out;

        const uint8_t* src = buf.data.data() + view.byteOffset;
        out.pixels = stbi_load_from_memory(src, view.byteLength,
                                           &out.width, &out.height, &comp,
                                           STBI_rgb_alpha);
    } else if (!img.uri.empty()) {
        // External file relative to the .gltf.
        const auto path = std::filesystem::path(gltf_dir) / img.uri;
        out.pixels = stbi_load(path.string().c_str(),
                               &out.width, &out.height, &comp, STBI_rgb_alpha);
    }

    if (!out.pixels) {
        spdlog::warn("glTF: failed to decode image '{}': {}",
                     img.uri.empty() ? img.name : img.uri,
                     stbi_failure_reason() ? stbi_failure_reason() : "(no reason)");
    }
    return out;
}

glm::mat4 build_node_transform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        // Column-major; glm matches.
        return glm::make_mat4(node.matrix.data());
    }
    glm::vec3 t = node.translation.size() == 3
                  ? glm::vec3(node.translation[0], node.translation[1], node.translation[2])
                  : glm::vec3(0.0f);
    glm::quat r = node.rotation.size() == 4
                  ? glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2])
                  : glm::quat(1, 0, 0, 0);
    glm::vec3 s = node.scale.size() == 3
                  ? glm::vec3(node.scale[0], node.scale[1], node.scale[2])
                  : glm::vec3(1.0f);
    return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
}

// glTF wrap mode (GL enum) → Vulkan address mode.
VkSamplerAddressMode gltf_wrap(int mode) {
    switch (mode) {
        case 33071: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;    // CLAMP_TO_EDGE
        case 33648: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;  // MIRRORED_REPEAT
        case 10497: default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;  // REPEAT
    }
}

// glTF min filter (GL enum) → (Vulkan min filter, mipmap mode). glTF encodes
// both the minification filter and the mip filter in one enum.
void gltf_min_filter(int mode, VkFilter& out_filter, VkSamplerMipmapMode& out_mip) {
    switch (mode) {
        case 9728: out_filter = VK_FILTER_NEAREST; out_mip = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // NEAREST
        case 9729: out_filter = VK_FILTER_LINEAR;  out_mip = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // LINEAR
        case 9984: out_filter = VK_FILTER_NEAREST; out_mip = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // NEAREST_MIPMAP_NEAREST
        case 9985: out_filter = VK_FILTER_LINEAR;  out_mip = VK_SAMPLER_MIPMAP_MODE_NEAREST; break; // LINEAR_MIPMAP_NEAREST
        case 9986: out_filter = VK_FILTER_NEAREST; out_mip = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break; // NEAREST_MIPMAP_LINEAR
        case 9987: out_filter = VK_FILTER_LINEAR;  out_mip = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break; // LINEAR_MIPMAP_LINEAR
        default:   out_filter = VK_FILTER_LINEAR;  out_mip = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break;
    }
}

} // namespace

std::unique_ptr<Scene> GltfLoader::load(VulkanDevice* device,
                                        VkDescriptorSetLayout material_layout,
                                        VkDescriptorPool material_pool,
                                        const std::string& path,
                                        TextureManager* textures) {
    if (!device) {
        spdlog::error("GltfLoader::load: null device");
        return nullptr;
    }
    if (!std::filesystem::exists(path)) {
        spdlog::error("GltfLoader::load: file not found: {}", path);
        return nullptr;
    }

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    bool ok = false;
    const std::string ext = std::filesystem::path(path).extension().string();
    if (ext == ".glb" || ext == ".GLB") {
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    } else {
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }
    if (!ok) {
        spdlog::error("GltfLoader::load({}): {}", path, err);
        return nullptr;
    }
    if (!warn.empty()) {
        spdlog::warn("GltfLoader::load({}): {}", path, warn);
    }

    const std::string gltf_dir = std::filesystem::path(path).parent_path().string();

    auto scene = std::make_unique<Scene>();

    // 1) Build one Texture per glTF *texture* (image + sampler), decided per
    //    usage: baseColor/emissive are sRGB, normal/MR/occlusion are linear.
    //    (The same image referenced by two textures with different sampler or
    //    colour space yields two GPU textures — correct, if rare.)

    // 1a) Classify each texture index's colour space by scanning materials.
    std::vector<bool> tex_is_srgb(model.textures.size(), false);
    auto mark_srgb = [&](int ti) {
        if (ti >= 0 && ti < (int)tex_is_srgb.size()) tex_is_srgb[ti] = true;
    };
    for (const auto& gm : model.materials) {
        mark_srgb(gm.pbrMetallicRoughness.baseColorTexture.index);
        mark_srgb(gm.emissiveTexture.index);
        // normalTexture / metallicRoughnessTexture / occlusionTexture stay linear.
    }

    // 1b) Build a SamplerDesc from a glTF sampler index (defaults when -1).
    auto sampler_desc_for = [&](int sampler_index) -> SamplerDesc {
        SamplerDesc d;
        d.max_anisotropy = 16.0f;  // clamped to device limit by the cache
        if (sampler_index >= 0 && sampler_index < (int)model.samplers.size()) {
            const auto& s = model.samplers[sampler_index];
            d.address_u = gltf_wrap(s.wrapS);
            d.address_v = gltf_wrap(s.wrapT);
            d.address_w = d.address_u;
            d.mag_filter = (s.magFilter == 9728) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            gltf_min_filter(s.minFilter, d.min_filter, d.mipmap_mode);
        }
        return d;
    };

    // 1c) Upload each glTF texture.
    scene->textures.resize(model.textures.size());
    for (size_t ti = 0; ti < model.textures.size(); ++ti) {
        const auto& gt = model.textures[ti];
        if (gt.source < 0 || gt.source >= (int)model.images.size()) continue;
        const tinygltf::Image& img = model.images[gt.source];
        const bool        srgb = tex_is_srgb[ti];
        const SamplerDesc sd   = sampler_desc_for(gt.sampler);

        const bool external_file = !img.uri.empty() && img.bufferView < 0 &&
                                   img.uri.rfind("data:", 0) != 0;

        std::shared_ptr<Texture> tex;
        if (textures && external_file) {
            // Route external files through the manager: dedup across scenes,
            // cooked-`.ctex` support, hot reload.
            TextureImportSettings st;
            st.srgb     = srgb;
            st.gen_mips = true;
            st.sampler  = sd;
            const std::string p =
                (std::filesystem::path(gltf_dir) / img.uri).lexically_normal().generic_string();
            tex = textures->load(p, st);
        } else {
            // Embedded/data-URI image, or no manager: decode + upload directly,
            // borrowing a cached sampler when a manager is present.
            DecodedImage di = decode_glb_image(model, img, gltf_dir);
            if (di.pixels) {
                const VkSampler s = textures ? textures->sampler(sd) : VK_NULL_HANDLE;
                tex = Texture::create_from_pixels(device, di.pixels,
                                                  static_cast<uint32_t>(di.width),
                                                  static_cast<uint32_t>(di.height),
                                                  srgb, /*gen_mips=*/true, s);
                stbi_image_free(di.pixels);
            }
        }
        scene->textures[ti] = std::move(tex);
    }

    // Helper: glTF Texture index → our Texture* (or nullptr).
    auto resolve_tex = [&](int tex_index) -> const Texture* {
        if (tex_index < 0 || tex_index >= (int)scene->textures.size()) return nullptr;
        return scene->textures[tex_index].get();
    };

    // Shared engine-wide defaults for unbound material slots (avoids one 1×1
    // texture + sampler per material). Null when no manager was supplied.
    const Texture* def_white  = textures ? textures->white()  : nullptr;
    const Texture* def_normal = textures ? textures->normal() : nullptr;
    const Texture* def_black  = textures ? textures->black()  : nullptr;

    // 2) Build materials.
    // Track per-material alpha mode so the DrawItems we emit later know
    // whether to route through the forward transparent pass.
    std::vector<bool> material_is_blend;
    material_is_blend.reserve(model.materials.size());
    scene->materials.reserve(model.materials.size());
    for (const auto& gm : model.materials) {
        MaterialUniforms u{};
        u.base_color_factor = glm::vec4(gm.pbrMetallicRoughness.baseColorFactor[0],
                                         gm.pbrMetallicRoughness.baseColorFactor[1],
                                         gm.pbrMetallicRoughness.baseColorFactor[2],
                                         gm.pbrMetallicRoughness.baseColorFactor[3]);
        u.metallic_factor    = gm.pbrMetallicRoughness.metallicFactor;
        u.roughness_factor   = gm.pbrMetallicRoughness.roughnessFactor;
        u.occlusion_strength = gm.occlusionTexture.strength;
        u.normal_scale       = gm.normalTexture.scale;
        // emissive_factor.a doubles as alpha_cutoff. MASK uses the file's
        // value, BLEND gets 0.5 so it casts binarised shadows (the shadow
        // pass shares this field). OPAQUE leaves it at 0.
        const float cutoff = (gm.alphaMode == "MASK")  ? gm.alphaCutoff
                          : (gm.alphaMode == "BLEND") ? 0.5f
                          : 0.0f;
        u.emissive_factor    = glm::vec4(gm.emissiveFactor[0],
                                          gm.emissiveFactor[1],
                                          gm.emissiveFactor[2],
                                          cutoff);

        material_is_blend.push_back(gm.alphaMode == "BLEND");

        auto mat = Material::create(device, material_layout, material_pool, u,
                                    resolve_tex(gm.pbrMetallicRoughness.baseColorTexture.index),
                                    resolve_tex(gm.normalTexture.index),
                                    resolve_tex(gm.pbrMetallicRoughness.metallicRoughnessTexture.index),
                                    resolve_tex(gm.occlusionTexture.index),
                                    resolve_tex(gm.emissiveTexture.index),
                                    def_white, def_normal, def_black);
        scene->materials.push_back(std::move(mat));
    }
    // Always have at least one fallback material so meshes with material=-1
    // still draw with sensible defaults.
    if (scene->materials.empty()) {
        scene->materials.push_back(
            Material::create(device, material_layout, material_pool, MaterialUniforms{},
                             nullptr, nullptr, nullptr, nullptr, nullptr,
                             def_white, def_normal, def_black));
    }

    // 3) Build meshes (one Mesh per glTF mesh; submeshes per primitive).
    scene->meshes.reserve(model.meshes.size());
    for (const auto& gmesh : model.meshes) {
        std::vector<SceneVertex> vertices;
        std::vector<uint32_t>    indices;
        std::vector<Submesh>     submeshes;

        for (const auto& prim : gmesh.primitives) {
            const uint32_t vert_base = static_cast<uint32_t>(vertices.size());
            const uint32_t index_base = static_cast<uint32_t>(indices.size());

            // Required: POSITION accessor.
            auto pos_it = prim.attributes.find("POSITION");
            if (pos_it == prim.attributes.end()) continue;
            const auto& pos_accessor = model.accessors[pos_it->second];
            const auto pos_bytes     = model.GetAccessorData(pos_it->second);
            if (pos_accessor.type != "VEC3" || pos_accessor.componentType != kCompFloat) continue;

            const size_t vcount = static_cast<size_t>(pos_accessor.count);

            // Optional accessors.
            std::vector<uint8_t> norm_bytes, uv_bytes, tan_bytes;
            const tinygltf::Accessor* norm_acc = nullptr;
            const tinygltf::Accessor* uv_acc   = nullptr;
            const tinygltf::Accessor* tan_acc  = nullptr;

            if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end()) {
                norm_acc   = &model.accessors[it->second];
                norm_bytes = model.GetAccessorData(it->second);
            }
            if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end()) {
                uv_acc   = &model.accessors[it->second];
                uv_bytes = model.GetAccessorData(it->second);
            }
            if (auto it = prim.attributes.find("TANGENT"); it != prim.attributes.end()) {
                tan_acc   = &model.accessors[it->second];
                tan_bytes = model.GetAccessorData(it->second);
            }

            const float* pos_p  = as<float>(pos_bytes);
            const float* norm_p = !norm_bytes.empty() ? as<float>(norm_bytes) : nullptr;
            const float* uv_p   = !uv_bytes.empty()   ? as<float>(uv_bytes)   : nullptr;
            const float* tan_p  = !tan_bytes.empty()  ? as<float>(tan_bytes)  : nullptr;

            vertices.reserve(vertices.size() + vcount);
            for (size_t i = 0; i < vcount; ++i) {
                SceneVertex v{};
                v.position = {pos_p[i * 3 + 0], pos_p[i * 3 + 1], pos_p[i * 3 + 2]};
                v.normal   = norm_p ? glm::vec3{norm_p[i * 3 + 0], norm_p[i * 3 + 1], norm_p[i * 3 + 2]}
                                    : glm::vec3{0.0f, 1.0f, 0.0f};
                v.uv       = uv_p ? glm::vec2{uv_p[i * 2 + 0], uv_p[i * 2 + 1]} : glm::vec2{0.0f};
                v.tangent  = tan_p ? glm::vec4{tan_p[i * 4 + 0], tan_p[i * 4 + 1],
                                                 tan_p[i * 4 + 2], tan_p[i * 4 + 3]}
                                    : glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
                vertices.push_back(v);
            }

            // Indices: required for indexed draws. Re-index against vert_base
            // so multiple primitives can share one VBO.
            if (prim.indices < 0 || prim.indices >= (int)model.accessors.size()) continue;
            const auto& idx_accessor = model.accessors[prim.indices];
            const auto  idx_bytes    = model.GetAccessorData(prim.indices);
            const size_t icount = static_cast<size_t>(idx_accessor.count);
            indices.reserve(indices.size() + icount);

            if (idx_accessor.componentType == kCompUnsignedShort) {
                const uint16_t* p = reinterpret_cast<const uint16_t*>(idx_bytes.data());
                for (size_t i = 0; i < icount; ++i) indices.push_back(vert_base + p[i]);
            } else if (idx_accessor.componentType == kCompUnsignedInt) {
                const uint32_t* p = reinterpret_cast<const uint32_t*>(idx_bytes.data());
                for (size_t i = 0; i < icount; ++i) indices.push_back(vert_base + p[i]);
            } else {
                spdlog::warn("glTF: unsupported index component type {}, primitive skipped",
                             idx_accessor.componentType);
                vertices.resize(vert_base); // rollback the vertex inserts
                continue;
            }

            Submesh sm{};
            sm.material_index = (prim.material >= 0)
                                ? static_cast<uint32_t>(prim.material)
                                : 0u;
            // LOD 0 = the source primitive. `Mesh::generate_lods` (below)
            // appends decimated tiers in place.
            MeshLod lod0{};
            lod0.index_offset       = index_base;
            lod0.index_count        = static_cast<uint32_t>(icount);
            lod0.distance_threshold = 0.0f;
            sm.lods.push_back(lod0);
            submeshes.push_back(std::move(sm));
        }

        // Generate 3 progressively-decimated LODs per submesh, switching
        // tiers at 5 / 15 / 30 world-units distance. These thresholds are
        // arbitrary defaults — a real scene system should compute them
        // from per-mesh bounding-sphere radii. Skip generation for very
        // small meshes (meshopt has nothing to simplify).
        if (vertices.size() >= 24 && indices.size() >= 64) {
            const std::vector<float> lod_ratios   = { 0.50f, 0.25f, 0.10f };
            const std::vector<float> lod_distances = { 5.0f, 15.0f, 30.0f };
            Mesh::generate_lods(vertices, indices, submeshes, lod_ratios, lod_distances);
        }

        scene->meshes.push_back(Mesh::create(device, vertices, indices, std::move(submeshes)));
    }

    // 4) Walk nodes (flat for now, no hierarchy composition) and emit one
    //    DrawItem per submesh.
    for (const auto& node : model.nodes) {
        if (node.mesh < 0 || node.mesh >= (int)scene->meshes.size()) continue;
        const auto* mesh = scene->meshes[node.mesh].get();
        if (!mesh) continue;
        const glm::mat4 xform = build_node_transform(node);

        for (size_t si = 0; si < mesh->submeshes().size(); ++si) {
            const uint32_t mat_idx = mesh->submeshes()[si].material_index;
            const Material* mat = (mat_idx < scene->materials.size())
                                  ? scene->materials[mat_idx].get()
                                  : (scene->materials.empty() ? nullptr : scene->materials[0].get());
            DrawItem d{};
            d.mesh          = mesh;
            d.material      = mat;
            d.model         = xform;
            d.submesh_index = static_cast<uint32_t>(si);
            d.is_blend      = (mat_idx < material_is_blend.size()) && material_is_blend[mat_idx];
            scene->draw_items.push_back(d);
        }
    }

    // If no nodes existed (e.g. headless meshes-only file), synthesise one
    // DrawItem per submesh of every loaded mesh at identity transform.
    if (scene->draw_items.empty()) {
        for (const auto& mesh_ptr : scene->meshes) {
            if (!mesh_ptr) continue;
            for (size_t si = 0; si < mesh_ptr->submeshes().size(); ++si) {
                const uint32_t mat_idx = mesh_ptr->submeshes()[si].material_index;
                const Material* mat = (mat_idx < scene->materials.size())
                                      ? scene->materials[mat_idx].get()
                                      : (scene->materials.empty() ? nullptr : scene->materials[0].get());
                DrawItem d{};
                d.mesh          = mesh_ptr.get();
                d.material      = mat;
                d.model         = glm::mat4(1.0f);
                d.submesh_index = static_cast<uint32_t>(si);
                d.is_blend      = (mat_idx < material_is_blend.size()) && material_is_blend[mat_idx];
                scene->draw_items.push_back(d);
            }
        }
    }

    spdlog::info("GltfLoader: loaded {} ({} meshes, {} materials, {} textures, {} draws)",
                 path,
                 scene->meshes.size(), scene->materials.size(),
                 scene->textures.size(), scene->draw_items.size());

    return scene;
}

} // namespace gws::renderer::gpu
