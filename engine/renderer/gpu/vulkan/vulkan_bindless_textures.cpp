// ============================================================================
// vulkan_bindless_textures.cpp — see header.
// ============================================================================

#include "vulkan_bindless_textures.h"
#include "vulkan_device.h"

#include <spdlog/spdlog.h>

namespace gws::renderer::gpu {

std::unique_ptr<VulkanBindlessTextures> VulkanBindlessTextures::create(VulkanDevice* device) {
    if (device == nullptr || !device->bindless_supported()) return nullptr;

    std::unique_ptr<VulkanBindlessTextures> t(new VulkanBindlessTextures());
    if (!t->init(device, device->max_bindless_textures())) return nullptr;
    return t;
}

bool VulkanBindlessTextures::init(VulkanDevice* device, uint32_t capacity) {
    device_ = device;
    alloc_.reset(capacity);
    VkDevice vk = device->get_device();

    // ---- layout: one binding, `capacity` descriptors deep --------------------
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = capacity;
    // Fragment and compute both sample material textures; the vertex stage does
    // not, and widening the stage mask costs descriptor budget on some drivers.
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo flag_info{};
    flag_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flag_info.bindingCount  = 1;
    flag_info.pBindingFlags = &flags;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings    = &binding;
    // Must match the pool's flag below. A layout asking for update-after-bind
    // cannot be allocated from a pool that does not permit it, and the error
    // arrives at allocation time rather than here.
    layout_info.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layout_info.pNext        = &flag_info;

    if (vkCreateDescriptorSetLayout(vk, &layout_info, nullptr, &layout_) != VK_SUCCESS) {
        spdlog::error("VulkanBindlessTextures: descriptor set layout creation failed");
        return false;
    }

    // ---- pool ---------------------------------------------------------------
    VkDescriptorPoolSize size{};
    size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    size.descriptorCount = capacity;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets       = 1;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &size;
    pool_info.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

    if (vkCreateDescriptorPool(vk, &pool_info, nullptr, &pool_) != VK_SUCCESS) {
        spdlog::error("VulkanBindlessTextures: descriptor pool creation failed");
        return false;
    }

    // ---- the one set --------------------------------------------------------
    // VARIABLE_DESCRIPTOR_COUNT means the count is supplied here rather than
    // baked into the layout, so the set costs what was asked for instead of the
    // driver's reported maximum.
    uint32_t variable_count = capacity;
    VkDescriptorSetVariableDescriptorCountAllocateInfo count_info{};
    count_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    count_info.descriptorSetCount = 1;
    count_info.pDescriptorCounts  = &variable_count;

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = pool_;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts        = &layout_;
    alloc_info.pNext              = &count_info;

    if (vkAllocateDescriptorSets(vk, &alloc_info, &set_) != VK_SUCCESS) {
        spdlog::error("VulkanBindlessTextures: descriptor set allocation failed");
        return false;
    }

    spdlog::info("VulkanBindlessTextures ready — {} slots", capacity);
    return true;
}

uint32_t VulkanBindlessTextures::register_texture(VkImageView view, VkSampler sampler) {
    if (device_ == nullptr || view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
        return kInvalid;

    const uint32_t index = alloc_.acquire();
    if (index == kInvalid) {
        // Say so once per exhaustion rather than per texture: a full table
        // means every later texture silently has none, and a log that scrolls
        // is a log nobody reads.
        static bool warned = false;
        if (!warned) {
            warned = true;
            spdlog::warn("VulkanBindlessTextures: table full at {} slots — further textures "
                         "will render untextured", alloc_.capacity());
        }
        return kInvalid;
    }

    VkDescriptorImageInfo image{};
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.imageView   = view;
    image.sampler     = sampler;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set_;
    write.dstBinding      = 0;
    write.dstArrayElement = index;      // the slot IS the array element
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &image;

    vkUpdateDescriptorSets(device_->get_device(), 1, &write, 0, nullptr);
    return index;
}

void VulkanBindlessTextures::unregister(uint32_t index) {
    alloc_.release(index);
}

VulkanBindlessTextures::~VulkanBindlessTextures() {
    if (device_ == nullptr) return;
    VkDevice vk = device_->get_device();
    // The set is freed with the pool; freeing it separately would need
    // FREE_DESCRIPTOR_SET on the pool, which update-after-bind pools do not
    // need and which buys nothing for a pool holding exactly one set.
    if (pool_   != VK_NULL_HANDLE) vkDestroyDescriptorPool(vk, pool_, nullptr);
    if (layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vk, layout_, nullptr);
}

}  // namespace gws::renderer::gpu
