#pragma once

// ============================================================================
// PlayUi -- the project's UI documents (.ui files) in Play.
// ============================================================================
//
// Scripts show and hide documents (Show UI / Hide UI), set their text (Set UI
// Text) and hear their buttons (On UI Button). This draws the shown ones over
// the game view in layer order, keeps their {placeholders} and bars live, and
// while a `modal` one is shown hands it the mouse: the cursor is free over
// the game and its buttons work. A `pause` document stands the game still.
//
// Live values a document can read (UiLookup, filled by main each frame):
//   player.<Attribute>   current value (bars: of its maximum)
//   player.ammo          rounds in the magazine
//   player.reserve       rounds in reserve
//   player.weapon        the weapon's name
//   flag.<key>           a world flag
//   time                 seconds of play
//
// The format is ui/ui_document.h (checked by tools/uidoc_check).

#include "ui/ui_document.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace schizo::editor {

class PlayUi {
public:
    /// Show a document (read fresh from its file). False, with a reason, when
    /// it cannot be read; lines it could not read are reported but the rest shows.
    bool show(const std::string& path, std::string* error = nullptr) {
        const std::string key = norm(path);
        if (find(key)) return true;
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            if (error) *error = "cannot open UI document '" + path + "'";
            return false;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        auto doc = schizo::ui::parse_ui_document(ss.str(), key);
        if (error && !doc->errors.empty()) {
            *error = path + ": " + doc->errors.front() + (doc->errors.size() > 1 ? " (and more)" : "");
        }
        for (auto& [id, b] : doc->buttons) {
            const std::string bid = id;
            b->on_click = [this, bid] { clicks_.push_back({bid, -1}); };
        }
        for (auto& g : doc->grids) {
            const std::string gid = g.id;
            g.grid->on_click = [this, gid](int cell) { clicks_.push_back({gid, cell}); };
        }
        docs_.push_back(std::move(doc));
        std::stable_sort(docs_.begin(), docs_.end(), [](const auto& a, const auto& b) { return a->layer < b->layer; });
        return true;
    }
    void hide(const std::string& path) {
        const std::string key = norm(path);
        docs_.erase(std::remove_if(docs_.begin(), docs_.end(), [&](const auto& d) { return d->path == key; }), docs_.end());
    }
    bool shown(const std::string& path) const { return find(norm(path)) != nullptr; }
    bool set_text(const std::string& path, const std::string& id, const std::string& text) {
        auto* d = find(norm(path));
        return d && schizo::ui::set_ui_text(*d, id, text);
    }
    /// Stop: nothing stays shown, nothing stays paused or asked for.
    void reset() {
        docs_.clear();
        clicks_.clear();
        script_pause = false;
        quit_requested = false;
    }

    bool modal() const {
        for (const auto& d : docs_) if (d->modal) return true;
        return false;
    }
    bool pauses() const {
        if (script_pause) return true;
        for (const auto& d : docs_) if (d->pause) return true;
        return false;
    }
    bool empty() const { return docs_.empty(); }

    /// Fill the live values (call once a frame, where the game is at hand).
    void refresh(const schizo::ui::UiLookup& lookup, const schizo::ui::UiSlotLookup& slots = {}) {
        for (auto& d : docs_) schizo::ui::refresh_ui_bindings(*d, lookup, slots);
    }
    /// The selected cell of a slots grid in a shown document (-1: none).
    int selected(const std::string& path, const std::string& id) const {
        const auto* d = find(norm(path));
        if (!d) return -1;
        for (const auto& g : d->grids) if (g.id == id) return g.grid->selected;
        return -1;
    }
    /// Pick a cell of a shown slots grid, as a click on it would (tests).
    bool pick(const std::string& id, int cell) {
        for (auto& d : docs_)
            for (auto& g : d->grids)
                if (g.id == id) { g.grid->selected = cell; return true; }
        return false;
    }

