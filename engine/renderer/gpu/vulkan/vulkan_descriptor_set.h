#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_map>

namespace gws::renderer::gpu {

/**
 * @brief Manages Vulkan descriptor sets and pools
 * 
 * Handles allocation of descriptor pools with growth strategy.
 * Each pool manages a set of descriptor sets that can be rapidly allocated/freed.
 */
class VulkanDescriptorSetAllocator {
public:
    VulkanDescriptorSetAllocator(VkDevice device);
    ~VulkanDescriptorSetAllocator();
    
    /**
     * @brief Create a descriptor set layout
     * @param bindings Array of VkDescriptorSetLayoutBinding
     * @param binding_count Number of bindings
     * @return Descriptor set layout handle
     */
    VkDescriptorSetLayout create_layout(
        const VkDescriptorSetLayoutBinding* bindings,
        uint32_t binding_count);
    
    /**
     * @brief Allocate a descriptor set from a pool
     * @param layout Descriptor set layout
     * @return Allocated descriptor set
     */
    VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    
    /**
     * @brief Free all descriptor sets (reset pools)
     */
    void reset();
    
    /**
     * @brief Get the device
     */
    VkDevice get_device() const { return device; }

private:
    struct DescriptorPool {
        VkDescriptorPool pool = VK_NULL_HANDLE;
        uint32_t allocated_sets = 0;
        uint32_t max_sets = 1000;
    };
    
    VkDescriptorPool create_descriptor_pool();
    
    VkDevice device;
    std::vector<std::unique_ptr<DescriptorPool>> pools;
    std::unordered_map<VkDescriptorSetLayout, std::vector<VkDescriptorSet>> layout_cache;
};

}  // namespace gws::renderer::gpu
