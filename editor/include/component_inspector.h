#pragma once

// Generic, reflection-driven inspector for an entity's authorable ECS components
// (F2). Lives in its own translation unit so main.cpp needs no EnTT / ECS include.

namespace schizo::scene { class Transform; }

namespace schizo::editor {

class EcsSceneBridge;

// Render an "ECS Components" section for the OOP entity that owns `tf`: lists its
// authorable gameplay components (Health, Ability State, …), edits each one's
// fields generically via core reflection, and offers Add / Remove. Returns true
// if anything changed this frame.
bool draw_ecs_component_inspector(EcsSceneBridge& bridge, schizo::scene::Transform* tf);

// Draw a floating, interactable inventory window for every ECS entity that has an
// Inventory AND the tag "ui.inventory_open" (a "UI intent" tag a gameplay script
// toggles via add_tag/remove_tag). Reuses the G4 inventory ops (use/equip/drop/
// unequip). Call once per frame. This is how a script "opens the inventory GUI":
// scripts can't draw ImGui, so the script sets the intent and the engine renders.
void draw_inventory_ui(EcsSceneBridge& bridge);

// G11 · gameplay UI. Draws, for entities tagged with the matching "ui.*" intent
// (a script toggles them via add_tag/remove_tag), a HUD overlay (health/stamina/
// sanity/ammo/awareness + a quest tracker) and the character-sheet and quest-
// journal windows — plus the inventory window above. Call once per frame; scripts
// can't draw ImGui, so they set the intent tags and the engine renders.
//   ui.hud            -> HUD overlay              ui.inventory_open -> inventory
//   ui.character_open -> character/stats sheet    ui.quest_open     -> quest journal
void draw_gameplay_ui(EcsSceneBridge& bridge);

}  // namespace schizo::editor
