#pragma once

// ============================================================================
// Action map -- which key or mouse button does what, by name.
// ============================================================================
//
// The player's controls are actions (move_forward, jump, fire, ability_1,
// ...), each bound to a key or a mouse button. The engine knows the defaults;
// a project can change them and add its own actions (input.cfg in the
// project: `inventory = I`); a player can rebind them in the game (saved per
// user and project). Scripts ask for actions, not keys (Action Pressed), so a
// rebinding reaches them too.
//
// Codes are GLFW key codes; mouse buttons are kMouseButton + 0..7.
//
//   # input.cfg / controls.txt
//   jump = Space
//   fire = Mouse Left
//   inventory = I
//   mouse_sensitivity = 1.0
//   invert_y = false
//
// Pure: names, defaults and the text format. Checked by tools/controls_check.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace schizo::input {

inline constexpr int kMouseButton = 1000;   // + 0 left, 1 right, 2 middle, 3.. extra

/// "Space", "Left Shift", "Mouse Left", "F6", "A"...; "None" for 0 / unknown.
inline std::string key_name(int code) {
    if (code >= kMouseButton && code < kMouseButton + 8) {
        static const char* m[] = {"Mouse Left", "Mouse Right", "Mouse Middle", "Mouse 4", "Mouse 5", "Mouse 6", "Mouse 7", "Mouse 8"};
        return m[code - kMouseButton];
    }
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return std::string(1, static_cast<char>(code));
    if (code >= 290 && code <= 314) return "F" + std::to_string(code - 289);
    if (code >= 320 && code <= 329) return "Numpad " + std::to_string(code - 320);
    static const struct { int code; const char* name; } k[] = {
        {32, "Space"}, {39, "'"}, {44, ","}, {45, "-"}, {46, "."}, {47, "/"}, {59, ";"}, {61, "="},
        {91, "["}, {92, "\\"}, {93, "]"}, {96, "`"},
        {256, "Escape"}, {257, "Enter"}, {258, "Tab"}, {259, "Backspace"}, {260, "Insert"}, {261, "Delete"},
        {262, "Right"}, {263, "Left"}, {264, "Down"}, {265, "Up"}, {266, "Page Up"}, {267, "Page Down"},
        {268, "Home"}, {269, "End"}, {280, "Caps Lock"},
        {340, "Left Shift"}, {341, "Left Ctrl"}, {342, "Left Alt"}, {344, "Right Shift"}, {345, "Right Ctrl"}, {346, "Right Alt"},
    };
    for (const auto& e : k) if (e.code == code) return e.name;
    return "None";
}

/// The code for a name (case does not matter); 0 when unknown or "None".
inline int key_code(const std::string& name) {
    std::string n;
    for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.back()))) n.pop_back();
    if (n.empty() || n == "none") return 0;
    for (int c = 1; c < kMouseButton + 8; ++c) {
        if (c == 400) c = kMouseButton;   // skip the gap between keys and mouse buttons
        const std::string kn = key_name(c);
        if (kn == "None") continue;
        std::string l;
        for (char ch : kn) l += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (l == n) return c;
    }
    // A few other spellings.
    if (n == "shift") return 340;
    if (n == "ctrl" || n == "control") return 341;
    if (n == "alt") return 342;
    if (n == "esc") return 256;
    if (n == "lmb") return kMouseButton;
    if (n == "rmb") return kMouseButton + 1;
    return 0;
}

struct ActionBinding {
    std::string action;
    int         key = 0;
    std::string label;   // shown in a controls menu ("Move forward")
};

class ActionMap {
public:
    ActionMap() { reset(); }

    /// The engine's defaults for the player's controls.
    void reset() {
        b_.clear();
        auto add = [&](const char* a, int k, const char* l) { b_.push_back({a, k, l}); };
        add("move_forward", 'W', "Move forward");
        add("move_back", 'S', "Move back");
        add("move_left", 'A', "Move left");
        add("move_right", 'D', "Move right");
        add("jump", 32, "Jump / climb up");
        add("sprint", 340, "Sprint");
        add("crouch", 341, "Crouch / slide");
        add("dash", 'V', "Dash");
        add("fire", kMouseButton, "Fire");
        add("aim", kMouseButton + 1, "Aim");
        add("reload", 'R', "Reload");
        add("interact", 'E', "Use / pick up");
        for (int i = 1; i <= 8; ++i)
            b_.push_back({"ability_" + std::to_string(i), '0' + i, "Ability " + std::to_string(i)});
        add("quick_save", 295, "Quick save");    // F6
        add("quick_load", 298, "Quick load");    // F9
        mouse_sensitivity = 1.0f;
        invert_y = false;
    }

