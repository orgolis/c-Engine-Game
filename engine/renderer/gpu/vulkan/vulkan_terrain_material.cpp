#include "vulkan_terrain_material.h"

#include "vulkan_device.h"
#include "vulkan_texture.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace gws::renderer::gpu {

namespace {
/// binding 0 is the UBO; 1 is the splat map; 2..13 are the per-layer maps.
constexpr uint32_t kUboBinding    = 0;
constexpr uint32_t kSplatBinding  = 1;
constexpr uint32_t kFirstImage    = 1;   // splat is the first image binding
constexpr uint32_t kImageCount    = 1 + kTerrainMaterialLayers * 3;   // 13
constexpr uint32_t kTotalBindings = 1 + kImageCount;                  // 14

/// Binding index for layer `i`'s map. Kept as functions rather than open-coded
/// arithmetic because the shader's binding numbers and these must agree
/// exactly, and a mismatch does not error — it samples the wrong texture.
constexpr uint32_t albedo_binding(int i) { return 2 + static_cast<uint32_t>(i); }
constexpr uint32_t normal_binding(int i) { return 6 + static_cast<uint32_t>(i); }
constexpr uint32_t mr_binding(int i)     { return 10 + static_cast<uint32_t>(i); }
}  // namespace

VkDescriptorSetLayout TerrainMaterial::create_descriptor_set_layout(VkDevice device) {
    std::vector<VkDescriptorSetLayoutBinding> bindings(kTotalBindings);
    bindings[kUboBinding].binding         = kUboBinding;
    bindings[kUboBinding].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[kUboBinding].descriptorCount = 1;
    bindings[kUboBinding].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    for (uint32_t i = kFirstImage; i < kTotalBindings; ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("TerrainMaterial: descriptor set layout creation failed");
    }
    return layout;
}

VkDescriptorPool TerrainMaterial::create_descriptor_pool(VkDevice device,
                                                         uint32_t max_materials) {
    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = max_materials;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = max_materials * kImageCount;

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = max_materials;
    ci.poolSizeCount = static_cast<uint32_t>(sizes.size());
    ci.pPoolSizes    = sizes.data();
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &ci, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("TerrainMaterial: descriptor pool creation failed");
    }
    return pool;
}

TerrainMaterial::~TerrainMaterial() { destroy(); }

TerrainMaterial::TerrainMaterial(TerrainMaterial&& o) noexcept
    : device_(o.device_), pool_(o.pool_), descriptor_set_(o.descriptor_set_),
      ubo_(o.ubo_), ubo_memory_(o.ubo_memory_), ubo_mapped_(o.ubo_mapped_),
      params_(o.params_) {
    for (int i = 0; i < kBindingCount; ++i) { tex_[i] = o.tex_[i]; o.tex_[i] = nullptr; }
    o.device_         = nullptr;
    o.pool_           = VK_NULL_HANDLE;
    o.descriptor_set_ = VK_NULL_HANDLE;
    o.ubo_            = VK_NULL_HANDLE;
    o.ubo_memory_     = VK_NULL_HANDLE;
    o.ubo_mapped_     = nullptr;
}

TerrainMaterial& TerrainMaterial::operator=(TerrainMaterial&& o) noexcept {
    if (this != &o) {
        destroy();
        device_         = o.device_;
        pool_           = o.pool_;
        descriptor_set_ = o.descriptor_set_;
        ubo_            = o.ubo_;
        ubo_memory_     = o.ubo_memory_;
        ubo_mapped_     = o.ubo_mapped_;
        params_         = o.params_;
        for (int i = 0; i < kBindingCount; ++i) { tex_[i] = o.tex_[i]; o.tex_[i] = nullptr; }
        o.device_         = nullptr;
        o.pool_           = VK_NULL_HANDLE;
        o.descriptor_set_ = VK_NULL_HANDLE;
        o.ubo_            = VK_NULL_HANDLE;
        o.ubo_memory_     = VK_NULL_HANDLE;
        o.ubo_mapped_     = nullptr;
    }
    return *this;
}

void TerrainMaterial::destroy() {
    if (!device_) return;
    VkDevice vkdev = device_->get_device();

    if (ubo_mapped_ != nullptr && ubo_memory_ != VK_NULL_HANDLE) {
        vkUnmapMemory(vkdev, ubo_memory_);
        ubo_mapped_ = nullptr;
    }
    if (descriptor_set_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(vkdev, pool_, 1, &descriptor_set_);
        descriptor_set_ = VK_NULL_HANDLE;
    }
    if (ubo_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkdev, ubo_, nullptr);
        ubo_ = VK_NULL_HANDLE;
    }
    if (ubo_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vkdev, ubo_memory_, nullptr);
        ubo_memory_ = VK_NULL_HANDLE;
    }
    device_ = nullptr;
}

