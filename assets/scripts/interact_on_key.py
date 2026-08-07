# interact_on_key.py — apply to the player. Press E to use the nearest
# interactable in range (pickups, chests, levers, NPCs...).
#
# An interactable is any entity with an "Interactable" component (add it in the
# Inspector). Give it a "Pickup" component to grant an item, or an Inventory to
# make it a lootable container. interact() fires the interactable's event on the
# gameplay event bus, so other scripts/systems can react to it.
import engine

prev_e = False

def on_start(e):
    engine.log("interact_on_key ready on entity " + str(e) + " — press E near an interactable")

def on_update(e, dt):
    global prev_e
    down = engine.key_down(69)   # GLFW key code for 'E'
    if down and not prev_e:
        if engine.interact(e):
            engine.log("interacted")
    prev_e = down
