#pragma once

#include "allocator_base.h"
#include "memory_tags.h"

namespace gws::memory {

// ====================
// Arena (Linear / Bump) Allocator   (Pillar A1 / Master Plan Stage 0.1)
// ====================
//
// A pointer into a block: allocate(n) returns the current pointer and
// advances it by n (aligned). No per-object free — reclaim everything with
// reset(). O(1) alloc, zero fragmentation. Lighter than StackAllocator:
// no per-allocation header, so it's ideal for bulk per-scope / per-frame
// transient data (draw-list building, job scratch, command encoding).
//
// Markers (get_marker / free_to_marker) give cheap nested-scope rewind
// without StackAllocator's per-allocation bookkeeping.

class ArenaAllocator : public Allocator {
public:
    // Owns a freshly-allocated buffer of `capacity` bytes.
    explicit ArenaAllocator(size_t capacity)
        : buffer_(new uint8_t[capacity]), capacity_(capacity),
          offset_(0), peak_(0), owns_buffer_(true) {}

    // Sub-allocates from a caller-owned buffer (e.g. a slice of a larger
    // reservation). Does not take ownership.
    ArenaAllocator(void* buffer, size_t capacity)
        : buffer_(static_cast<uint8_t*>(buffer)), capacity_(capacity),
          offset_(0), peak_(0), owns_buffer_(false) {}

    ~ArenaAllocator() override { if (owns_buffer_) delete[] buffer_; }

    ArenaAllocator(const ArenaAllocator&)            = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // Returns nullptr on exhaustion so call sites can fall back to a
    // general allocator instead of crashing.
    void* allocate(size_t size, size_t alignment = 16) override {
        const size_t aligned = align_up(offset_, alignment);
        if (aligned + size > capacity_) return nullptr;  // out of arena
        void* p = buffer_ + aligned;
        offset_ = aligned + size;
        if (offset_ > peak_) peak_ = offset_;
        return p;
    }

    // Tagged overload — the tag is informational until the MemoryTracker
    // lands (later 0.1 increment); kept so call sites can be written
    // tag-aware now.
    void* allocate(size_t size, size_t alignment, AllocationTag /*tag*/) {
        return allocate(size, alignment);
    }

    template <typename T>
    T* alloc_array(size_t count) {
        return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
    }

    // No-op: individual frees aren't supported; use reset() / markers.
    void deallocate(void* /*ptr*/) override {}

    void reset() override { offset_ = 0; }

    // Nested-scope rewind.
    size_t get_marker() const { return offset_; }
    void   free_to_marker(size_t marker) { if (marker <= capacity_) offset_ = marker; }

    size_t used() const     { return offset_; }
    size_t capacity() const { return capacity_; }

    AllocationStats get_stats() const override {
        AllocationStats s;
        s.total_allocated = offset_;
        s.total_reserved  = capacity_;
        s.peak_used       = peak_;
        s.fragmentation   = 0;  // bump allocator never fragments
        return s;
    }

private:
    uint8_t* buffer_;
    size_t   capacity_;
    size_t   offset_;
    size_t   peak_;
    bool     owns_buffer_;
};

}  // namespace gws::memory
