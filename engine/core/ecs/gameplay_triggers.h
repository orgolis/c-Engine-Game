#pragma once

// ============================================================================
// G0 · Trigger volumes — spatial regions that emit events on enter / exit.
// ============================================================================
//
// A TriggerVolume is a box or sphere. When an ACTOR entity (carrying the
// TriggerActor marker — usually the player) enters or leaves, the volume's named
// events fire on the GameplayEventBus (instigator = the actor, target = the
// volume). Shape, size, and event names are all authored data, so the SAME
// primitive is a damage zone, an objective region, a save room, a level exit, a
// ghost's hunting area — for any game.

#include "world.h"
#include "components.h"            // Transform
#include "gameplay_events.h"       // GameplayEventBus / GameplayEvent
#include "authorable_components.h"
#include "component_serialize.h"   // detail::put_bytes / get_bytes

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

struct TriggerVolume {
    uint32_t  shape = 0;                        // 0 = box (half_extents), 1 = sphere (radius)
    glm::vec3 half_extents{1.0f, 1.0f, 1.0f};
    float     radius = 1.0f;
    std::string enter_event = "trigger.enter";
    std::string exit_event  = "trigger.exit";
    bool once = false;                          // enter fires once, then the volume disarms
    // ---- runtime (not serialized) ----
    std::vector<uint32_t> inside;               // actor ids currently inside
    bool disarmed = false;
};

// Marker: only entities with this component are tested against volumes. `enabled`
// lets a game gate an actor without removing the component. (Not an empty struct —
// EnTT's empty-type optimization would make add()/get() return void.)
struct TriggerActor { bool enabled = true; };

inline bool point_in_volume(const TriggerVolume& v, const glm::vec3& center, const glm::vec3& p) {
    if (v.shape == 1) {
        const glm::vec3 d = p - center;
        return glm::dot(d, d) <= v.radius * v.radius;
    }
    const glm::vec3 d = glm::abs(p - center);
    return d.x <= v.half_extents.x && d.y <= v.half_extents.y && d.z <= v.half_extents.z;
}

// One overlap pass: diff each volume's inside-set against the current actors and
// publish enter/exit events. O(volumes * actors) — fine at gameplay trigger counts.
inline void tick_triggers(World& w, GameplayEventBus& bus) {
    w.each<TriggerVolume, Transform>([&](Entity ve, TriggerVolume& v, Transform& vt) {
        if (v.disarmed) { v.inside.clear(); return; }

        std::vector<uint32_t> now;
        for (auto ae : w.view<TriggerActor, Transform>()) {
            if (!w.get<TriggerActor>(ae).enabled) continue;   // gated actors don't trigger
            const Transform& at = w.get<Transform>(ae);
            if (point_in_volume(v, vt.position, at.position))
                now.push_back(static_cast<uint32_t>(ae));
        }

        auto contains = [](const std::vector<uint32_t>& s, uint32_t x) {
            for (uint32_t e : s)
                if (e == x) return true;
            return false;
        };
        for (uint32_t a : now) if (!contains(v.inside, a)) {          // entered
            GameplayEvent e; e.name = v.enter_event;
            e.instigator = static_cast<Entity>(a); e.target = ve;
            bus.publish(e);
            if (v.once) v.disarmed = true;
        }
        for (uint32_t p : v.inside) if (!contains(now, p)) {          // exited
            GameplayEvent e; e.name = v.exit_event;
            e.instigator = static_cast<Entity>(p); e.target = ve;
            bus.publish(e);
        }
        v.inside = std::move(now);
    });
}

// ---- custom serialization (skips the runtime inside-set / disarmed flag) ----
inline void serialize_trigger_volume(const void* obj, std::vector<uint8_t>& out) {
    const auto& v = *static_cast<const TriggerVolume*>(obj);
    detail::put_bytes(out, &v.shape, 4);
    detail::put_bytes(out, &v.half_extents, 12);
    detail::put_bytes(out, &v.radius, 4);
    auto put_str = [&](const std::string& s) {
        const uint32_t n = static_cast<uint32_t>(s.size());
        detail::put_bytes(out, &n, 4);
        detail::put_bytes(out, s.data(), n);
    };
    put_str(v.enter_event);
    put_str(v.exit_event);
    const uint8_t once = v.once ? 1 : 0;
    detail::put_bytes(out, &once, 1);
}
inline void deserialize_trigger_volume(void* obj, const uint8_t* data, size_t n) {
    auto& v = *static_cast<TriggerVolume*>(obj);
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_bytes(p, e, &v.shape, 4))        return;
    if (!detail::get_bytes(p, e, &v.half_extents, 12)) return;
    if (!detail::get_bytes(p, e, &v.radius, 4))       return;
    auto get_str = [&](std::string& s) -> bool {
        uint32_t len = 0;
        if (!detail::get_bytes(p, e, &len, 4) || p + len > e) return false;
        s.assign(reinterpret_cast<const char*>(p), len); p += len; return true;
    };
    if (!get_str(v.enter_event)) return;
    if (!get_str(v.exit_event))  return;
    uint8_t once = 0;
    if (!detail::get_bytes(p, e, &once, 1)) return;
    v.once = once != 0;
    v.inside.clear();
    v.disarmed = false;
}

inline void serialize_trigger_actor(const void* obj, std::vector<uint8_t>& out) {
    const uint8_t en = static_cast<const TriggerActor*>(obj)->enabled ? 1 : 0;
    detail::put_bytes(out, &en, 1);
}
inline void deserialize_trigger_actor(void* obj, const uint8_t* data, size_t n) {
    const uint8_t* p = data; const uint8_t* e = data + n; uint8_t en = 1;
    if (detail::get_bytes(p, e, &en, 1)) static_cast<TriggerActor*>(obj)->enabled = (en != 0);
}

// Register both as authorable (idempotent). Call at startup (the bridge ctor does).
inline void register_trigger_components() {
    register_authorable(make_authorable_custom<TriggerVolume>(
        "Trigger Volume", &serialize_trigger_volume, &deserialize_trigger_volume));
    register_authorable(make_authorable_custom<TriggerActor>(
        "Trigger Actor", &serialize_trigger_actor, &deserialize_trigger_actor));
}

}  // namespace schizo::ecs
