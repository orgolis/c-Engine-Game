#pragma once

#include "allocator_base.h"

#include <cstdint>

namespace gws::memory {

// ====================
// Pool Allocator
// Fixed-size object pool - optimal for entities, particles, constraints
// All allocations are the same size. O(1) alloc/dealloc with intrusive free-list.
// ====================

class PoolAllocator : public Allocator {
private:
    struct FreeNode {
        FreeNode* next;
    };
    
    uint8_t* buffer;
    FreeNode* free_list;
    size_t object_size;
    size_t object_count;
    size_t allocated_count;
    
public:
    // Construct with object size and count
    explicit PoolAllocator(size_t obj_size, size_t obj_count)
        : object_size(obj_size), object_count(obj_count), allocated_count(0) {
        
        // Ensure minimum object size for free-list pointer
        if (object_size < sizeof(FreeNode)) {
            object_size = sizeof(FreeNode);
        }
        
        buffer = new uint8_t[object_size * object_count];
        
        // Initialize free-list: link all blocks together
        free_list = nullptr;
        for (size_t i = 0; i < object_count; ++i) {
            FreeNode* node = reinterpret_cast<FreeNode*>(buffer + i * object_size);
            node->next = free_list;
            free_list = node;
        }
    }
    
    ~PoolAllocator() {
        delete[] buffer;
    }
    
    // Allocate an object from the pool
    void* allocate(size_t size, size_t alignment = 16) override {
        // Pool allocator ignores size and alignment (fixed size)
        // Just returns next free object
        (void)size;
        (void)alignment;
        
        if (!free_list) {
            return nullptr;  // Pool exhausted
        }
        
        FreeNode* node = free_list;
        free_list = node->next;
        allocated_count++;
        
        return reinterpret_cast<void*>(node);
    }
    
    // Return object to the pool
    void deallocate(void* ptr) override {
        if (!ptr) return;
        
        FreeNode* node = reinterpret_cast<FreeNode*>(ptr);
        node->next = free_list;
        free_list = node;
        
        if (allocated_count > 0) {
            allocated_count--;
        }
    }
    
    // Get statistics
    AllocationStats get_stats() const override {
        AllocationStats stats;
        stats.total_allocated = allocated_count * object_size;
        stats.total_reserved = object_count * object_size;
        stats.allocation_count = allocated_count;
        stats.peak_used = stats.total_allocated;
        stats.fragmentation = 0;  // Pool has zero fragmentation
        return stats;
    }
    
    // Reset all objects back to the free-list
    void reset() override {
        free_list = nullptr;
        for (size_t i = 0; i < object_count; ++i) {
            FreeNode* node = reinterpret_cast<FreeNode*>(buffer + i * object_size);
            node->next = free_list;
            free_list = node;
        }
        allocated_count = 0;
    }
    
    // Check if pool is full
    bool is_full() const {
        return allocated_count >= object_count;
    }
    
    // Get utilization percentage (0-100)
    float get_utilization() const {
        if (object_count == 0) return 0.0f;
        return (static_cast<float>(allocated_count) / object_count) * 100.0f;
    }
};

}  // namespace gws::memory
