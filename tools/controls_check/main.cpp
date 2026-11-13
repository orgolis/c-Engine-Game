// ============================================================================
// controls_check -- the action map (input/action_map.h): key names, the
// defaults, rebinding, the settings text, and pressed / held / released.
// ============================================================================
#include "input/action_map.h"

#include <cstdio>
#include <set>
#include <string>

using namespace schizo::input;

static int g_fail = 0, g_pass = 0;
static void check(const char* what, bool ok) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    (ok ? g_pass : g_fail)++;
}

int main() {
    // Every named key reads back as itself.
    bool round = true;
    int named = 0;
    for (int c = 1; c <= kMouseWheelDown; ++c) {
        if (c == 400) c = kMouseButton;
        const std::string n = key_name(c);
        if (n == "None") continue;
        ++named;
        if (key_code(n) != c) { round = false; std::printf("  %d -> %s -> %d\n", c, n.c_str(), key_code(n)); }
    }
    check("key names: every named key and mouse button reads back as itself", round && named > 90);
    check("key names: other spellings and case", key_code("space") == 32 && key_code("LMB") == kMouseButton &&
                                                 key_code("ctrl") == 341 && key_code("f6") == 295 && key_code("nonsense") == 0);

    ActionMap m;
    check("defaults: WASD, Space, mouse buttons, 1-8, F6 / F9",
          m.key("move_forward") == 'W' && m.key("jump") == 32 && m.key("fire") == kMouseButton &&
          m.key("aim") == kMouseButton + 1 && m.key("ability_3") == '3' && m.key("quick_save") == 295 && m.key("quick_load") == 298);
    check("defaults: the wheel changes guns, Q the last one; Weapon 1-4 unbound",
          m.key("next_weapon") == kMouseWheelDown && m.key("prev_weapon") == kMouseWheelUp && m.key("last_weapon") == 'Q' &&
          m.has("weapon_1") && m.key("weapon_1") == 0 && key_name(kMouseWheelUp) == "Wheel Up" && key_code("wheel down") == kMouseWheelDown);
    std::set<int> keys;
    bool unique = true;
    for (const auto& b : m.bindings()) if (b.key && !keys.insert(b.key).second) unique = false;
    check("defaults: no key does two things", unique);

    m.set("jump", 'F');
    check("rebind: jump on F", m.key("jump") == 'F');
    m.set("dash", 'F');
    check("rebind: a key taken by another action moves (jump is left unbound)", m.key("dash") == 'F' && m.key("jump") == 0);
    m.set("inventory", 'I');
    check("a project's own action", m.has("inventory") && m.key("inventory") == 'I');

    // The text: written, read into fresh defaults, the same.
    m.mouse_sensitivity = 1.75f;
    m.invert_y = true;
    ActionMap r;
    const auto errs = r.read(m.text());
    check("settings text: written and read back, the same bindings and mouse",
          errs.empty() && r.key("dash") == 'F' && r.key("jump") == 0 && r.key("inventory") == 'I' &&
          r.mouse_sensitivity == 1.75f && r.invert_y && r.text() == m.text());
    ActionMap p;
    const auto e2 = p.read("# a project's input.cfg\ninventory = I\nlabel inventory = Open the bag\nskills = K\njump = Bananas\n");
    check("input.cfg: new actions with labels; an unknown key is reported and skipped",
          p.key("inventory") == 'I' && p.label("inventory") == "Open the bag" && p.key("skills") == 'K' &&
          p.key("jump") == 32 && e2.size() == 1 && e2[0].rfind("line 5:", 0) == 0);

    // Pressed / held / released.
    ActionStates s;
    bool space = false;
    auto down = [&](int code) { return code == 32 && space; };
    ActionMap d;
    s.update(d, down);
    const bool idle = !s.down("jump") && !s.pressed("jump");
    space = true; s.update(d, down);
    const bool press = s.pressed("jump") && s.down("jump");
    s.update(d, down);
    const bool hold = !s.pressed("jump") && s.down("jump");
    space = false; s.update(d, down);
    check("pressed once, then held, then released", idle && press && hold && s.released("jump") && !s.down("jump"));

    std::printf("\ncontrols_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("ALL OK\n");
    return g_fail ? 1 : 0;
}
