# spinner.py — rotates this entity and pulses an emissive glow.
# Attach via Inspector: Add Component -> Script, path: assets/scripts/spinner.py
# Public fields (edit them in the Inspector under "Parameters"):
# @param speed float 90.0 Spin Speed (deg/s)
# @param glow  bool  true  Emissive Pulse
import engine
import math

angle = 0.0
speed = 90.0
glow  = True

def on_start(e):
    global speed, glow
    speed = engine.get_param_float(e, "speed", 90.0)   # Inspector-authored value
    glow  = engine.get_param_bool(e, "glow", True)
    engine.log("spinner on " + str(e) + " speed=" + str(speed) + " glow=" + str(glow))

def on_update(e, dt):
    global angle
    angle = angle + dt * speed
    engine.set_rotation(e, 0.0, angle, 0.0)
    if glow:
        g = 0.5 + 0.5 * math.sin(engine.time() * 3.0)
        engine.set_emissive(e, 0.2, 0.6, 1.0, g * 4.0)
