# survivor.py — apply to a survivor with Needs / Sanity / Flashlight components
# (Inspector ▸ Add Component ▸ Gameplay). Then Play:
#   F = toggle flashlight    (battery drains while on; sanity drains in the dark
#   and near Fear Sources, regenerates in light)
# Needs (Hunger/Thirst) decay automatically; restore them with a food item
# (engine.use_item(e, "food_id")). Watch need.critical / sanity.low in the log.
import engine

prev_f = False

def on_start(e):
    engine.log("survivor ready on entity " + str(e) + " — F toggles the flashlight")

def on_update(e, dt):
    global prev_f
    f = engine.key_down(70)   # GLFW 'F'
    if f and not prev_f:
        on = engine.toggle_flashlight(e)
        engine.log("flashlight " + ("on" if on else "off"))
    prev_f = f
