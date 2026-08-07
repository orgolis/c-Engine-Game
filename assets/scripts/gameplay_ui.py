# gameplay_ui.py — apply to the player to show the gameplay UI. On start it turns
# on the HUD overlay (health/stamina/sanity/ammo/nitro bars + a quest tracker,
# whichever components the entity has). Keys toggle the full-screen panels:
#   I = inventory     C = character / stats     J = quest journal
# The engine renders these; the script just sets the "ui.*" intent tags.
import engine

prev = {}

def toggle(e, key, tag):
    down = engine.key_down(key)
    was = prev.get(key, False)
    prev[key] = down
    if down and not was:
        if engine.has_tag(e, tag): engine.remove_tag(e, tag)
        else: engine.add_tag(e, tag)

def on_start(e):
    engine.add_tag(e, "ui.hud")
    engine.log("gameplay_ui ready — HUD on; I inventory, C character, J quests")

def on_update(e, dt):
    toggle(e, 73, "ui.inventory_open")   # I
    toggle(e, 67, "ui.character_open")   # C
    toggle(e, 74, "ui.quest_open")       # J
