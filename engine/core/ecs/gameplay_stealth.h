#pragma once

// ============================================================================
// G16 · Stealth & detection — vision cones, awareness, hearing/noise.
// ============================================================================
//
// An observer (guard) has a VisionCone (fov + range) and an Awareness meter that
// rises while a Stealth target is inside its cone (faster when close, slower for a
// low-detectability/crouched target) and decays otherwise, stepping Unaware ->
// Suspicious -> Alert with events on each change. emit_noise() bumps the awareness
// of observers within earshot (a gunshot alerts guards). Data-driven; line-of-
// sight blockers are the integration layer (physics raycast).

#include "world.h"
#include "components.h"           // Transform
#include "gameplay_events.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

enum class Alertness : uint32_t { Unaware = 0, Suspicious = 1, Alert = 2 };

struct VisionCone { float fov_deg = 90.0f; float range = 15.0f; };
struct Awareness  { float level = 0.0f; Alertness state = Alertness::Unaware; uint32_t target = 0xFFFFFFFFu;
                    float rise = 1.5f; float decay = 0.5f; };
struct Stealth    { float detectability = 1.0f; };   // <1 = stealthier (crouch / cover)
struct Hearing    { float radius = 12.0f; };

inline glm::vec3 entity_forward(const Transform& t) { return glm::normalize(t.rotation * glm::vec3(0.0f, 0.0f, 1.0f)); }

inline bool can_see(const glm::vec3& obs, const glm::vec3& fwd, float fov_deg, float range, const glm::vec3& tgt) {
    glm::vec3 to = tgt - obs;
    const float dist = glm::length(to);
    if (dist > range) return false;
    if (dist < 1e-4f) return true;                    // on top of each other
    to /= dist;
    return glm::dot(fwd, to) >= std::cos(glm::radians(fov_deg * 0.5f));
}

// Advance every observer's awareness from what it can see this frame.
inline void tick_stealth(World& w, float dt, GameplayEventBus* bus = nullptr) {
    struct Tgt { Entity e; glm::vec3 pos; float det; };
    std::vector<Tgt> targets;
    w.each<Stealth, Transform>([&](Entity e, Stealth& s, Transform& t) { targets.push_back({e, t.position, s.detectability}); });

    w.each<VisionCone, Awareness, Transform>([&](Entity e, VisionCone& vc, Awareness& aw, Transform& t) {
        const glm::vec3 fwd = entity_forward(t);
        Entity seen = null_entity; float best = 1e9f; float seen_det = 1.0f;
        for (const auto& tg : targets) {
            if (tg.e == e) continue;
            if (can_see(t.position, fwd, vc.fov_deg, vc.range, tg.pos)) {
                const float d = glm::distance(t.position, tg.pos);
                if (d < best) { best = d; seen = tg.e; seen_det = tg.det; }
            }
        }

        const Alertness prev = aw.state;
        if (seen != null_entity) {
            const float prox = 1.0f - std::min(1.0f, best / std::max(1.0f, vc.range));
            aw.level += aw.rise * seen_det * (0.3f + prox) * dt;
            aw.target = static_cast<uint32_t>(seen);
        } else {
            aw.level -= aw.decay * dt;
        }
        aw.level = std::clamp(aw.level, 0.0f, 1.0f);
        aw.state = aw.level >= 1.0f ? Alertness::Alert
                 : (aw.level >= 0.3f ? Alertness::Suspicious : Alertness::Unaware);

        if (aw.state != prev && bus) {
            const char* name = aw.state == Alertness::Alert ? "stealth.alert"
                             : (aw.state == Alertness::Suspicious ? "stealth.suspicious" : "stealth.lost");
            GameplayEvent ev; ev.name = name; ev.instigator = e; ev.target = static_cast<Entity>(aw.target); bus->publish(ev);
        }
    });
}

// A noise at `pos` that carries `loudness` metres bumps the awareness of every
// observer with a Hearing sensor within range (a gunshot alerts nearby guards).
inline void emit_noise(World& w, const glm::vec3& pos, float loudness, GameplayEventBus* bus = nullptr, Entity source = null_entity) {
    w.each<Hearing, Awareness, Transform>([&](Entity e, Hearing& h, Awareness& aw, Transform& t) {
        const float d = glm::distance(pos, t.position);
        if (d <= h.radius && d <= loudness) {
            aw.level = std::min(1.0f, aw.level + 0.6f * (1.0f - d / std::max(1.0f, loudness)));
            if (bus) { GameplayEvent ev; ev.name = "stealth.noise"; ev.instigator = source; ev.target = e; ev.value = loudness; bus->publish(ev); }
        }
    });
}

// ---------------- custom serialization ----------------
inline void serialize_vision(const void* o, std::vector<uint8_t>& out) {
    const auto& v = *static_cast<const VisionCone*>(o); detail::put_f32(out, v.fov_deg); detail::put_f32(out, v.range);
}
inline void deserialize_vision(void* o, const uint8_t* d, size_t n) {
    auto& v = *static_cast<VisionCone*>(o); const uint8_t* p = d; const uint8_t* e = d + n;
    detail::get_f32(p, e, v.fov_deg); detail::get_f32(p, e, v.range);
}
inline void serialize_awareness(const void* o, std::vector<uint8_t>& out) {
    const auto& a = *static_cast<const Awareness*>(o); detail::put_f32(out, a.rise); detail::put_f32(out, a.decay);
}
inline void deserialize_awareness(void* o, const uint8_t* d, size_t n) {
    auto& a = *static_cast<Awareness*>(o); const uint8_t* p = d; const uint8_t* e = d + n;
    detail::get_f32(p, e, a.rise); detail::get_f32(p, e, a.decay);
    a.level = 0.0f; a.state = Alertness::Unaware; a.target = 0xFFFFFFFFu;
}
inline void serialize_stealth(const void* o, std::vector<uint8_t>& out) { detail::put_f32(out, static_cast<const Stealth*>(o)->detectability); }
inline void deserialize_stealth(void* o, const uint8_t* d, size_t n) { const uint8_t* p = d; detail::get_f32(p, d + n, static_cast<Stealth*>(o)->detectability); }
inline void serialize_hearing(const void* o, std::vector<uint8_t>& out) { detail::put_f32(out, static_cast<const Hearing*>(o)->radius); }
inline void deserialize_hearing(void* o, const uint8_t* d, size_t n) { const uint8_t* p = d; detail::get_f32(p, d + n, static_cast<Hearing*>(o)->radius); }

inline void register_stealth_components() {
    register_authorable(make_authorable_custom<VisionCone>("Vision Cone", &serialize_vision, &deserialize_vision));
    register_authorable(make_authorable_custom<Awareness>("Awareness", &serialize_awareness, &deserialize_awareness));
    register_authorable(make_authorable_custom<Stealth>("Stealth", &serialize_stealth, &deserialize_stealth));
    register_authorable(make_authorable_custom<Hearing>("Hearing", &serialize_hearing, &deserialize_hearing));
}

}  // namespace schizo::ecs
