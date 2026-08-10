#include "task_runner.h"

#include <algorithm>
#include <memory>

namespace gws::tasks {

void TaskContext::set_progress(float fraction, std::string status) {
    std::lock_guard<std::mutex> lk(*mutex_);
    info_->progress = fraction;
    if (!status.empty()) info_->status = std::move(status);
}

void TaskRunner::start(unsigned workers) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (running_) return;
    running_ = true;

    if (workers == 0) {
        // Background work gets a minority of the machine. Taking every core
        // would make an import smooth and the editor it is running inside
        // stutter, which trades one freeze for another.
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        workers = std::max(1u, std::min(4u, hw / 4));
    }
    workers_.reserve(workers);
    for (unsigned i = 0; i < workers; ++i)
        workers_.emplace_back([this] { worker_loop(); });
}

void TaskRunner::stop() {
    std::vector<std::thread> to_join;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!running_) return;
        running_ = false;
        // Ask every outstanding task to stop. A task that never checks
        // cancelled() still finishes normally; this only shortens the wait for
        // the ones that do.
        for (auto& t : tasks_) t->cancelled.store(true, std::memory_order_relaxed);
        to_join.swap(workers_);
    }
    cv_.notify_all();
    for (auto& t : to_join) if (t.joinable()) t.join();

    // Deliberately does NOT run completion callbacks. They expect the editor
    // thread and a live scene; firing them during shutdown would hand a
    // half-torn-down editor a mesh to install.
    std::lock_guard<std::mutex> lk(mutex_);
    tasks_.clear();
    completed_.clear();
}

uint64_t TaskRunner::submit(std::string label, TaskFn fn, CompleteFn on_complete) {
    if (!fn) return 0;
    auto e = std::make_shared<Entry>();
    uint64_t id;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        id = next_id_++;
        e->info.id       = id;
        e->info.label    = std::move(label);
        e->info.state    = TaskState::Pending;
        e->info.progress = -1.0f;      // unknown until the task says otherwise
        e->fn            = std::move(fn);
        e->on_complete   = std::move(on_complete);
        tasks_.push_back(e);
    }
    cv_.notify_one();
    return id;
}

void TaskRunner::cancel(uint64_t id) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& t : tasks_)
        if (t->info.id == id) { t->cancelled.store(true, std::memory_order_relaxed); return; }
}

void TaskRunner::cancel_all() {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& t : tasks_) t->cancelled.store(true, std::memory_order_relaxed);
}

void TaskRunner::worker_loop() {
    for (;;) {
        std::shared_ptr<Entry> e;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] {
                if (!running_) return true;
                for (auto& t : tasks_)
                    if (t->info.state == TaskState::Pending) return true;
                return false;
            });
            if (!running_) return;

            for (auto& t : tasks_) {
                if (t->info.state != TaskState::Pending) continue;
                e = t;
                break;
            }
            if (!e) continue;

            // A task cancelled before it ever ran must not run at all — that is
            // the whole value of cancelling something still in the queue.
            if (e->cancelled.load(std::memory_order_relaxed)) {
                e->info.state = TaskState::Cancelled;
                completed_.push_back(e);
                tasks_.erase(std::remove(tasks_.begin(), tasks_.end(), e), tasks_.end());
                continue;
            }
            e->info.state = TaskState::Running;
        }

        // Run OUTSIDE the lock: a 16-second import holding the mutex would
        // block snapshot(), submit() and poll() for its whole duration, which
        // is the freeze this class exists to prevent.
        TaskState  result = TaskState::Succeeded;
        std::string error;
        try {
            TaskContext ctx(&e->cancelled, &mutex_, &e->info);
            e->fn(ctx);
            if (e->cancelled.load(std::memory_order_relaxed)) result = TaskState::Cancelled;
        } catch (const std::exception& ex) {
            result = TaskState::Failed;
            error  = ex.what();
        } catch (...) {
            result = TaskState::Failed;
            error  = "unknown exception";
        }

        {
            std::lock_guard<std::mutex> lk(mutex_);
            e->info.state = result;
            e->info.error = std::move(error);
            if (result == TaskState::Succeeded && e->info.progress >= 0.0f)
                e->info.progress = 1.0f;
            completed_.push_back(e);
            tasks_.erase(std::remove(tasks_.begin(), tasks_.end(), e), tasks_.end());
        }
    }
}

size_t TaskRunner::poll() {
    std::vector<std::shared_ptr<Entry>> done;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        done.swap(completed_);
    }
    // Callbacks run with the lock released so one may submit follow-up work
    // (a cook finishing and queueing its upload) without deadlocking.
    for (auto& e : done) {
        if (!e->on_complete) continue;
        try {
            e->on_complete(e->info);
        } catch (...) {
            // A failing completion handler must not take the editor with it,
            // nor stop the other completions in this batch.
        }
    }
    return done.size();
}

std::vector<TaskInfo> TaskRunner::snapshot() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<TaskInfo> out;
    out.reserve(tasks_.size() + completed_.size());
    for (const auto& t : tasks_)     out.push_back(t->info);
    for (const auto& t : completed_) out.push_back(t->info);
    std::sort(out.begin(), out.end(),
              [](const TaskInfo& a, const TaskInfo& b) { return a.id < b.id; });
    return out;
}

size_t TaskRunner::active_count() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return tasks_.size();
}

}  // namespace gws::tasks
