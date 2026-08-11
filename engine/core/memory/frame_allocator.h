#pragma once

#include "arena_allocator.h"
#include "alloc_worker.h"

#include <memory>
#include <vector>
#include <cstdint>

namespace gws::memory {

// ====================
// Frame Allocator (double-buffered, per-worker)  (Pillar A1 / Stage 0.1)
// ====================
//
// Per-frame transient arena that is reset once per frame and DOUBLE-
// BUFFERED (two arenas ping-pong), so frame N's results survive into frame
// N+1's first reads.
//
// THREAD-SAFE BY PARTITIONING: it holds one double-buffered arena PER
// WORKER. allocate() routes to the calling thread's worker arena (via
// gws::memory::alloc_worker_index(), set by the job system), so concurrent
// allocations from different workers never contend and need no lock.
// begin_frame() is called once by the main thread at the top of the frame
// (no jobs running) and flips every worker's buffers.
//
//   FrameAllocator g_frame(8*1024*1024, js.worker_count());
//   // top of loop (main thread):  g_frame.begin_frame();
//   // in any job on any worker:    T* p = g_frame.alloc_array<T>(n);

class FrameAllocator {
public:
    explicit FrameAllocator(size_t bytes_per_buffer, uint32_t num_workers = 1) {
        if (num_workers == 0) num_workers = 1;
        slots_.resize(num_workers);
        for (Slot& s : slots_) {
            s.a       = std::make_unique<ArenaAllocator>(bytes_per_buffer);
            s.b       = std::make_unique<ArenaAllocator>(bytes_per_buffer);
            s.current = s.a.get();
            s.previous = s.b.get();
        }
    }

    FrameAllocator(const FrameAllocator&)            = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    // Main thread only, top of frame: flip + reset every worker's buffer.
    void begin_frame() {
        for (Slot& s : slots_) {
            ArenaAllocator* tmp = s.current;
            s.current  = s.previous;
            s.previous = tmp;
            s.current->reset();
        }
        ++frame_;
    }

    void* allocate(size_t size, size_t alignment = 16) {
        return slot().current->allocate(size, alignment);
    }
    void* allocate(size_t size, size_t alignment, AllocationTag tag) {
        return slot().current->allocate(size, alignment, tag);
    }
    template <typename T>
    T* alloc_array(size_t count) {
        return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
    }

    ArenaAllocator& current()  { return *slot().current; }
    ArenaAllocator& previous() { return *slot().previous; }
    uint64_t        frame() const { return frame_; }
    uint32_t        worker_count() const { return static_cast<uint32_t>(slots_.size()); }

    // Aggregate used bytes across all worker buffers (this frame).
    size_t total_used() const {
        size_t n = 0;
        for (const Slot& s : slots_) n += s.current->used();
        return n;
    }

private:
    struct Slot {
        std::unique_ptr<ArenaAllocator> a;
        std::unique_ptr<ArenaAllocator> b;
        ArenaAllocator* current  = nullptr;
        ArenaAllocator* previous = nullptr;
    };
    Slot& slot() {
        uint32_t i = alloc_worker_index();
        if (i >= slots_.size()) i = 0;  // out-of-range guard
        return slots_[i];
    }

    std::vector<Slot> slots_;
    uint64_t          frame_ = 0;
};

}  // namespace gws::memory
