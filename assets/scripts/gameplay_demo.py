# gameplay_demo.py — drive the G0–G4 gameplay systems from a script.
# Attach to a "Gameplay Test Dummy" (Add Entity > Gameplay Test Dummy), then Play:
#   SPACE = dash ability   |   1 = grant 100 XP   |   2 = unlock a skill
#   3 = use a Health Potion (heals)   |   4 = take 25 physical damage
# Watch the values change live in Inspector > Gameplay Components (ECS).
import engine

prev = {}

def edge(name, down):
    global prev
    was = prev.get(name, False)
    prev[name] = down
    return down and not was

def on_start(e):
    engine.log("gameplay_demo on entity " + str(e)
               + " — Health=" + str(engine.get_attribute(e, "Health"))
               + " Level=" + str(engine.get_level(e)))

def on_update(e, dt):
    # SPACE: dash (ability 0). Costs Stamina, on cooldown — grants state.dashing.
    if edge("space", engine.key_down(engine.KEY_SPACE)):
        ok = engine.activate_ability(e, 0, 0)
        engine.log("dash -> " + str(ok) + "  Stamina=" + str(engine.get_attribute(e, "Stamina")))

    # 1: gain XP (levels up at 100; raises Strength via per-level growth).
    if edge("k1", engine.key_down(49)):
        engine.grant_xp(e, 100.0)
        engine.log("level = " + str(engine.get_level(e)))

    # 2: unlock the first skill node (spends a skill point, +5 Strength).
    if edge("k2", engine.key_down(50)):
        ok = engine.unlock_skill(e, "might1")
        engine.log("unlock might1 -> " + str(ok) + "  Strength=" + str(engine.get_attribute(e, "Strength")))

    # 3: use a Health Potion from the inventory (heals +40).
    if edge("k3", engine.key_down(51)):
        ok = engine.use_item(e, "potion_hp")
        engine.log("use potion -> " + str(ok) + "  Health=" + str(engine.get_attribute(e, "Health")))

    # 4: take 25 physical damage (routed through G0 resist/immunity).
    if edge("k4", engine.key_down(52)):
        dealt = engine.apply_damage(e, 25.0, "physical")
        engine.log("took " + str(dealt) + " dmg  Health=" + str(engine.get_attribute(e, "Health")))
