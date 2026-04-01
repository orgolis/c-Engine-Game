#pragma once

#include <cstddef>
#include <cstdint>

namespace gws::memory {

// ====================
// Base Allocator Interface
// ====================

// Statistics for tracking allocator health
struct AllocationStats {
    size_t total_allocated;  // Total bytes currently allocated
    size_t total_reserved;   // Total bytes reserved/capacity
    size_t allocation_count; // Number of active allocations
    size_t peak_used;        // Peak bytes used (high water mark)
    size_t fragmentation;    // Fragmentation ratio (0-100)
    
    AllocationStats() 
        : total_allocated(0), total_reserved(0), allocation_count(0), 
          peak_used(0), fragmentation(0) {}
};

// Base class for all allocators
class Allocator {
public:
    virtual ~Allocator() = default;
    
    // Allocate memory with given size and alignment (default 16 bytes)
    virtual void* allocate(size_t size, size_t alignment = 16) = 0;
    
    // Deallocate previously allocated memory
    virtual void deallocate(void* ptr) = 0;
    
    // Get statistics about allocator state
    virtual AllocationStats get_stats() const = 0;
    
    // Reset allocator to empty state (if supported)
    virtual void reset() {}
    
    // Defragment (if supported)
    virtual void defragment() {}
};

// ====================
// Helper Functions
// ====================

// Align a value up to the nearest multiple of alignment
inline size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Check if a pointer is aligned
inline bool is_aligned(const void* ptr, size_t alignment) {
    return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
}

}  // namespace gws::memory