    /// Lay out over the view [vmin, vmax], take the mouse when `pointer` (a
    /// modal document is shown and the cursor is over the view), and draw.
    void frame(ImDrawList* out, ImVec2 vmin, ImVec2 vmax, bool pointer) {
        const float w = vmax.x - vmin.x, h = vmax.y - vmin.y;
        if (w < 2.0f || h < 2.0f) return;
        // Only the top modal document hears the mouse (a HUD underneath does not).
        schizo::ui::UiDocument* top_modal = nullptr;
        for (auto& d : docs_) if (d->modal) top_modal = d.get();
        const ImGuiIO& io = ImGui::GetIO();
        for (auto& d : docs_) {
            d->canvas.layout(w, h);
            if (pointer && d.get() == top_modal) {
                d->canvas.pointer_move(io.MousePos.x - vmin.x, io.MousePos.y - vmin.y);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) d->canvas.pointer_button(true);
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) d->canvas.pointer_button(false);
            }
            draw(d->canvas.paint(), d->canvas.scale(), out, vmin);
        }
    }

    /// Buttons and grid cells clicked since the last call: (id, cell or -1).
    std::vector<std::pair<std::string, int>> take_clicks() { return std::exchange(clicks_, {}); }

    bool script_pause = false;     // Pause Game from a script
    bool quit_requested = false;   // Quit Game from a script

private:
    static std::string norm(const std::string& p) {
        return std::filesystem::path(p).lexically_normal().generic_string();
    }
    schizo::ui::UiDocument* find(const std::string& key) const {
        for (const auto& d : docs_) if (d->path == key) return d.get();
        return nullptr;
    }
    static ImU32 col(const schizo::ui::UiColor& c) {
        auto b = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return IM_COL32(b(c.r), b(c.g), b(c.b), b(c.a));
    }
    /// The bold face loaded beside the regular one (imgui_vulkan.cpp names it "UiBold"); else the regular.
    static ImFont* bold_font() {
        static ImFont* bold = [] {
            ImFontAtlas* atlas = ImGui::GetIO().Fonts;
            for (ImFont* f : atlas->Fonts)
                if (f && std::string(f->GetDebugName()).rfind("UiBold", 0) == 0) return f;
            return static_cast<ImFont*>(nullptr);
        }();
        return bold ? bold : ImGui::GetFont();
    }
    static void draw(const schizo::ui::UiDrawList& dl, float scale, ImDrawList* out, ImVec2 o) {
        for (const auto& c : dl) {
            const ImVec2 mn(o.x + c.rect.x, o.y + c.rect.y), mx(mn.x + c.rect.w, mn.y + c.rect.h);
            if (c.type == schizo::ui::UiCmdType::Rect) {
                const float r = (c.rounding >= 0.0f ? c.rounding : 4.0f) * scale;
                if (c.drop) {   // a soft shadow: a few widening, fading layers below and to the right
                    for (int i = 3; i >= 1; --i) {
                        const float g = static_cast<float>(i) * 3.0f * scale;
                        out->AddRectFilled(ImVec2(mn.x - g * 0.5f, mn.y + g * 0.3f), ImVec2(mx.x + g * 0.5f, mx.y + g),
                                           IM_COL32(0, 0, 0, static_cast<int>(26.0f * c.color.a)), r + g);
                    }
                }
                if (c.color.a > 0.0f) out->AddRectFilled(mn, mx, col(c.color), r);
                if (c.border_width > 0.0f && c.border.a > 0.0f) {
                    const float bw = std::max(1.0f, c.border_width * scale);
                    out->AddRect(ImVec2(mn.x + bw * 0.5f, mn.y + bw * 0.5f), ImVec2(mx.x - bw * 0.5f, mx.y - bw * 0.5f), col(c.border),
                                 std::max(0.0f, r - bw * 0.5f), 0, bw);
                }
            } else if (c.type == schizo::ui::UiCmdType::Text && !c.text.empty()) {
                ImFont* font = c.bold ? bold_font() : ImGui::GetFont();
                const float size = std::max(6.0f, c.font_size * scale);
                const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, c.text.c_str());
                float x = mn.x;
                if (c.align == schizo::ui::UiAlign::Center) x = mn.x + (c.rect.w - ts.x) * 0.5f;
                else if (c.align == schizo::ui::UiAlign::Right) x = mx.x - ts.x;
                const float y = mn.y + (c.rect.h - ts.y) * 0.5f;
                if (c.shadow) {
                    const float d = std::max(1.0f, size * 0.06f);
                    out->AddText(font, size, ImVec2(x + d, y + d), IM_COL32(0, 0, 0, static_cast<int>(170.0f * c.color.a)), c.text.c_str());
                }
                out->AddText(font, size, ImVec2(x, y), col(c.color), c.text.c_str());
            }
        }
    }

    std::vector<std::unique_ptr<schizo::ui::UiDocument>> docs_;
    std::vector<std::pair<std::string, int>> clicks_;
};

/// The one in Play (scripts reach it through the script API).
inline PlayUi& play_ui() { static PlayUi u; return u; }

}  // namespace schizo::editor
