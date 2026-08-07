# shooter.py — apply to a player that has a Weapon component (Inspector ▸ Add
# Component ▸ Gameplay ▸ Weapon, pick a weapon + set ammo/reserve). Then Play:
#   Left mouse = fire (aims along the entity's forward)   |   R = reload
# Fire resolves hitscan against Combat Actors (or spawns a projectile) and routes
# damage through the G0 pipeline. Watch weapon.fire / weapon.hit in the log.
import engine

prev_r = False

def on_start(e):
    engine.log("shooter ready on entity " + str(e) + " — LMB to fire, R to reload")

def on_update(e, dt):
    global prev_r
    if engine.mouse_down(engine.MOUSE_LEFT):
        engine.fire_weapon(e)          # auto weapons fire while held; cooldown gates the rate
    r = engine.key_down(82)            # GLFW 'R'
    if r and not prev_r:
        if engine.reload_weapon(e):
            engine.log("reloading")
    prev_r = r
