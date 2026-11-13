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

/// One cell's content in a slots grid (an inventory).
struct UiSlot {
    std::string name;     // what is in it ("" empty)
    int         count = 0;
    std::string detail;   // a second line: kind, equipped...
    std::string info;     // a description, for {<grid>.info} (not drawn in the cell)
};

/// A grid of slots (an inventory, a stash): cells drawn in rows, the
/// contents from the game, a click picks a cell (`selected`) and reports it.
class SlotGrid : public Widget {
public:
    int   cols = 6, rows = 4;
    float gap = 8.0f;                 // reference pixels between cells
    float font_size = 18.0f;
    std::vector<UiSlot> slots;
    int   selected = -1;
    std::function<void(int)> on_click;
    UiColor cell{0.12f, 0.14f, 0.19f, 0.92f}, cell_hover{0.2f, 0.24f, 0.32f, 0.95f}, cell_selected{0.32f, 0.42f, 0.62f, 0.98f};

    SlotGrid() { interactive = true; }
    int cell_at(float px, float py) const {
        if (!rect.contains(px, py)) return -1;
        const float cw = rect.w / static_cast<float>(cols), ch = rect.h / static_cast<float>(rows);
        const int c = std::min(cols - 1, static_cast<int>((px - rect.x) / cw));
        const int r = std::min(rows - 1, static_cast<int>((py - rect.y) / ch));
        return r * cols + c;
    }
    void on_pointer(float x, float y) override { hover_ = cell_at(x, y); }
    void on_hover(bool h) override { if (!h) hover_ = -1; }
    void on_activate() override {
        if (hover_ < 0) return;
        selected = hover_;
        if (on_click) on_click(selected);
    }
    void paint_self(UiDrawList& dl) const override {
        const float cw = rect.w / static_cast<float>(cols), ch = rect.h / static_cast<float>(rows);
        const float g = std::min(cw, ch) * 0.06f;
        for (int i = 0; i < cols * rows; ++i) {
            const UiRect r{rect.x + (i % cols) * cw + g * 0.5f, rect.y + (i / cols) * ch + g * 0.5f, cw - g, ch - g};
            const UiColor c = i == selected ? cell_selected : i == hover_ ? cell_hover : cell;
            dl.push_back({UiCmdType::Rect, r, c, {}, 0.0f, UiAlign::Left, 0});
            if (i >= static_cast<int>(slots.size()) || slots[static_cast<size_t>(i)].name.empty()) continue;
            const UiSlot& s = slots[static_cast<size_t>(i)];
            UiRect name = r;
            name.h = r.h * 0.6f;
            dl.push_back({UiCmdType::Text, name, UiColor{1, 1, 1, 1}, s.name, font_size, UiAlign::Center, 0});
            UiRect low = r;
            low.y = r.y + r.h * 0.6f;
            low.h = r.h * 0.36f;
            std::string tail = s.count > 1 ? "x" + std::to_string(s.count) : std::string();
            if (!s.detail.empty()) tail = tail.empty() ? s.detail : tail + "  " + s.detail;
            if (!tail.empty())
                dl.push_back({UiCmdType::Text, low, UiColor{1, 0.9f, 0.6f, 0.95f}, tail, font_size * 0.8f, UiAlign::Center, 0});
        }
    }
    float scale_hint = 1.0f;

private:
    int hover_ = -1;
};

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
    struct EnableBinding { Button* button; std::string source; };   // enabled while the source is above 0
    struct SlotsBinding { SlotGrid* grid; std::string id, source; };
    std::vector<TextBinding> texts;
    std::vector<BarBinding>  bars;
    std::vector<EnableBinding> enables;
    std::vector<SlotsBinding>  grids;
    std::vector<std::string> errors;   // "line 4: unknown anchor 'middle'"
};

/// A live value for a binding: the number (and its maximum, for bars), or text.
struct UiValue {
    bool        found = false;
    double      value = 0.0, max = 0.0;
    std::string text;       // when not empty, what {source} shows
};
using UiLookup = std::function<UiValue(const std::string& source)>;
/// The contents of a slots grid (`bind=player.inventory`).
using UiSlotLookup = std::function<std::vector<UiSlot>(const std::string& source)>;

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
            if (kv.count("enabled")) doc->enables.push_back({b, kv["enabled"]});
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
        } else if (kind == "slots") {
            auto* g = doc->canvas.root().add<SlotGrid>();
            g->cols = std::max(1, static_cast<int>(num("cols", 6)));
            g->rows = std::max(1, static_cast<int>(num("rows", 4)));
            g->font_size = num("size", 18.0f);
            if (kv.count("color")) parse_color(kv["color"], g->cell);
            if (!kv.count("id")) err("slots need an id (it is what On UI Button names, with the cell as Index)");
            else doc->grids.push_back({g, kv["id"], kv.count("bind") ? kv["bind"] : std::string()});
            w = g;
        } else {
            err("unknown widget '" + kind + "' (panel, label, button, bar, slots)");
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
inline void refresh_ui_bindings(UiDocument& doc, const UiLookup& game_lookup, const UiSlotLookup& slot_lookup = {}) {
    for (auto& g : doc.grids) {
        g.grid->scale_hint = doc.canvas.scale();
        if (slot_lookup && !g.source.empty()) g.grid->slots = slot_lookup(g.source);
        if (g.grid->selected >= g.grid->cols * g.grid->rows) g.grid->selected = -1;
    }
    // {<slots id>.name / .count / .detail / .info / .index}: the selected cell.
    const UiLookup lookup = [&](const std::string& src) {
        for (const auto& g : doc.grids) {
            if (src.rfind(g.id + ".", 0) != 0) continue;
            const std::string what = src.substr(g.id.size() + 1);
            UiValue v;
            v.found = true;
            const int i = g.grid->selected;
            const UiSlot* s = (i >= 0 && i < static_cast<int>(g.grid->slots.size())) ? &g.grid->slots[static_cast<size_t>(i)] : nullptr;
            if (what == "index") v.value = i;
            else if (what == "count") v.value = s ? s->count : 0;
            else if (what == "name") v.text = s && !s->name.empty() ? s->name : std::string(" ");
            else if (what == "detail") v.text = s && !s->detail.empty() ? s->detail : std::string(" ");
            else if (what == "info") v.text = s && !s->info.empty() ? s->info : std::string(" ");
            else v.found = false;
            return v;
        }
        return game_lookup(src);
    };
    for (auto& e : doc.enables) {
        const UiValue v = lookup(e.source);
        const bool want = v.found && v.value > 0.0;
        if (e.button->interactive != want) e.button->set_enabled(want);   // only on a change: keeps hover / press
    }
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
