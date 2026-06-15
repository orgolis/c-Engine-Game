#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>

namespace gws::jobs {

struct Job;

// Chase–Lev work-stealing deque (bounded). The OWNER thread pushes/pops at
// the bottom (LIFO, cache-hot); STEALER threads take from the top (FIFO,
// oldest = biggest remaining subtree). Lock-free; only the single-element
// pop/steal race is resolved with a CAS on `top_`.
//
// Bounded to CAPACITY live jobs per worker — generous for a frame's graph;
// overflow asserts in debug (grow to a resizable array if it ever trips).
class WorkStealingQueue {
public:
    static constexpr int64_t CAPACITY = 4096;          // power of two
    static constexpr int64_t MASK     = CAPACITY - 1;

    WorkStealingQueue() : top_(0), bottom_(0) {}
    WorkStealingQueue(const WorkStealingQueue&)            = delete;
    WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;

    // Owner only.
    void push(Job* job) {
        const int64_t b = bottom_.load(std::memory_order_relaxed);
        assert(b - top_.load(std::memory_order_relaxed) < CAPACITY &&
               "WorkStealingQueue overflow — raise CAPACITY");
        buffer_[b & MASK] = job;
        std::atomic_thread_fence(std::memory_order_release);
        bottom_.store(b + 1, std::memory_order_relaxed);
    }

    // Owner only. Returns nullptr when empty (or lost the last-element race).
    Job* pop() {
        const int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t t = top_.load(std::memory_order_relaxed);

        Job* job = nullptr;
        if (t <= b) {
            job = buffer_[b & MASK];
            if (t == b) {
                // Last element: race against a concurrent steal.
                if (!top_.compare_exchange_strong(t, t + 1,
                        std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    job = nullptr;  // a stealer won
                }
                bottom_.store(b + 1, std::memory_order_relaxed);
            }
        } else {
            // Empty — restore bottom.
            bottom_.store(b + 1, std::memory_order_relaxed);
        }
        return job;
    }

    // Any stealer thread. Returns nullptr when empty or it lost the race.
    Job* steal() {
        int64_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        const int64_t b = bottom_.load(std::memory_order_acquire);

        Job* job = nullptr;
        if (t < b) {
            job = buffer_[t & MASK];
            if (!top_.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return nullptr;  // another thief won
            }
        }
        return job;
    }

    bool empty() const {
        return bottom_.load(std::memory_order_relaxed) <=
               top_.load(std::memory_order_relaxed);
    }

private:
    alignas(64) std::atomic<int64_t> top_;     // stealers advance this
    alignas(64) std::atomic<int64_t> bottom_;  // owner advances this
    Job* buffer_[CAPACITY];
};

}  // namespace gws::jobs
