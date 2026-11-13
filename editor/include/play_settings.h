#pragma once

// ============================================================================
// PlaySettings -- the player's controls and a game's settings in Play.
// ============================================================================
//
// Controls: the action map (input/action_map.h) -- the engine's defaults, the
// project's `input.cfg` over them, then the player's own choices, saved per
// user and project (<config>/games/<project>/controls.txt; GWS_SETTINGS_DIR
// moves that folder, for tests). Main reads the keys once a frame into
// `states`; movement, weapons, abilities, interacting and scripts ask it.
//
// Settings scripts can change by name (Set Setting / Next Setting Value):
//   quality            Low, Medium, High           (graphics, live)
//   ambient_occlusion  On, Off                      (graphics, live)
//   reflections        On, Off                      (graphics, live)
//   clouds             On, Off                      (graphics, live)
//   volumetric         On, Off                      (graphics, live)
//   shadow_quality     Low, Medium, High, Ultra     (graphics, live)
//   render_scale       50%, 67%, 75%, 100%          (graphics, on restart)
//   vsync              On, Off                      (graphics, on restart)
//   fov                Camera, 70, 80, 90, 100, 110 (game; Camera = the camera's own)
//   mouse_sensitivity  0.25 .. 4                    (controls)
//   invert_y           On, Off                      (controls)
// Graphics settings are the machine's (the same as the editor's Render
// Settings, saved there); main applies requests and publishes what is set.

#include "input/action_map.h"
#include "gws/platform/user_dirs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace schizo::editor {

class PlaySettings {
public:
    schizo::input::ActionMap    map;
    schizo::input::ActionStates states;
    std::string rebinding;           // the action waiting for its new key ("" none)
    int         swallowed_key = 0;   // the key that ended a rebinding: held back from the game until released
    float       fov = 0.0f;          // degrees; 0 = the camera's own

    // Graphics: what is set now (published by main as text) and changes asked
    // for (applied by main, which holds the renderer's settings).
    std::map<std::string, std::string>               graphics;
    std::vector<std::pair<std::string, std::string>> graphics_requests;

    /// A project opened: its controls, then this player's.
    void load_for_project(const std::string& project_dir, const std::string& project_name) {
        project_dir_ = project_dir;
        map.reset();
        fov = 0.0f;
        rebinding.clear();
        states.clear();
        std::string text;
        if (read_file((std::filesystem::path(project_dir) / "input.cfg").string(), text)) map.read(text);
        user_file_ = settings_dir(project_name) / "controls.txt";
        if (read_file(user_file_.string(), text)) read_user(text);
    }
    void save() const {
        if (user_file_.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(user_file_.parent_path(), ec);
        std::ofstream f(user_file_, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << "# Controls and game settings (this player, this game)\n" << map.text();
        char buf[48];
        std::snprintf(buf, sizeof buf, "fov = %.0f\n", static_cast<double>(fov));
        f << buf;
    }
    /// The project's controls again (its input.cfg over the engine's).
    void reset_controls() {
        map.reset();
        std::string text;
        if (read_file((std::filesystem::path(project_dir_) / "input.cfg").string(), text)) map.read(text);
        save();
    }

    /// A setting's choices, in order (empty: not a cycling setting).
    static std::vector<std::string> choices(const std::string& name) {
        if (name == "quality") return {"Low", "Medium", "High"};
        if (name == "shadow_quality") return {"Low", "Medium", "High", "Ultra"};
        if (name == "render_scale") return {"50%", "67%", "75%", "100%"};
        if (name == "fov") return {"Camera", "70", "80", "90", "100", "110"};
        if (name == "ambient_occlusion" || name == "reflections" || name == "clouds" || name == "volumetric" ||
            name == "vsync" || name == "invert_y")
            return {"On", "Off"};
        return {};
    }
    /// The value a setting has now, as text ("" for an unknown name).
    std::string get(const std::string& name) const {
        if (name == "fov") return fov <= 0.0f ? "Camera" : number(fov, 0);
        if (name == "mouse_sensitivity") return number(map.mouse_sensitivity, 2);
        if (name == "invert_y") return map.invert_y ? "On" : "Off";
        auto it = graphics.find(name);
        return it == graphics.end() ? std::string() : it->second;
    }
    /// Set a setting; false for an unknown name or value.
    bool set(const std::string& name, const std::string& value) {
        if (name == "fov") {
            fov = (value == "Camera" || value == "0") ? 0.0f : std::clamp(static_cast<float>(std::atof(value.c_str())), 40.0f, 130.0f);
            save();
            return true;
        }
        if (name == "mouse_sensitivity") {
            map.mouse_sensitivity = std::clamp(static_cast<float>(std::atof(value.c_str())), 0.05f, 10.0f);
            save();
            return true;
        }
        if (name == "invert_y") {
            map.invert_y = value == "On" || value == "on" || value == "true" || value == "1";
            save();
            return true;
        }
        const auto c = choices(name);
        if (c.empty() || std::find(c.begin(), c.end(), value) == c.end()) return false;
        graphics_requests.push_back({name, value});
        graphics[name] = value;   // shown at once; main applies it this frame
        return true;
    }
    /// The next choice after the current one (wrapping); the new value.
    std::string next(const std::string& name) {
        const auto c = choices(name);
        if (c.empty()) return get(name);
        const std::string now = get(name);
        size_t i = 0;
        while (i < c.size() && c[i] != now) ++i;
        // A value that is not one of the choices (quality "Custom", after
        // single effects were changed) steps to the first choice.
        const std::string v = i < c.size() ? c[(i + 1) % c.size()] : c.front();
        set(name, v);
        return v;
    }

private:
    static std::string number(float v, int decimals) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.*f", decimals, static_cast<double>(v));
        return buf;
    }
    static bool read_file(const std::string& path, std::string& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }
    static std::filesystem::path settings_dir(const std::string& project) {
        std::string safe;
        for (char c : project) safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
        if (safe.empty()) safe = "game";
        if (const char* d = std::getenv("GWS_SETTINGS_DIR"); d && *d) return std::filesystem::path(d) / safe;
        const std::filesystem::path base = gws::platform::user_dir(gws::platform::UserDir::Config);
        return (base.empty() ? std::filesystem::path(".") : base) / "games" / safe;
    }
    void read_user(const std::string& text) {
        std::string rest;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("fov", 0) == 0 && line.find('=') != std::string::npos) {
                fov = static_cast<float>(std::atof(line.substr(line.find('=') + 1).c_str()));
                continue;
            }
            rest += line + "\n";
        }
        map.read(rest);
    }
    std::filesystem::path user_file_;
    std::string           project_dir_;
};

inline PlaySettings& play_settings() { static PlaySettings s; return s; }

}  // namespace schizo::editor