std::unique_ptr<TerrainMaterial> TerrainMaterial::create(
    VulkanDevice* device,
    VkDescriptorSetLayout layout,
    VkDescriptorPool pool,
    const TerrainMaterialUniforms& params,
    const Texture* splat,
    const std::array<TerrainLayerTextures, kTerrainMaterialLayers>& layers,
    const Texture* default_white,
    const Texture* default_normal)
{
    if (!device || layout == VK_NULL_HANDLE || pool == VK_NULL_HANDLE) {
        spdlog::error("TerrainMaterial::create: missing device/layout/pool");
        return nullptr;
    }
    // The splat map is the one texture with no sensible default. Substituting
    // white would give every layer full weight everywhere, which blends all four
    // into mud and looks like a painting bug rather than a missing texture.
    if (!splat) {
        spdlog::error("TerrainMaterial::create: no splat map — refusing to build a "
                      "material that would blend every layer at full weight");
        return nullptr;
    }
    if (!default_white || !default_normal) {
        spdlog::error("TerrainMaterial::create: shared default textures are required");
        return nullptr;
    }

    auto out     = std::unique_ptr<TerrainMaterial>(new TerrainMaterial());
    out->device_ = device;
    out->pool_   = pool;
    out->params_ = params;

    VkDevice vkdev = device->get_device();

    // 1) Uniform buffer, host-coherent and PERSISTENTLY mapped. Terrain factors
    //    are tuned with sliders, so this buffer is rewritten far more often than
    //    a scene material's; mapping once removes a map/unmap pair from every
    //    frame of a drag.
    {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = sizeof(TerrainMaterialUniforms);
        bi.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(vkdev, &bi, nullptr, &out->ubo_) != VK_SUCCESS) {
            spdlog::error("TerrainMaterial: vkCreateBuffer (UBO) failed");
            return nullptr;
        }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(vkdev, out->ubo_, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = device->find_memory_type(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(vkdev, &ai, nullptr, &out->ubo_memory_) != VK_SUCCESS) {
            spdlog::error("TerrainMaterial: vkAllocateMemory (UBO) failed");
            return nullptr;
        }
        vkBindBufferMemory(vkdev, out->ubo_, out->ubo_memory_, 0);
        if (vkMapMemory(vkdev, out->ubo_memory_, 0, sizeof(TerrainMaterialUniforms),
                        0, &out->ubo_mapped_) != VK_SUCCESS) {
            spdlog::error("TerrainMaterial: vkMapMemory (UBO) failed");
            return nullptr;
        }
        std::memcpy(out->ubo_mapped_, &params, sizeof(TerrainMaterialUniforms));
    }

    // 2) Descriptor set
    {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &layout;
        if (vkAllocateDescriptorSets(vkdev, &ai, &out->descriptor_set_) != VK_SUCCESS) {
            spdlog::error("TerrainMaterial: vkAllocateDescriptorSets failed "
                          "(terrain descriptor pool exhausted?)");
            return nullptr;
        }
    }

    // 3) Resolve slots. Each default is the IDENTITY for its map: white leaves
    //    albedo and metal/rough factors untouched, and flat blue unpacks to
    //    (0,0,1) — no perturbation. That is what lets an unbound slot cost
    //    nothing instead of needing a branch in the shader.
    out->tex_[0] = splat;
    for (int i = 0; i < kTerrainMaterialLayers; ++i) {
        out->tex_[1 + i] = layers[i].albedo ? layers[i].albedo : default_white;
        out->tex_[1 + kTerrainMaterialLayers + i] =
            layers[i].normal ? layers[i].normal : default_normal;
        out->tex_[1 + kTerrainMaterialLayers * 2 + i] =
            layers[i].metallic_roughness ? layers[i].metallic_roughness : default_white;
    }

    out->rewrite_textures();
    return out;
}

void TerrainMaterial::update_uniforms(const TerrainMaterialUniforms& params) {
    params_ = params;
    if (ubo_mapped_ != nullptr)
        std::memcpy(ubo_mapped_, &params, sizeof(TerrainMaterialUniforms));
}

void TerrainMaterial::rewrite_textures() {
    if (!device_ || descriptor_set_ == VK_NULL_HANDLE) return;
    VkDevice vkdev = device_->get_device();

    VkDescriptorBufferInfo bufinfo{};
    bufinfo.buffer = ubo_;
    bufinfo.offset = 0;
    bufinfo.range  = sizeof(TerrainMaterialUniforms);

    std::array<VkDescriptorImageInfo, kBindingCount> imgs{};
    for (int i = 0; i < kBindingCount; ++i) {
        if (!tex_[i]) return;   // never write a half-populated set
        imgs[i].sampler     = tex_[i]->sampler();
        imgs[i].imageView   = tex_[i]->view();
        imgs[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(kTotalBindings);

    VkWriteDescriptorSet ubo_write{};
    ubo_write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ubo_write.dstSet          = descriptor_set_;
    ubo_write.dstBinding      = kUboBinding;
    ubo_write.descriptorCount = 1;
    ubo_write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_write.pBufferInfo     = &bufinfo;
    writes.push_back(ubo_write);

    // Binding order must match the shader exactly: 1 splat, 2..5 albedo,
    // 6..9 normal, 10..13 metal/rough. A mismatch here does not error — it
    // samples a normal map as albedo, which reads as "the terrain turned blue".
    auto push_image = [&](uint32_t binding, int slot) {
        VkWriteDescriptorSet w{};
        w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet          = descriptor_set_;
        w.dstBinding      = binding;
        w.descriptorCount = 1;
        w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo      = &imgs[slot];
        writes.push_back(w);
    };
    push_image(kSplatBinding, 0);
    for (int i = 0; i < kTerrainMaterialLayers; ++i) {
        push_image(albedo_binding(i), 1 + i);
        push_image(normal_binding(i), 1 + kTerrainMaterialLayers + i);
        push_image(mr_binding(i),     1 + kTerrainMaterialLayers * 2 + i);
    }

    vkUpdateDescriptorSets(vkdev, static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
}

void TerrainMaterial::bind(VkCommandBuffer cmd, VkPipelineLayout pipeline_layout,
                           uint32_t set_index) const {
    if (descriptor_set_ == VK_NULL_HANDLE) return;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                            set_index, 1, &descriptor_set_, 0, nullptr);
}

}  // namespace gws::renderer::gpu
