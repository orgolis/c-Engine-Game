#pragma once

// ============================================================================
// G15 · Survival & horror — needs, sanity, light/darkness, fear.
// ============================================================================
//
// Needs (hunger/thirst/temperature/stamina) are G0 attributes that DECAY over
// time and fire a critical event at a threshold; food/water restore them via item
// on-use effects. Sanity drains in darkness or near a FearSource and regenerates
// in light (a nearby LightSource or the survivor's own Flashlight, whose battery
// drains while on). Bleed/poison ailments are just G0 DoT effects. Data-driven —
// no baked survival rules.

#include "world.h"
#include "components.h"           // Transform
#include "gameplay_events.h"
#include "gameplay_attributes.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

// ---------------- needs ----------------
struct Need {
    std::string attribute;              // e.g. "Hunger", "Thirst" (a G0 attribute)
    float       rate = 1.0f;            // drain per second
    float       critical = 15.0f;       // fire the event when at/below this
    std::string critical_event = "need.critical";
    bool        fired = false;          // runtime edge latch
};
struct Needs { std::vector<Need> needs; };

inline void tick_needs(World& w, float dt, GameplayEventBus* bus = nullptr) {
    w.each<Needs, AttributeSet>([&](Entity e, Needs& n, AttributeSet& a) {
        for (auto& nd : n.needs) {
            a.adjust(nd.attribute, -nd.rate * dt);
            const float v = a.get(nd.attribute);
            if (v <= nd.critical && !nd.fired) {
                nd.fired = true;
                if (bus) { GameplayEvent ev; ev.name = nd.critical_event; ev.target = e; ev.param = nd.attribute; ev.value = v; bus->publish(ev); }
            } else if (v > nd.critical) {
                nd.fired = false;
            }
        }
    });
}

// ---------------- sanity / light / fear ----------------
struct Sanity     { float value = 100.0f, max = 100.0f, dark_drain = 5.0f, light_regen = 3.0f; bool low = false; };
struct LightSource { float radius = 8.0f; };
struct Flashlight  { float battery = 100.0f, max_battery = 100.0f, drain = 5.0f; bool on = true; float radius = 6.0f; };
struct FearSource  { float radius = 6.0f, drain = 15.0f; };

inline void tick_survival(World& w, float dt, GameplayEventBus* bus = nullptr) {
    tick_needs(w, dt, bus);

    // flashlight battery drains while on
    w.each<Flashlight>([&](Entity, Flashlight& fl) {
        if (fl.on && fl.battery > 0.0f) {
            fl.battery = std::max(0.0f, fl.battery - fl.drain * dt);
            if (fl.battery <= 0.0f) fl.on = false;
        }
    });

    // gather light + fear sources once (avoids nested view iteration)
    struct Src { glm::vec3 pos; float radius; float drain; };
    std::vector<Src> lights, fears;
    w.each<LightSource, Transform>([&](Entity, LightSource& l, Transform& t) { lights.push_back({t.position, l.radius, 0.0f}); });
    w.each<FearSource,  Transform>([&](Entity, FearSource&  f, Transform& t) { fears.push_back({t.position, f.radius, f.drain}); });

    w.each<Sanity, Transform>([&](Entity e, Sanity& s, Transform& t) {
        bool in_light = false;
        if (const Flashlight* fl = w.try_get<Flashlight>(e)) if (fl->on && fl->battery > 0.0f) in_light = true;
        if (!in_light)
            for (const auto& L : lights) if (glm::distance(L.pos, t.position) <= L.radius) { in_light = true; break; }
        float fear = 0.0f;
        for (const auto& F : fears) if (glm::distance(F.pos, t.position) <= F.radius) fear += F.drain;

        if (fear > 0.0f)      s.value -= fear * dt;          // fear overrides light
        else if (in_light)    s.value += s.light_regen * dt;
        else                  s.value -= s.dark_drain * dt;  // darkness
        s.value = std::clamp(s.value, 0.0f, s.max);

        const bool now_low = s.value <= s.max * 0.2f;
        if (now_low && !s.low && bus) { GameplayEvent ev; ev.name = "sanity.low"; ev.target = e; ev.value = s.value; bus->publish(ev); }
        s.low = now_low;
    });
}

