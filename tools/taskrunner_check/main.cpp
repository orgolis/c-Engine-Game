// ============================================================================
// taskrunner_check — the contract that keeps the editor unfrozen and correct.
//
// Two properties matter more than the rest, and both are the kind that "seems
// to work" right up until they do not:
//
//   1. COMPLETION CALLBACKS RUN ON THE POLLING THREAD. Everything a finished
//      import wants to do — upload a mesh, touch the scene graph — is illegal
//      off the editor thread. If a callback ever fired on a worker, the bug it
//      caused would appear as a random driver crash days later, in code that
//      looks correct. So the thread identity is asserted directly.
//
//   2. THE RUNNER MUST NOT HOLD ITS LOCK WHILE A TASK RUNS. A 16 s import that
//      blocks snapshot() for 16 s reproduces the freeze this class exists to
//      remove — while appearing to work, because the task still completes.
//      Tested by querying the runner WHILE a task is deliberately parked.
//
// Timing is kept to short, generous waits on condition variables rather than
// fixed sleeps, so the test is fast and does not go flaky on a loaded runner.
// ============================================================================
#include "jobs/task_runner.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <cstdint>

using namespace gws::tasks;
using namespace std::chrono_literals;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// Wait for a predicate, up to a generous bound. Returns false on timeout, so a
// hang is reported as a failed assertion rather than hanging the suite.
template <typename F>
bool wait_until(F pred, std::chrono::milliseconds bound = 5000ms) {
    const auto deadline = std::chrono::steady_clock::now() + bound;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

// Drain completions until `n` callbacks have fired or we give up.
bool poll_until(TaskRunner& r, std::atomic<int>& counter, int n) {
    return wait_until([&] { r.poll(); return counter.load() >= n; });
}

}  // namespace

