#include "vulkan_descriptor_set.h"
#include "gws/core/logging/logger.h"

namespace gws::renderer::gpu {

VulkanDescriptorSetAllocator::VulkanDescriptorSetAllocator(VkDevice device)
    : device(device) {
    GWS_LOG_INFO("✅ Descriptor set allocator initialized");
}

VulkanDescriptorSetAllocator::~VulkanDescriptorSetAllocator() {
    reset();
}

VkDescriptorSetLayout VulkanDescriptorSetAllocator::create_layout(
    const VkDescriptorSetLayoutBinding* bindings,
    uint32_t binding_count) {
    
    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = binding_count;
    layout_info.pBindings = bindings;
    
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &layout) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to create descriptor set layout");
        return VK_NULL_HANDLE;
    }
    
    GWS_LOG_DEBUG("✅ Descriptor set layout created with {} bindings", binding_count);
    return layout;
}

VkDescriptorPool VulkanDescriptorSetAllocator::create_descriptor_pool() {
    // Create pool with support for common descriptor types
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 50},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 50},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 50},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 50},
    };
    
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 8;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = 1000;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to create descriptor pool");
        return VK_NULL_HANDLE;
    }
    
    GWS_LOG_DEBUG("✅ Descriptor pool created");
    return pool;
}

VkDescriptorSet VulkanDescriptorSetAllocator::allocate(VkDescriptorSetLayout layout) {
    // Try to allocate from existing pools
    for (auto& pool_ptr : pools) {
        if (pool_ptr->allocated_sets >= pool_ptr->max_sets) {
            continue;
        }
        
        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = pool_ptr->pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &layout;
        
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        VkResult result = vkAllocateDescriptorSets(device, &alloc_info, &descriptor_set);
        
        if (result == VK_SUCCESS) {
            pool_ptr->allocated_sets++;
            return descriptor_set;
        }
    }
    
    // Create new pool if all are full
    VkDescriptorPool new_pool = create_descriptor_pool();
    if (new_pool == VK_NULL_HANDLE) {
        GWS_LOG_ERROR("Failed to create new descriptor pool");
        return VK_NULL_HANDLE;
    }
    
    auto pool_ptr = std::make_unique<DescriptorPool>();
    pool_ptr->pool = new_pool;
    
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = new_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;
    
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &alloc_info, &descriptor_set) != VK_SUCCESS) {
        GWS_LOG_ERROR("Failed to allocate descriptor set");
        return VK_NULL_HANDLE;
    }
    
    pool_ptr->allocated_sets = 1;
    pools.push_back(std::move(pool_ptr));
    
    GWS_LOG_DEBUG("Allocated descriptor set from new pool (total pools: {})", pools.size());
    return descriptor_set;
}

void VulkanDescriptorSetAllocator::reset() {
    for (auto& pool_ptr : pools) {
        if (pool_ptr->pool != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            vkResetDescriptorPool(device, pool_ptr->pool, 0);
        }
    }
    pools.clear();
    layout_cache.clear();
    GWS_LOG_DEBUG("Reset all descriptor pools");
}

}  // namespace gws::renderer::gpu
