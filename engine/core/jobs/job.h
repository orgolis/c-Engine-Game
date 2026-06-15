#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace gws::jobs {

struct Job;

// A job's work function. It receives its own Job* so it can read its inline
// payload (`data`) and spawn child jobs (fork-join).
using JobFunction = void (*)(Job*);

// One unit of work. POD-ish and cache-line aligned so adjacent jobs in a
// per-thread ring don't false-share their atomic across stealing threads.
//
//   function    : the work to run
//   parent      : fork-join parent (its unfinished count is decremented
//                 when this job and all its children complete)
//   unfinished  : 1 (this job) + number of direct children still running.
//                 The job is "done" when this hits 0.
//   data        : inline payload — pack arguments / a small functor here
//                 (no per-job heap allocation on the hot path).
struct alignas(64) Job {
    JobFunction          function = nullptr;
    Job*                 parent   = nullptr;
    std::atomic<int32_t> unfinished{0};
    int32_t              _pad      = 0;

    static constexpr size_t kPayloadSize = 96;
    char                 data[kPayloadSize];
};

}  // namespace gws::jobs
