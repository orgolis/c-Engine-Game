/**
 * @file hzb_streak.h
 * @brief The HZB occlusion hysteresis table: draw key -> frames occluded in a row.
 *
 * The editor drops a draw only once it has tested occluded two frames running,
 * so a draw's streak has to survive from one frame to the next, keyed by a hash
 * of its mesh, submesh and quantised position. That table used to be a vector
 * of (key, streak) pairs, std::sort-ed every frame so it could be searched.
 * Once every draw was occlusion-tested rather than just the nearest 4096, the
 * 20k scene had ~17k occluded draws, and that sort alone was most of a
 * millisecond.
 *
 * This is a flat open-addressing table instead: a lookup is a probe or two,
 * concurrent lookups are safe (they only read), and a frame's table is built
 * in one linear pass of inserts. Two of them are swapped each frame: the one
 * being read is last frame's, the one being filled is this frame's.
 *
 * Free of Vulkan and of DrawItem, so hzbstreak_check can test it against a
 * std::map without a device (the same split as draw_order.h).
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gws::renderer::gpu {

class HzbStreakTable {
public:
    /// Empty the table and size it for up to `expected` inserts (load factor
    /// at most 1/2). Keeps its storage: nothing is allocated in steady state.
    void reset(size_t expected) {
        size_t cap = 16;
        while (cap < expected * 2) cap <<= 1;
        if (vals_.size() != cap) {
            keys_.assign(cap, 0);
            vals_.assign(cap, 0);
        } else {
            std::fill(vals_.begin(), vals_.end(), uint8_t{0});
        }
        mask_  = cap - 1;
        shift_ = 64 - log2_(cap);
        count_ = 0;
    }

    /// The streak recorded for `key`, or 0 when it has none. Read-only, so any
    /// number of threads may call it at once (as long as nobody inserts).
    uint8_t get(uint64_t key) const {
        if (vals_.empty()) return 0;
        for (size_t s = slot_(key);; s = (s + 1) & mask_) {
            if (vals_[s] == 0)    return 0;          // empty slot: not present
            if (keys_[s] == key)  return vals_[s];
        }
    }

    /// Record `streak` (1..255) for `key`, overwriting an existing entry. Call
    /// reset() first with at least the number of distinct keys to be put.
    void put(uint64_t key, uint8_t streak) {
        if (streak == 0) return;                     // 0 marks an empty slot
        if ((count_ + 1) * 2 > vals_.size()) grow_();
        for (size_t s = slot_(key);; s = (s + 1) & mask_) {
            if (vals_[s] == 0) { keys_[s] = key; vals_[s] = streak; ++count_; return; }
            if (keys_[s] == key) { vals_[s] = streak; return; }
        }
    }

    size_t size() const { return count_; }
    void   clear()      { reset(0); }

private:
    // The key is already a hash, but an FNV hash's low bits are its weakest;
    // a Fibonacci multiply spreads all 64 bits into the slot index.
    size_t slot_(uint64_t key) const {
        return static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> shift_) & mask_;
    }
    static unsigned log2_(size_t pow2) {
        unsigned n = 0;
        while ((size_t{1} << n) < pow2) ++n;
        return n;
    }
    // Only if a caller under-reserved: rehash into twice the room.
    void grow_() {
        std::vector<uint64_t> k;
        std::vector<uint8_t>  v;
        k.swap(keys_);
        v.swap(vals_);
        const size_t old_cap = v.size();
        keys_.assign(std::max<size_t>(old_cap * 2, 16), 0);
        vals_.assign(keys_.size(), 0);
        mask_  = keys_.size() - 1;
        shift_ = 64 - log2_(keys_.size());
        count_ = 0;
        for (size_t i = 0; i < old_cap; ++i)
            if (v[i] != 0) put(k[i], v[i]);
    }

    std::vector<uint64_t> keys_;
    std::vector<uint8_t>  vals_;    // 0 = empty slot
    size_t   mask_  = 0;
    unsigned shift_ = 64;
    size_t   count_ = 0;
};

}  // namespace gws::renderer::gpu
