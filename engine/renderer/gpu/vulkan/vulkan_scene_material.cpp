/**
 * @file vulkan_scene_material.cpp
 * @brief Material implementation. Allocates a host-coherent UBO for the
 *        factors and writes 5 combined-image-sampler bindings + 1 UBO
 *        binding to the descriptor set on construction.
 */

#include "vulkan_scene_material.h"
#include "vulkan_texture.h"
#include "vulkan_device.h"

#include <spdlog/spdlog.h>
#include <array>
#include <cstring>
#include <stdexcept>
#include <cstdint>

namespace gws::renderer::gpu {

VkDescriptorSetLayout Material::create_descriptor_set_layout(VkDevice device) {
    std::array<VkDescriptorSetLayoutBinding, 8> bindings{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    for (uint32_t i = 1; i < 8; ++i) {
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
        throw std::runtime_error("Material: descriptor set layout creation failed");
    }
    return layout;
}

VkDescriptorPool Material::create_descriptor_pool(VkDevice device, uint32_t max_materials) {
    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = max_materials;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = max_materials * 7;   // 5 PBR maps + 2 detail

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.maxSets       = max_materials;
    ci.poolSizeCount = static_cast<uint32_t>(sizes.size());
    ci.pPoolSizes    = sizes.data();
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &ci, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("Material: descriptor pool creation failed");
    }
    return pool;
}

Material::~Material() { destroy(); }

Material::Material(Material&& other) noexcept
    : device_(other.device_),
      pool_(other.pool_),
      descriptor_set_(other.descriptor_set_),
      ubo_(other.ubo_),
      ubo_memory_(other.ubo_memory_),
      fallback_white_(std::move(other.fallback_white_)),
      fallback_normal_(std::move(other.fallback_normal_)),
      fallback_black_(std::move(other.fallback_black_)) {
    for (int i = 0; i < 5; ++i) { tex_[i] = other.tex_[i]; other.tex_[i] = nullptr; }
    other.device_         = nullptr;
    other.pool_           = VK_NULL_HANDLE;
    other.descriptor_set_ = VK_NULL_HANDLE;
    other.ubo_            = VK_NULL_HANDLE;
    other.ubo_memory_     = VK_NULL_HANDLE;
}

Material& Material::operator=(Material&& other) noexcept {
    if (this != &other) {
        destroy();
        device_         = other.device_;
        pool_           = other.pool_;
        descriptor_set_ = other.descriptor_set_;
        ubo_            = other.ubo_;
        ubo_memory_     = other.ubo_memory_;
        fallback_white_  = std::move(other.fallback_white_);
        fallback_normal_ = std::move(other.fallback_normal_);
        fallback_black_  = std::move(other.fallback_black_);
        for (int i = 0; i < 5; ++i) { tex_[i] = other.tex_[i]; other.tex_[i] = nullptr; }
        other.device_         = nullptr;
        other.pool_           = VK_NULL_HANDLE;
        other.descriptor_set_ = VK_NULL_HANDLE;
        other.ubo_            = VK_NULL_HANDLE;
        other.ubo_memory_     = VK_NULL_HANDLE;
    }
    return *this;
}

void Material::destroy() {
    if (!device_) return;
    VkDevice vkdev = device_->get_device();

    if (descriptor_set_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(vkdev, pool_, 1, &descriptor_set_);
        descriptor_set_ = VK_NULL_HANDLE;
    }
    if (ubo_        != VK_NULL_HANDLE) { vkDestroyBuffer(vkdev, ubo_, nullptr);     ubo_ = VK_NULL_HANDLE; }
    if (ubo_memory_ != VK_NULL_HANDLE) { vkFreeMemory(vkdev, ubo_memory_, nullptr); ubo_memory_ = VK_NULL_HANDLE; }
    fallback_white_.reset();
    fallback_normal_.reset();
    fallback_black_.reset();
    device_ = nullptr;
}

std::unique_ptr<Material> Material::create(VulkanDevice* device,
                                           VkDescriptorSetLayout layout,
                                           VkDescriptorPool pool,
                                           const MaterialUniforms& params,
                                           const Texture* base_color,
                                           const Texture* normal,
                                           const Texture* metallic_roughness,
                                           const Texture* occlusion,
                                           const Texture* emissive,
                                           const Texture* default_white,
                                           const Texture* default_normal,
                                           const Texture* default_black,
                                           const Texture* detail_albedo,
                                           const Texture* detail_normal) {
    if (!device || layout == VK_NULL_HANDLE || pool == VK_NULL_HANDLE) {
        spdlog::error("Material::create: missing device/layout/pool");
        return nullptr;
    }

    auto out     = std::unique_ptr<Material>(new Material());
    out->device_ = device;
    out->pool_   = pool;
    out->params_ = params; // cache for the RT reflection pass

    VkDevice vkdev = device->get_device();

    // 1) Uniform buffer (host-coherent for simple updates)
    {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = sizeof(MaterialUniforms);
        bi.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(vkdev, &bi, nullptr, &out->ubo_) != VK_SUCCESS) {
            spdlog::error("Material: vkCreateBuffer (UBO) failed");
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
            spdlog::error("Material: vkAllocateMemory (UBO) failed");
            return nullptr;
        }
        vkBindBufferMemory(vkdev, out->ubo_, out->ubo_memory_, 0);

        void* mapped = nullptr;
        vkMapMemory(vkdev, out->ubo_memory_, 0, sizeof(MaterialUniforms), 0, &mapped);
        std::memcpy(mapped, &params, sizeof(MaterialUniforms));
        vkUnmapMemory(vkdev, out->ubo_memory_);
    }

