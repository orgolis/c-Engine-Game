#pragma once

// ============================================================================
// G1 · Data-driven gameplay state machine — high-level action states.
// ============================================================================
//
// The player (and NPCs, doors, hazards) live in a named STATE — idle, move,
// dodge, attack, stagger, dead — that gates what they can do. This is a generic,
// authored FSM over G0:
//
//   * each GameplayState grants a set of GameplayTags on enter and removes them
//     on exit (so "dodge" grants "state.dodging" + "state.iframe", which an
//     ability's block_tags or a damage check reads);
//   * transitions fire on a named EVENT and are gated by require/block tags, so
//     "any -> stagger on 'hit' unless blocking" is pure data;
//   * a state may auto-time-out (duration) and fire an event on itself, so
//     "dodge -> idle after 0.4s" needs no code.
//
// Events are sent DIRECTLY to an entity's machine (deterministic), and every
// enter/exit is also broadcast on the GameplayEventBus (param = state name) for
// observers — UI, audio, VFX, animation. No genre-specific states are baked in.

#include "world.h"
#include "gameplay_tags.h"
#include "gameplay_events.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

struct GameplayState {
    std::string name;                       // "idle", "dodge", "attack", "dead"
    std::vector<std::string> tags;          // granted on enter, removed on exit
    float       duration = 0.0f;            // >0 = auto time-out after this many seconds
    std::string timeout_event;              // event sent to self when the duration elapses
};

struct StateTransition {
    std::string from;                       // "" = from ANY state
    std::string to;
    std::string on_event;                   // event name that triggers this transition
    std::vector<std::string> require_tags;  // entity must have all
    std::vector<std::string> block_tags;    // entity must have none
};

struct StateMachine {
    std::vector<GameplayState>   states;
    std::vector<StateTransition> transitions;
    std::string initial;                    // starting state ("" = states[0])
    // ---- runtime (not serialized) ----
    std::string current;
    float       time_in_state = 0.0f;
    bool        started = false;
};

inline const GameplayState* find_state(const StateMachine& sm, const std::string& name) {
    for (const auto& s : sm.states) if (s.name == name) return &s;
    return nullptr;
}

inline void set_state_tags(World& w, Entity e, const std::vector<std::string>& tags, bool add) {
    if (tags.empty()) return;
    if (add && !w.has<GameplayTags>(e)) w.add<GameplayTags>(e, GameplayTags{});
    GameplayTags* g = w.try_get<GameplayTags>(e);
    if (!g) return;
    for (const auto& t : tags) { if (add) g->add(t); else g->remove_exact(t); }
}

// Switch to a state: remove the old state's tags, grant the new state's tags,
// reset the timer, and broadcast exit/enter events (if a bus is supplied).
inline void enter_state(World& w, Entity e, StateMachine& sm, const std::string& name,
                        GameplayEventBus* bus = nullptr) {
    if (const GameplayState* cur = find_state(sm, sm.current))
        set_state_tags(w, e, cur->tags, /*add=*/false);
    if (bus && !sm.current.empty()) {
        GameplayEvent ev; ev.name = "state.exited"; ev.target = e; ev.param = sm.current;
        bus->publish(ev);
    }
    sm.current = name;
    sm.time_in_state = 0.0f;
    sm.started = true;
    if (const GameplayState* ns = find_state(sm, name))
        set_state_tags(w, e, ns->tags, /*add=*/true);
    if (bus) {
        GameplayEvent ev; ev.name = "state.entered"; ev.target = e; ev.param = name;
        bus->publish(ev);
    }
}

// Deliver an event to the entity's machine: take the first matching, tag-gated
// transition from the current (or any) state. Returns true if the state changed.
inline bool send_state_event(World& w, Entity e, StateMachine& sm, const std::string& event_name,
                             GameplayEventBus* bus = nullptr) {
    const GameplayTags* tags = w.try_get<GameplayTags>(e);
    for (const auto& t : sm.transitions) {
        if (t.on_event != event_name) continue;
        if (!t.from.empty() && t.from != sm.current) continue;
        bool ok = true;
        for (const auto& rt : t.require_tags) if (!tags || !tags->has(rt)) { ok = false; break; }
        if (ok && tags) for (const auto& bt : t.block_tags) if (tags->has(bt)) { ok = false; break; }
        if (!ok) continue;
        enter_state(w, e, sm, t.to, bus);
        return true;
    }
    return false;
}

