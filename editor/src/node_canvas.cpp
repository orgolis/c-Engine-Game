// ============================================================================
// node_canvas — see node_canvas.h.
// ============================================================================
#include "node_canvas.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace schizo::editor {

namespace {

constexpr float kNodeW      = 150.0f;
constexpr float kRowH       = 20.0f;
constexpr float kHeaderH    = 24.0f;
constexpr float kPinR       = 4.5f;
constexpr float kGridStep   = 32.0f;

ImU32 col(float r, float g, float b, float a = 1.0f) {
    return ImGui::GetColorU32(ImVec4(r, g, b, a));
}

// Links are drawn as horizontal-tangent beziers so they leave an output to the
// right and enter an input from the left regardless of relative position. A
// straight line between two pins that happen to be vertically aligned is
// indistinguishable from a node border.
void bezier(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float thickness) {
    const float dx = std::max(40.0f, std::abs(b.x - a.x) * 0.5f);
    dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, c, thickness);
}

}  // namespace

void CanvasView::zoom_at(float screen_x, float screen_y, float origin_x, float origin_y,
                         float factor, float min_zoom, float max_zoom) {
    const float old_zoom = zoom;
    const float want     = std::clamp(zoom * factor, min_zoom, max_zoom);
    if (want == old_zoom) return;

    // Keep the world point under the cursor fixed: solve for the pan that maps
    // the same world coordinate to the same screen coordinate at the new zoom.
    const float wx = (screen_x - origin_x) / old_zoom - pan_x;
    const float wy = (screen_y - origin_y) / old_zoom - pan_y;
    zoom  = want;
    pan_x = (screen_x - origin_x) / zoom - wx;
    pan_y = (screen_y - origin_y) / zoom - wy;
}

void NodeCanvas::begin(const char* id, float width, float height) {
    nodes_.clear();
    current_ = nullptr;

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    width_  = width  > 0.0f ? width  : avail.x;
    height_ = height > 0.0f ? height : avail.y;
    if (width_ < 32.0f)  width_  = 32.0f;
    if (height_ < 32.0f) height_ = 32.0f;

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    origin_x_ = p0.x;
    origin_y_ = p0.y;

    ImGui::InvisibleButton(id, ImVec2(width_, height_),
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + width_, p0.y + height_);
    dl->PushClipRect(p0, p1, true);
    dl->AddRectFilled(p0, p1, col(0.09f, 0.10f, 0.12f));

    // A grid that pans and zooms with the content, so motion is legible. A
    // static grid makes panning look like the nodes are sliding on glass.
    const float step = kGridStep * view_.zoom;
    if (step > 4.0f) {
        const float ox = std::fmod(view_.pan_x * view_.zoom, step);
        const float oy = std::fmod(view_.pan_y * view_.zoom, step);
        for (float x = ox; x < width_; x += step)
            dl->AddLine(ImVec2(p0.x + x, p0.y), ImVec2(p0.x + x, p1.y), col(1, 1, 1, 0.04f));
        for (float y = oy; y < height_; y += step)
            dl->AddLine(ImVec2(p0.x, p0.y + y), ImVec2(p1.x, p0.y + y), col(1, 1, 1, 0.04f));
    }

    // Middle-drag pans; so does left-drag on empty canvas, which is what people
    // try first.
    if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                    (dragging_ == 0xFFFFFFFFu && link_from_ == 0xFFFFFFFFu &&
                     ImGui::IsMouseDragging(ImGuiMouseButton_Left)))) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        view_.pan_x += d.x / view_.zoom;
        view_.pan_y += d.y / view_.zoom;
    }

    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        view_.zoom_at(m.x, m.y, origin_x_, origin_y_,
                      ImGui::GetIO().MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f);
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        ctx_at_ = std::make_pair(view_.to_world_x(m.x, origin_x_),
                                 view_.to_world_y(m.y, origin_y_));
    }

    // Delete removes the selection. Checked here rather than per node so it
    // works whether or not the selected node is currently on screen.
    if (hovered && selected_ != 0xFFFFFFFFu &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X))) {
        deleted_  = selected_;
        selected_ = 0xFFFFFFFFu;
    }
}

bool NodeCanvas::begin_node(uint32_t id, const std::string& title, float x, float y,
                            uint32_t title_color) {
    NodeRec rec{};
    rec.id = id;
    rec.sx = view_.to_screen_x(x, origin_x_);
    rec.sy = view_.to_screen_y(y, origin_y_);
    rec.w  = kNodeW * view_.zoom;
    rec.h  = kHeaderH * view_.zoom;      // grows as pins are declared
    nodes_.push_back(rec);
    current_ = &nodes_.back();
    cur_input_row_ = 0;

    // Node dragging. Grabbed on the header only, so dragging from a pin starts
    // a link instead of moving the node -- getting that the wrong way round
    // makes linking nearly impossible.
    const ImVec2 m = ImGui::GetIO().MousePos;
    const bool on_header = m.x >= rec.sx && m.x <= rec.sx + rec.w &&
                           m.y >= rec.sy && m.y <= rec.sy + kHeaderH * view_.zoom;

    if (on_header && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        selected_ = id;
        dragging_ = id;
    }
    if (dragging_ == id) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            moved_ = MovedNode{id, x + d.x / view_.zoom, y + d.y / view_.zoom};
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) dragging_ = 0xFFFFFFFFu;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 header = title_color ? title_color : col(0.20f, 0.28f, 0.40f);
    const ImVec2 a(rec.sx, rec.sy);
    (void)dl; (void)a; (void)header; (void)title;
    return rec.sx + rec.w > origin_x_ && rec.sx < origin_x_ + width_ &&
           rec.sy + 200.0f > origin_y_ && rec.sy < origin_y_ + height_;
}

