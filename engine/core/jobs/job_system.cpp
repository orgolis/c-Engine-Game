#include "job_system.h"
#include "work_stealing_queue.h"
#include "memory/alloc_worker.h"  // set this thread's per-worker allocator slot

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace gws::jobs {

namespace {
// Per-thread job ring — jobs are sub-allocated round-robin, no per-job heap
// allocation. Reused once a job has retired; sized well above the number of
// jobs ever live at once per thread.
constexpr uint32_t kJobRingSize = 1024;  // power of two
static_assert((kJobRingSize & (kJobRingSize - 1)) == 0, "ring size must be pow2");

thread_local Job      t_job_ring[kJobRingSize];
thread_local uint32_t t_job_head     = 0;
thread_local unsigned t_worker_index = 0;
thread_local uint32_t t_rng          = 0x9e3779b9u;

inline uint32_t xorshift() {
    uint32_t x = t_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    t_rng = x;
    return x;
}
}  // namespace

struct JobSystem::Impl {
    std::vector<std::unique_ptr<WorkStealingQueue>> queues;
    std::vector<std::thread>                        threads;
    std::atomic<bool>                               running{false};

    // Idle-worker parking: workers that find no work sleep on `sleep_cv`
    // instead of busy-spinning. `submit_gen` is bumped on every run()/wake
    // request so a worker that was about to sleep can detect work that
    // arrived during its pre-park spin (avoids the lost-wakeup race). A
    // short timed wait is a belt-and-braces backstop.
    std::mutex                                      sleep_mutex;
    std::condition_variable                         sleep_cv;
    std::atomic<uint64_t>                           submit_gen{0};

    void wake_one() {
        submit_gen.fetch_add(1, std::memory_order_release);
        sleep_cv.notify_one();
    }
    void wake_all() {
        submit_gen.fetch_add(1, std::memory_order_release);
        sleep_cv.notify_all();
    }
};

JobSystem& JobSystem::instance() {
    static JobSystem s;
    return s;
}

unsigned JobSystem::this_worker_index() { return t_worker_index; }

void JobSystem::init(unsigned num_workers) {
    if (initialized_) return;
    if (num_workers == 0) num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 4;
    num_workers_ = num_workers;

    impl_ = new Impl();
    impl_->queues.reserve(num_workers_);
    for (unsigned i = 0; i < num_workers_; ++i)
        impl_->queues.push_back(std::make_unique<WorkStealingQueue>());

    impl_->running.store(true, std::memory_order_release);

    // The calling thread is worker 0 and helps execute while it waits.
    t_worker_index = 0;
    t_rng          = 0x9e3779b9u;

    for (unsigned i = 1; i < num_workers_; ++i)
        impl_->threads.emplace_back([this, i] { worker_loop(i); });

    initialized_ = true;
}

void JobSystem::shutdown() {
    if (!initialized_) return;
    impl_->running.store(false, std::memory_order_release);
    impl_->wake_all();  // release any parked workers so they can exit
    for (auto& th : impl_->threads)
        if (th.joinable()) th.join();
    delete impl_;
    impl_         = nullptr;
    num_workers_  = 0;
    initialized_  = false;
}

Job* JobSystem::allocate_job() {
    Job* j = &t_job_ring[(t_job_head++) & (kJobRingSize - 1)];
    j->function = nullptr;
    j->parent   = nullptr;
    j->unfinished.store(0, std::memory_order_relaxed);
    return j;
}

Job* JobSystem::create_job(JobFunction fn) {
    Job* j = allocate_job();
    j->function = fn;
    j->unfinished.store(1, std::memory_order_relaxed);
    return j;
}

Job* JobSystem::create_job_as_child(Job* parent, JobFunction fn) {
    parent->unfinished.fetch_add(1, std::memory_order_relaxed);
    Job* j = allocate_job();
    j->function = fn;
    j->parent   = parent;
    j->unfinished.store(1, std::memory_order_relaxed);
    return j;
}

void JobSystem::run(Job* job) {
    impl_->queues[t_worker_index]->push(job);
    impl_->wake_one();  // a parked worker can pick this up (or steal it)
}

Job* JobSystem::get_job() {
    WorkStealingQueue* q = impl_->queues[t_worker_index].get();
    if (Job* j = q->pop()) return j;

    if (num_workers_ > 1) {
        const unsigned victim = xorshift() % num_workers_;
        if (victim != t_worker_index) {
            if (Job* j = impl_->queues[victim]->steal()) return j;
        }
    }
    return nullptr;  // nothing to do right now
}

void JobSystem::execute(Job* job) {
    if (job->function) job->function(job);
    finish(job);
}

void JobSystem::finish(Job* job) {
    const int32_t remaining =
        job->unfinished.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0 && job->parent)
        finish(job->parent);  // cascade completion up the fork-join tree
}

void JobSystem::wait(const Job* job) {
    // Spin-help: instead of blocking, run other jobs until this one is done.
    while (!is_done(job)) {
        if (Job* j = get_job()) execute(j);
        else std::this_thread::yield();
    }
}

void JobSystem::worker_loop(unsigned index) {
    t_worker_index = index;
    t_rng          = index * 2654435761u + 1u;
    gws::memory::alloc_worker_index() = index;  // per-worker frame-allocator slot

    while (impl_->running.load(std::memory_order_acquire)) {
        if (Job* j = get_job()) {
            execute(j);
            continue;
        }

        // No work found. Snapshot the submit generation, then spin briefly —
        // work is often about to appear (a sibling job split, a steal target
        // filling up). Spinning avoids the cost of parking/waking for short
        // gaps.
        const uint64_t gen = impl_->submit_gen.load(std::memory_order_acquire);
        for (int spin = 0; spin < 64; ++spin) {
            if (!impl_->running.load(std::memory_order_acquire)) return;
            if (Job* j = get_job()) { execute(j); break; }
            std::this_thread::yield();
        }
        if (!impl_->running.load(std::memory_order_acquire)) return;

        // Still nothing — park until a submit bumps the generation (or a
        // short timeout fires as a lost-wakeup backstop).
        std::unique_lock<std::mutex> lk(impl_->sleep_mutex);
        impl_->sleep_cv.wait_for(lk, std::chrono::milliseconds(2), [&] {
            return !impl_->running.load(std::memory_order_acquire) ||
                   impl_->submit_gen.load(std::memory_order_acquire) != gen;
        });
    }
}

}  // namespace gws::jobs
