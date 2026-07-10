# spinner.py — rotates this entity and pulses an emissive glow.
# Attach via Inspector: Add Component -> Script, path: assets/scripts/spinner.py
import engine
import math

angle = 0.0

def on_start(e):
    engine.log("spinner attached to entity " + str(e))

def on_update(e, dt):
    global angle
    angle = angle + dt * 90.0          # 90 deg/s
    engine.set_rotation(e, 0.0, angle, 0.0)
    glow = 0.5 + 0.5 * math.sin(engine.time() * 3.0)
    engine.set_emissive(e, 0.2, 0.6, 1.0, glow * 4.0)
