#pragma once

// ============================================================================
// G12 · Shooter / gunplay — weapons, ammo, hitscan + projectiles, spread.
// ============================================================================
//
// A WeaponDef is authored data (fire rate, damage + type, magazine, spread,
// range, hitscan-vs-projectile, reload). WeaponState is the per-shooter runtime
// (equipped weapon, ammo, cooldown, reload). fire_weapon() resolves hitscan shots
// against G2 hurtboxes (ray-vs-sphere) and routes damage through G0, or spawns a
// Projectile that travels and hits. Composes with combat/damage/effects — no
// bespoke shooter code beyond the firing/ammo model.

#include "world.h"
#include "components.h"           // Transform
#include "gameplay_events.h"
#include "gameplay_damage.h"
#include "gameplay_combat.h"      // CombatActor / combat_hurtbox / HitSphere
#include "gameplay_items.h"       // Rng
#include "authorable_components.h"
#include "component_serialize.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

enum class FireMode : uint32_t { Semi = 0, Auto = 1, Burst = 2 };

struct WeaponDef {
    std::string id, name;
    FireMode    fire_mode = FireMode::Semi;
    float       damage = 20.0f;
    std::string damage_type = "kinetic";
    float       rpm = 600.0f;             // rounds/min -> cooldown = 60/rpm
    int         mag_size = 30;
    int         pellets = 1;              // >1 = shotgun
    float       spread_deg = 1.0f;
    float       range = 100.0f;
    float       projectile_speed = 0.0f;  // 0 = hitscan
    float       reload_time = 2.0f;
};

inline std::vector<WeaponDef>& weapon_registry() { static std::vector<WeaponDef> r; return r; }
inline void register_weapon(const WeaponDef& d) {
    for (auto& e : weapon_registry()) if (e.id == d.id) { e = d; return; }
    weapon_registry().push_back(d);
}
inline const WeaponDef* find_weapon(const std::string& id) {
    for (const auto& e : weapon_registry()) if (e.id == id) return &e;
    return nullptr;
}

struct WeaponState {
    std::string weapon_id;
    int   ammo_in_mag = 0;
    int   reserve = 0;
    float cooldown = 0.0f;    // runtime
    float reloading = 0.0f;   // runtime
};

struct Projectile {
    glm::vec3 velocity{0.0f};
    float     damage = 0.0f;
    std::string damage_type;
    uint32_t  owner = 0xFFFFFFFFu;
    float     lifetime = 3.0f;
    float     radius = 0.1f;
};

// ---- helpers ----
inline bool ray_sphere(const glm::vec3& o, const glm::vec3& d, const glm::vec3& c, float r, float max_t, float& out_t) {
    const glm::vec3 oc = o - c;
    const float b = glm::dot(oc, d);
    const float cc = glm::dot(oc, oc) - r * r;
    const float disc = b * b - cc;
    if (disc < 0.0f) return false;
    const float sq = std::sqrt(disc);
    float t = -b - sq;
    if (t < 0.0f) t = -b + sq;              // origin inside the sphere
    if (t < 0.0f || t > max_t) return false;
    out_t = t;
    return true;
}
inline glm::vec3 apply_spread(const glm::vec3& dir, float deg, uint64_t seed) {
    if (deg <= 0.0f) return glm::normalize(dir);
    Rng rng(seed);
    const float ax = (rng.unit() * 2.0f - 1.0f) * glm::radians(deg);
    const float ay = (rng.unit() * 2.0f - 1.0f) * glm::radians(deg);
    const glm::vec3 up = std::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 right = glm::normalize(glm::cross(dir, up));
    const glm::vec3 realup = glm::cross(right, glm::normalize(dir));
    return glm::normalize(glm::normalize(dir) + right * std::tan(ax) + realup * std::tan(ay));
}

// Nearest CombatActor hurtbox hit by a ray (excluding the shooter), within max_t.
inline Entity hitscan_target(World& w, Entity shooter, const glm::vec3& origin, const glm::vec3& dir,
                             float max_t, float& out_t) {
    Entity best = null_entity; float best_t = max_t;
    w.each<CombatActor, Transform>([&](Entity e, CombatActor& ca, Transform& t) {
        if (e == shooter) return;
        const HitSphere hb = combat_hurtbox(ca, t.position);
        float tt;
        if (ray_sphere(origin, dir, hb.center, hb.radius, best_t, tt) && tt < best_t) { best_t = tt; best = e; }
    });
    out_t = best_t;
    return best;
}

