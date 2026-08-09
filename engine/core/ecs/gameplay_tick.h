#pragma once
// ============================================================================
// tick_all_gameplay() — the ONE gameplay frame.
//
// Extracted from EcsSceneBridge::tick_gameplay for the same reason the
// registration list was extracted (see register_all.h): a headless runner needs
// to tick exactly what the editor ticks, and a second copy of this list would
// drift. A headless test that silently ticks fewer systems than play mode is
// worse than no headless test, because it reports green while covering less.
//
// Add a gameplay system here once and the editor, the headless runner and any
// automated play session all get it.
// ============================================================================
#include "world.h"

#include "gameplay_abilities.h"
#include "gameplay_building.h"
#include "gameplay_combat.h"
#include "gameplay_crafting.h"
#include "gameplay_economy.h"
#include "gameplay_effects.h"
#include "gameplay_events.h"
#include "gameplay_npc.h"
#include "gameplay_progression.h"
#include "gameplay_state_machine.h"
#include "gameplay_stealth.h"
#include "gameplay_survival.h"
#include "gameplay_triggers.h"
#include "gameplay_vehicles.h"
#include "gameplay_weapons.h"

namespace schizo::ecs {

// Frame-data combat runs at a FIXED 60 Hz regardless of the caller's frame
// rate, so startup/active/recovery windows stay deterministic. `combat_accum`
// carries the leftover time between calls and belongs to the caller.
inline constexpr float kCombatStep = 1.0f / 60.0f;

inline void tick_all_gameplay(World& w,
                              GameplayEventBus& bus,
                              TimerManager& timers,
                              float& combat_accum,
                              float dt) {
    tick_effects(w, dt);                    // active effects: periodic ticks + expiry
    tick_abilities(w, dt);                  // ability cooldowns
    tick_regen(w, dt);                      // G3: resource regeneration
    tick_state_machines(w, &bus, dt);       // G1: FSM start/age/timeout -> events

    combat_accum += dt;
    for (int guard = 0; combat_accum >= kCombatStep && guard < 8; ++guard) {
        tick_combat(w, bus);                // G2: frame-data melee at a fixed step
        combat_accum -= kCombatStep;
    }

    tick_triggers(w, bus);                  // enter/exit -> events
    tick_harvest(w, dt);                    // G5: harvest-node respawn cooldowns
    tick_spawners(w, dt, &bus);             // G8: encounter spawner requests
    tick_weapons(w, dt);                    // G12: weapon cooldown / reload
    tick_projectiles(w, dt, &bus);          // G12: projectile travel + hits
    tick_race(w, dt, &bus);                 // G13: race clocks / checkpoints / laps
    tick_factory(w, dt, &bus);              // G14: extractors / machines / conveyors / power
    tick_survival(w, dt, &bus);             // G15: needs / sanity / flashlight / fear
    tick_stealth(w, dt, &bus);              // G16: vision cones / awareness
    timers.tick(dt);                        // one-shot / repeating timers
    bus.flush();                            // dispatch everything queued this frame
}

} // namespace schizo::ecs
