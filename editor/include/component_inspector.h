#pragma once

// Generic, reflection-driven inspector for an entity's authorable ECS components
// (F2). Lives in its own translation unit so main.cpp needs no EnTT / ECS include.

#include "edit_coalescer.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "reflection/reflection.h"

namespace schizo::scene { class Transform; }

namespace schizo::editor {

class EcsSceneBridge;

// Render an "ECS Components" section for the OOP entity that owns `tf`: lists its
// authorable gameplay components (Health, Ability State, …), edits each one's
// fields generically via core reflection, and offers Add / Remove. Returns true
// if anything changed this frame.
// Write a serialized component blob back onto an entity's ECS component.
// Lives here rather than in main.cpp because main.cpp deliberately includes no
// EnTT/ECS headers -- that separation is why this translation unit exists. Used
// by the undo/redo path for inspector field edits.
void apply_component_bytes(EcsSceneBridge& bridge, uint32_t entity_id,
                           const std::string& component,
                           const std::vector<uint8_t>& bytes);

// `coalescer` and `on_edit` are optional. When supplied, field edits are
// coalesced into ONE undo entry per drag (see edit_coalescer.h) and reported
// through on_edit. Passing null keeps the old behaviour, which is what the
// headless checks want -- they have no undo stack.
bool draw_ecs_component_inspector(EcsSceneBridge& bridge, schizo::scene::Transform* tf,
                                  EditCoalescer* coalescer = nullptr,
                                  const std::function<void(const CoalescedEdit&)>& on_edit = {});

// Draw a floating, interactable inventory window for every ECS entity that has an
// Inventory AND the tag "ui.inventory_open" (a "UI intent" tag a gameplay script
// toggles via add_tag/remove_tag). Reuses the G4 inventory ops (use/equip/drop/
// unequip). Call once per frame. This is how a script "opens the inventory GUI":
// scripts can't draw ImGui, so the script sets the intent and the engine renders.
/// Draw every reflected field of `obj` with a type-appropriate widget.
/// Returns true if anything was edited.
///
/// The same drawer the ECS inspector uses, exposed so the OOP scene components
/// can share it. That matters more than saving code: it means a field added to
/// a reflected component appears in the inspector AND in the save file with no
/// UI or serializer edit, so the two cannot drift apart -- a component you can
/// configure but not save is worse than one you cannot configure.
bool draw_reflected_fields(void* obj, const gws::reflect::TypeInfo& ti);

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