inline bool toggle_flashlight(World& w, Entity e) {
    Flashlight* fl = w.try_get<Flashlight>(e);
    if (!fl) return false;
    if (!fl->on && fl->battery <= 0.0f) return false;   // dead battery
    fl->on = !fl->on;
    return fl->on;
}

// ---------------- custom serialization ----------------
inline void serialize_needs(const void* o, std::vector<uint8_t>& out) {
    const auto& n = *static_cast<const Needs*>(o);
    uint32_t c = static_cast<uint32_t>(n.needs.size()); detail::put_bytes(out, &c, 4);
    for (const auto& nd : n.needs) {
        detail::put_str(out, nd.attribute); detail::put_f32(out, nd.rate); detail::put_f32(out, nd.critical);
        detail::put_str(out, nd.critical_event);
    }
}
inline void deserialize_needs(void* o, const uint8_t* d, size_t n) {
    auto& ns = *static_cast<Needs*>(o); ns.needs.clear();
    const uint8_t* p = d; const uint8_t* e = d + n;
    uint32_t c = 0; if (!detail::get_bytes(p, e, &c, 4)) return;
    for (uint32_t i = 0; i < c; ++i) {
        Need nd;
        if (!detail::get_str(p, e, nd.attribute)) return;
        detail::get_f32(p, e, nd.rate); detail::get_f32(p, e, nd.critical);
        detail::get_str(p, e, nd.critical_event);
        ns.needs.push_back(std::move(nd));
    }
}
inline void serialize_sanity(const void* o, std::vector<uint8_t>& out) {
    const auto& s = *static_cast<const Sanity*>(o);
    detail::put_f32(out, s.value); detail::put_f32(out, s.max); detail::put_f32(out, s.dark_drain); detail::put_f32(out, s.light_regen);
}
inline void deserialize_sanity(void* o, const uint8_t* d, size_t n) {
    auto& s = *static_cast<Sanity*>(o); const uint8_t* p = d; const uint8_t* e = d + n;
    detail::get_f32(p, e, s.value); detail::get_f32(p, e, s.max); detail::get_f32(p, e, s.dark_drain); detail::get_f32(p, e, s.light_regen); s.low = false;
}
inline void serialize_flashlight(const void* o, std::vector<uint8_t>& out) {
    const auto& f = *static_cast<const Flashlight*>(o);
    detail::put_f32(out, f.max_battery); detail::put_f32(out, f.drain); detail::put_f32(out, f.radius);
}
inline void deserialize_flashlight(void* o, const uint8_t* d, size_t n) {
    auto& f = *static_cast<Flashlight*>(o); const uint8_t* p = d; const uint8_t* e = d + n;
    detail::get_f32(p, e, f.max_battery); detail::get_f32(p, e, f.drain); detail::get_f32(p, e, f.radius);
    f.battery = f.max_battery; f.on = true;
}
inline void serialize_lightsource(const void* o, std::vector<uint8_t>& out) { detail::put_f32(out, static_cast<const LightSource*>(o)->radius); }
inline void deserialize_lightsource(void* o, const uint8_t* d, size_t n) { const uint8_t* p = d; detail::get_f32(p, d + n, static_cast<LightSource*>(o)->radius); }
inline void serialize_fearsource(const void* o, std::vector<uint8_t>& out) {
    const auto& f = *static_cast<const FearSource*>(o); detail::put_f32(out, f.radius); detail::put_f32(out, f.drain);
}
inline void deserialize_fearsource(void* o, const uint8_t* d, size_t n) {
    auto& f = *static_cast<FearSource*>(o); const uint8_t* p = d; const uint8_t* e = d + n;
    detail::get_f32(p, e, f.radius); detail::get_f32(p, e, f.drain);
}
inline void register_survival_components() {
    register_authorable(make_authorable_custom<Needs>("Needs", &serialize_needs, &deserialize_needs));
    register_authorable(make_authorable_custom<Sanity>("Sanity", &serialize_sanity, &deserialize_sanity));
    register_authorable(make_authorable_custom<Flashlight>("Flashlight", &serialize_flashlight, &deserialize_flashlight));
    register_authorable(make_authorable_custom<LightSource>("Light Source", &serialize_lightsource, &deserialize_lightsource));
    register_authorable(make_authorable_custom<FearSource>("Fear Source", &serialize_fearsource, &deserialize_fearsource));
}

}  // namespace schizo::ecs
