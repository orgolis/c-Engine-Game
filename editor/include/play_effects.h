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

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace schizo::editor {

class PlayEffects {
public:
    // ---- guns ------------------------------------------------------------------------------
    void gun_fired(const schizo::ecs::WeaponDef& wd, const glm::vec3& muzzle, const glm::vec3& dir,
                   const std::vector<schizo::ecs::ShotTrace>& shots) {
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
            if (s.hit == schizo::ecs::ShotTrace::Hit::None) continue;
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
        if (const auto* e = effect(d, "trail")) fx_.stream(*e, from, to, dt);
    }
    /// One in flight took effect.
    void ability_impact(const schizo::ecs::AbilityDef& d, const schizo::ecs::AbilityImpact& im) {
        schizo::vfx::EffectSpawn at;
        at.pos = im.point + im.normal * 0.05f;
        at.dir = -im.normal;
        at.normal = im.normal;
        if (d.targeting == schizo::ecs::AbilityTargeting::AreaAtAim) at.scale = std::max(0.3f, d.radius / 3.0f);
        if (const auto* e = effect(d, "hit")) fx_.burst(*e, at);
    }

    // ---- the frame -------------------------------------------------------------------------
    void update(float dt) { fx_.update(dt); }
    /// Append this frame's billboards (camera-facing, farthest first).
    void build(const glm::vec3& cam_pos, std::vector<schizo::vfx::BillboardVertex>& out) const { fx_.build(cam_pos, out); }
    void clear() { fx_.clear(); }
    void apply_shift(const glm::vec3& d) { fx_.apply_shift(d); }
    size_t alive() const { return fx_.alive(); }
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
