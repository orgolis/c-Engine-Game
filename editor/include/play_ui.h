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
            b->on_click = [this, bid] { clicks_.push_back(bid); };
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
    void refresh(const schizo::ui::UiLookup& lookup) {
        for (auto& d : docs_) schizo::ui::refresh_ui_bindings(*d, lookup);
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

    /// Buttons clicked since the last call (their ids).
    std::vector<std::string> take_clicks() { return std::exchange(clicks_, {}); }

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
    static void draw(const schizo::ui::UiDrawList& dl, float scale, ImDrawList* out, ImVec2 o) {
        ImFont* font = ImGui::GetFont();
        for (const auto& c : dl) {
            const ImVec2 mn(o.x + c.rect.x, o.y + c.rect.y), mx(mn.x + c.rect.w, mn.y + c.rect.h);
            if (c.type == schizo::ui::UiCmdType::Rect) {
                out->AddRectFilled(mn, mx, col(c.color), 4.0f * scale);
            } else if (c.type == schizo::ui::UiCmdType::Text && !c.text.empty()) {
                const float size = std::max(6.0f, c.font_size * scale);
                const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, c.text.c_str());
                float x = mn.x;
                if (c.align == schizo::ui::UiAlign::Center) x = mn.x + (c.rect.w - ts.x) * 0.5f;
                else if (c.align == schizo::ui::UiAlign::Right) x = mx.x - ts.x;
                const float y = mn.y + (c.rect.h - ts.y) * 0.5f;
                out->AddText(font, size, ImVec2(x, y), col(c.color), c.text.c_str());
            }
        }
    }

    std::vector<std::unique_ptr<schizo::ui::UiDocument>> docs_;
    std::vector<std::string> clicks_;
};

/// The one in Play (scripts reach it through the script API).
inline PlayUi& play_ui() { static PlayUi u; return u; }

}  // namespace schizo::editor
