#pragma once

#include "allocator_base.h"
#include <cstring>

namespace gws::memory {

// ====================
// Stack Allocator
// Linear LIFO allocator - perfect for frame-local and temporary allocations
// Memory is freed in reverse order (LIFO). Very fast, zero fragmentation.
// ====================

class StackAllocator : public Allocator {
private:
    uint8_t* buffer;
    size_t capacity;
    size_t top;  // Current allocation pointer
    size_t peak_used;
    
    struct AllocationHeader {
        size_t size;
        size_t alignment;
    };
    
public:
    // Construct with fixed buffer capacity
    explicit StackAllocator(size_t buffer_size) 
        : capacity(buffer_size), top(0), peak_used(0) {
        buffer = new uint8_t[buffer_size];
    }
    
    ~StackAllocator() {
        delete[] buffer;
    }
    
    // Allocate memory from the top of the stack
    void* allocate(size_t size, size_t alignment = 16) override {
        // Room for header
        size_t header_size = sizeof(AllocationHeader);
        
        // Align the pointer for the header
        size_t header_pos = align_up(top, alignof(AllocationHeader));
        
        // Align the actual data after the header
        size_t data_pos = align_up(header_pos + header_size, alignment);
        
        size_t total_needed = data_pos + size;
        
        // Check if we have enough space
        if (top + (total_needed - top) > capacity) {
            return nullptr;  // Out of memory
        }
        
        // Write header before the data
        AllocationHeader* header = reinterpret_cast<AllocationHeader*>(buffer + header_pos);
        header->size = size;
        header->alignment = alignment;
        
        // Update tracking
        top = data_pos + size;
        if (top > peak_used) {
            peak_used = top;
        }
        
        return buffer + data_pos;
    }
    
    // Stack allocator doesn't support individual deallocation
    // Call reset() to clear the entire stack instead
    void deallocate(void* ptr) override {
        // Stack allocator can't deallocate individual allocations
        // User must use reset() or allow natural LIFO unwinding
        (void)ptr;  // Suppress unused warning
    }
    
    // Get statistics
    AllocationStats get_stats() const override {
        AllocationStats stats;
        stats.total_allocated = top;
        stats.total_reserved = capacity;
        stats.peak_used = peak_used;
        stats.fragmentation = 0;  // Stack has zero fragmentation
        return stats;
    }
    
    // Reset the entire stack
    void reset() override {
        top = 0;
    }
    
    // Get current top pointer (useful for markers)
    size_t get_marker() const {
        return top;
    }
    
    // Free to a previous marker (for scope-based allocation)
    void free_to_marker(size_t marker) {
        if (marker <= capacity) {
            top = marker;
        }
    }
};

}  // namespace gws::memory
