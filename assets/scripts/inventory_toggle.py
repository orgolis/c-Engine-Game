# inventory_toggle.py — apply to the Player to open an interactable inventory GUI.
#
# Attach via Inspector > Add Component > Script (path: assets/scripts/inventory_toggle.py),
# then Play and press  I  to open/close the inventory. Inside the window you can
# Use / Equip / Drop items and Unequip gear — all live.
#
# How it works: scripts can't draw ImGui directly, so this script toggles a
# "UI intent" tag ("ui.inventory_open") on the entity; the engine renders the
# interactable inventory window for any entity carrying that tag.
import engine

prev_i = False

def on_start(e):
    # Seed a starter loadout if the player has no items yet (uses the demo catalog:
    # potion_hp, sword_iron, helm_set, chest_set). Remove this block if the player
    # already gets items elsewhere.
    if engine.item_count(e, "potion_hp") == 0 and engine.item_count(e, "sword_iron") == 0:
        engine.add_item(e, "potion_hp", 5)
        engine.add_item(e, "sword_iron", 1)
        engine.add_item(e, "helm_set", 1)
        engine.add_item(e, "chest_set", 1)
    engine.log("inventory_toggle ready on entity " + str(e) + " — press I to open the inventory")

def on_update(e, dt):
    global prev_i
    down = engine.key_down(73)   # GLFW key code for 'I'
    if down and not prev_i:      # rising edge = one toggle per press
        if engine.has_tag(e, "ui.inventory_open"):
            engine.remove_tag(e, "ui.inventory_open")
        else:
            engine.add_tag(e, "ui.inventory_open")
    prev_i = down