    int key(const std::string& action) const {
        for (const auto& x : b_) if (x.action == action) return x.key;
        return 0;
    }
    std::string label(const std::string& action) const {
        for (const auto& x : b_) if (x.action == action) return x.label.empty() ? action : x.label;
        return action;
    }
    bool has(const std::string& action) const {
        for (const auto& x : b_) if (x.action == action) return true;
        return false;
    }
    /// Bind `action` to `key` (a new action is added). Another action that had
    /// the key gives it up, so one key never does two things.
    void set(const std::string& action, int key) {
        if (key != 0)
            for (auto& x : b_) if (x.key == key && x.action != action) x.key = 0;
        for (auto& x : b_)
            if (x.action == action) { x.key = key; return; }
        b_.push_back({action, key, {}});
    }
    const std::vector<ActionBinding>& bindings() const { return b_; }

    float mouse_sensitivity = 1.0f;   // multiplies the engine's look speed
    bool  invert_y = false;

    /// Read `action = Key` lines over what is there (unknown keys are
    /// reported, the rest still read). `label action = Text` names an action.
    std::vector<std::string> read(const std::string& text) {
        std::vector<std::string> errors;
        std::istringstream in(text);
        std::string line;
        int ln = 0;
        while (std::getline(in, line)) {
            ++ln;
            const size_t hash = line.find('#');
            if (hash != std::string::npos) line = line.substr(0, hash);
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
            if (k.empty()) continue;
            if (k == "mouse_sensitivity") { mouse_sensitivity = std::clamp(static_cast<float>(std::atof(v.c_str())), 0.05f, 10.0f); continue; }
            if (k == "invert_y") { invert_y = v == "true" || v == "1" || v == "on"; continue; }
            if (k.rfind("label ", 0) == 0) {
                const std::string a = trim(k.substr(6));
                if (!has(a)) b_.push_back({a, 0, v});
                for (auto& x : b_) if (x.action == a) x.label = v;
                continue;
            }
            const int code = key_code(v);
            if (code == 0 && trim(v) != "None") { errors.push_back("line " + std::to_string(ln) + ": unknown key '" + v + "'"); continue; }
            set(k, code);
        }
        return errors;
    }
    std::string text() const {
        std::string s;
        for (const auto& x : b_) s += x.action + " = " + key_name(x.key) + "\n";
        char buf[64];
        std::snprintf(buf, sizeof buf, "mouse_sensitivity = %.2f\n", static_cast<double>(mouse_sensitivity));
        s += buf;
        s += std::string("invert_y = ") + (invert_y ? "true" : "false") + "\n";
        return s;
    }

private:
    static std::string trim(const std::string& s) {
        const size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
    }
    std::vector<ActionBinding> b_;
};

/// Pressed this frame, held, released this frame -- from what a reader says
/// about each action's key (call update once a frame).
class ActionStates {
public:
    template <class KeyDown>
    void update(const ActionMap& m, KeyDown&& key_down) {
        prev_.swap(now_);
        now_.clear();
        for (const auto& b : m.bindings()) now_.push_back({b.action, b.key != 0 && key_down(b.key)});
    }
    bool down(const std::string& a) const { return find(now_, a); }
    bool pressed(const std::string& a) const { return find(now_, a) && !find(prev_, a); }
    bool released(const std::string& a) const { return !find(now_, a) && find(prev_, a); }
    void clear() { now_.clear(); prev_.clear(); }

private:
    static bool find(const std::vector<std::pair<std::string, bool>>& v, const std::string& a) {
        for (const auto& x : v) if (x.first == a) return x.second;
        return false;
    }
    std::vector<std::pair<std::string, bool>> now_, prev_;
};

}  // namespace schizo::input
