#pragma once
// ============================================================================
// register_all_components() — the ONE place that knows which gameplay modules
// contribute authorable components.
//
// Why this exists: the list used to be duplicated. The editor's EcsSceneBridge
// constructor had one copy and tools/gameplay_check had another, so a new
// module had to be registered twice and could silently be missing from one of
// them. Adding a docs generator (issue #10) would have made a third copy, and a
// generated reference that quietly omits a module is worse than no reference.
//
// Adding a module is now one line here, and the editor, the checks and the
// generated documentation all pick it up together.
//
// Registration is idempotent — register_authorable() replaces an entry with the
// same name — so calling this more than once is harmless.
// ============================================================================
#include "components.h"
#include "authorable_components.h"

#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "gameplay_triggers.h"
#include "gameplay_state_machine.h"
#include "gameplay_combat.h"
#include "gameplay_progression.h"
#include "gameplay_skills.h"
#include "gameplay_inventory.h"
#include "gameplay_economy.h"
#include "gameplay_interaction.h"
#include "gameplay_quests.h"
#include "gameplay_npc.h"
#include "gameplay_savegame.h"
#include "gameplay_weapons.h"
#include "gameplay_vehicles.h"
#include "gameplay_building.h"
#include "gameplay_survival.h"
#include "gameplay_stealth.h"

namespace schizo::ecs {

// Register the core component set plus every gameplay module's authorable
// components. Order is presentation order in the inspector and the generated
// docs; it carries no dependency meaning.
inline void register_all_components() {
    register_core_components();
    register_attribute_component();      // G0  AttributeSet
    register_tags_component();           // G0  Gameplay Tags
    register_trigger_components();       // G0  Trigger Volume / Trigger Actor
    register_state_machine_component();  // G1  State Machine
    register_combat_component();         // G2  Combat Actor
    register_progression_components();   // G3  Derived Stats / Regeneration / Progression
    register_skill_components();         // G3  Skill Tree / Unlocked Skills
    register_inventory_components();     // G4  Inventory / Equipment
    register_economy_components();       // G5  Vendor / Harvest Node
    register_quest_component();          // G6  Quest Log
    register_interaction_components();   // G7  Interactable / Pickup
    register_npc_components();           // G8  Faction / Aggro / Spawner
    register_persistence_components();   // G9  Save Id / World Flags
    register_weapon_components();        // G12 Weapon
    register_vehicle_components();       // G13 Vehicle / Race Progress
    register_building_components();      // G14 Building / Extractor / Machine / Generator / Conveyor
    register_survival_components();      // G15 Needs / Sanity / Flashlight / Light Source / Fear Source
    register_stealth_components();       // G16 Vision Cone / Awareness / Stealth / Hearing
}

} // namespace schizo::ecs
