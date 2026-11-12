#include "logic_script_panel.h"

#include "logic_nodes.h"
#include "node_canvas.h"   // CanvasView: the pan/zoom arithmetic

#include "vscript/io.h"
#include "vscript/program.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::editor {

namespace {

using namespace schizo::vs;

struct PinRef {
    int  node = 0;
    int  pin = -1;      // index into the node's shape
    bool input = false;
    bool valid() const { return node != 0 && pin >= 0; }
};

struct PanelState {
    Registry     reg;
    LogicApiSlot slot;            // never pointed at an engine: the editor only needs shapes
    Script       script;
    std::string  path;
    bool         dirty = false;
    int          graph = 0;
    CanvasView   view;
    int          selected = 0;
    int          dragging = 0;
    PinRef       link_from;
    bool         palette_open = false;
    float        palette_x = 0.0f, palette_y = 0.0f;   // canvas position for the new node
    PinRef       palette_for;     // palette opened by dropping a wire: connect the new node to this
    char         search[64] = {};
    char         path_buf[260] = {};
    std::vector<CompileMessage> messages;
    bool         compiled_ok = false;
    std::vector<std::string> undo, redo;
    std::string  last_snapshot;
    std::string  status;

    PanelState() {
        register_standard_library(reg);
        register_engine_logic_nodes(reg, &slot);
        new_script();
    }
    void new_script() {
        script = Script{};
        script.name = "New Logic Script";
        Graph& g = script.add_graph(GraphKind::Event, "EventGraph");
        g.add("event.begin_play", 60.0f, 60.0f);
        g.add("event.tick", 60.0f, 220.0f);
        graph = 0;
        selected = 0;
        undo.clear();
        redo.clear();
        last_snapshot = to_text(script);
        dirty = false;
        recompile();
    }
    Graph* cur() {
        if (script.graphs.empty()) return nullptr;
        graph = std::clamp(graph, 0, static_cast<int>(script.graphs.size()) - 1);
        return &script.graphs[static_cast<size_t>(graph)];
    }
    void recompile() {
        Program p;
        messages.clear();
        compiled_ok = compile(reg, script, p, messages);
    }
    /// Call after any edit: records an undo step and recompiles.
    void changed() {
        const std::string now = to_text(script);
        if (now == last_snapshot) return;
        undo.push_back(last_snapshot);
        if (undo.size() > 200) undo.erase(undo.begin());
        redo.clear();
        last_snapshot = now;
        dirty = true;
        recompile();
    }
    void restore(const std::string& text) {
        Script s;
        if (from_text(text, s)) {
            script = std::move(s);
            last_snapshot = text;
            dirty = true;
            recompile();
        }
    }
    void do_undo() {
        if (undo.empty()) return;
        redo.push_back(last_snapshot);
        const std::string t = undo.back();
        undo.pop_back();
        restore(t);
    }
    void do_redo() {
        if (redo.empty()) return;
        undo.push_back(last_snapshot);
        const std::string t = redo.back();
        redo.pop_back();
        restore(t);
    }
    bool load(const std::string& p) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        Script      s;
        std::string err;
        if (!from_text(ss.str(), s, &err)) {
            status = "could not read " + p + ": " + err;
            return false;
        }
        script = std::move(s);
        if (script.graphs.empty()) script.add_graph(GraphKind::Event, "EventGraph");
        path = p;
        graph = 0;
        selected = 0;
        undo.clear();
        redo.clear();
        last_snapshot = to_text(script);
        dirty = false;
        status = "opened " + p;
        recompile();
        return true;
    }
    bool save() {
        if (path.empty()) {
            status = "give the script a file name first";
            return false;
        }
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) {
            status = "could not write " + path;
            return false;
        }
        f << to_text(script);
        dirty = false;
        status = "saved " + path;
        return true;
    }
};

PanelState& state() {
    static PanelState s;
    return s;
}

ImU32 type_color(Type t) {
    switch (t.kind) {
        case Kind::Exec: return IM_COL32(240, 240, 240, 255);
        case Kind::Bool: return IM_COL32(200, 60, 60, 255);
        case Kind::Int: return IM_COL32(60, 200, 170, 255);
        case Kind::Float: return IM_COL32(130, 220, 80, 255);
        case Kind::String: return IM_COL32(230, 80, 200, 255);
        case Kind::Vec2:
        case Kind::Vec3:
        case Kind::Vec4: return IM_COL32(240, 200, 60, 255);
        case Kind::Quat: return IM_COL32(150, 160, 250, 255);
        case Kind::Color: return IM_COL32(250, 140, 60, 255);
        case Kind::Transform: return IM_COL32(240, 130, 50, 255);
        case Kind::Entity: return IM_COL32(60, 150, 250, 255);
        default: return IM_COL32(150, 150, 150, 255);
    }
}

