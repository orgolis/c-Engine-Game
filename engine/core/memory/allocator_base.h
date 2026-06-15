#pragma once

#include <cstddef>
#include <cstdint>

#include "memory_tags.h"
#include "memory_tracker.h"

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

    // ---- Tag-aware tracked allocation (Master Plan Stage 0.1) ----
    //
    // Opt-in: forwards to the virtual allocate()/deallocate(), and records
    // the allocation against `tag` in the MemoryTracker so usage/leaks can
    // be grouped by subsystem. No-op tracking in release builds. Works
    // through an `Allocator*` base pointer. Code that uses allocate_tracked
    // must free with deallocate_tracked so the table stays balanced.
    void* allocate_tracked(size_t size, size_t alignment, AllocationTag tag) {
        void* p = allocate(size, alignment);
        detail::track_alloc(p, size, tag);
        return p;
    }
    void deallocate_tracked(void* ptr) {
        detail::track_free(ptr);
        deallocate(ptr);
    }
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
