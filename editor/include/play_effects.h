#pragma once
// ============================================================================
// play_effects -- what guns, abilities and creatures' attacks look like in Play.
//
// The particles are vfx/effects.h's (effect blocks in the project's .items
// files, else the engine's built-in set); this picks which effect each moment
// gets and where it goes:
//
//   a shot     <weapon>_muzzle at the muzzle, <weapon>_tracer along each
//              tracer round, <weapon>_impact where a round hits the world,
//              <weapon>_hit where it hits someone (each else the plain name)
//   water      <weapon>_splash (else splash) where a round, a beam or
//              something in flight meets water, splash_big where something
//              lands in it with an area -- and rings spread on the water
//   an ability <vfx>_windup while it is cast, _cast as it goes off, _beam to
//              where an instant aimed one reached, _trail while one flies,
//              _hit where it lands, _area around the caster, _self on it --
//              each else the element's (fire, cold, shock, poison, physical,
//              explosive, magic, heal) built-in one
//
// The billboards go to the same particle pass as the scene's emitters.
// ============================================================================

#include "ecs/gameplay_ability_cast.h"
#include "ecs/gameplay_weapons.h"
#include "vfx/effects.h"

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace schizo::editor {

class PlayEffects {
public:
    std::function<void(const schizo::ecs::WeaponDef&,const glm::vec3&,const glm::vec3&,const std::vector<schizo::ecs::ShotTrace>&)> network_gun;
    std::function<void(int,const std::string&,const glm::vec3&,const glm::vec3&,float)> network_ability;
    // ---- water -----------------------------------------------------------------------------
    /// Where the segment a -> b first goes into water (set by the editor in
    /// Play: the physical water volumes); none: no splashes.
    std::function<bool(const glm::vec3& a, const glm::vec3& b, glm::vec3& at)> water_crossing;
    /// A ring on the water at `at`, `strength` about 0.2 (a step) to 2 (a blast).
    std::function<void(const glm::vec3& at, float strength)> on_ripple;
    /// Something touched the water at `at`: a splash and its rings.
    void splash(const glm::vec3& at, float strength, const std::string& own = std::string()) {
        using schizo::vfx::find_effect_of;
        schizo::vfx::EffectSpawn s;
        s.pos = at + glm::vec3(0.0f, 0.02f, 0.0f);
        s.dir = glm::vec3(0.0f, 1.0f, 0.0f);
        s.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        const auto* e = strength >= 1.2f ? find_effect_of({own.empty() ? std::string("splash_big") : own + "_splash", "splash_big"})
                                         : find_effect_of({own.empty() ? std::string("splash") : own + "_splash", "splash"});
        if (e) fx_.burst(*e, s);
        if (on_ripple) on_ripple(at, strength);
        static const bool log_splash = std::getenv("GWS_TEST_PLAY") != nullptr;
        if (log_splash)
            spdlog::info("[test] water: a splash at ({:.1f}, {:.2f}, {:.1f}), strength {:.2f}", at.x, at.y, at.z, strength);
    }
    /// A round, a beam: a splash where a -> b goes into water (before what it hit).
    bool splash_along(const glm::vec3& a, const glm::vec3& b, float strength, const std::string& own = std::string()) {
        glm::vec3 w;
        const bool hit = water_crossing && water_crossing(a, b, w);
        static const bool log_water = std::getenv("GWS_TEST_WATER_LOG") != nullptr;
        if (log_water)
            spdlog::info("[test] water: ({:.1f}, {:.2f}, {:.1f}) -> ({:.1f}, {:.2f}, {:.1f}): {}", a.x, a.y, a.z, b.x, b.y, b.z,
                         !water_crossing ? "no test" : hit ? "goes in" : "stays out");
        if (!hit) return false;
        splash(w, strength, own);
        return true;
    }

    // ---- guns ------------------------------------------------------------------------------
    void gun_fired(const schizo::ecs::WeaponDef& wd, const glm::vec3& muzzle, const glm::vec3& dir,
                   const std::vector<schizo::ecs::ShotTrace>& shots) {
        if(network_gun)network_gun(wd,muzzle,dir,shots);
        using schizo::vfx::find_effect_of;
        schizo::vfx::EffectSpawn at;
        at.pos = muzzle;
        at.dir = glm::length(dir) > 1e-5f ? glm::normalize(dir) : glm::vec3(0.0f, 0.0f, -1.0f);
        if (const auto* m = find_effect_of({wd.id + "_muzzle", "muzzle"})) fx_.burst(*m, at);
        for (const auto& s : shots) {
            const glm::vec3 path = s.end - muzzle;
            const float len = glm::length(path);
            if (s.tracer && len > 0.5f)
                if (const auto* t = find_effect_of({wd.id + "_tracer", "tracer"})) {
                    schizo::vfx::EffectSpawn tr;
                    tr.pos = muzzle;
                    tr.dir = path / len;
                    tr.reach = len;
                    fx_.burst(*t, tr);
                }
            // Into water on the way: a splash where it went in.
            const bool wet = splash_along(muzzle, s.end, 0.55f, wd.id);
            if (s.hit == schizo::ecs::ShotTrace::Hit::None) continue;
            if (wet && s.hit == schizo::ecs::ShotTrace::Hit::World) continue;   // under water: no sparks on the bed
            schizo::vfx::EffectSpawn hit;
            hit.pos = s.end;
            hit.dir = len > 1e-5f ? path / len : at.dir;
            hit.normal = s.hit == schizo::ecs::ShotTrace::Hit::World ? s.normal : -hit.dir;
            hit.pos += hit.normal * 0.03f;
            const auto* e = s.hit == schizo::ecs::ShotTrace::Hit::World ? find_effect_of({wd.id + "_impact", "impact"})
                                                                          : find_effect_of({wd.id + "_hit", "impact_actor"});
            if (e) fx_.burst(*e, hit);
        }
    }

    // ---- abilities -------------------------------------------------------------------------
    /// While one is being cast: its wind-up around `at` (a hand, a creature).
    void ability_windup(const schizo::ecs::AbilityDef& d, const glm::vec3& at, float dt) {
        if (const auto* e = effect(d, "windup")) fx_.stream(*e, at, at, dt);
    }
    /// It went off: from `hand` (what the eye sees it start from).
    void ability_fired(const schizo::ecs::AbilityDef& d, const schizo::ecs::AbilityFired& f, const glm::vec3& hand) {
        if(network_ability)network_ability(7,d.id,f.point,f.dir,0);
        using schizo::ecs::AbilityTargeting;
        schizo::vfx::EffectSpawn at;
        at.pos = hand;
        at.dir = f.dir;
        const float area_scale = std::max(0.3f, d.radius / 3.0f);
        switch (d.targeting) {
            case AbilityTargeting::Self:
                at.pos = f.point;
                if (const auto* e = effect(d, "self")) fx_.burst(*e, at);
                return;
            case AbilityTargeting::AreaAroundSelf:
                at.pos = f.point + glm::vec3(0.0f, 0.15f, 0.0f);
                at.scale = area_scale;
                if (const auto* e = effect(d, "area")) fx_.burst(*e, at);
                return;
            default: break;
        }
        if (const auto* e = effect(d, "cast")) fx_.burst(*e, at);
        if (f.launched) return;   // its trail and hit come as it flies
        // At once: a beam to where it reached, and what it does there.
        const glm::vec3 path = f.point - hand;
        const float len = glm::length(path);
        if (len > 0.3f) splash_along(hand, f.point, d.targeting == AbilityTargeting::AreaAtAim ? 1.4f : 0.6f);
        if (d.targeting == AbilityTargeting::Aimed && len > 0.3f)
            if (const auto* e = effect(d, "beam")) {
                schizo::vfx::EffectSpawn b;
                b.pos = hand;
                b.dir = path / len;
                b.reach = len;
                fx_.burst(*e, b);
            }
        schizo::vfx::EffectSpawn hit;
        hit.pos = f.point;
        hit.dir = len > 1e-5f ? path / len : f.dir;
        hit.normal = -hit.dir;
        if (d.targeting == AbilityTargeting::AreaAtAim) { hit.scale = area_scale; hit.normal = glm::vec3(0.0f, 1.0f, 0.0f); }
        if (const auto* e = effect(d, "hit")) fx_.burst(*e, hit);
    }
    /// One in flight moved from `from` to `to` this frame.
    void ability_flight(const schizo::ecs::AbilityDef& d, const glm::vec3& from, const glm::vec3& to, float dt) {
        if(network_ability)network_ability(9,d.id,from,to,dt);
        if (const auto* e = effect(d, "trail")) fx_.stream(*e, from, to, dt);
        // Into water this frame (a grenade, a firebolt over a lake): a splash.
        splash_along(from, to, d.targeting == schizo::ecs::AbilityTargeting::AreaAtAim ? 1.4f : 0.7f);
    }
    /// One in flight took effect.
    void ability_impact(const schizo::ecs::AbilityDef& d, const schizo::ecs::AbilityImpact& im) {
        if(network_ability)network_ability(8,d.id,im.point,im.normal,im.on_actor?1.f:0.f);
        schizo::vfx::EffectSpawn at;
        at.pos = im.point + im.normal * 0.05f;
        at.dir = -im.normal;
        at.normal = im.normal;
        if (d.targeting == schizo::ecs::AbilityTargeting::AreaAtAim) at.scale = std::max(0.3f, d.radius / 3.0f);
        if (const auto* e = effect(d, "hit")) fx_.burst(*e, at);
        // Landed in shallow water or on the bed under it: the blast throws water.
        if (d.targeting == schizo::ecs::AbilityTargeting::AreaAtAim)
            splash_along(im.point + glm::vec3(0.0f, 3.0f, 0.0f), im.point - glm::vec3(0.0f, 0.2f, 0.0f),
                         std::max(1.2f, d.radius / 2.0f));
    }

    // ---- the frame -------------------------------------------------------------------------
    void update(float dt) { fx_.update(dt); }
    /// Append this frame's billboards (camera-facing, farthest first).
    void build(const glm::vec3& cam_pos, std::vector<schizo::vfx::BillboardVertex>& out) const { fx_.build(cam_pos, out); }
    void clear() { fx_.clear(); }
    void apply_shift(const glm::vec3& d) { fx_.apply_shift(d); }
    size_t alive() const { return fx_.alive(); }
    std::vector<schizo::vfx::EffectParticles::PointLight> lights() const { return fx_.lights(); }
    schizo::vfx::EffectParticles& particles() { return fx_; }

    /// An ability's effect for a stage: <vfx>_<stage>, <element>_<stage>, magic_<stage>.
    static const schizo::vfx::EffectDef* effect(const schizo::ecs::AbilityDef& d, const char* stage) {
        bool restores = d.damage <= 0.0f;
        for (const auto& e : d.target_effects) restores = restores && e.amount >= 0.0f;
        bool gives = false;
        for (const auto& e : d.self_effects) gives = gives || e.amount > 0.0f;
        for (const auto& e : d.target_effects) gives = gives || e.amount > 0.0f;
        const std::string el = schizo::vfx::effect_element(d.damage_type, restores && gives);
        const std::string s = stage;
        return schizo::vfx::find_effect_of({d.vfx.empty() ? std::string() : d.vfx + "_" + s, el + "_" + s, "magic_" + s});
    }

private:
    schizo::vfx::EffectParticles fx_;
};

}  // namespace schizo::editor
