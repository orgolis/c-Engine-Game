#pragma once

// ====================
// ECS System Scheduler  (Pillar D3 / Master Plan Stage 1.3)
// ====================
//
// A system is a function over the World. Each system declares the component
// types it reads and writes; two systems CONFLICT iff one writes a type the
// other reads or writes. The scheduler builds a dependency DAG (a system
// depends on every EARLIER-registered system it conflicts with) and runs it
// in parallel WAVES: all systems whose dependencies are satisfied run
// concurrently on the job system; conflicting systems are serialized by an
// edge, in registration order (deterministic — required for netcode).
//
// Structural changes (create/destroy/add/remove) are NOT applied during
// iteration; each system records them into its own EntityCommandBuffer,
// which the scheduler plays back at the sync point after each wave, in
// registration order.

#include "world.h"
#include "jobs/job_system.h"
#include "reflection/reflection.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace schizo::ecs {

// ---- deferred structural changes ----
class EntityCommandBuffer {
public:
    void destroy(Entity e) {
        ops_.emplace_back([e](World& w) { if (w.valid(e)) w.destroy(e); });
    }
    template <typename T, typename... Args>
    void add(Entity e, Args... args) {
        ops_.emplace_back([=](World& w) { if (w.valid(e)) w.add<T>(e, args...); });
    }
    template <typename T>
    void remove(Entity e) {
        ops_.emplace_back([e](World& w) { if (w.valid(e) && w.has<T>(e)) w.remove<T>(e); });
    }
    // The new entity's id isn't known until playback, so init runs then.
    void create(std::function<void(World&, Entity)> init) {
        ops_.emplace_back([init = std::move(init)](World& w) { init(w, w.create()); });
    }
    void playback(World& w) {
        for (auto& op : ops_) op(w);
        ops_.clear();
    }
    bool   empty() const { return ops_.empty(); }
    size_t size()  const { return ops_.size(); }

private:
    std::vector<std::function<void(World&)>> ops_;
};

// ---- access declaration ----
struct SystemAccess {
    std::vector<gws::reflect::TypeId> reads;
    std::vector<gws::reflect::TypeId> writes;
};
using SystemFn = std::function<void(World&, EntityCommandBuffer&)>;

struct System {
    const char*  name = "";
    SystemFn     fn;
    SystemAccess access;
};

class SystemScheduler {
public:
    // Fluent registration: add(...).reads<A,B>().writes<C>();
    class Builder {
    public:
        explicit Builder(System* s) : s_(s) {}
        template <typename... Ts> Builder& reads() {
            (s_->access.reads.push_back(gws::reflect::type_id_of<Ts>()), ...);
            return *this;
        }
        template <typename... Ts> Builder& writes() {
            (s_->access.writes.push_back(gws::reflect::type_id_of<Ts>()), ...);
            return *this;
        }
    private:
        System* s_;
    };

    Builder add(const char* name, SystemFn fn) {
        systems_.push_back(System{name, std::move(fn), {}});
        return Builder(&systems_.back());
    }

    size_t system_count() const { return systems_.size(); }
    void   clear() { systems_.clear(); }

    // Execute all systems for one frame: dependency waves + command playback.
    void run(World& world) {
        const size_t n = systems_.size();
        if (n == 0) return;

        // Dependency DAG: i depends on every earlier j it conflicts with.
        std::vector<int>                 remaining(n, 0);
        std::vector<std::vector<size_t>> dependents(n);
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < i; ++j)
                if (conflict(systems_[i].access, systems_[j].access)) {
                    dependents[j].push_back(i);
                    ++remaining[i];
                }

        std::vector<size_t> ready;
        for (size_t i = 0; i < n; ++i)
            if (remaining[i] == 0) ready.push_back(i);

        auto& js = gws::jobs::JobSystem::instance();

        while (!ready.empty()) {
            // Run the wave: members are mutually non-conflicting (if i and j
            // conflicted, the later one would still be blocked), so they run
            // concurrently. Each gets its own command buffer.
            std::vector<EntityCommandBuffer> cmds(ready.size());
            auto run_one = [&](size_t k) {
                systems_[ready[k]].fn(world, cmds[k]);
            };
            if (js.initialized() && ready.size() > 1) {
                js.parallel_for(0, ready.size(), run_one, 1);
            } else {
                for (size_t k = 0; k < ready.size(); ++k) run_one(k);
            }

            // Sync point: play back structural changes in registration order.
            std::vector<size_t> order(ready.size());
            for (size_t k = 0; k < ready.size(); ++k) order[k] = k;
            std::sort(order.begin(), order.end(),
                      [&](size_t a, size_t b) { return ready[a] < ready[b]; });
            for (size_t k : order)
                if (!cmds[k].empty()) cmds[k].playback(world);

            // Advance the frontier.
            std::vector<size_t> next;
            for (size_t i : ready)
                for (size_t d : dependents[i])
                    if (--remaining[d] == 0) next.push_back(d);
            ready.swap(next);
        }
    }

private:
    static bool contains(const std::vector<gws::reflect::TypeId>& v, gws::reflect::TypeId id) {
        for (gws::reflect::TypeId x : v) if (x == id) return true;
        return false;
    }
    static bool conflict(const SystemAccess& a, const SystemAccess& b) {
        for (gws::reflect::TypeId w : a.writes)
            if (contains(b.reads, w) || contains(b.writes, w)) return true;
        for (gws::reflect::TypeId w : b.writes)
            if (contains(a.reads, w) || contains(a.writes, w)) return true;
        return false;
    }

    std::vector<System> systems_;
};

}  // namespace schizo::ecs
