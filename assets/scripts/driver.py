# driver.py — apply to an entity with a Vehicle component (Inspector ▸ Add
# Component ▸ Gameplay ▸ Vehicle, pick a vehicle). Then Play and drive:
#   W / S = throttle / reverse    A / D = steer    Space = brake    Shift = boost
# The arcade drive model integrates on the entity's Transform each frame.
import engine

def on_start(e):
    engine.log("driver ready on entity " + str(e) + " — WASD to drive, Space brake, Shift boost")

def on_update(e, dt):
    throttle = 0.0
    if engine.key_down(engine.KEY_W): throttle += 1.0
    if engine.key_down(engine.KEY_S): throttle -= 1.0
    steer = 0.0
    if engine.key_down(engine.KEY_D): steer += 1.0
    if engine.key_down(engine.KEY_A): steer -= 1.0
    brake = engine.key_down(engine.KEY_SPACE)
    boost = engine.key_down(engine.KEY_LSHIFT)
    engine.drive(e, throttle, steer, brake, boost)
