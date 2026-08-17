#pragma once

// ============================================================
// Core CPU Profiler  (Master Plan Stage 0.6 / Pillar N1–N3 baseline)
// ============================================================
//
// The instrumentation API the plan wants standing *now* so every later stage
// is born instrumented (the full flame-graph UI is Stage 14). A scoped CPU
// timer emits {tag, thread, start, end} into a PER-THREAD buffer — so it works
// correctly across the job system's worker threads with no contention on the
// hot path (each thread only touches its own buffer while a frame runs).
//
// Collection model (matches this engine's fork-join execution): all parallel
// work (parallel_for / parallel_each) is synchronous — the main thread blocks
// until every task returns — so at end_frame() NO worker is running profiled
// code. end_frame() therefore safely walks every thread's buffer, merges into
// per-tag stats, and clears. (The plan's "drained by idle workers" is the same
// idea; this is the simple correct v1.)
//
// Zero-cost when GWS_PROFILE_ENABLED == 0 (macros expand to nothing).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#ifndef GWS_PROFILE_ENABLED
#  define GWS_PROFILE_ENABLED 1   // cheap (coarse zones); leave on by default.
#endif

namespace gws::profile {

using profile_clock = std::chrono::steady_clock;

inline uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            profile_clock::now().time_since_epoch()).count());
}

// One completed zone. `tag` is a string literal (stable address); we only
// build std::strings at aggregation time, never on the hot path.
struct Event {
    const char* tag;
    uint64_t    start_ns;
    uint64_t    end_ns;
    uint32_t    thread_id;
    uint32_t    depth;       // nesting level within this thread (for flame UI)
};

struct ThreadBuffer {
    std::vector<Event> events;
    uint32_t           depth     = 0;
    uint32_t           thread_id = 0;
    ~ThreadBuffer();             // unregisters from the profiler (defined below)
};

class Profiler {
public:
    static Profiler& instance() { static Profiler p; return p; }

    struct TagStat {
        uint64_t total_ns = 0;   // summed across ALL threads (total CPU work)
        uint64_t max_ns   = 0;   // slowest single call
        uint32_t calls    = 0;
    };

    void register_buffer(ThreadBuffer* b) {
        std::lock_guard<std::mutex> lk(mutex_);
        buffers_.push_back(b);
    }
    // Called from a thread's ThreadBuffer dtor (thread exiting). Salvage its
    // not-yet-collected events into `retired_` so a thread that ends mid-frame
    // doesn't silently drop its data, then drop the dangling pointer.
    void retire_buffer(ThreadBuffer* b);  // defined after ThreadBuffer is complete

    void begin_frame() { frame_begin_ns_ = now_ns(); }

    // Call from the main thread when no worker is running profiled code.
    void end_frame() {
        const uint64_t end = now_ns();
        std::lock_guard<std::mutex> lk(mutex_);
        last_.clear();
        last_frame_ns_ = end - frame_begin_ns_;
        auto accumulate = [this](const Event& e) {
            TagStat& s = last_[e.tag];
            const uint64_t d = e.end_ns - e.start_ns;
            s.total_ns += d;
            s.calls    += 1;
            if (d > s.max_ns) s.max_ns = d;
        };
        for (ThreadBuffer* b : buffers_) {
            for (const Event& e : b->events) accumulate(e);
            b->events.clear();
            b->depth = 0;
        }
        for (const Event& e : retired_) accumulate(e);   // threads that exited
        retired_.clear();

        // A frame that took absurdly long is the single most useful thing this
        // profiler knows, and until now it only showed it in an on-screen
        // overlay -- which nobody can read during the freeze they are trying to
        // report. Frames over the threshold name themselves in the log
        // instead, so "it freezes when I click" arrives as a measurement.
        // Threshold in ms via GWS_SLOW_FRAME_MS, default 100.
        static const double slow_ms = [] {
            const char* v = std::getenv("GWS_SLOW_FRAME_MS");
            const double d = (v != nullptr) ? std::atof(v) : 0.0;
            return (d > 0.0) ? d : 100.0;
        }();
        const double frame_ms = double(last_frame_ns_) / 1.0e6;
        if (frame_ms >= slow_ms) {
            ++slow_frames_;
            // Unbounded logging would itself stall a machine that is already
            // stalling; the first 40 are plenty to find a culprit.
            if (slow_frames_ <= 40) {
                // spdlog rather than stderr: the Hub launches the editor with no
                // console, so a stderr-only report is lost in exactly the case
                // this exists for -- someone reproducing a freeze on their own
                // machine. This lands in editor.log.
                // format_report() only reads last_, so it is safe under the lock.
                spdlog::warn("[slow-frame] {}", format_report());
            }
        }
    }

