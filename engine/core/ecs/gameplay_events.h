#pragma once

// ============================================================================
// G0 · Event bus + timer/cooldown manager — reactive gameplay scheduling.
// ============================================================================
//
// Two small, backend-free services every gameplay module leans on:
//
//   GameplayEventBus — named publish/subscribe. Anything (abilities, damage,
//     triggers, scripts, UI) publishes a GameplayEvent; anything subscribes by
//     name (or "*" for all). Publishing QUEUES; flush() dispatches once per frame
//     so handlers that publish more events don't re-enter. Events carry common
//     fields games encode meaning into — no engine-baked event taxonomy.
//
//   TimerManager — after(delay) one-shots and every(interval) repeats, each a
//     callback. Drives cooldowns, delayed spawns, ticking objectives, respawns.
//
// Both are plain classes (the editor bridge owns one of each and ticks them);
// neither depends on the renderer or physics.

#include "world.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace schizo::ecs {

// A generic gameplay event: named, with common fields the game gives meaning to.
struct GameplayEvent {
    std::string name;                        // "damage.dealt", "item.pickup", "zone.enter"
    Entity      instigator = null_entity;    // who caused it
    Entity      target     = null_entity;    // what it happened to
    float       value      = 0.0f;           // amount / magnitude
    std::string param;                       // free string payload (type, id, …)
};

// Named publish/subscribe. publish() queues; flush() dispatches the queue.
class GameplayEventBus {
public:
    using Handler = std::function<void(const GameplayEvent&)>;
    using SubId   = uint32_t;

    // Subscribe to an exact event name, or "*" for every event. Returns a handle.
    SubId subscribe(std::string name, Handler fn) {
        const SubId id = ++next_id_;
        subs_.push_back({id, std::move(name), std::move(fn)});
        return id;
    }
    void unsubscribe(SubId id) {
        for (size_t i = 0; i < subs_.size(); ++i)
            if (subs_[i].id == id) { subs_.erase(subs_.begin() + i); return; }
    }

    void publish(GameplayEvent e) { queue_.push_back(std::move(e)); }  // deferred
    void emit(const GameplayEvent& e) { dispatch(e); }                 // immediate

    // Dispatch everything queued so far (FIFO). Events published BY handlers during
    // this flush are held for the next flush, so dispatch always terminates.
    void flush() {
        const size_t count = queue_.size();
        for (size_t i = 0; i < count; ++i) {
            GameplayEvent e = queue_[i];   // copy — a handler may grow/realloc queue_
            dispatch(e);
        }
        queue_.erase(queue_.begin(), queue_.begin() + std::min(count, queue_.size()));
    }
    size_t pending() const { return queue_.size(); }
    void   clear() { subs_.clear(); queue_.clear(); }

private:
    void dispatch(const GameplayEvent& e) {
        auto snapshot = subs_;   // (un)subscribe during dispatch stays safe
        for (auto& s : snapshot)
            if (s.name == "*" || s.name == e.name) s.fn(e);
    }
    struct Sub { SubId id; std::string name; Handler fn; };
    std::vector<Sub>           subs_;
    std::vector<GameplayEvent> queue_;
    SubId                      next_id_ = 0;
};

// One-shot and repeating timers. Tick once per frame.
class TimerManager {
public:
    using Callback = std::function<void()>;
    using TimerId  = uint32_t;

    TimerId after(float delay, Callback fn)    { return add(delay, 0.0f, std::move(fn)); }
    TimerId every(float interval, Callback fn) { return add(interval, interval, std::move(fn)); }
    void    cancel(TimerId id) { for (auto& t : timers_) if (t.id == id) t.dead = true; }

    void tick(float dt) {
        // Index-based (a callback may add timers -> push_back can realloc; existing
        // indices stay valid, appended timers get their turn this same pass).
        for (size_t i = 0; i < timers_.size(); ++i) {
            if (timers_[i].dead) continue;
            timers_[i].remaining -= dt;
            while (!timers_[i].dead && timers_[i].remaining <= 0.0f) {
                Callback fn = timers_[i].fn;                 // copy before invoking
                const float interval = timers_[i].interval;
                fn();
                if (interval > 0.0f && !timers_[i].dead) timers_[i].remaining += interval;
                else                                      timers_[i].dead = true;
            }
        }
        timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                                     [](const Timer& t) { return t.dead; }),
                      timers_.end());
    }
    size_t active() const { return timers_.size(); }
    void   clear() { timers_.clear(); }

private:
    TimerId add(float remaining, float interval, Callback fn) {
        const TimerId id = ++next_id_;
        timers_.push_back({id, remaining, interval, std::move(fn), false});
        return id;
    }
    struct Timer { TimerId id; float remaining; float interval; Callback fn; bool dead; };
    std::vector<Timer> timers_;
    TimerId            next_id_ = 0;
};

}  // namespace schizo::ecs