ImU32 title_color(const NodeDef& d) {
    if (d.builtin == Builtin::Event || d.builtin == Builtin::CustomEvent || d.builtin == Builtin::FnEntry)
        return IM_COL32(150, 40, 40, 255);
    if (d.pure) return IM_COL32(50, 110, 60, 255);
    if (d.category.rfind("Flow", 0) == 0) return IM_COL32(90, 90, 100, 255);
    return IM_COL32(40, 80, 140, 255);
}

std::string node_title(const NodeDef& d, const Node& n) {
    switch (d.builtin) {
        case Builtin::VarGet: return "Get " + n.name;
        case Builtin::VarSet: return "Set " + n.name;
        case Builtin::LocalGet: return "Get local " + n.name;
        case Builtin::LocalSet: return "Set local " + n.name;
        case Builtin::FnCall: return n.name;
        case Builtin::CustomEvent: return "Event " + n.name;
        case Builtin::CallEvent: return "Call " + n.name;
        default: return d.named && !n.name.empty() ? d.title + ": " + n.name : d.title;
    }
}

bool contains_ci(const std::string& hay, const char* needle) {
    if (!needle[0]) return true;
    std::string h = hay, n = needle;
    for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return h.find(n) != std::string::npos;
}

const char* kTypeNames[] = {"bool", "int", "float", "string", "vec2", "vec3", "vec4", "quat", "color", "transform", "entity"};
const Type  kTypes[] = {kBool, kInt, kFloat, kString, kVec2, kVec3, kVec4, kQuat, kColor, kTransform, kEntity};

