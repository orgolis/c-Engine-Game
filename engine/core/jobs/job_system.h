#pragma once

#include "job.h"

#include <cstddef>
#include <new>
#include <type_traits>

namespace gws::jobs {

// ====================
// Job System (Pillar A2 / Master Plan Stage 0.3)
// ====================
//
// Work-stealing thread pool with fork-join dependencies and a spin-help
// wait, plus a parallel_for built on top. The calling (main) thread is
// worker 0 and participates in execution while it waits.
//
//   js.init();                               // workers = hardware cores
//   js.parallel_for(0, n, [&](size_t i){ ... });
//   // or low-level fork-join:
//   Job* root  = js.create_job(&fn);
//   Job* child = js.create_job_as_child(root, &fn);
//   js.run(child); js.run(root); js.wait(root);
//   js.shutdown();
//
// Determinism note (for netcode, Stage 7): simulation jobs must be
// side-effect-free into per-entity scratch and merged in a fixed order —
// never rely on steal order or cross-thread float-accumulation order.
class JobSystem {
public:
    static JobSystem& instance();

    // `num_workers` = total participating threads INCLUDING the caller
    // (which becomes worker 0). 0 => std::thread::hardware_concurrency().
    void init(unsigned num_workers = 0);
    void shutdown();
    bool     initialized()  const { return initialized_; }
    unsigned worker_count() const { return num_workers_; }
    static unsigned this_worker_index();

    // ---- Low-level fork-join ----
    Job* allocate_job();                              // from caller's ring
    Job* create_job(JobFunction fn);                  // unfinished = 1
    Job* create_job_as_child(Job* parent, JobFunction fn);

    void run(Job* job);                               // submit to caller's queue
    void wait(const Job* job);                        // spin-help until done
    static bool is_done(const Job* job) {
        return job->unfinished.load(std::memory_order_acquire) <= 0;
    }

    // ---- parallel_for ----
    // Splits [begin,end) recursively and runs in parallel. `fn` may be
    // callable as fn(size_t i) OR fn(size_t begin, size_t end). grain==0
    // auto-picks ~workers*4 chunks. Blocks until the whole range is done.
    template <typename F>
    void parallel_for(size_t begin, size_t end, F fn, size_t grain = 0);

    // Exposed for the parallel_for trampoline (internal use).
    void execute(Job* job);

private:
    Job* get_job();         // pop own queue, else steal a random victim
    void finish(Job* job);
    void worker_loop(unsigned index);

    struct Impl;
    Impl*    impl_         = nullptr;
    unsigned num_workers_  = 0;
    bool     initialized_  = false;
};

// ---- parallel_for implementation ----
namespace detail {

// Dispatch to fn(begin,end) when available, else loop fn(i).
template <typename F>
auto invoke_range(F& fn, size_t b, size_t e, int)
    -> decltype(fn(b, e), void()) { fn(b, e); }
template <typename F>
void invoke_range(F& fn, size_t b, size_t e, long) {
    for (size_t i = b; i < e; ++i) fn(i);
}

template <typename F>
struct ParallelForData {
    F          fn;
    size_t     begin;
    size_t     end;
    size_t     grain;
    JobSystem* sys;
};

template <typename F>
void parallel_for_job(Job* job) {
    auto* d = reinterpret_cast<ParallelForData<F>*>(job->data);
    const size_t count = d->end - d->begin;
    if (count <= d->grain) {
        invoke_range(d->fn, d->begin, d->end, 0);
        return;
    }
    const size_t mid = d->begin + count / 2;
    JobSystem& sys = *d->sys;
    Job* left = sys.create_job_as_child(job, &parallel_for_job<F>);
    new (left->data) ParallelForData<F>{ d->fn, d->begin, mid, d->grain, d->sys };
    sys.run(left);
    Job* right = sys.create_job_as_child(job, &parallel_for_job<F>);
    new (right->data) ParallelForData<F>{ d->fn, mid, d->end, d->grain, d->sys };
    sys.run(right);
}

}  // namespace detail

template <typename F>
void JobSystem::parallel_for(size_t begin, size_t end, F fn, size_t grain) {
    if (end <= begin) return;
    if (grain == 0) {
        const size_t n      = end - begin;
        const size_t chunks = (num_workers_ ? num_workers_ : 1) * 4;
        grain = (n + chunks - 1) / chunks;
        if (grain == 0) grain = 1;
    }
    using Data = detail::ParallelForData<F>;
    static_assert(sizeof(Data) <= sizeof(Job::data),
                  "parallel_for functor + range too large for inline Job payload");
    static_assert(std::is_trivially_destructible<F>::value,
                  "parallel_for functor must be trivially destructible (capture by value of PODs)");
    Job* root = create_job(&detail::parallel_for_job<F>);
    new (root->data) Data{ fn, begin, end, grain, this };
    run(root);
    wait(root);
}

}  // namespace gws::jobs
