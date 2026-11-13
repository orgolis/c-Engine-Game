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
#include "gameplay_inventory.h"   // ammunition carried as an item
#include "authorable_components.h"
#include "component_serialize.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
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
    float       draw_time = 0.4f;         // seconds from switching to it until it fires
    int         pellets = 1;              // >1 = shotgun
    float       spread_deg = 1.0f;
    float       range = 100.0f;
    float       projectile_speed = 0.0f;  // 0 = hitscan
    float       reload_time = 2.0f;
    // Feel (C3). Every default is "none", so a weapon written before these
    // existed fires exactly as it did.
    int         burst = 3;                // shots per trigger pull in Burst mode
    float       recoil_pitch = 0.0f;      // degrees the view kicks up per shot
    float       recoil_yaw = 0.0f;        // degrees of random sideways kick per shot (+/-)
    float       spread_bloom = 0.0f;      // degrees added to the spread per shot ...
    float       spread_max = 10.0f;       // ... up to this much in all
    float       bloom_recovery = 6.0f;    // degrees per second the bloom settles back
    float       ads_fov_scale = 1.0f;     // the view's field of view while aiming (0.6 = zoomed in)
    float       ads_spread = 1.0f;        // spread while aiming, as a share of hip-fire spread
    float       ads_time = 0.15f;         // seconds to raise the sights
    bool        muzzle_flash = false;
    int         tracer_every = 0;         // a visible tracer every Nth round (0: none)
    glm::vec3   tracer_color = glm::vec3(1.0f, 0.75f, 0.35f);
    float       impact_size = 0.0f;       // mark left where a shot hits the world (0: none)
    std::string ammo_item;                // reserve is this inventory item, when set
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
    // runtime, C3
    float bloom = 0.0f;       // degrees of extra spread from firing
    float aim = 0.0f;         // 0 hip .. 1 sights up
    bool  aiming = false;     // the player holds aim
    int   burst_left = 0;     // rounds still to come in this burst
    int   rounds_fired = 0;   // for "a tracer every Nth round"
    float kick_pitch = 0.0f, kick_yaw = 0.0f;   // recoil not yet given to the view (degrees)
    int   reserve_synced = -1;                  // the reserve last read from the inventory (ammo_item)
};

/// One round's path, for drawing it: where it started, where it stopped,
/// and what stopped it.
struct ShotTrace {
    enum class Hit : uint8_t { None, World, Actor };
    glm::vec3 start{0.0f}, end{0.0f}, normal{0.0f, 1.0f, 0.0f};
    Hit       hit = Hit::None;
    Entity    target = null_entity;
    bool      tracer = false;
};

/// Level geometry for shots: true when the ray from `o` along unit `d` hits
/// something within `max_t`, with the distance and the surface normal there.
using WorldRayFn = std::function<bool(const glm::vec3& o, const glm::vec3& d, float max_t, float& t, glm::vec3& normal)>;

/// The spread a shot leaves with: base plus bloom, narrowed by aiming.
inline float current_spread(const WeaponDef& wd, const WeaponState& ws) {
    const float hip = std::min(wd.spread_deg + ws.bloom, std::max(wd.spread_deg, wd.spread_max));
    return hip * (1.0f + (wd.ads_spread - 1.0f) * ws.aim);
}

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
// With `world`, hitscan rounds also stop at level geometry (a wall shields
// the one behind it); with `shots`, each round's path is reported for tracers
// and impact marks. Recoil goes into ws.kick_* for the view to take.
inline bool fire_weapon(World& w, Entity shooter, const glm::vec3& origin, const glm::vec3& aim,
                        WeaponState& ws, GameplayEventBus* bus = nullptr, uint64_t seed = 1,
                        const WorldRayFn* world = nullptr, std::vector<ShotTrace>* shots = nullptr) {
    const WeaponDef* wd = find_weapon(ws.weapon_id);
    if (!wd || ws.reloading > 0.0f || ws.cooldown > 0.0f || ws.ammo_in_mag <= 0) return false;
    --ws.ammo_in_mag;
    ws.cooldown = 60.0f / (wd->rpm > 1.0f ? wd->rpm : 1.0f);
    ++ws.rounds_fired;
    if (bus) { GameplayEvent ev; ev.name = "weapon.fire"; ev.instigator = shooter; ev.param = ws.weapon_id; bus->publish(ev); }

    const float spread = current_spread(*wd, ws);
    const int pellets = wd->pellets > 0 ? wd->pellets : 1;
    for (int i = 0; i < pellets; ++i) {
        const glm::vec3 dir = apply_spread(aim, spread, seed + static_cast<uint64_t>(i));
        ShotTrace st;
        st.start = origin;
        st.tracer = wd->tracer_every > 0 && (ws.rounds_fired % wd->tracer_every) == 0 && i == 0;
        if (wd->projectile_speed > 0.0f) {
            const Entity pe = w.create();
            Transform t; t.position = origin; w.add<Transform>(pe, t);
            w.add<Projectile>(pe, Projectile{dir * wd->projectile_speed, wd->damage, wd->damage_type,
                                             static_cast<uint32_t>(shooter), wd->range / wd->projectile_speed + 0.1f, 0.1f});
            st.end = origin + dir * std::min(wd->range, 2.0f);   // the muzzle end; the projectile draws itself
        } else {
            float wall_t = wd->range;
            glm::vec3 wall_n(0.0f, 1.0f, 0.0f);
            const bool wall = world && *world && (*world)(origin, dir, wd->range, wall_t, wall_n);
            float t;
            const Entity hit = hitscan_target(w, shooter, origin, dir, wall ? wall_t : wd->range, t);
            if (hit != null_entity) {
                const float dealt = apply_damage(w, hit, DamageInfo{wd->damage, wd->damage_type});
                if (bus) { GameplayEvent ev; ev.name = "weapon.hit"; ev.instigator = shooter; ev.target = hit;
                           ev.value = dealt; ev.param = wd->damage_type; bus->publish(ev); }
                st.end = origin + dir * t;
                st.hit = ShotTrace::Hit::Actor;
                st.target = hit;
                st.normal = -dir;
            } else if (wall) {
                st.end = origin + dir * wall_t;
                st.hit = ShotTrace::Hit::World;
                st.normal = wall_n;
                if (bus) { GameplayEvent ev; ev.name = "weapon.impact"; ev.instigator = shooter; ev.value = wall_t;
                           ev.param = ws.weapon_id; bus->publish(ev); }
            } else {
                st.end = origin + dir * wd->range;
            }
        }
        if (shots) shots->push_back(st);
    }
    // After the round left: spread blooms, the view kicks.
    ws.bloom = std::min(ws.bloom + wd->spread_bloom, std::max(0.0f, wd->spread_max - wd->spread_deg));
    if (wd->recoil_pitch != 0.0f || wd->recoil_yaw != 0.0f) {
        Rng rng(seed * 2654435761u + 17u);
        const float ads = 1.0f - 0.35f * ws.aim;   // a braced aim kicks less
        ws.kick_pitch += wd->recoil_pitch * ads;
        ws.kick_yaw += (rng.unit() * 2.0f - 1.0f) * wd->recoil_yaw * ads;
    }
    return true;
}