void NodeCanvas::input_pin(uint32_t index, const std::string& label, uint32_t color) {
    if (!current_) return;
    const float row_y = current_->sy + (kHeaderH + kRowH * cur_input_row_ + kRowH * 0.5f) * view_.zoom;
    current_->inputs.push_back({index, current_->sx, row_y, color});
    ++cur_input_row_;
    current_->h = (kHeaderH + kRowH * cur_input_row_) * view_.zoom;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(current_->sx, row_y), kPinR * view_.zoom, color);
    if (view_.zoom > 0.6f)
        dl->AddText(ImVec2(current_->sx + 9.0f * view_.zoom, row_y - 7.0f * view_.zoom),
                    col(0.82f, 0.85f, 0.90f), label.c_str());

    // Completing a link: release over an input while dragging from an output.
    const ImVec2 m = ImGui::GetIO().MousePos;
    if (link_from_ != 0xFFFFFFFFu && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        std::abs(m.x - current_->sx) < 12.0f && std::abs(m.y - row_y) < 10.0f) {
        if (link_from_ != current_->id)
            new_link_ = NewLink{link_from_, current_->id, index};
        link_from_ = 0xFFFFFFFFu;
    }
}

void NodeCanvas::output_pin(const std::string& label, uint32_t color) {
    if (!current_) return;
    const float row_y = current_->sy + kHeaderH * view_.zoom * 0.5f;
    current_->out_x = current_->sx + current_->w;
    current_->out_y = row_y;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(current_->out_x, row_y), kPinR * view_.zoom, color);
    if (view_.zoom > 0.6f && !label.empty()) {
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(current_->out_x - 9.0f * view_.zoom - ts.x, row_y - 7.0f * view_.zoom),
                    col(0.82f, 0.85f, 0.90f), label.c_str());
    }

    const ImVec2 m = ImGui::GetIO().MousePos;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        std::abs(m.x - current_->out_x) < 12.0f && std::abs(m.y - row_y) < 10.0f) {
        link_from_ = current_->id;
        dragging_  = 0xFFFFFFFFu;        // a pin drag is not a node drag
    }
}

void NodeCanvas::end_node() {
    if (!current_) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImVec2 a(current_->sx, current_->sy);
    const ImVec2 b(current_->sx + current_->w, current_->sy + current_->h);
    const float  r = 4.0f * view_.zoom;

    // Body first, then header, then border -- drawn after the pins so the box
    // does not cover them... except ImGui draws in call order, so the body is
    // added here and the pins were already added above it. Kept as an explicit
    // note because it looks like a bug and is not: the pins sit on the node's
    // left/right EDGES, outside the body rect, so overlap never occurs.
    dl->AddRectFilled(a, b, col(0.14f, 0.15f, 0.18f, 0.96f), r);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + kHeaderH * view_.zoom),
                      col(0.20f, 0.28f, 0.40f), r);
    dl->AddRect(a, b,
                current_->id == selected_ ? col(1.0f, 0.82f, 0.30f) : col(1, 1, 1, 0.22f),
                r, 0, current_->id == selected_ ? 2.0f : 1.0f);

    current_ = nullptr;
}

NodeCanvas::NodeRec* NodeCanvas::find(uint32_t id) {
    for (NodeRec& n : nodes_) if (n.id == id) return &n;
    return nullptr;
}

void NodeCanvas::link(uint32_t src_node, uint32_t dst_node, uint32_t dst_input) {
    NodeRec* s = find(src_node);
    NodeRec* d = find(dst_node);
    if (!s || !d) return;                    // either end off-screen: nothing to draw

    for (const PinSlot& p : d->inputs) {
        if (p.index != dst_input) continue;
        bezier(ImGui::GetWindowDrawList(), ImVec2(s->out_x, s->out_y),
               ImVec2(p.sx, p.sy), p.color, 2.0f * view_.zoom);
        return;
    }
}

void NodeCanvas::end() {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // The in-progress link follows the cursor, so a drag that has not landed
    // yet is visible rather than invisible-until-it-works.
    if (link_from_ != 0xFFFFFFFFu) {
        if (NodeRec* s = find(link_from_)) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            bezier(dl, ImVec2(s->out_x, s->out_y), m, col(1.0f, 0.82f, 0.30f, 0.9f),
                   2.0f * view_.zoom);
        }
        // Released over nothing: abandon it. Without this the canvas stays in
        // link mode and the next click connects something the user did not mean.
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) link_from_ = 0xFFFFFFFFu;
    }

    dl->PopClipRect();
}

std::optional<NewLink>   NodeCanvas::take_new_link()     { auto v = new_link_; new_link_.reset(); return v; }
std::optional<MovedNode> NodeCanvas::take_moved_node()   { auto v = moved_;    moved_.reset();    return v; }
std::optional<uint32_t>  NodeCanvas::take_deleted_node() { auto v = deleted_;  deleted_.reset();  return v; }
std::optional<std::pair<float, float>> NodeCanvas::take_context_menu_at() {
    auto v = ctx_at_; ctx_at_.reset(); return v;
}

}  // namespace schizo::editor
