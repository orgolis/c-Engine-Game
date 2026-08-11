#pragma once

#include "allocator_base.h"

#include <cstdint>

namespace gws::memory {

// ====================
// Segregated-Fit Allocator  (Pillar A1 / Master Plan Stage 0.1)
// ====================
//
// O(1)-ish variable-size allocator: free blocks are bucketed into size-class
// free lists (bins by floor(log2 size)), so finding a fit is a bin lookup
// instead of the O(n) linear scan the basic GeneralAllocator does. Adjacent
// free blocks coalesce on free via an explicit physical doubly-linked list,
// keeping fragmentation bounded.
//
// This is the "size-segregated free-lists + coalescing" design the master
// plan calls for. (A production deployment may still prefer a vendored TLSF
// / mimalloc; this is a clean, self-contained, well-tested step up from
// linear first-fit.)
//
// Not thread-safe — wrap in LockedAllocator for shared use across jobs.

class SegregatedAllocator : public Allocator {
public:
    explicit SegregatedAllocator(size_t capacity)
        : capacity_(capacity), used_(0), peak_(0) {
        buffer_ = new uint8_t[capacity];
        for (auto& b : bins_) b = nullptr;
        // One free block spanning the whole buffer.
        Block* first    = reinterpret_cast<Block*>(buffer_);
        first->size     = capacity - HEADER;
        first->free     = true;
        first->phys_prev = nullptr;
        first->phys_next = nullptr;
        bin_push(first);
    }
    ~SegregatedAllocator() override { delete[] buffer_; }

    SegregatedAllocator(const SegregatedAllocator&)            = delete;
    SegregatedAllocator& operator=(const SegregatedAllocator&) = delete;

    void* allocate(size_t size, size_t alignment = 16) override {
        if (size == 0) return nullptr;
        (void)alignment;                       // payloads are 16-aligned (HEADER%16==0)
        size = align_up(size, 16);

        Block* b = find_fit(size);
        if (!b) return nullptr;                // out of memory
        bin_remove(b);

        // Split off the remainder if it can hold a header + a min payload.
        if (b->size >= size + HEADER + 16) {
            Block* rem = reinterpret_cast<Block*>(
                reinterpret_cast<uint8_t*>(b) + HEADER + size);
            rem->size      = b->size - size - HEADER;
            rem->free      = true;
            rem->phys_prev = b;
            rem->phys_next = b->phys_next;
            if (b->phys_next) b->phys_next->phys_prev = rem;
            b->phys_next   = rem;
            b->size        = size;
            bin_push(rem);
        }

        b->free = false;
        used_  += b->size;
        if (used_ > peak_) peak_ = used_;
        return payload(b);
    }

    void deallocate(void* ptr) override {
        if (!ptr) return;
        Block* b = header_from(ptr);
        if (b->free) return;                   // double free — ignore

        b->free = true;
        used_  -= b->size;

        // Coalesce forward.
        Block* next = b->phys_next;
        if (next && next->free) {
            bin_remove(next);
            b->size += HEADER + next->size;
            b->phys_next = next->phys_next;
            if (next->phys_next) next->phys_next->phys_prev = b;
        }
        // Coalesce backward.
        Block* prev = b->phys_prev;
        if (prev && prev->free) {
            bin_remove(prev);
            prev->size += HEADER + b->size;
            prev->phys_next = b->phys_next;
            if (b->phys_next) b->phys_next->phys_prev = prev;
            b = prev;
        }
        bin_push(b);
    }

    AllocationStats get_stats() const override {
        AllocationStats s;
        s.total_allocated = used_;
        s.total_reserved  = capacity_;
        s.peak_used       = peak_;
        size_t free_space = 0, largest_free = 0, alloc_count = 0;
        for (const Block* b = reinterpret_cast<const Block*>(buffer_); b; b = b->phys_next) {
            if (b->free) {
                free_space += b->size;
                if (b->size > largest_free) largest_free = b->size;
            } else {
                ++alloc_count;
            }
        }
        s.allocation_count = alloc_count;
        s.fragmentation = (free_space > 0)
            ? static_cast<size_t>(100.0 * (1.0 - static_cast<double>(largest_free) / free_space))
            : 0;
        return s;
    }

private:
    struct Block {
        size_t size;        // payload size in bytes
        Block* phys_prev;   // lower-address physical neighbour
        Block* phys_next;   // higher-address physical neighbour
        Block* free_prev;   // free-list links (valid only while free)
        Block* free_next;
        bool   free;
    };
    static constexpr size_t HEADER = ((sizeof(Block) + 15) / 16) * 16;  // 16-aligned
    static constexpr int    BINS   = 48;

    static int bin_index(size_t size) {
        int i = 0;
        while ((size >>= 1) != 0 && i < BINS - 1) ++i;
        return i;
    }
    void bin_push(Block* b) {
        const int k = bin_index(b->size);
        b->free_prev = nullptr;
        b->free_next = bins_[k];
        if (bins_[k]) bins_[k]->free_prev = b;
        bins_[k] = b;
    }
    void bin_remove(Block* b) {
        if (b->free_prev) b->free_prev->free_next = b->free_next;
        else bins_[bin_index(b->size)] = b->free_next;
        if (b->free_next) b->free_next->free_prev = b->free_prev;
        b->free_prev = b->free_next = nullptr;
    }
    Block* find_fit(size_t size) {
        // Exact bin may hold blocks too small (size class is a range); scan
        // it for a real fit, then any higher bin's first block always fits.
        for (int k = bin_index(size); k < BINS; ++k) {
            for (Block* b = bins_[k]; b; b = b->free_next)
                if (b->size >= size) return b;
        }
        return nullptr;
    }
    void*  payload(Block* b) const { return reinterpret_cast<uint8_t*>(b) + HEADER; }
    Block* header_from(void* p) const {
        return reinterpret_cast<Block*>(static_cast<uint8_t*>(p) - HEADER);
    }

    uint8_t* buffer_;
    size_t   capacity_;
    Block*   bins_[BINS];
    size_t   used_;
    size_t   peak_;
};

}  // namespace gws::memory