/// A combo to pick a type (scalar or array). Returns true when it changed.
bool type_combo(const char* label, Type& t) {
    bool changed = false;
    int  cur = 2;
    for (int i = 0; i < IM_ARRAYSIZE(kTypes); ++i)
        if (kTypes[i].kind == t.kind) cur = i;
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::Combo(label, &cur, kTypeNames, IM_ARRAYSIZE(kTypeNames))) {
        t = Type(kTypes[cur].kind, t.array);
        changed = true;
    }
    ImGui::SameLine();
    bool arr = t.array;
    ImGui::PushID(label);
    if (ImGui::Checkbox("array", &arr)) {
        t.array = arr;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

/// Edit a literal in place. Returns true when it changed.
bool value_editor(const char* id, Type type, Value& v, float width) {
    if (v.type != type) v = Value::default_of(type);
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    bool changed = false;
    if (type.array) {
        ImGui::TextDisabled("[%d]", static_cast<int>(v.items().size()));
    } else switch (type.kind) {
        case Kind::Bool: {
            bool b = v.as_bool();
            if (ImGui::Checkbox("##v", &b)) { v = Value::boolean(b); changed = true; }
            break;
        }
        case Kind::Int: {
            int i = static_cast<int>(v.as_int());
            if (ImGui::DragInt("##v", &i, 0.2f)) { v = Value::integer(i); changed = true; }
            break;
        }
        case Kind::Float: {
            float f = static_cast<float>(v.as_float());
            if (ImGui::DragFloat("##v", &f, 0.01f)) { v = Value::number(f); changed = true; }
            break;
        }
        case Kind::String: {
            char buf[256];
            std::snprintf(buf, sizeof buf, "%s", v.str().c_str());
            if (ImGui::InputText("##v", buf, sizeof buf)) { v = Value::string(buf); changed = true; }
            break;
        }
        case Kind::Vec2: changed = ImGui::DragFloat2("##v", v.u.v, 0.01f); break;
        case Kind::Vec3: changed = ImGui::DragFloat3("##v", v.u.v, 0.01f); break;
        case Kind::Vec4:
        case Kind::Quat: changed = ImGui::DragFloat4("##v", v.u.v, 0.01f); break;
        case Kind::Color: changed = ImGui::ColorEdit4("##v", v.u.v, ImGuiColorEditFlags_NoInputs); break;
        default: ImGui::TextDisabled("-"); break;
    }
    ImGui::PopID();
    return changed;
}

bool pins_compatible(const Pin& out, const Pin& in) {
    if (out.type.is_exec() != in.type.is_exec()) return false;
    if (out.type.is_exec()) return true;
    if (out.type.is_any() || in.type.is_any()) return true;
    return can_convert(out.type, in.type);
}

/// Connect an output to an input, applying the pin rules: one wire into a
/// data input, one wire out of a flow output. Also settles a wildcard node's
/// type from the pin it is being wired to.
void connect(PanelState& st, Graph& g, PinRef a, PinRef b) {
    if (a.input == b.input || a.node == b.node) return;
    const PinRef out = a.input ? b : a, in = a.input ? a : b;
    Node* on = g.find(out.node);
    Node* in_n = g.find(in.node);
    Shape os, is;
    if (!on || !in_n || !shape_of(st.reg, st.script, g, *on, os) || !shape_of(st.reg, st.script, g, *in_n, is)) return;
    const Pin& op = os.pins[static_cast<size_t>(out.pin)];
    const Pin& ip = is.pins[static_cast<size_t>(in.pin)];
    if (!pins_compatible(op, ip)) {
        st.status = "cannot connect " + type_name(op.type) + " to " + type_name(ip.type);
        return;
    }
    if (op.type.is_exec()) {
        g.links.erase(std::remove_if(g.links.begin(), g.links.end(),
                                     [&](const Link& l) { return l.from == out.node && l.from_pin == op.name; }),
                      g.links.end());
    } else {
        g.unlink_input(in.node, ip.name);
        if (ip.type.is_any() && !op.type.is_any()) in_n->targ = ip.type.array ? op.type.element() : op.type;
        if (op.type.is_any() && !ip.type.is_any()) on->targ = op.type.array ? ip.type.element() : ip.type;
    }
    g.link(out.node, op.name, in.node, ip.name);
    st.changed();
}

/// What the palette offers: registry nodes plus the script's own variables,
/// functions and custom events.
struct PaletteEntry {
    std::string label, category, def, name;
};

std::vector<PaletteEntry> palette_entries(PanelState& st, const Graph& g) {
    std::vector<PaletteEntry> out;
    for (const Variable& v : st.script.variables) {
        out.push_back({"Get " + v.name, "Variables", "var.get", v.name});
        out.push_back({"Set " + v.name, "Variables", "var.set", v.name});
    }
    for (const Variable& v : g.locals) {
        out.push_back({"Get local " + v.name, "Variables", "local.get", v.name});
        out.push_back({"Set local " + v.name, "Variables", "local.set", v.name});
    }
    for (const Graph& f : st.script.graphs)
        if (f.kind == GraphKind::Function) out.push_back({"Call " + f.name, "Functions", "fn.call", f.name});
    for (const Graph& eg : st.script.graphs)
        if (eg.kind == GraphKind::Event)
            for (const Node& n : eg.nodes)
                if (n.def == "event.custom" && !n.name.empty())
                    out.push_back({"Call event " + n.name, "Events", "event.call", n.name});
    for (const auto& d : st.reg.all()) {
        switch (d->builtin) {
            case Builtin::VarGet: case Builtin::VarSet: case Builtin::LocalGet: case Builtin::LocalSet:
            case Builtin::FnCall: case Builtin::CallEvent:
                continue;   // offered per variable / function above
            case Builtin::FnEntry: case Builtin::FnReturn:
                if (g.kind != GraphKind::Function) continue;
                break;
            case Builtin::Event: case Builtin::CustomEvent:
                if (g.kind != GraphKind::Event) continue;
                break;
            default: break;
        }
        out.push_back({d->title, d->category, d->id, ""});
    }
    return out;
}

void draw_side_panel(PanelState& st) {
    ImGui::TextUnformatted("Graphs");
    for (int i = 0; i < static_cast<int>(st.script.graphs.size()); ++i) {
        Graph& g = st.script.graphs[static_cast<size_t>(i)];
        ImGui::PushID(i);
        const std::string label = (g.kind == GraphKind::Function ? "f  " : "ev ") + g.name;
        if (ImGui::Selectable(label.c_str(), st.graph == i)) {
            st.graph = i;
            st.selected = 0;
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Function")) {
        int n = 1;
        std::string name;
        do name = "Function" + std::to_string(n++); while (st.script.find_graph(name));
        Graph& f = st.script.add_graph(GraphKind::Function, name);
        f.add("fn.entry", 60.0f, 80.0f);
        f.add("fn.return", 420.0f, 80.0f);
        st.graph = static_cast<int>(st.script.graphs.size()) - 1;
        st.changed();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Event Graph")) {
        int n = 2;
        std::string name;
        do name = "EventGraph" + std::to_string(n++); while (st.script.find_graph(name));
        st.script.add_graph(GraphKind::Event, name);
        st.graph = static_cast<int>(st.script.graphs.size()) - 1;
        st.changed();
    }

    Graph* g = st.cur();
    if (g) {
        ImGui::SeparatorText(g->kind == GraphKind::Function ? "Function" : "Graph");
        char nb[96];
        std::snprintf(nb, sizeof nb, "%s", g->name.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##gname", nb, sizeof nb, ImGuiInputTextFlags_EnterReturnsTrue) && nb[0] &&
            !st.script.find_graph(nb)) {
            // Calls refer to a function by name: keep them pointing at it.
            for (Graph& og : st.script.graphs)
                for (Node& n : og.nodes)
                    if (n.def == "fn.call" && n.name == g->name) n.name = nb;
            g->name = nb;
            st.changed();
        }
        if (g->kind == GraphKind::Function) {
            if (ImGui::Checkbox("Pure (no flow pins)", &g->pure)) st.changed();
            auto params = [&](const char* title, std::vector<Param>& ps, const char* prefix) {
                ImGui::TextUnformatted(title);
                int remove = -1;
                for (int i = 0; i < static_cast<int>(ps.size()); ++i) {
                    Param& p = ps[static_cast<size_t>(i)];
                    ImGui::PushID(title);
                    ImGui::PushID(i);
                    char pb[64];
                    std::snprintf(pb, sizeof pb, "%s", p.name.c_str());
                    ImGui::SetNextItemWidth(90.0f);
                    if (ImGui::InputText("##n", pb, sizeof pb) && pb[0]) { p.name = pb; st.changed(); }
                    ImGui::SameLine();
                    if (type_combo("##t", p.type)) { p.def = Value::default_of(p.type); st.changed(); }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) remove = i;
                    ImGui::PopID();
                    ImGui::PopID();
                }
                if (remove >= 0) { ps.erase(ps.begin() + remove); st.changed(); }
                ImGui::PushID(title);
                if (ImGui::SmallButton("+ add")) {
                    ps.push_back(Param{prefix + std::to_string(ps.size()), kFloat, Value::number(0.0)});
                    st.changed();
                }
                ImGui::PopID();
            };
            params("Inputs", g->inputs, "in");
            params("Outputs", g->outputs, "out");
        }
        if (st.script.graphs.size() > 1 && ImGui::SmallButton("Delete this graph")) {
            st.script.graphs.erase(st.script.graphs.begin() + st.graph);
            st.graph = 0;
            st.selected = 0;
            st.changed();
            return;
        }
    }

    ImGui::SeparatorText("Variables");
    int remove = -1;
    for (int i = 0; i < static_cast<int>(st.script.variables.size()); ++i) {
        Variable& v = st.script.variables[static_cast<size_t>(i)];
        ImGui::PushID(1000 + i);
        char vb[64];
        std::snprintf(vb, sizeof vb, "%s", v.name.c_str());
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputText("##n", vb, sizeof vb, ImGuiInputTextFlags_EnterReturnsTrue) && vb[0] &&
            !st.script.find_variable(vb)) {
            for (Graph& og : st.script.graphs)
                for (Node& n : og.nodes)
                    if ((n.def == "var.get" || n.def == "var.set") && n.name == v.name) n.name = vb;
            v.name = vb;
            st.changed();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) remove = i;
        if (type_combo("##t", v.type)) { v.def = Value::default_of(v.type); st.changed(); }
        if (value_editor("def", v.type, v.def, 160.0f)) st.changed();
        if (ImGui::Checkbox("show in Inspector", &v.exposed)) st.changed();
        ImGui::Separator();
        ImGui::PopID();
    }
    if (remove >= 0) {
        st.script.variables.erase(st.script.variables.begin() + remove);
        st.changed();
    }
    if (ImGui::SmallButton("+ Variable")) {
        int n = 1;
        std::string name;
        do name = "Var" + std::to_string(n++); while (st.script.find_variable(name));
        st.script.add_variable(name, kFloat);
        st.changed();
    }

    // Details of the selected node: the things that are not pins.
    Node* sel = g ? g->find(st.selected) : nullptr;
    if (sel) {
        const NodeDef* d = st.reg.find(sel->def);
        ImGui::SeparatorText("Selected node");
        ImGui::TextUnformatted(d ? d->title.c_str() : sel->def.c_str());
        if (d && !d->tooltip.empty()) ImGui::TextWrapped("%s", d->tooltip.c_str());
        if (d && d->named) {
            char eb[64];
            std::snprintf(eb, sizeof eb, "%s", sel->name.c_str());
            if (ImGui::InputText(d->name_label.empty() ? "name" : d->name_label.c_str(), eb, sizeof eb)) sel->name = eb;
            if (ImGui::IsItemDeactivatedAfterEdit()) st.changed();
        }
        if (d && d->builtin == Builtin::CustomEvent) {
            char eb[64];
            std::snprintf(eb, sizeof eb, "%s", sel->name.c_str());
            if (ImGui::InputText("name", eb, sizeof eb, ImGuiInputTextFlags_EnterReturnsTrue) && eb[0]) {
                for (Graph& og : st.script.graphs)
                    for (Node& n : og.nodes)
                        if (n.def == "event.call" && n.name == sel->name) n.name = eb;
                sel->name = eb;
                st.changed();
            }
            int rm = -1;
            for (int i = 0; i < static_cast<int>(sel->params.size()); ++i) {
                Param& p = sel->params[static_cast<size_t>(i)];
                ImGui::PushID(2000 + i);
                char pb[64];
                std::snprintf(pb, sizeof pb, "%s", p.name.c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText("##n", pb, sizeof pb) && pb[0]) { p.name = pb; st.changed(); }
                ImGui::SameLine();
                if (type_combo("##t", p.type)) { p.def = Value::default_of(p.type); st.changed(); }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) rm = i;
                ImGui::PopID();
            }
            if (rm >= 0) { sel->params.erase(sel->params.begin() + rm); st.changed(); }
            if (ImGui::SmallButton("+ parameter")) {
                sel->params.push_back(Param{"p" + std::to_string(sel->params.size()), kFloat, Value::number(0.0)});
                st.changed();
            }
        }
        if (d && (d->builtin == Builtin::Sequence || d->builtin == Builtin::MultiGate || d->builtin == Builtin::Select ||
                  d->builtin == Builtin::MakeArray)) {
            int n = std::max(d->builtin == Builtin::MakeArray ? 1 : 2, sel->extra);
            if (ImGui::InputInt("pins", &n) && n >= 1 && n <= 32) { sel->extra = n; st.changed(); }
        }
        if (d && (d->builtin == Builtin::SwitchInt || d->builtin == Builtin::SwitchString)) {
            int rm = -1;
            for (int i = 0; i < static_cast<int>(sel->cases.size()); ++i) {
                ImGui::PushID(3000 + i);
                char cb[64];
                std::snprintf(cb, sizeof cb, "%s", sel->cases[static_cast<size_t>(i)].c_str());
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputText("##c", cb, sizeof cb) && cb[0]) { sel->cases[static_cast<size_t>(i)] = cb; st.changed(); }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) rm = i;
                ImGui::PopID();
            }
            if (rm >= 0) { sel->cases.erase(sel->cases.begin() + rm); st.changed(); }
            if (ImGui::SmallButton("+ case")) {
                sel->cases.push_back(std::to_string(sel->cases.size()));
                st.changed();
            }
        }
        Shape sh;
        if ((shape_of(st.reg, st.script, *g, *sel, sh) && sh.has_wildcard()) || !sel->targ.is_none()) {
            Type t = sel->targ.is_none() ? kFloat : sel->targ;
            if (type_combo("type", t)) { sel->targ = t; st.changed(); }
        }
        for (const CompileMessage& m : st.messages)
            if (m.graph == st.graph && m.node == sel->id)
                ImGui::TextColored(m.error ? ImVec4(1, 0.4f, 0.4f, 1) : ImVec4(1, 0.8f, 0.3f, 1), "%s", m.text.c_str());
    }
}