    uint64_t slow_frame_count() const { return slow_frames_; }

    const std::unordered_map<std::string, TagStat>& last_frame() const { return last_; }
    uint64_t last_frame_ns() const { return last_frame_ns_; }

    // One-line, longest-first report. Times in ms.
    std::string format_report() const {
        std::vector<std::pair<std::string, TagStat>> v(last_.begin(), last_.end());
        std::sort(v.begin(), v.end(),
                  [](const auto& a, const auto& b) { return a.second.total_ns > b.second.total_ns; });
        char buf[256];
        std::snprintf(buf, sizeof buf, "frame %.3fms", last_frame_ns_ / 1.0e6);
        std::string out = buf;
        for (const auto& [tag, s] : v) {
            std::snprintf(buf, sizeof buf, " | %s %.3fms x%u (max %.3fms)",
                          tag.c_str(), s.total_ns / 1.0e6, s.calls, s.max_ns / 1.0e6);
            out += buf;
        }
        return out;
    }

private:
    std::mutex                                mutex_;
    std::vector<ThreadBuffer*>                buffers_;
    std::vector<Event>                        retired_;   // from exited threads
    std::unordered_map<std::string, TagStat>  last_;
    uint64_t                                  slow_frames_    = 0;
    uint64_t                                  frame_begin_ns_ = 0;
    uint64_t                                  last_frame_ns_  = 0;
};

inline void Profiler::retire_buffer(ThreadBuffer* b) {
    std::lock_guard<std::mutex> lk(mutex_);
    retired_.insert(retired_.end(), b->events.begin(), b->events.end());
    buffers_.erase(std::remove(buffers_.begin(), buffers_.end(), b),
                   buffers_.end());
}

inline ThreadBuffer::~ThreadBuffer() {
    Profiler::instance().retire_buffer(this);
}

// The calling thread's buffer (registered once, on first use).
inline ThreadBuffer& thread_buffer() {
    thread_local ThreadBuffer tb;
    thread_local bool registered = [](ThreadBuffer* p) {
        p->thread_id = static_cast<uint32_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        Profiler::instance().register_buffer(p);
        return true;
    }(&tb);
    (void)registered;
    return tb;
}

// RAII zone: records a {tag,thread,start,end} event for its lifetime.
class ScopedZone {
public:
    explicit ScopedZone(const char* tag) : tag_(tag) {
        ThreadBuffer& tb = thread_buffer();
        depth_ = tb.depth++;
        start_ = now_ns();
    }
    ~ScopedZone() {
        const uint64_t end = now_ns();
        ThreadBuffer& tb = thread_buffer();
        if (tb.depth) --tb.depth;
        tb.events.push_back(Event{ tag_, start_, end, tb.thread_id, depth_ });
    }
    ScopedZone(const ScopedZone&) = delete;
    ScopedZone& operator=(const ScopedZone&) = delete;
private:
    const char* tag_;
    uint64_t    start_;
    uint32_t    depth_;
};

}  // namespace gws::profile

#if GWS_PROFILE_ENABLED
#  define GWS_PROFILE_CONCAT2(a, b) a##b
#  define GWS_PROFILE_CONCAT(a, b)  GWS_PROFILE_CONCAT2(a, b)
#  define GWS_PROFILE_ZONE(tag) \
       ::gws::profile::ScopedZone GWS_PROFILE_CONCAT(_gws_zone_, __LINE__){ tag }
#  define GWS_PROFILE_FRAME_BEGIN() ::gws::profile::Profiler::instance().begin_frame()
#  define GWS_PROFILE_FRAME_END()   ::gws::profile::Profiler::instance().end_frame()
#else
#  define GWS_PROFILE_ZONE(tag)     ((void)0)
#  define GWS_PROFILE_FRAME_BEGIN() ((void)0)
#  define GWS_PROFILE_FRAME_END()   ((void)0)
#endif
