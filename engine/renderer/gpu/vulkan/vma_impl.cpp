// The one translation unit that compiles the Vulkan Memory Allocator.
//
// VMA (third_party/vma/include/vk_mem_alloc.h, v3.1.0, MIT, AMD) replaces a
// vkAllocateMemory per mesh buffer with sub-allocations out of large blocks.
// Drivers commonly cap live allocations at 4096 (maxMemoryAllocationCount), so
// one allocation per asset was a ceiling on project size the engine did not
// know it had (PERFORMANCE_AUDIT.md F9). Function pointers are fetched at run
// time (VMA_DYNAMIC_VULKAN_FUNCTIONS, set by the vma target).
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
