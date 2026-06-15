#pragma once

#include "memory_tags.h"
#include <cstddef>
#include <cstdint>

// ====================
// Memory Tracker (Pillar A1 / Master Plan Stage 0.1)
// ====================
//
// A side table keyed by pointer recording {size, tag} for every tracked
// allocation. Gives:
//   - live-bytes / live-count per AllocationTag  (usage attribution)
//   - leak detection = "live allocations at shutdown grouped by tag"
//   - a hook for the profiler panel (Stage 14) via for_each_tag()
//
// Opt-in: feed it through Allocator::allocate_tracked / deallocate_tracked
// (or detail::track_alloc/track_free). Raw allocate()/deallocate() stay
// untracked and zero-overhead. Enabled by default in debug builds; compiles
// to nothing when GWS_MEMORY_TRACKING == 0 (release default).

#ifndef GWS_MEMORY_TRACKING
#  if defined(NDEBUG)
#    define GWS_MEMORY_TRACKING 0
#  else
#    define GWS_MEMORY_TRACKING 1
#  endif
#endif

#if GWS_MEMORY_TRACKING
#  include <mutex>
#  include <unordered_map>
#endif

namespace gws::memory {

struct TagStats {
    size_t   live_bytes   = 0;  // currently-allocated bytes for this tag
    size_t   live_count   = 0;  // currently-live allocations
    size_t   peak_bytes   = 0;  // high-water mark
    uint64_t total_allocs = 0;  // cumulative allocations (lifetime)
};

#if GWS_MEMORY_TRACKING

class MemoryTracker {
public:
    static MemoryTracker& instance() { static MemoryTracker t; return t; }

    void on_alloc(const void* ptr, size_t size, AllocationTag tag) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lk(mutex_);
        live_[ptr] = Record{size, tag};
        TagStats& s = tags_[index(tag)];
        s.live_bytes += size;
        s.live_count += 1;
        s.total_allocs += 1;
        if (s.live_bytes > s.peak_bytes) s.peak_bytes = s.live_bytes;
        total_bytes_ += size;
        total_count_ += 1;
    }

    void on_free(const void* ptr) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = live_.find(ptr);
        if (it == live_.end()) return;  // untracked ptr or double free — ignore
        TagStats& s = tags_[index(it->second.tag)];
        s.live_bytes -= it->second.size;
        if (s.live_count) s.live_count -= 1;
        total_bytes_ -= it->second.size;
        if (total_count_) total_count_ -= 1;
        live_.erase(it);
    }

    TagStats stats_for(AllocationTag tag) const {
        std::lock_guard<std::mutex> lk(mutex_);
        return tags_[index(tag)];
    }
    size_t total_live_bytes() const { std::lock_guard<std::mutex> lk(mutex_); return total_bytes_; }
    size_t total_live_count() const { std::lock_guard<std::mutex> lk(mutex_); return total_count_; }
    bool   has_leaks()        const { std::lock_guard<std::mutex> lk(mutex_); return !live_.empty(); }

    // Visit per-tag live stats — for the profiler panel / shutdown report.
    template <typename Fn>
    void for_each_tag(Fn&& fn) const {
        std::lock_guard<std::mutex> lk(mutex_);
        for (size_t i = 0; i < kTagCount; ++i)
            fn(static_cast<AllocationTag>(i), tags_[i]);
    }

    void reset() {
        std::lock_guard<std::mutex> lk(mutex_);
        live_.clear();
        for (auto& s : tags_) s = TagStats{};
        total_bytes_ = 0;
        total_count_ = 0;
    }

private:
    MemoryTracker() = default;
    static constexpr size_t kTagCount = static_cast<size_t>(AllocationTag::Count);
    static size_t index(AllocationTag t) {
        const size_t i = static_cast<size_t>(t);
        return i < kTagCount ? i : 0;
    }
    struct Record { size_t size; AllocationTag tag; };

    mutable std::mutex mutex_;
    std::unordered_map<const void*, Record> live_;
    TagStats tags_[kTagCount];
    size_t   total_bytes_ = 0;
    size_t   total_count_ = 0;
};

namespace detail {
inline void track_alloc(const void* p, size_t s, AllocationTag t) {
    MemoryTracker::instance().on_alloc(p, s, t);
}
inline void track_free(const void* p) { MemoryTracker::instance().on_free(p); }
}  // namespace detail

#else  // GWS_MEMORY_TRACKING == 0 — everything compiles to nothing.

class MemoryTracker {
public:
    static MemoryTracker& instance() { static MemoryTracker t; return t; }
    void on_alloc(const void*, size_t, AllocationTag) {}
    void on_free(const void*) {}
    TagStats stats_for(AllocationTag) const { return {}; }
    size_t total_live_bytes() const { return 0; }
    size_t total_live_count() const { return 0; }
    bool   has_leaks()        const { return false; }
    template <typename Fn> void for_each_tag(Fn&&) const {}
    void reset() {}
};

namespace detail {
inline void track_alloc(const void*, size_t, AllocationTag) {}
inline void track_free(const void*) {}
}  // namespace detail

#endif

}  // namespace gws::memory
