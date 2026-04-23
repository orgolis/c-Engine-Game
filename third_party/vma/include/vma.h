#pragma once

// Vulkan Memory Allocator (VMA) - Version 3.0.1
// https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
//
// This is a wrapper header that includes VMA with proper configuration
// for the GameWorldshaper engine.

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cmath>
#include <limits>
#include <atomic>
#include <algorithm>
#include <vector>
#include <unordered_map>

// VMA Configuration
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_STATIC_VULKAN_FUNCTIONS 0

// Use inline SPIR-V compilation
#define VMA_LEAK_DETECTION 1

// Include VMA implementation
// Note: The actual VMA implementation will be included from the single-header
// For now, we'll define the essential VMA structures here

namespace gws::renderer::gpu {

// Forward declare VMA types (actual implementation from vk_mem_alloc.h)
typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;
typedef struct VmaAllocationInfo {
    uint32_t memoryType;
    VkDeviceMemory deviceMemory;
    VkDeviceSize offset;
    VkDeviceSize size;
    void* pMappedData;
    void* pUserData;
} VmaAllocationInfo;

typedef struct VmaAllocationCreateInfo {
    VmaAllocationCreateFlagBits flags;
    VmaMemoryUsage usage;
    VkMemoryPropertyFlags requiredFlags;
    VkMemoryPropertyFlags preferredFlags;
    uint32_t memoryTypeBits;
    VmaPool pool;
    void* pUserData;
    float priority;
} VmaAllocationCreateInfo;

enum VmaAllocationCreateFlagBits {
    VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT = 0x00000001,
    VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT = 0x00000002,
    VMA_ALLOCATION_CREATE_MAPPED_BIT = 0x00000004,
    VMA_ALLOCATION_CREATE_USER_DATA_COPY_BIT = 0x00000020,
    VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT = 0x00000040,
    VMA_ALLOCATION_CREATE_DONT_BIND_MEMORY_BIT = 0x00000080,
    VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT = 0x00010000,
    VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT = 0x00020000,
    VMA_ALLOCATION_CREATE_STRATEGY_MIN_OFFSET_BIT = 0x00040000,
    VMA_ALLOCATION_CREATE_STRATEGY_OPTIMAL_BIT = 0x00000000,
};

enum VmaMemoryUsage {
    VMA_MEMORY_USAGE_UNKNOWN = 0,
    VMA_MEMORY_USAGE_GPU_ONLY = 1,
    VMA_MEMORY_USAGE_CPU_ONLY = 2,
    VMA_MEMORY_USAGE_CPU_TO_GPU = 3,
    VMA_MEMORY_USAGE_GPU_TO_CPU = 4,
    VMA_MEMORY_USAGE_CPU_COPY = 5,
    VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED = 6,
};

typedef struct VmaAllocatorCreateInfo {
    VmaAllocatorCreateFlagBits flags;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDeviceSize preferredLargeHeapBlockSize;
    const VkAllocationCallbacks* pAllocationCallbacks;
    PFN_vkGetInstanceProcAddr pVkGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr pVkGetDeviceProcAddr;
    VkInstance instance;
    uint32_t vulkanApiVersion;
} VmaAllocatorCreateInfo;

enum VmaAllocatorCreateFlagBits {
    VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT = 0x00000001,
    VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT = 0x00000002,
    VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT = 0x00000004,
    VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT = 0x00000008,
    VMA_ALLOCATOR_CREATE_AMD_DEVICE_COHERENT_MEMORY_BIT = 0x00000010,
    VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT = 0x00000020,
    VMA_ALLOCATOR_CREATE_DISABLE_BLOCK_STATISTICS_BIT = 0x00000040,
};

// VMA function declarations (will be dynamically loaded)
VkResult vmaCreateAllocator(const VmaAllocatorCreateInfo* pCreateInfo,
                            VmaAllocator* pAllocator);

void vmaDestroyAllocator(VmaAllocator allocator);

VkResult vmaAllocateMemory(VmaAllocator allocator,
                          const VkMemoryRequirements* pVkMemoryRequirements,
                          const VmaAllocationCreateInfo* pCreateInfo,
                          VmaAllocation* pAllocation,
                          VmaAllocationInfo* pAllocationInfo);

void vmaFreeMemory(VmaAllocator allocator, VmaAllocation allocation);

VkResult vmaMapMemory(VmaAllocator allocator, VmaAllocation allocation,
                     void** ppData);

void vmaUnmapMemory(VmaAllocator allocator, VmaAllocation allocation);

VkResult vmaAllocateBufferMemory(VmaAllocator allocator,
                                const VkBuffer buffer,
                                const VmaAllocationCreateInfo* pCreateInfo,
                                VmaAllocation* pAllocation,
                                VmaAllocationInfo* pAllocationInfo);

VkResult vmaAllocateImageMemory(VmaAllocator allocator,
                               const VkImage image,
                               const VmaAllocationCreateInfo* pCreateInfo,
                               VmaAllocation* pAllocation,
                               VmaAllocationInfo* pAllocationInfo);

void vmaGetAllocationInfo(VmaAllocator allocator, VmaAllocation allocation,
                         VmaAllocationInfo* pAllocationInfo);

}  // namespace gws::renderer::gpu