// Fire once. Hitscan resolves + applies damage immediately; projectile weapons
// spawn a Projectile per pellet. Returns false if on cooldown / reloading / empty.
inline bool fire_weapon(World& w, Entity shooter, const glm::vec3& origin, const glm::vec3& aim,
                        WeaponState& ws, GameplayEventBus* bus = nullptr, uint64_t seed = 1) {
    const WeaponDef* wd = find_weapon(ws.weapon_id);
    if (!wd || ws.reloading > 0.0f || ws.cooldown > 0.0f || ws.ammo_in_mag <= 0) return false;
    --ws.ammo_in_mag;
    ws.cooldown = 60.0f / (wd->rpm > 1.0f ? wd->rpm : 1.0f);
    if (bus) { GameplayEvent ev; ev.name = "weapon.fire"; ev.instigator = shooter; ev.param = ws.weapon_id; bus->publish(ev); }

    const int pellets = wd->pellets > 0 ? wd->pellets : 1;
    for (int i = 0; i < pellets; ++i) {
        const glm::vec3 dir = apply_spread(aim, wd->spread_deg, seed + static_cast<uint64_t>(i));
        if (wd->projectile_speed > 0.0f) {
            const Entity pe = w.create();
            Transform t; t.position = origin; w.add<Transform>(pe, t);
            w.add<Projectile>(pe, Projectile{dir * wd->projectile_speed, wd->damage, wd->damage_type,
                                             static_cast<uint32_t>(shooter), wd->range / wd->projectile_speed + 0.1f, 0.1f});
        } else {
            float t;
            const Entity hit = hitscan_target(w, shooter, origin, dir, wd->range, t);
            if (hit != null_entity) {
                const float dealt = apply_damage(w, hit, DamageInfo{wd->damage, wd->damage_type});
                if (bus) { GameplayEvent ev; ev.name = "weapon.hit"; ev.instigator = shooter; ev.target = hit;
                           ev.value = dealt; ev.param = wd->damage_type; bus->publish(ev); }
            }
        }
    }
    return true;
}

inline void reload_weapon(WeaponState& ws) {
    const WeaponDef* wd = find_weapon(ws.weapon_id);
    if (!wd || ws.reloading > 0.0f || ws.ammo_in_mag >= wd->mag_size || ws.reserve <= 0) return;
    ws.reloading = wd->reload_time;
}

inline void tick_weapons(World& w, float dt) {
    w.each<WeaponState>([&](Entity, WeaponState& ws) {
        if (ws.cooldown > 0.0f) ws.cooldown = std::max(0.0f, ws.cooldown - dt);
        if (ws.reloading > 0.0f) {
            ws.reloading -= dt;
            if (ws.reloading <= 0.0f) {
                ws.reloading = 0.0f;
                if (const WeaponDef* wd = find_weapon(ws.weapon_id)) {
                    const int need = wd->mag_size - ws.ammo_in_mag;
                    const int take = std::min(need, ws.reserve);
                    ws.ammo_in_mag += take; ws.reserve -= take;
                }
            }
        }
    });
}

// Advance projectiles: move, collide vs hurtboxes (apply damage), expire.
inline void tick_projectiles(World& w, float dt, GameplayEventBus* bus = nullptr) {
    std::vector<Entity> dead;
    w.each<Projectile, Transform>([&](Entity e, Projectile& pr, Transform& t) {
        t.position += pr.velocity * dt;
        pr.lifetime -= dt;
        bool hit = false;
        w.each<CombatActor, Transform>([&](Entity te, CombatActor& ca, Transform& tt) {
            if (hit || static_cast<uint32_t>(te) == pr.owner) return;
            const HitSphere hb = combat_hurtbox(ca, tt.position);
            const glm::vec3 d = t.position - hb.center;
            const float rr = hb.radius + pr.radius;
            if (glm::dot(d, d) <= rr * rr) {
                const float dealt = apply_damage(w, te, DamageInfo{pr.damage, pr.damage_type});
                if (bus) { GameplayEvent ev; ev.name = "projectile.hit"; ev.instigator = static_cast<Entity>(pr.owner);
                           ev.target = te; ev.value = dealt; bus->publish(ev); }
                hit = true;
            }
        });
        if (hit || pr.lifetime <= 0.0f) dead.push_back(e);
    });
    for (Entity e : dead) w.destroy(e);
}

// ---------------- custom serialization ----------------
inline void serialize_weapon_state(const void* obj, std::vector<uint8_t>& out) {
    const auto& ws = *static_cast<const WeaponState*>(obj);
    detail::put_str(out, ws.weapon_id);
    detail::put_i32(out, ws.ammo_in_mag);
    detail::put_i32(out, ws.reserve);
}
inline void deserialize_weapon_state(void* obj, const uint8_t* data, size_t n) {
    auto& ws = *static_cast<WeaponState*>(obj);
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_str(p, e, ws.weapon_id)) return;
    detail::get_i32(p, e, ws.ammo_in_mag);
    detail::get_i32(p, e, ws.reserve);
    ws.cooldown = 0.0f; ws.reloading = 0.0f;
}
inline void register_weapon_components() {
    register_authorable(make_authorable_custom<WeaponState>("Weapon", &serialize_weapon_state, &deserialize_weapon_state));
}

}  // namespace schizo::ecs
