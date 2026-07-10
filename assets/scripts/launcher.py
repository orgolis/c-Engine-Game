# launcher.py — press F while playing to launch a glowing physics cube
# from this entity's position. Shows input, spawning, physics and rendering
# from one script.
import engine

cooldown = 0.0

def on_start(e):
    engine.log("launcher ready on entity " + str(e) + " — press F to fire")

def on_update(e, dt):
    global cooldown
    cooldown = cooldown - dt
    if engine.key_down(engine.KEY_F) and cooldown <= 0.0:
        cooldown = 0.35
        p = engine.get_position(e)
        c = engine.spawn_cube(p[0], p[1] + 1.5, p[2], 0.5, 1.0, 0.45, 0.1, True)
        engine.add_impulse(c, 0.0, 3.0, -9.0)
        engine.set_emissive(c, 1.0, 0.45, 0.1, 3.0)
        engine.audio_play(e)   # fires the entity's AudioSource, if it has one