void draw_canvas(PanelState& st) {
    Graph* gp = st.cur();
    if (!gp) return;
    Graph& g = *gp;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2       size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 100.0f);
    size.y = std::max(size.y, 100.0f);
    ImGui::BeginChild("##canvas", size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float  z = st.view.zoom;
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(28, 28, 32, 255));
    const float grid = 32.0f * z;
    for (float x = std::fmod(st.view.pan_x * z, grid); x < size.x; x += grid)
        dl->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + size.y), IM_COL32(40, 40, 46, 255));
    for (float y = std::fmod(st.view.pan_y * z, grid); y < size.y; y += grid)
        dl->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(origin.x + size.x, origin.y + y), IM_COL32(40, 40, 46, 255));

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    if (hovered && io.MouseWheel != 0.0f)
        st.view.zoom_at(io.MousePos.x, io.MousePos.y, origin.x, origin.y, io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f, 0.3f, 2.0f);
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        st.view.pan_x += io.MouseDelta.x / z;
        st.view.pan_y += io.MouseDelta.y / z;
    }
    ImGui::SetWindowFontScale(z);

    const float row = ImGui::GetTextLineHeight() + 6.0f * z;
    const float pin_r = 5.0f * z;
    std::unordered_map<uint64_t, ImVec2> pin_pos;   // (node, pin) -> screen centre
    auto key = [](int node, int pin) { return (static_cast<uint64_t>(static_cast<uint32_t>(node)) << 32) | static_cast<uint32_t>(pin); };
    std::unordered_map<int, Shape> shapes;
    PinRef hover_pin;
    int    hover_node = 0;
    bool   widget_active = false;

    for (Node& n : g.nodes) {
        Shape sh;
        std::string why;
        const bool ok = shape_of(st.reg, st.script, g, n, sh, &why);
        const NodeDef* def = st.reg.find(n.def);
        const std::string title = ok && def ? node_title(*def, n) : ("? " + n.def);
        std::vector<int> ins, outs;
        if (ok)
            for (int i = 0; i < static_cast<int>(sh.pins.size()); ++i) (sh.pins[static_cast<size_t>(i)].input ? ins : outs).push_back(i);
        // Width: title, or the widest input label + widest output label.
        float w_in = 0.0f, w_out = 0.0f;
        for (int i : ins) {
            const Pin& p = sh.pins[static_cast<size_t>(i)];
            float w = ImGui::CalcTextSize(p.name.c_str()).x + 22.0f * z;
            const bool linked = std::any_of(g.links.begin(), g.links.end(), [&](const Link& l) { return l.to == n.id && l.to_pin == p.name; });
            if (!linked && !p.type.is_exec() && !p.type.is_any() && !p.type.array && p.type.kind != Kind::Entity && p.type.kind != Kind::Transform)
                w += (p.type.kind == Kind::Bool ? 26.0f : p.type.kind == Kind::Vec3 ? 150.0f : p.type.kind == Kind::String ? 100.0f : 64.0f) * z;
            w_in = std::max(w_in, w);
        }
        for (int i : outs) w_out = std::max(w_out, ImGui::CalcTextSize(sh.pins[static_cast<size_t>(i)].name.c_str()).x + 22.0f * z);
        const float w = std::max(ImGui::CalcTextSize(title.c_str()).x + 20.0f * z, w_in + w_out + 12.0f * z);
        const float h = row * (1.0f + static_cast<float>(std::max(ins.size(), outs.size()))) + 6.0f * z;
        const ImVec2 p0(st.view.to_screen_x(n.x, origin.x), st.view.to_screen_y(n.y, origin.y));
        const ImVec2 p1(p0.x + w, p0.y + h);

        bool has_error = !ok;
        std::string err_text = why;
        for (const CompileMessage& m : st.messages)
            if (m.graph == st.graph && m.node == n.id && m.error) { has_error = true; err_text = m.text; }

        dl->AddRectFilled(p0, p1, IM_COL32(48, 48, 54, 240), 5.0f * z);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + row), def ? title_color(*def) : IM_COL32(120, 40, 40, 255), 5.0f * z, ImDrawFlags_RoundCornersTop);
        dl->AddText(ImVec2(p0.x + 8.0f * z, p0.y + 3.0f * z), IM_COL32(255, 255, 255, 255), title.c_str());
        dl->AddRect(p0, p1, has_error ? IM_COL32(255, 70, 70, 255) : n.id == st.selected ? IM_COL32(255, 200, 60, 255) : IM_COL32(20, 20, 20, 255),
                    5.0f * z, 0, has_error || n.id == st.selected ? 2.0f : 1.0f);
        const bool over_node = hovered && io.MousePos.x >= p0.x && io.MousePos.x <= p1.x && io.MousePos.y >= p0.y && io.MousePos.y <= p1.y;
        if (over_node) hover_node = n.id;
        if (over_node && has_error && !err_text.empty()) ImGui::SetTooltip("%s", err_text.c_str());

        auto draw_pin = [&](int idx, bool input, int slot) {
            const Pin& p = sh.pins[static_cast<size_t>(idx)];
            const float cy = p0.y + row * (1.5f + static_cast<float>(slot));
            const ImVec2 c(input ? p0.x : p1.x, cy);
            pin_pos[key(n.id, idx)] = c;
            const ImU32 col = type_color(p.type);
            if (p.type.is_exec()) dl->AddTriangleFilled(ImVec2(c.x - pin_r, c.y - pin_r), ImVec2(c.x + pin_r, c.y), ImVec2(c.x - pin_r, c.y + pin_r), col);
            else if (p.type.array) dl->AddRectFilled(ImVec2(c.x - pin_r, c.y - pin_r), ImVec2(c.x + pin_r, c.y + pin_r), col);
            else dl->AddCircleFilled(c, pin_r, col);
            const float dx = io.MousePos.x - c.x, dy = io.MousePos.y - c.y;
            if (hovered && dx * dx + dy * dy <= (pin_r * 2.2f) * (pin_r * 2.2f)) hover_pin = PinRef{n.id, idx, input};
            const ImVec2 ts = ImGui::CalcTextSize(p.name.c_str());
            if (input) {
                dl->AddText(ImVec2(c.x + 10.0f * z, cy - ts.y * 0.5f), IM_COL32(220, 220, 220, 255), p.name.c_str());
                const bool linked = std::any_of(g.links.begin(), g.links.end(), [&](const Link& l) { return l.to == n.id && l.to_pin == p.name; });
                if (!linked && !p.type.is_exec() && !p.type.is_any() && !p.type.array && p.type.kind != Kind::Entity && p.type.kind != Kind::Transform) {
                    Value v = p.def;
                    ImGui::SetCursorScreenPos(ImVec2(c.x + 14.0f * z + ts.x, cy - row * 0.5f + 2.0f * z));
                    const float ww = (p.type.kind == Kind::Vec3 ? 140.0f : p.type.kind == Kind::String ? 92.0f : 56.0f) * z;
                    const std::string wid = std::to_string(n.id) + ":" + p.name;
                    if (value_editor(wid.c_str(), p.type, v, ww)) n.set_default(p.name, v);
                    if (ImGui::IsItemActive()) widget_active = true;
                    if (ImGui::IsItemDeactivatedAfterEdit()) st.changed();
                }
            } else {
                dl->AddText(ImVec2(c.x - 10.0f * z - ts.x, cy - ts.y * 0.5f), IM_COL32(220, 220, 220, 255), p.name.c_str());
            }
        };
        for (size_t i = 0; i < ins.size(); ++i) draw_pin(ins[i], true, static_cast<int>(i));
        for (size_t i = 0; i < outs.size(); ++i) draw_pin(outs[i], false, static_cast<int>(i));
        if (ok) shapes.emplace(n.id, std::move(sh));
    }

    // Wires.
    auto wire = [&](ImVec2 a, ImVec2 b, ImU32 col, float thick) {
        const float d = std::max(40.0f * z, std::fabs(b.x - a.x) * 0.5f);
        dl->AddBezierCubic(a, ImVec2(a.x + d, a.y), ImVec2(b.x - d, b.y), b, col, thick);
    };
    for (const Link& l : g.links) {
        const auto fs = shapes.find(l.from), ts = shapes.find(l.to);
        if (fs == shapes.end() || ts == shapes.end()) continue;
        const int fp = fs->second.find(l.from_pin, false), tp = ts->second.find(l.to_pin, true);
        if (fp < 0 || tp < 0) continue;
        const Type t = fs->second.pins[static_cast<size_t>(fp)].type;
        wire(pin_pos[key(l.from, fp)], pin_pos[key(l.to, tp)], type_color(t), (t.is_exec() ? 3.0f : 2.0f) * z);
    }

    // ---- interaction ------------------------------------------------------
    const bool lmb_click = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !widget_active && !ImGui::IsAnyItemActive();
    if (lmb_click) {
        if (hover_pin.valid()) {
            if (io.KeyAlt) {   // Alt-click a pin: cut its wires
                const Shape& sh = shapes[hover_pin.node];
                const std::string& pn = sh.pins[static_cast<size_t>(hover_pin.pin)].name;
                g.links.erase(std::remove_if(g.links.begin(), g.links.end(), [&](const Link& l) {
                                  return hover_pin.input ? (l.to == hover_pin.node && l.to_pin == pn) : (l.from == hover_pin.node && l.from_pin == pn);
                              }), g.links.end());
                st.changed();
            } else {
                st.link_from = hover_pin;
            }
        } else if (hover_node) {
            st.selected = hover_node;
            st.dragging = hover_node;
        } else {
            st.selected = 0;
        }
    }
    if (st.dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (Node* n = g.find(st.dragging)) {
            n->x += io.MouseDelta.x / z;
            n->y += io.MouseDelta.y / z;
        }
    } else if (st.dragging) {
        st.dragging = 0;
        st.changed();
    }
    if (st.link_from.valid()) {
        const ImVec2 a = pin_pos[key(st.link_from.node, st.link_from.pin)];
        if (st.link_from.input) wire(io.MousePos, a, IM_COL32(255, 255, 255, 180), 2.0f * z);
        else wire(a, io.MousePos, IM_COL32(255, 255, 255, 180), 2.0f * z);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (hover_pin.valid()) {
                connect(st, g, st.link_from, hover_pin);
            } else if (hovered && !hover_node) {
                // Dropped on empty canvas: offer nodes, and wire the chosen one up.
                st.palette_for = st.link_from;
                st.palette_x = st.view.to_world_x(io.MousePos.x, origin.x);
                st.palette_y = st.view.to_world_y(io.MousePos.y, origin.y);
                st.search[0] = 0;
                ImGui::OpenPopup("##palette");
            }
            st.link_from = PinRef{};
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !hover_node) {
        st.palette_for = PinRef{};
        st.palette_x = st.view.to_world_x(io.MousePos.x, origin.x);
        st.palette_y = st.view.to_world_y(io.MousePos.y, origin.y);
        st.search[0] = 0;
        ImGui::OpenPopup("##palette");
    }
    if (st.selected && hovered && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        g.remove(st.selected);
        st.selected = 0;
        st.changed();
    }
    if (hovered && !ImGui::IsAnyItemActive() && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
        if (const Node* src = g.find(st.selected)) {
            Node copy = *src;
            copy.id = g.next_id++;
            copy.x += 30.0f;
            copy.y += 30.0f;
            g.nodes.push_back(copy);
            st.selected = copy.id;
            st.changed();
        }
    }

    ImGui::SetWindowFontScale(1.0f);
    if (ImGui::BeginPopup("##palette")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##search", "search nodes", st.search, sizeof st.search);
        ImGui::BeginChild("##list", ImVec2(280.0f, 320.0f));
        // When a wire is being dropped, only offer nodes it can connect to.
        Pin from_pin;
        bool filter_pin = false;
        if (st.palette_for.valid()) {
            const auto it = shapes.find(st.palette_for.node);
            if (it != shapes.end()) { from_pin = it->second.pins[static_cast<size_t>(st.palette_for.pin)]; filter_pin = true; }
        }
        std::string last_cat;
        std::vector<PaletteEntry> entries = palette_entries(st, g);
        std::stable_sort(entries.begin(), entries.end(), [](const PaletteEntry& a, const PaletteEntry& b) { return a.category < b.category; });
        for (const PaletteEntry& e : entries) {
            if (!contains_ci(e.label, st.search) && !contains_ci(e.category, st.search)) continue;
            Node probe;
            probe.def = e.def;
            probe.name = e.name;
            Shape ps;
            int   match = -1;
            if (filter_pin) {
                if (!shape_of(st.reg, st.script, g, probe, ps)) continue;
                for (int i = 0; i < static_cast<int>(ps.pins.size()) && match < 0; ++i) {
                    const Pin& q = ps.pins[static_cast<size_t>(i)];
                    if (q.input == from_pin.input) continue;
                    if (from_pin.input ? pins_compatible(q, from_pin) : pins_compatible(from_pin, q)) match = i;
                }
                if (match < 0) continue;
            }
            if (e.category != last_cat) {
                ImGui::TextDisabled("%s", e.category.c_str());
                last_cat = e.category;
            }
            ImGui::PushID(&e);
            if (ImGui::Selectable(("  " + e.label).c_str())) {
                Node& nn = g.add(e.def, st.palette_x, st.palette_y);
                nn.name = e.name;
                if (e.def == "event.custom") nn.name = "MyEvent" + std::to_string(nn.id);
                if (e.def == "flow.switch_int") nn.cases = {"0", "1"};
                if (e.def == "flow.switch_string") nn.cases = {"a", "b"};
                const int new_id = nn.id;
                st.selected = new_id;
                if (filter_pin && match >= 0) connect(st, g, st.palette_for, PinRef{new_id, match, !from_pin.input});
                st.changed();
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
}

}  // namespace

void open_logic_script(const std::string& path, bool* open_flag) {
    PanelState& st = state();
    if (!st.load(path)) {
        st.new_script();
        st.path = path;
        st.status = "new script: " + path;
    }
    std::snprintf(st.path_buf, sizeof st.path_buf, "%s", path.c_str());
    if (open_flag) *open_flag = true;
}

void draw_logic_script_panel(bool* open) {
    PanelState& st = state();
    ImGui::SetNextWindowSize(ImVec2(1100.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Logic Script", open, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button("New")) { st.new_script(); st.path.clear(); st.path_buf[0] = 0; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputTextWithHint("##path", "assets/scripts/my_script.lgraph", st.path_buf, sizeof st.path_buf);
    ImGui::SameLine();
    if (ImGui::Button("Open") && st.path_buf[0]) st.load(st.path_buf);
    ImGui::SameLine();
    const bool ctrl_s = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S);
    if (ImGui::Button(st.dirty ? "Save*" : "Save") || ctrl_s) {
        st.path = st.path_buf;
        if (!st.path.empty() && st.path.find('.') == std::string::npos) st.path += ".lgraph";
        st.save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo") || (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !ImGui::IsAnyItemActive())) st.do_undo();
    ImGui::SameLine();
    if (ImGui::Button("Redo") || (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y) && !ImGui::IsAnyItemActive())) st.do_redo();
    ImGui::SameLine();
    int errors = 0;
    for (const CompileMessage& m : st.messages) errors += m.error ? 1 : 0;
    if (errors == 0) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "compiles");
    else ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%d error%s", errors, errors == 1 ? "" : "s");
    if (!st.status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", st.status.c_str());
    }

    ImGui::BeginChild("##side", ImVec2(250.0f, 0.0f), true);
    draw_side_panel(st);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float msg_h = errors ? std::min(90.0f, 22.0f * static_cast<float>(errors) + 8.0f) : 0.0f;
    ImGui::BeginChild("##graph", ImVec2(0.0f, -msg_h - (errors ? 4.0f : 0.0f)), false, ImGuiWindowFlags_NoScrollbar);
    draw_canvas(st);
    ImGui::EndChild();
    if (errors) {
        ImGui::BeginChild("##messages", ImVec2(0.0f, msg_h), true);
        for (const CompileMessage& m : st.messages) {
            if (!m.error) continue;
            const std::string where = m.graph >= 0 && m.graph < static_cast<int>(st.script.graphs.size())
                                          ? st.script.graphs[static_cast<size_t>(m.graph)].name : std::string("script");
            ImGui::PushID(&m);
            if (ImGui::Selectable((where + ": " + m.text).c_str()) && m.graph >= 0) {
                st.graph = m.graph;
                st.selected = m.node;
                if (const Graph* g = st.cur())
                    if (const Node* n = g->find(m.node)) {   // bring the node into view
                        st.view.pan_x = 200.0f - n->x;
                        st.view.pan_y = 150.0f - n->y;
                    }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::EndGroup();
    ImGui::End();
}

}  // namespace schizo::editor