    // 2) Allocate descriptor set
    {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &layout;
        if (vkAllocateDescriptorSets(vkdev, &ai, &out->descriptor_set_) != VK_SUCCESS) {
            spdlog::error("Material: vkAllocateDescriptorSets failed");
            return nullptr;
        }
    }

    // 3) Resolve each slot. Preference: supplied texture → shared engine-wide
    //    default (from the TextureManager) → a material-owned 1×1 fallback.
    //    Using the shared defaults avoids one 1×1 texture + sampler per
    //    material for the common "no emissive / no AO" case.
    auto pick = [&](const Texture* supplied,
                    const Texture* shared_default,
                    std::unique_ptr<Texture>& fallback,
                    auto fallback_factory) -> const Texture* {
        if (supplied)       return supplied;
        if (shared_default) return shared_default;
        if (!fallback)      fallback = fallback_factory(device);
        return fallback.get();
    };

    const Texture* base_tex   = pick(base_color,         default_white,  out->fallback_white_,  &Texture::create_default_white);
    const Texture* normal_tex = pick(normal,             default_normal, out->fallback_normal_, &Texture::create_default_normal);
    const Texture* mr_tex     = pick(metallic_roughness, default_white,  out->fallback_white_,  &Texture::create_default_white);
    const Texture* ao_tex     = pick(occlusion,          default_white,  out->fallback_white_,  &Texture::create_default_white);
    const Texture* emis_tex   = pick(emissive,           default_black,  out->fallback_black_,  &Texture::create_default_black);
    // Detail slots fall back to the same shared defaults. They are never
    // sampled meaningfully when unused, because the cache zeroes the detail
    // STRENGTHS whenever the maps are absent -- so the blend is an identity and
    // what sits in the slot cannot matter.
    const Texture* dtl_alb_tex = pick(detail_albedo, default_white,  out->fallback_white_,  &Texture::create_default_white);
    const Texture* dtl_nrm_tex = pick(detail_normal, default_normal, out->fallback_normal_, &Texture::create_default_normal);

    out->tex_[0] = base_tex;
    out->tex_[1] = normal_tex;
    out->tex_[2] = mr_tex;
    out->tex_[3] = ao_tex;
    out->tex_[4] = emis_tex;
    out->tex_[5] = dtl_alb_tex;
    out->tex_[6] = dtl_nrm_tex;

    // 4) Write descriptor set
    VkDescriptorBufferInfo bufinfo{};
    bufinfo.buffer = out->ubo_;
    bufinfo.offset = 0;
    bufinfo.range  = sizeof(MaterialUniforms);

    auto make_image_info = [](const Texture* t) {
        VkDescriptorImageInfo info{};
        info.sampler     = t->sampler();
        info.imageView   = t->view();
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return info;
    };

    std::array<VkDescriptorImageInfo, 7> imgs = {
        make_image_info(base_tex),
        make_image_info(normal_tex),
        make_image_info(mr_tex),
        make_image_info(ao_tex),
        make_image_info(emis_tex),
        make_image_info(dtl_alb_tex),
        make_image_info(dtl_nrm_tex),
    };

    std::array<VkWriteDescriptorSet, 8> writes{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = out->descriptor_set_;
    writes[0].dstBinding      = 0;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo     = &bufinfo;

    for (uint32_t i = 0; i < 7; ++i) {
        writes[i + 1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i + 1].dstSet          = out->descriptor_set_;
        writes[i + 1].dstBinding      = i + 1;
        writes[i + 1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i + 1].descriptorCount = 1;
        writes[i + 1].pImageInfo      = &imgs[i];
    }

    vkUpdateDescriptorSets(vkdev, static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
    return out;
}

void Material::rewrite_textures() {
    if (!device_ || descriptor_set_ == VK_NULL_HANDLE) return;
    for (const Texture* t : tex_) if (!t) return;  // never fully resolved

    std::array<VkDescriptorImageInfo, 5> imgs{};
    std::array<VkWriteDescriptorSet, 5>  writes{};
    for (uint32_t i = 0; i < 5; ++i) {
        imgs[i].sampler     = tex_[i]->sampler();
        imgs[i].imageView   = tex_[i]->view();
        imgs[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = descriptor_set_;
        writes[i].dstBinding      = i + 1;  // bindings 1..5 (0 is the UBO)
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo      = &imgs[i];
    }
    vkUpdateDescriptorSets(device_->get_device(),
                           static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void Material::bind(VkCommandBuffer cmd, VkPipelineLayout pipeline_layout,
                    uint32_t set_index) const {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout, set_index, 1,
                            &descriptor_set_, 0, nullptr);
}

} // namespace gws::renderer::gpu
