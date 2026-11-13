#pragma once

// ============================================================================
// UI documents (.ui) -- a game's screens as files in its project.
// ============================================================================
//
// A menu, a HUD, a death screen: written as text in the project, shown and
// hidden by its scripts (Show UI / Hide UI / Set UI Text / On UI Button), not
// built into the engine. Each line is one widget; values are key=value.
//
//   # Pause menu
//   ui modal pause layer=10       # modal: the mouse is free over the game and
//                                 # game input is off while shown; pause: the
//                                 # game stands still while shown
//   panel  id=dim    anchor=fill color=0,0,0,0.55
//   label  id=title  anchor=center x=0 y=-150 w=600 h=70 text="Paused" size=54 align=center
//   button id=resume anchor=center x=0 y=-40 w=360 h=64 text="Resume" size=26
//   bar    id=hp     anchor=top_left x=24 y=24 w=320 h=22 color=0.85,0.2,0.2,1 bind=player.Health
//   label  id=ammo   anchor=bottom_right x=-24 y=-24 w=240 h=30 text="{player.ammo} / {player.reserve}" align=right
//
// Positions are in a 1920x1080 reference screen, scaled to the real one.
// `anchor` is the point of the screen the widget hangs from and also the
// point of the widget that sits there (top_left, top, top_right, left,
// center, right, bottom_left, bottom, bottom_right); `fill` covers the screen
// (x, y, w, h are then insets). Text in {braces} and a bar's `bind` read live
// values from the game (see PlayUi for the names: player.<Attribute>,
// player.ammo, flag.<key>, ...).
//
// Pure: parsing and binding refresh need no renderer. Checked by
// tools/uidoc_check.

#include "ui/canvas.h"
#include "ui/widget.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::ui {

struct UiDocument {
    std::string path;
    bool modal = false;     // frees the mouse over the game, turns game input off
    bool pause = false;     // the game stands still while it is shown
    int  layer = 0;         // drawing order: higher on top
    Canvas canvas;
    std::unordered_map<std::string, Widget*> ids;
    std::vector<std::pair<std::string, Button*>> buttons;   // id, widget

    struct TextBinding { Label* label; Button* button; std::string templ; };   // one of label / button
    struct BarBinding  { ProgressBar* bar; std::string source; };
    std::vector<TextBinding> texts;
    std::vector<BarBinding>  bars;
    std::vector<std::string> errors;   // "line 4: unknown anchor 'middle'"
};

/// A live value for a binding: the number (and its maximum, for bars), or text.
struct UiValue {
    bool        found = false;
    double      value = 0.0, max = 0.0;
    std::string text;       // when not empty, what {source} shows
};
using UiLookup = std::function<UiValue(const std::string& source)>;

namespace ui_doc_detail {

// Split a line into words; key="quoted text" keeps its spaces.
inline std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') { quoted = !quoted; cur += c; continue; }
        if (!quoted && c == '#') break;
        if (!quoted && std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

inline std::string unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

inline bool parse_color(const std::string& s, UiColor& c) {
    float v[4] = {1, 1, 1, 1};
    int n = 0;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',') && n < 4) v[n++] = static_cast<float>(std::atof(part.c_str()));
    if (n < 3) return false;
    c = UiColor{v[0], v[1], v[2], n >= 4 ? v[3] : 1.0f};
    return true;
}

inline bool anchor_point(const std::string& a, glm::vec2& p) {
    static const struct { const char* name; float x, y; } k[] = {
        {"top_left", 0, 0},      {"top", 0.5f, 0},      {"top_right", 1, 0},
        {"left", 0, 0.5f},       {"center", 0.5f, 0.5f}, {"right", 1, 0.5f},
        {"bottom_left", 0, 1},   {"bottom", 0.5f, 1},   {"bottom_right", 1, 1},
    };
    for (const auto& e : k)
        if (a == e.name) { p = {e.x, e.y}; return true; }
    return false;
}

}  // namespace ui_doc_detail