/// Trigger handling for a held button: `pressed` is this frame's edge, `held`
/// whether it is down. Semi fires on the press, Auto while held, Burst fires
/// `burst` rounds from one press. Returns rounds fired this call.
inline int trigger_weapon(World& w, Entity shooter, const glm::vec3& origin, const glm::vec3& aim, WeaponState& ws,
                          bool pressed, bool held, GameplayEventBus* bus = nullptr, uint64_t seed = 1,
                          const WorldRayFn* world = nullptr, std::vector<ShotTrace>* shots = nullptr) {
    const WeaponDef* wd = find_weapon(ws.weapon_id);
    if (!wd) return 0;
    bool want = false;
    switch (wd->fire_mode) {
        case FireMode::Semi: want = pressed; break;
        case FireMode::Auto: want = held; break;
        case FireMode::Burst:
            if (pressed && ws.burst_left <= 0) ws.burst_left = std::max(1, wd->burst);
            want = ws.burst_left > 0;
            break;
    }
    if (!want) return 0;
    if (!fire_weapon(w, shooter, origin, aim, ws, bus, seed, world, shots)) {
        if (ws.ammo_in_mag <= 0) ws.burst_left = 0;   // a burst ends with the magazine
        return 0;
    }
    if (wd->fire_mode == FireMode::Burst) --ws.burst_left;
    return 1;
}

/// For a weapon whose ammunition is an inventory item: what a reload took
/// from the reserve comes out of the inventory, and the reserve is then what
/// the inventory holds (picked-up rounds count at once). Call once a frame.
inline void sync_ammo_with_inventory(WeaponState& ws, Inventory& inv) {
    const WeaponDef* wd = find_weapon(ws.weapon_id);
    if (!wd || wd->ammo_item.empty()) return;
    if (ws.reserve_synced >= 0 && ws.reserve < ws.reserve_synced)
        inventory_remove(inv, wd->ammo_item, ws.reserve_synced - ws.reserve);
    ws.reserve = inventory_count(inv, wd->ammo_item);
    ws.reserve_synced = ws.reserve;
}

/// Take the recoil not yet applied to the view (degrees), and clear it.
inline void take_recoil(WeaponState& ws, float& pitch, float& yaw) {
    pitch = ws.kick_pitch;
    yaw = ws.kick_yaw;
    ws.kick_pitch = ws.kick_yaw = 0.0f;
}

inline void reload_weapon(WeaponState& ws) {
    const WeaponDef* wd = find_weapon(ws.weapon_id);
    if (!wd || ws.reloading > 0.0f || ws.ammo_in_mag >= wd->mag_size || ws.reserve <= 0) return;
    ws.reloading = wd->reload_time;
}

inline void tick_weapons(World& w, float dt) {
    w.each<WeaponState>([&](Entity, WeaponState& ws) {
        if (ws.cooldown > 0.0f) ws.cooldown = std::max(0.0f, ws.cooldown - dt);
        if (const WeaponDef* wd = find_weapon(ws.weapon_id)) {
            if (ws.bloom > 0.0f) ws.bloom = std::max(0.0f, ws.bloom - wd->bloom_recovery * dt);
            const float step = wd->ads_time > 1e-4f ? dt / wd->ads_time : 1.0f;
            const bool  can_aim = ws.reloading <= 0.0f;   // the sights come down to reload
            ws.aim = ws.aiming && can_aim ? std::min(1.0f, ws.aim + step) : std::max(0.0f, ws.aim - step);
        }
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
