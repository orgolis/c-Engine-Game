#pragma once

// ====================
// Memory profiler snapshot (Stage 14 — Pillar N3)
// ====================
//
// Two feeds for the perf overlay's memory view:
//
//   1. Per-tag attribution — read straight from the MemoryTracker (live/peak
//      bytes + count per AllocationTag). Answers "which subsystem is using the
//      heap right now" and "what was its high-water mark".
//
//   2. Per-allocator health — reserved/used/peak/fragmentation for the engine's
//      named allocators (arena/pool/general/...). The allocators already report
//      AllocationStats via get_stats(); the missing piece was a way to ENUMERATE
//      them. AllocatorRegistry is an opt-in directory: an allocator (or its
//      owner) registers a name + a stats getter, and the overlay walks the list.
//      Opt-in keeps it zero-cost and non-invasive — nothing is forced to
//      register, and registration is just a function pointer, not ownership.
//
// Header-only, lives in core/memory so any layer can read it without depending
// on the renderer/editor.

#include "allocator_base.h"
#include "memory_tags.h"
#include "memory_tracker.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace gws::memory {

// ---- (1) Per-tag snapshot ----

struct TagUsage {
    AllocationTag tag;
    const char*   name;        // to_string(tag), stable literal
    TagStats      stats;       // live/peak bytes + count
};

struct MemorySnapshot {
    std::vector<TagUsage> tags;        // one per AllocationTag (Count entries)
    size_t                total_live_bytes = 0;
    size_t                total_live_count = 0;
};

// Gather the tracker's current per-tag stats. Cheap; call once per overlay frame.
inline MemorySnapshot snapshot_memory() {
    MemorySnapshot s;
    s.tags.reserve(static_cast<size_t>(AllocationTag::Count));
    MemoryTracker::instance().for_each_tag([&](AllocationTag t, const TagStats& st) {
        s.tags.push_back(TagUsage{t, to_string(t), st});
    });
    s.total_live_bytes = MemoryTracker::instance().total_live_bytes();
    s.total_live_count = MemoryTracker::instance().total_live_count();
    return s;
}

// ---- (2) Per-allocator registry (opt-in) ----

// One enumerable allocator: a display name + a getter that returns its current
// AllocationStats (reserved/used/peak/fragmentation). The getter is a plain
// std::function so the registry holds no ownership and any allocator type fits.
struct AllocatorProbe {
    std::string                          name;
    std::function<AllocationStats()>     get_stats;
};

class AllocatorRegistry {
public:
    static AllocatorRegistry& instance() { static AllocatorRegistry r; return r; }

    // Register a named allocator; returns a token used to unregister. Safe to
    // call from any thread. The getter must outlive the registration (typically
    // a lambda capturing the allocator by pointer — unregister in its dtor).
    using Token = uint64_t;
    Token add(std::string name, std::function<AllocationStats()> get_stats) {
        std::lock_guard<std::mutex> lk(mutex_);
        const Token tok = ++next_;
        probes_.push_back(Entry{tok, AllocatorProbe{std::move(name), std::move(get_stats)}});
        return tok;
    }
    void remove(Token tok) {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto it = probes_.begin(); it != probes_.end(); ++it)
            if (it->tok == tok) { probes_.erase(it); return; }
    }

    // Snapshot every registered allocator (name + live stats) for the overlay.
    std::vector<std::pair<std::string, AllocationStats>> snapshot() const {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<std::pair<std::string, AllocationStats>> out;
        out.reserve(probes_.size());
        for (const Entry& e : probes_)
            out.emplace_back(e.probe.name, e.probe.get_stats ? e.probe.get_stats() : AllocationStats{});
        return out;
    }
    size_t count() const { std::lock_guard<std::mutex> lk(mutex_); return probes_.size(); }

private:
    struct Entry { Token tok; AllocatorProbe probe; };
    mutable std::mutex mutex_;
    std::vector<Entry> probes_;
    Token              next_ = 0;
};

// RAII helper: register on construction, unregister on destruction. Drop one of
// these next to an allocator you want visible in the overlay:
//     ScopedAllocatorProbe probe_{"frame", [&]{ return frame_alloc.get_stats(); }};
class ScopedAllocatorProbe {
public:
    ScopedAllocatorProbe(std::string name, std::function<AllocationStats()> get_stats)
        : tok_(AllocatorRegistry::instance().add(std::move(name), std::move(get_stats))) {}
    ~ScopedAllocatorProbe() { AllocatorRegistry::instance().remove(tok_); }
    ScopedAllocatorProbe(const ScopedAllocatorProbe&) = delete;
    ScopedAllocatorProbe& operator=(const ScopedAllocatorProbe&) = delete;
private:
    AllocatorRegistry::Token tok_;
};

}  // namespace gws::memory
