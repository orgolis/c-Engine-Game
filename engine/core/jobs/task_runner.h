#pragma once

// ============================================================================
// TaskRunner — long work that must not freeze the editor.
//
// WHY NOT JobSystem. The existing job system is fork-join: you `run` work and
// `wait` for it, and `wait` spin-helps until done. That is the right shape for
// splitting a frame's work across cores, and the wrong shape here. Cooking the
// six models in assets/models takes a measured **15.9 s**. Handing that to a
// fork-join pool either blocks the caller in `wait`, or occupies workers that
// frame-parallel work needs — an editor that renders at 3 fps while importing
// is only marginally better than one that freezes.
//
// So this owns a small pool of its OWN threads. Sized separately and
// deliberately: background work should leave the machine responsive, not race
// the renderer for every core.
//
// THE RULE THAT SHAPES THE API. Completion callbacks do NOT run on a worker.
// They are queued and run from `poll()` on the thread that calls it — the
// editor thread. Almost everything you want to do when an import finishes
// (upload a mesh, create a descriptor set, touch the scene graph) is illegal
// off that thread, and an API that made it *look* safe would be a trap. So
// worker threads may only produce data; the editor thread installs it.
//
// CANCELLATION IS COOPERATIVE, and honestly so. A task must check
// `ctx.cancelled()` at points where stopping is safe. There is no way to abort
// a thread mid-parse without leaking or corrupting, so pretending otherwise
// would be worse than making the task author check.
// ============================================================================

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gws::tasks {

enum class TaskState : uint8_t {
    Pending,      // queued, no worker yet
    Running,
    Succeeded,
    Failed,       // the task function threw
    Cancelled,    // cancel() was honoured (before or during the run)
};

// What a task reports about itself. Copied out under lock for the UI, so it is
// a snapshot and never a live pointer into a running task.
struct TaskInfo {
    uint64_t    id = 0;
    std::string label;
    TaskState   state = TaskState::Pending;
    float       progress = 0.0f;   // 0..1, or -1 for "no idea how long"
    std::string status;            // e.g. "reading Castelia City.obj"
    std::string error;             // set when state == Failed
};

// Handed to the task body. The ONLY channel a worker has back to the editor.
class TaskContext {
public:
    // Poll this wherever stopping would leave nothing half-written.
    bool cancelled() const { return cancelled_->load(std::memory_order_relaxed); }

    // fraction < 0 means indeterminate — the honest value when the total is not
    // known yet, and better than a bar that sits at 0 or jumps to 90.
    void set_progress(float fraction, std::string status = {});

private:
    friend class TaskRunner;
    TaskContext(std::atomic<bool>* c, std::mutex* m, TaskInfo* info)
        : cancelled_(c), mutex_(m), info_(info) {}

    std::atomic<bool>* cancelled_;
    std::mutex*        mutex_;
    TaskInfo*          info_;
};

class TaskRunner {
public:
    using TaskFn     = std::function<void(TaskContext&)>;
    using CompleteFn = std::function<void(const TaskInfo&)>;

    ~TaskRunner() { stop(); }

    // `workers` defaults to a small share of the machine, never the whole thing:
    // this is background work and the foreground has to stay smooth.
    void start(unsigned workers = 0);

    // Cancels everything outstanding and joins. Safe to call twice.
    void stop();

    // Queue work. `on_complete` runs from poll() on the polling thread — see the
    // header note — and runs for every terminal state, including Cancelled and
    // Failed, so a caller can always clean up.
    uint64_t submit(std::string label, TaskFn fn, CompleteFn on_complete = {});

    // Request cancellation. A Pending task is cancelled immediately; a Running
    // one stops at its next cancelled() check.
    void cancel(uint64_t id);
    void cancel_all();

    // Run completion callbacks for finished tasks. Call once per frame from the
    // editor thread. Returns how many completed.
    size_t poll();

    // For the UI. Ordered oldest-first so a progress list does not jump around.
    std::vector<TaskInfo> snapshot() const;

    size_t active_count() const;      // pending + running
    bool   idle() const { return active_count() == 0; }

private:
    struct Entry {
        TaskInfo          info;
        TaskFn            fn;
        CompleteFn        on_complete;
        std::atomic<bool> cancelled{false};
    };

    void worker_loop();

    mutable std::mutex       mutex_;
    std::condition_variable  cv_;
    std::vector<std::thread> workers_;
    // shared_ptr so a worker can hold an entry alive while the list changes.
    std::vector<std::shared_ptr<Entry>> tasks_;
    std::vector<std::shared_ptr<Entry>> completed_;   // waiting for poll()
    uint64_t next_id_ = 1;
    bool     running_ = false;
};

}  // namespace gws::tasks