int main() {
    std::printf("=== taskrunner_check ===\n\n");

    // ---- 1. a task runs, reports, and completes ----------------------------
    {
        TaskRunner r;
        r.start(2);
        std::atomic<int> done{0};
        TaskInfo seen;

        r.submit("import", [](TaskContext& ctx) {
            ctx.set_progress(0.5f, "halfway");
        }, [&](const TaskInfo& i) { seen = i; ++done; });

        check(poll_until(r, done, 1), "a submitted task completes");
        check(seen.state == TaskState::Succeeded, "state is Succeeded");
        check(seen.label == "import", "label survives to the callback");
        check(seen.status == "halfway", "progress status is reported");
        check(seen.progress == 1.0f, "a succeeded task ends at 1.0");
        check(r.idle(), "runner is idle afterwards");
    }

    // ---- 2. completion runs on the POLLING thread --------------------------
    // The property that keeps GPU work legal.
    {
        TaskRunner r;
        r.start(2);
        const auto main_thread = std::this_thread::get_id();
        std::atomic<int> done{0};
        std::atomic<bool> worker_id_differed{false};
        std::thread::id cb_thread;

        r.submit("t", [&](TaskContext&) {
            worker_id_differed = (std::this_thread::get_id() != main_thread);
        }, [&](const TaskInfo&) { cb_thread = std::this_thread::get_id(); ++done; });

        check(poll_until(r, done, 1), "task completed");
        check(worker_id_differed.load(), "the task body really ran off the calling thread");
        check(cb_thread == main_thread,
              "the completion callback ran on the POLLING thread, not a worker");
    }

    // ---- 3. the runner stays responsive WHILE a task is running ------------
    // The freeze test. A task parks; the runner must still answer.
    {
        TaskRunner r;
        r.start(2);
        std::mutex m;
        std::condition_variable cv;
        bool release = false;
        std::atomic<bool> entered{false};
        std::atomic<int> done{0};

        r.submit("long import", [&](TaskContext& ctx) {
            ctx.set_progress(-1.0f, "working");
            entered = true;
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] { return release; });
        }, [&](const TaskInfo&) { ++done; });

        check(wait_until([&] { return entered.load(); }), "the task started");

        // These would block forever if the runner held its lock during the task.
        const auto snap = r.snapshot();
        check(!snap.empty(), "snapshot() answers while a task is running");
        check(r.active_count() == 1, "active_count() answers while a task is running");
        check(snap[0].status == "working", "live progress is visible to the UI");

        const uint64_t second = r.submit("queued", [](TaskContext&) {});
        check(second != 0, "submit() answers while a task is running");

        { std::lock_guard<std::mutex> lk(m); release = true; }
        cv.notify_all();
        check(poll_until(r, done, 1), "the parked task finishes once released");
    }

    // ---- 4. cancelling a task that has not started yet ---------------------
    // A queued cancel must prevent the work entirely — otherwise "cancel" only
    // means "please finish anyway".
    {
        TaskRunner r;
        r.start(1);                       // one worker, so #2 must queue
        std::mutex m;
        std::condition_variable cv;
        bool release = false;
        std::atomic<bool> blocker_in{false};
        std::atomic<bool> second_ran{false};
        std::atomic<int> done{0};

        r.submit("blocker", [&](TaskContext&) {
            blocker_in = true;
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] { return release; });
        }, [&](const TaskInfo&) { ++done; });
        check(wait_until([&] { return blocker_in.load(); }), "blocker occupies the only worker");

        const uint64_t id = r.submit("queued", [&](TaskContext&) { second_ran = true; },
                                     [&](const TaskInfo&) { ++done; });
        r.cancel(id);

        { std::lock_guard<std::mutex> lk(m); release = true; }
        cv.notify_all();

        check(poll_until(r, done, 2), "both tasks reach a terminal state");
        check(!second_ran.load(), "a task cancelled before starting never runs its body");
    }

    // ---- 5. cancelling a RUNNING task is cooperative -----------------------
    {
        TaskRunner r;
        r.start(2);
        std::atomic<bool> spinning{false};
        std::atomic<int> done{0};
        TaskInfo seen;

        const uint64_t id = r.submit("cancellable", [&](TaskContext& ctx) {
            spinning = true;
            while (!ctx.cancelled()) std::this_thread::sleep_for(1ms);
        }, [&](const TaskInfo& i) { seen = i; ++done; });

        check(wait_until([&] { return spinning.load(); }), "task is running");
        r.cancel(id);
        check(poll_until(r, done, 1), "a cancelled running task terminates");
        check(seen.state == TaskState::Cancelled, "its state is Cancelled, not Succeeded");
    }

    // ---- 6. a throwing task fails, it does not crash -----------------------
    {
        TaskRunner r;
        r.start(2);
        std::atomic<int> done{0};
        TaskInfo seen;

        r.submit("bad", [](TaskContext&) { throw std::runtime_error("parse error"); },
                 [&](const TaskInfo& i) { seen = i; ++done; });

        check(poll_until(r, done, 1), "a throwing task still completes");
        check(seen.state == TaskState::Failed, "its state is Failed");
        check(seen.error.find("parse error") != std::string::npos,
              "the exception message is kept for the UI");
    }

    // ---- 7. many tasks, none lost ------------------------------------------
    {
        TaskRunner r;
        r.start(4);
        std::atomic<int> ran{0}, done{0};
        for (int i = 0; i < 64; ++i)
            r.submit("t" + std::to_string(i), [&](TaskContext&) { ++ran; },
                     [&](const TaskInfo&) { ++done; });

        check(poll_until(r, done, 64), "all 64 tasks complete");
        check(ran.load() == 64, "every task body ran exactly once");
        check(r.idle(), "runner drains to idle");
    }

    // ---- 8. a completion callback may submit more work ----------------------
    // Cook-then-upload chains do this; it must not deadlock on poll()'s lock.
    {
        TaskRunner r;
        r.start(2);
        std::atomic<int> done{0};
        r.submit("first", [](TaskContext&) {}, [&](const TaskInfo&) {
            ++done;
            r.submit("second", [](TaskContext&) {}, [&](const TaskInfo&) { ++done; });
        });
        check(poll_until(r, done, 2), "a callback can queue follow-up work without deadlock");
    }

    // ---- 9. stop() with work outstanding, and double stop ------------------
    {
        TaskRunner r;
        r.start(2);
        std::atomic<bool> in{false};
        r.submit("spinner", [&](TaskContext& ctx) {
            in = true;
            while (!ctx.cancelled()) std::this_thread::sleep_for(1ms);
        });
        check(wait_until([&] { return in.load(); }), "task running before stop");
        r.stop();                       // must cancel + join, not hang
        check(true, "stop() returns with a task still outstanding");
        r.stop();                       // must be a no-op
        check(true, "stop() is safe to call twice");
        check(r.active_count() == 0, "nothing outstanding after stop");
    }

    // ---- 10. degenerate input ----------------------------------------------
    {
        TaskRunner r;
        r.start(1);
        check(r.submit("null", nullptr) == 0, "submitting a null task is rejected");
        r.cancel(999999);
        check(true, "cancelling an unknown id is harmless");
        check(r.poll() == 0, "poll() with nothing pending returns 0");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL taskrunner_check\n");
    return g_fail ? 1 : 0;
}