inline void start_state_machine(World& w, Entity e, StateMachine& sm, GameplayEventBus* bus = nullptr) {
    if (sm.started || sm.states.empty()) return;
    const std::string& first = (!sm.initial.empty() && find_state(sm, sm.initial))
                                   ? sm.initial : sm.states.front().name;
    enter_state(w, e, sm, first, bus);
}

// Advance every machine: start it on first tick, age the current state, and fire
// its timeout event when the duration elapses.
inline void tick_state_machines(World& w, GameplayEventBus* bus, float dt) {
    w.each<StateMachine>([&](Entity e, StateMachine& sm) {
        if (!sm.started) { start_state_machine(w, e, sm, bus); return; }
        sm.time_in_state += dt;
        if (const GameplayState* s = find_state(sm, sm.current))
            if (s->duration > 0.0f && sm.time_in_state >= s->duration && !s->timeout_event.empty())
                send_state_event(w, e, sm, s->timeout_event, bus);
    });
}

// ---- custom serialization (states + transitions + initial; runtime skipped) ----
// String/vector helpers live in component_serialize.h (detail::put_str, …).

inline void serialize_state_machine(const void* obj, std::vector<uint8_t>& out) {
    const auto& sm = *static_cast<const StateMachine*>(obj);
    detail::put_str(out, sm.initial);
    uint32_t ns = static_cast<uint32_t>(sm.states.size());
    detail::put_bytes(out, &ns, 4);
    for (const auto& s : sm.states) {
        detail::put_str(out, s.name);
        detail::put_str_vec(out, s.tags);
        detail::put_bytes(out, &s.duration, 4);
        detail::put_str(out, s.timeout_event);
    }
    uint32_t nt = static_cast<uint32_t>(sm.transitions.size());
    detail::put_bytes(out, &nt, 4);
    for (const auto& t : sm.transitions) {
        detail::put_str(out, t.from);
        detail::put_str(out, t.to);
        detail::put_str(out, t.on_event);
        detail::put_str_vec(out, t.require_tags);
        detail::put_str_vec(out, t.block_tags);
    }
}

inline void deserialize_state_machine(void* obj, const uint8_t* data, size_t n) {
    auto& sm = *static_cast<StateMachine*>(obj);
    sm.states.clear(); sm.transitions.clear();
    sm.current.clear(); sm.time_in_state = 0.0f; sm.started = false;
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_str(p, e, sm.initial)) return;
    uint32_t ns = 0;
    if (!detail::get_bytes(p, e, &ns, 4)) return;
    for (uint32_t i = 0; i < ns; ++i) {
        GameplayState s;
        if (!detail::get_str(p, e, s.name))          return;
        if (!detail::get_str_vec(p, e, s.tags))      return;
        if (!detail::get_bytes(p, e, &s.duration, 4)) return;
        if (!detail::get_str(p, e, s.timeout_event)) return;
        sm.states.push_back(std::move(s));
    }
    uint32_t nt = 0;
    if (!detail::get_bytes(p, e, &nt, 4)) return;
    for (uint32_t i = 0; i < nt; ++i) {
        StateTransition t;
        if (!detail::get_str(p, e, t.from))            return;
        if (!detail::get_str(p, e, t.to))              return;
        if (!detail::get_str(p, e, t.on_event))        return;
        if (!detail::get_str_vec(p, e, t.require_tags)) return;
        if (!detail::get_str_vec(p, e, t.block_tags))   return;
        sm.transitions.push_back(std::move(t));
    }
}

inline void register_state_machine_component() {
    register_authorable(make_authorable_custom<StateMachine>(
        "State Machine", &serialize_state_machine, &deserialize_state_machine));
}

}  // namespace schizo::ecs