/// Build a document from .ui text. Lines it cannot read are reported in
/// `errors` and left out; the rest still builds.
inline std::unique_ptr<UiDocument> parse_ui_document(const std::string& text, const std::string& path = {}) {
    using namespace ui_doc_detail;
    auto doc = std::make_unique<UiDocument>();
    doc->path = path;
    doc->canvas.set_reference_resolution({1920.0f, 1080.0f});
    doc->canvas.set_scale_mode(Canvas::ScaleMode::Expand);
    std::istringstream in(text);
    std::string line;
    int ln = 0;
    auto err = [&](const std::string& m) { doc->errors.push_back("line " + std::to_string(ln) + ": " + m); };
    while (std::getline(in, line)) {
        ++ln;
        const auto tk = tokens(line);
        if (tk.empty()) continue;
        const std::string& kind = tk[0];
        std::unordered_map<std::string, std::string> kv;
        std::vector<std::string> flags;
        for (size_t i = 1; i < tk.size(); ++i) {
            const size_t eq = tk[i].find('=');
            if (eq == std::string::npos) flags.push_back(tk[i]);
            else kv[tk[i].substr(0, eq)] = unquote(tk[i].substr(eq + 1));
        }
        if (kind == "ui") {
            for (const auto& f : flags) {
                if (f == "modal") doc->modal = true;
                else if (f == "pause") doc->pause = true;
                else err("unknown option '" + f + "'");
            }
            if (kv.count("layer")) doc->layer = std::atoi(kv["layer"].c_str());
            continue;
        }
        auto num = [&](const char* k, float d) { return kv.count(k) ? static_cast<float>(std::atof(kv[k].c_str())) : d; };
        Widget* w = nullptr;
        if (kind == "panel") {
            UiColor c{0, 0, 0, 0.5f};
            if (kv.count("color") && !parse_color(kv["color"], c)) err("bad color '" + kv["color"] + "'");
            w = doc->canvas.root().add<Panel>(c);
        } else if (kind == "label") {
            UiColor c{1, 1, 1, 1};
            if (kv.count("color") && !parse_color(kv["color"], c)) err("bad color '" + kv["color"] + "'");
            const std::string al = kv.count("align") ? kv["align"] : "left";
            const UiAlign a = al == "center" ? UiAlign::Center : al == "right" ? UiAlign::Right : UiAlign::Left;
            auto* l = doc->canvas.root().add<Label>(kv["text"], num("size", 22.0f), c, a);
            if (l->text.find('{') != std::string::npos) doc->texts.push_back({l, nullptr, l->text});
            w = l;
        } else if (kind == "button") {
            auto* b = doc->canvas.root().add<Button>(kv["text"]);
            b->font_size = num("size", 24.0f);
            if (b->text.find('{') != std::string::npos) doc->texts.push_back({nullptr, b, b->text});
            if (kv.count("color")) parse_color(kv["color"], b->color_normal);
            if (kv.count("hover")) parse_color(kv["hover"], b->color_hover);
            if (kv.count("text_color")) parse_color(kv["text_color"], b->text_color);
            w = b;
            if (kv.count("id")) doc->buttons.push_back({kv["id"], b});
            else err("a button needs an id (it is what On UI Button names)");
        } else if (kind == "bar") {
            UiColor fill{0.85f, 0.2f, 0.2f, 1.0f}, bg{0, 0, 0, 0.6f};
            if (kv.count("color") && !parse_color(kv["color"], fill)) err("bad color '" + kv["color"] + "'");
            if (kv.count("bg") && !parse_color(kv["bg"], bg)) err("bad color '" + kv["bg"] + "'");
            auto* b = doc->canvas.root().add<ProgressBar>(fill, bg);
            b->fraction = num("value", 1.0f);
            if (kv.count("bind")) doc->bars.push_back({b, kv["bind"]});
            w = b;
        } else {
            err("unknown widget '" + kind + "' (panel, label, button, bar)");
            continue;
        }
        // Placement.
        const std::string anchor = kv.count("anchor") ? kv["anchor"] : "top_left";
        const float x = num("x", 0), y = num("y", 0), ww = num("w", 200), hh = num("h", 40);
        if (anchor == "fill") {
            w->set_stretch(x, y, kv.count("w") ? ww : 0.0f, kv.count("h") ? hh : 0.0f);
        } else {
            glm::vec2 a(0.0f);
            if (!anchor_point(anchor, a)) { err("unknown anchor '" + anchor + "'"); a = glm::vec2(0.0f); }
            // The widget's own matching point sits at the anchor, moved by (x, y).
            w->anchor_min = w->anchor_max = a;
            w->offset_min = {x - ww * a.x, y - hh * a.y};
            w->offset_max = {w->offset_min.x + ww, w->offset_min.y + hh};
        }
        if (kv.count("hidden")) w->visible = kv["hidden"] != "0" && kv["hidden"] != "false";
        if (kv.count("id")) doc->ids[kv["id"]] = w;
    }
    return doc;
}

/// Fill {source} placeholders and bars from the game. Unknown sources show
/// as "?" (and leave a bar as it is).
inline void refresh_ui_bindings(UiDocument& doc, const UiLookup& lookup) {
    for (auto& t : doc.texts) {
        std::string out;
        const std::string& s = t.templ;
        for (size_t i = 0; i < s.size();) {
            if (s[i] == '{') {
                const size_t j = s.find('}', i);
                if (j != std::string::npos) {
                    const UiValue v = lookup(s.substr(i + 1, j - i - 1));
                    if (!v.found) out += "?";
                    else if (!v.text.empty()) out += v.text;
                    else {
                        char buf[32];
                        const double r = std::floor(v.value + 0.5);
                        if (std::fabs(v.value - r) < 1e-6) std::snprintf(buf, sizeof buf, "%.0f", r);
                        else std::snprintf(buf, sizeof buf, "%.1f", v.value);
                        out += buf;
                    }
                    i = j + 1;
                    continue;
                }
            }
            out += s[i++];
        }
        if (t.label) t.label->text = out;
        if (t.button) t.button->text = out;
    }
    for (auto& b : doc.bars) {
        const UiValue v = lookup(b.source);
        if (v.found && v.max > 0.0) b.bar->fraction = static_cast<float>(std::clamp(v.value / v.max, 0.0, 1.0));
    }
}

/// Set a widget's text (a label's or a button's). A label set this way stops
/// following its {placeholders}. False when there is no such widget.
inline bool set_ui_text(UiDocument& doc, const std::string& id, const std::string& text) {
    auto it = doc.ids.find(id);
    if (it == doc.ids.end()) return false;
    if (auto* l = dynamic_cast<Label*>(it->second)) {
        l->text = text;
        for (size_t i = 0; i < doc.texts.size(); ++i)
            if (doc.texts[i].label == l) { doc.texts.erase(doc.texts.begin() + static_cast<long>(i)); break; }
        return true;
    }
    if (auto* b = dynamic_cast<Button*>(it->second)) {
        b->text = text;
        for (size_t i = 0; i < doc.texts.size(); ++i)
            if (doc.texts[i].button == b) { doc.texts.erase(doc.texts.begin() + static_cast<long>(i)); break; }
        return true;
    }
    return false;
}

}  // namespace schizo::ui
