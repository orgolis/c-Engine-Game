#pragma once

// ============================================================================
// bindless_index_allocator — slot bookkeeping for the bindless texture table.
//
// Deliberately free of Vulkan. The descriptor writes are hard to get wrong
// quietly: a bad write trips validation. The BOOKKEEPING is the half that fails
// silently -- a slot handed out twice makes two materials share one texture,
// and a slot never reused makes a scene that streams textures run the table dry
// and start rendering whatever happened to be in slot 0. Neither errors.
//
// So it lives here, as plain arithmetic that a headless check can hammer.
// ============================================================================

#include <cstdint>
#include <vector>

namespace gws::renderer::gpu {

class BindlessIndexAllocator {
public:
    /// Returned by acquire() when the table is full. Not 0: zero is a perfectly
    /// good slot, and a sentinel that collides with a valid value is how "the
    /// table is full" turns into "everything uses the first texture".
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu;

    BindlessIndexAllocator() = default;
    explicit BindlessIndexAllocator(uint32_t capacity) : capacity_(capacity) {}

    void reset(uint32_t capacity) {
        capacity_ = capacity;
        next_     = 0;
        free_.clear();
    }

    /// Take a slot. Freed slots are reused before fresh ones are handed out --
    /// without that, a scene that loads and drops textures exhausts a table it
    /// never simultaneously filled.
    uint32_t acquire() {
        if (!free_.empty()) {
            const uint32_t i = free_.back();
            free_.pop_back();
            return i;
        }
        if (next_ >= capacity_) return kInvalid;
        return next_++;
    }

    /// Give a slot back. Ignores kInvalid and out-of-range values rather than
    /// asserting: release() is called from teardown paths where the acquire may
    /// itself have failed, and turning that into a crash helps nobody.
    void release(uint32_t index) {
        if (index == kInvalid || index >= next_) return;
        free_.push_back(index);
    }

    /// Slots currently handed out.
    uint32_t live() const { return next_ - static_cast<uint32_t>(free_.size()); }
    uint32_t capacity() const { return capacity_; }
    /// Highest slot ever handed out, +1. The descriptor array only needs
    /// writing up to here.
    uint32_t high_water() const { return next_; }
    bool full() const { return free_.empty() && next_ >= capacity_; }

private:
    uint32_t              capacity_ = 0;
    uint32_t              next_     = 0;   // never-yet-used slots start here
    std::vector<uint32_t> free_;           // returned slots, reused first
};

}  // namespace gws::renderer::gpu
