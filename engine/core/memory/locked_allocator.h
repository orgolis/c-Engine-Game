#pragma once

#include "allocator_base.h"
#include <mutex>

namespace gws::memory {

// ====================
// Locked Allocator (thread-safe wrapper)  (Pillar A1 / Stage 0.1)
// ====================
//
// Wraps any Allocator so alloc/free/reset are serialized by a mutex. Use it
// for Pool / General allocators shared across job threads (variable-size,
// per-object free). The FrameAllocator is partitioned per-worker and needs
// no lock — prefer it for hot per-frame transient data; reserve LockedAllocator
// for genuinely shared, longer-lived allocations.

class LockedAllocator : public Allocator {
public:
    explicit LockedAllocator(Allocator* inner) : inner_(inner) {}

    void* allocate(size_t size, size_t alignment = 16) override {
        std::lock_guard<std::mutex> lk(mutex_);
        return inner_->allocate(size, alignment);
    }
    void deallocate(void* ptr) override {
        std::lock_guard<std::mutex> lk(mutex_);
        inner_->deallocate(ptr);
    }
    void reset() override {
        std::lock_guard<std::mutex> lk(mutex_);
        inner_->reset();
    }
    void defragment() override {
        std::lock_guard<std::mutex> lk(mutex_);
        inner_->defragment();
    }
    AllocationStats get_stats() const override {
        std::lock_guard<std::mutex> lk(mutex_);
        return inner_->get_stats();
    }

private:
    Allocator*         inner_;
    mutable std::mutex mutex_;
};

}  // namespace gws::memory
