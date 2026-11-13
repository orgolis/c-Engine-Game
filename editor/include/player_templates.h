#pragma once

// ============================================================================
// Player templates (C6): Create > Player gives a player that already plays --
// first person, third person, a shooter, or an action RPG -- instead of a
// capsule and a camera to be fitted out component by component.
// ============================================================================
//
// The scene side (capsule, collider, camera) is EntityFactory::CreatePlayer;
// this adds the gameplay components once the new entity has a gameplay entity
// (the bridge mirrors scene entities each frame, so a template waits a frame).
// Weapons, ammunition and abilities come from a starter .items file written to
// the project's assets/gameplay/ the first time it is needed, so they are
// ordinary data the Items editor opens.

#include "ecs/gameplay_ability_cast.h"
#include "ecs/gameplay_attributes.h"
#include "ecs/gameplay_camera.h"
#include "ecs/gameplay_inventory.h"
#include "ecs/gameplay_item_file.h"
#include "ecs/gameplay_life.h"
#include "ecs/gameplay_movement.h"
#include "ecs/gameplay_savegame.h"
#include "ecs/gameplay_tags.h"
#include "ecs/gameplay_triggers.h"
#include "ecs/gameplay_weapons.h"
#include "ecs_bridge.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace schizo::editor {

enum class PlayerTemplate : uint8_t { Bare, FirstPerson, ThirdPerson, Shooter, ActionRpg };

inline const char* player_template_name(PlayerTemplate t) {
    switch (t) {
        case PlayerTemplate::Bare:        return "Bare (capsule and camera)";
        case PlayerTemplate::FirstPerson: return "First person";
        case PlayerTemplate::ThirdPerson: return "Third person";
        case PlayerTemplate::Shooter:     return "Shooter";
        case PlayerTemplate::ActionRpg:   return "Action RPG";
    }
    return "?";
}

inline const char* starter_items_text() {
    return "# Starter gameplay data, written by Create > Player templates.\n"
           "# Open it in the Items editor (double-click) and change anything.\n"
           "\n"
           "item rifle_rounds\n"
           "  name Rifle Rounds\n"
           "  kind material\n"
           "  max_stack 300\n"
           "  weight 0.01\n"
           "end\n"
           "\n"
           "item health_potion\n"
           "  name Health Potion\n"
           "  kind consumable\n"
           "  max_stack 10\n"
           "  weight 0.2\n"
           "  on_use Health 40\n"
           "end\n"
           "\n"
           "weapon starter_rifle\n"
           "  name Rifle\n"
           "  fire auto\n"
           "  damage 18 kinetic\n"
           "  rpm 600\n"
           "  magazine 30\n"
           "  reload 1.8\n"
           "  spread 0.5\n"
           "  bloom 0.3 4 6\n"
           "  recoil 0.6 0.25\n"
           "  aim 0.7 0.35 0.15\n"
           "  range 150\n"
           "  tracer 3\n"
           "  impact 0.12\n"
           "  flash\n"
           "  ammo rifle_rounds\n"
           "end\n"
           "\n"
           "ability firebolt\n"
           "  name Firebolt\n"
           "  cost Mana 15\n"
           "  cooldown 1.5\n"
           "  cast 0.35\n"
           "  target aimed\n"
           "  range 35\n"
           "  damage 30 fire\n"
           "  vfx firebolt\n"
           "end\n"
           "\n"
           "ability frost_nova\n"
           "  name Frost Nova\n"
           "  cost Mana 25\n"
           "  cooldown 8\n"
           "  target area\n"
           "  radius 5\n"
           "  damage 15 cold\n"
           "  effect Speed -3 3\n"
           "  vfx frost_nova\n"
           "end\n"
           "\n"
           "ability mend\n"
           "  name Mend\n"
           "  cost Mana 20\n"
           "  cooldown 12\n"
           "  cast 1.5\n"
           "  still\n"
           "  target self\n"
           "  self Health 40\n"
           "  vfx mend\n"
           "end\n";
}

/// Make sure the starter data is in the project and registered.
inline void ensure_starter_data(std::string dir = "assets/gameplay") {
    namespace fs = std::filesystem;
    if (const char* d = std::getenv("GWS_GAMEPLAY_DIR"); d && *d) dir = d;   // tests keep it out of the project
    const fs::path p = fs::path(dir) / "starter.items";
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        fs::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary);
        if (f) f << starter_items_text();
    }
    if (!schizo::ecs::find_weapon("starter_rifle") || !schizo::ecs::find_ability_def("firebolt")) {
        if (schizo::ecs::load_items_file(p.string()) == 0) schizo::ecs::register_items_text(starter_items_text());
    }
}

/// Give the player's gameplay entity what the template plays with. False if
/// the bridge has not mirrored the entity yet (try again next frame).
/// The template's gameplay components on a gameplay entity.
inline void outfit_player(schizo::ecs::World& w, schizo::ecs::Entity e, PlayerTemplate t) {
    namespace ecs = schizo::ecs;
    if (t == PlayerTemplate::Bare) return;
    ensure_starter_data();

    auto put = [&](auto comp) {
        using T = decltype(comp);
        if (w.has<T>(e)) w.get<T>(e) = comp;
        else w.add<T>(e, comp);
    };
    ecs::AttributeSet attrs;
    attrs.define("Health", 100, 0, 100);
    attrs.define("Stamina", 100, 0, 100);
    attrs.define("Speed", 5, 0, 20);
    if (t == PlayerTemplate::ActionRpg) attrs.define("Mana", 100, 0, 100);
    put(attrs);
    ecs::GameplayTags tags;
    tags.add("ui.hud");
    tags.add("team.player");
    put(tags);
    put(ecs::SaveId{ecs::kSaveKeyPlayer});
    ecs::Mortal mortal;
    mortal.respawns = true;   // from the last save, or where Play started
    put(mortal);
    ecs::Inventory inv;
    inv.max_slots = 24;
    put(ecs::Movement{});
    if (t == PlayerTemplate::ThirdPerson || t == PlayerTemplate::ActionRpg) put(ecs::CameraRig{});
    if (t == PlayerTemplate::Shooter) {
        ecs::WeaponState ws;
        ws.weapon_id = "starter_rifle";
        ws.ammo_in_mag = 30;
        put(ws);
        ecs::ItemInstance rounds;
        rounds.def_id = "rifle_rounds";
        rounds.quantity = 150;
        ecs::inventory_add(inv, rounds);
    }
    if (t == PlayerTemplate::ActionRpg) {
        ecs::AbilityBar bar;
        bar.slots = {"firebolt", "frost_nova", "mend"};
        put(bar);
        ecs::ItemInstance pot;
        pot.def_id = "health_potion";
        pot.quantity = 3;
        ecs::inventory_add(inv, pot);
    }
    put(inv);
    put(ecs::TriggerActor{});   // checkpoints and other trigger volumes notice the player
}

/// Give the player's gameplay entity what the template plays with. False if
/// the bridge has not mirrored the entity yet (try again next frame).
inline bool apply_player_template(EcsSceneBridge& bridge, const schizo::scene::Transform* tf, PlayerTemplate t) {
    namespace ecs = schizo::ecs;
    if (t == PlayerTemplate::Bare) return true;
    const uint32_t id = bridge.ecs_entity_id(tf);
    ecs::World& w = bridge.world();
    const auto e = static_cast<ecs::Entity>(id);
    if (id == kNoEcsEntity || !w.valid(e)) return false;
    outfit_player(w, e, t);
    return true;
}

}  // namespace schizo::editor
