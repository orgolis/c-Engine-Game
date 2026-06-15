#pragma once

#include <cstdint>

namespace gws::memory {

// The calling thread's worker slot index. The FrameAllocator uses it to
// pick a per-worker arena so concurrent allocations don't contend. Set by
// the job system on each worker thread (main thread = 0, the default).
//
// Lives in `memory` (the lower layer) so the job system depends on memory,
// not the other way around. A function-local thread_local gives one value
// per thread, shared across translation units via the inline ODR.
inline uint32_t& alloc_worker_index() {
    static thread_local uint32_t index = 0;
    return index;
}

}  // namespace gws::memory
