#include "logic_script_panel.h"

#include "logic_debug.h"
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
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
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

/// What a held left mouse button is doing on the canvas.
enum class Drag { None, Nodes, Band, MoveBox, ResizeBox };

struct PanelState {
    Registry     reg;
    LogicApiSlot slot;            // never pointed at an engine: the editor only needs shapes
    Script       script;
    std::string  path;
    bool         dirty = false;
    int          graph = 0;
    CanvasView   view;
    int          selected = 0;    // the node the side panel shows (the last one picked)
    std::vector<int> sel;         // every selected node
    int          sel_box = 0;     // the selected comment box
    Drag         drag = Drag::None;
    bool         drag_moved = false;
    bool         press_additive = false;
    int          press_node = 0;
    float        band_x = 0.0f, band_y = 0.0f;   // where a box selection started (canvas units)
    std::vector<int> band_base;   // what was selected before it
    std::vector<int> carry;       // nodes a moving comment box takes along

    // Debugging. Breakpoints are (graph index, node id); they belong to the
    // file and are handed to the logic host whenever they change.
    std::set<std::pair<int, int>> bps;
    uint32_t     debug_entity = 0;      // the object whose run is shown while the game plays
    int          want_center = 0;       // a node to bring to the middle of the canvas
    // The running game as this script sees it, refreshed every frame.
    LogicDebugger* dbg = nullptr;       // set while an object is running this file
    bool         stopped_here = false;  // ... and that object is stopped at a breakpoint in it
    int          stopped_graph = -1, stopped_node = 0;
    std::string  key_of, key;           // `path` as the logic host spells it, cached
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
    int          uid = 0;         // names this script's tab for ImGui

    PanelState() {
        static int next_uid = 1;
        uid = next_uid++;
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
        clear_selection();
        undo.clear();
        redo.clear();
        last_snapshot = to_text(script);
        dirty = false;
        recompile();
    }
    /// The file by the spelling the logic host uses for it.
    const std::string& file_key() {
        if (key_of != path || key.empty()) {
            std::error_code ec;
            const std::filesystem::path c = std::filesystem::weakly_canonical(path, ec);
            key = ec ? path : c.generic_string();
            key_of = path;
        }
        return key;
    }
    void push_breakpoints() {
        LogicDebugger* d = logic_debugger();
        if (d && !path.empty()) d->set_breakpoints(path, std::vector<std::pair<int, int>>(bps.begin(), bps.end()));
    }
    bool is_selected(int id) const { return std::find(sel.begin(), sel.end(), id) != sel.end(); }
    void clear_selection() {
        sel.clear();
        selected = 0;
        sel_box = 0;
    }
    void select_only(int id) {
        sel.assign(1, id);
        selected = id;
        sel_box = 0;
    }
    void toggle(int id) {
        const auto it = std::find(sel.begin(), sel.end(), id);
        if (it != sel.end()) sel.erase(it);
        else sel.push_back(id);
        selected = sel.empty() ? 0 : sel.back();
        sel_box = 0;
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
        clear_selection();
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
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) {
            status = "could not write " + path;
            return false;
        }
        f << to_text(script);
        dirty = false;
        status = "saved " + path;
        push_breakpoints();   // the file may have just got its name
        return true;
    }
    std::string tab_name() const {
        return path.empty() ? std::string("untitled") : std::filesystem::path(path).filename().string();
    }
    /// The tab the panel starts with, before anything was done in it.
    bool untouched() const { return path.empty() && !dirty && undo.empty(); }
};

/// Every script open in the panel, one tab each.
struct Docs {
    std::vector<std::unique_ptr<PanelState>> list;
    int  active = 0;
    int  select = -1;    // tab to bring forward on the next frame
    int  closing = -1;   // tab whose close waits for "save the changes?"
    std::vector<std::string> found;   // .lgraph files offered by Open
    char open_buf[260] = {};
};

Docs& docs() {
    static Docs d;
    if (d.list.empty()) d.list.push_back(std::make_unique<PanelState>());
    d.active = std::clamp(d.active, 0, static_cast<int>(d.list.size()) - 1);
    return d;
}

bool same_file(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code ea, eb;
    const std::filesystem::path pa = std::filesystem::weakly_canonical(a, ea);
    const std::filesystem::path pb = std::filesystem::weakly_canonical(b, eb);
    return ea || eb ? a == b : pa == pb;
}

/// Bring `path` up in a tab: the one that already has it, or a new one. A
/// path with no file behind it becomes a new script that will be saved there.
PanelState& open_doc(const std::string& path) {
    Docs& d = docs();
    for (size_t i = 0; i < d.list.size(); ++i)
        if (same_file(d.list[i]->path, path)) {
            d.select = static_cast<int>(i);
            return *d.list[i];
        }
    int at = d.active;
    if (!d.list[static_cast<size_t>(at)]->untouched()) {
        d.list.push_back(std::make_unique<PanelState>());
        at = static_cast<int>(d.list.size()) - 1;
    }
    PanelState&     st = *d.list[static_cast<size_t>(at)];
    std::error_code ec;
    const bool      exists = std::filesystem::exists(path, ec);
    if (st.load(path)) {
        std::snprintf(st.path_buf, sizeof st.path_buf, "%s", path.c_str());
    } else if (!exists) {
        st.new_script();
        st.path = path;
        st.script.name = std::filesystem::path(path).stem().string();
        st.last_snapshot = to_text(st.script);
        st.status = "new script: " + path;
        st.dirty = true;   // not on disk until it is saved
        std::snprintf(st.path_buf, sizeof st.path_buf, "%s", path.c_str());
    }
    // A file that is there but cannot be read stays untouched on disk: the tab
    // keeps no path, so Save cannot write an empty script over it.
    d.select = at;
    return st;
}

void close_doc(int index) {
    Docs& d = docs();
    if (index < 0 || index >= static_cast<int>(d.list.size())) return;
    d.list.erase(d.list.begin() + index);
    if (d.list.empty()) d.list.push_back(std::make_unique<PanelState>());
    if (d.active >= index && d.active > 0) --d.active;
    d.active = std::clamp(d.active, 0, static_cast<int>(d.list.size()) - 1);
    d.select = d.active;
}

/// The project's logic scripts, for the Open list.
std::vector<std::string> find_logic_scripts() {
    std::vector<std::string> out;
    std::error_code          ec;
    std::filesystem::recursive_directory_iterator it("assets", std::filesystem::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && out.size() < 500; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() == ".lgraph") out.push_back(it->path().generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::function<std::string(uint32_t)>& entity_namer() {
    static std::function<std::string(uint32_t)> f;
    return f;
}
std::string entity_label(uint32_t e) {
    const std::string n = entity_namer() ? entity_namer()(e) : std::string();
    return (n.empty() ? std::string("object") : n) + "  #" + std::to_string(e);
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
        case Builtin::SendEvent: return n.name.empty() ? d.title : "Send " + n.name;
        case Builtin::RemoteGet: return n.name.empty() ? d.title : "Get " + n.name + " of";
        case Builtin::RemoteSet: return n.name.empty() ? d.title : "Set " + n.name + " of";
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

/// The parameter list of a custom event, or of the event a node sends.
void edit_params(PanelState& st, Node& node) {
    int rm = -1;
    for (int i = 0; i < static_cast<int>(node.params.size()); ++i) {
        Param& p = node.params[static_cast<size_t>(i)];
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
    if (rm >= 0) { node.params.erase(node.params.begin() + rm); st.changed(); }
    if (ImGui::SmallButton("+ parameter")) {
        node.params.push_back(Param{"p" + std::to_string(node.params.size()), kFloat, Value::number(0.0)});
        st.changed();
    }
}

void draw_side_panel(PanelState& st) {
    ImGui::TextUnformatted("Graphs");
    for (int i = 0; i < static_cast<int>(st.script.graphs.size()); ++i) {
        Graph& g = st.script.graphs[static_cast<size_t>(i)];
        ImGui::PushID(i);
        const std::string label = (g.kind == GraphKind::Function ? "f  " : "ev ") + g.name;
        if (ImGui::Selectable(label.c_str(), st.graph == i)) {
            st.graph = i;
            st.clear_selection();
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
            st.clear_selection();
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
        if (st.dbg) {
            std::string live;
            if (st.dbg->variable_text(st.debug_entity, v.name, live)) ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "now: %s", live.c_str());
        }
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

    if (CommentBox* box = g ? g->find_box(st.sel_box) : nullptr) {
        ImGui::SeparatorText("Comment box");
        char cb[512];
        std::snprintf(cb, sizeof cb, "%s", box->text.c_str());
        if (ImGui::InputTextMultiline("##boxtext", cb, sizeof cb, ImVec2(-1.0f, 60.0f))) box->text = cb;
        if (ImGui::IsItemDeactivatedAfterEdit()) st.changed();
        ImGui::ColorEdit3("colour", box->color, ImGuiColorEditFlags_NoInputs);
        if (ImGui::IsItemDeactivatedAfterEdit()) st.changed();
    }

    // Details of the selected node: the things that are not pins.
    Node* sel = g ? g->find(st.selected) : nullptr;
    if (sel) {
        const NodeDef* d = st.reg.find(sel->def);
        ImGui::SeparatorText(st.sel.size() > 1 ? "Selected nodes" : "Selected node");
        if (st.sel.size() > 1) ImGui::TextDisabled("%d selected; showing the last one", static_cast<int>(st.sel.size()));
        ImGui::TextUnformatted(d ? d->title.c_str() : sel->def.c_str());
        if (d && !d->tooltip.empty()) ImGui::TextWrapped("%s", d->tooltip.c_str());
        {
            char nb[256];
            std::snprintf(nb, sizeof nb, "%s", sel->comment.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputTextWithHint("##nodecomment", "a note shown above the node", nb, sizeof nb)) sel->comment = nb;
            if (ImGui::IsItemDeactivatedAfterEdit()) st.changed();
        }
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
            edit_params(st, *sel);
        }
        if (d && d->builtin == Builtin::SendEvent) {
            char eb[64];
            std::snprintf(eb, sizeof eb, "%s", sel->name.c_str());
            if (ImGui::InputText("event", eb, sizeof eb)) sel->name = eb;
            if (ImGui::IsItemDeactivatedAfterEdit()) st.changed();
            ImGui::TextDisabled("what it sends (same order as the event's parameters)");
            edit_params(st, *sel);
            // Fill the list in from a custom event of this script with that name.
            for (const Graph& og : st.script.graphs)
                for (const Node& n : og.nodes)
                    if (n.def == "event.custom" && n.name == sel->name && !n.params.empty() && n.id != sel->id &&
                        sel->params.empty() && ImGui::SmallButton("use this script's parameters")) {
                        sel->params = n.params;
                        st.changed();
                    }
        }
        if (d && (d->builtin == Builtin::RemoteGet || d->builtin == Builtin::RemoteSet)) {
            char eb[64];
            std::snprintf(eb, sizeof eb, "%s", sel->name.c_str());
            if (ImGui::InputText("variable", eb, sizeof eb)) sel->name = eb;
            if (ImGui::IsItemDeactivatedAfterEdit()) st.changed();
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
        if (sel->targ.is_exec()) {
            // a knot on a flow wire: nothing to pick
        } else if ((shape_of(st.reg, st.script, *g, *sel, sh) && sh.has_wildcard()) || !sel->targ.is_none()) {
            Type t = sel->targ.is_none() ? kFloat : sel->targ;
            if (type_combo("type", t)) { sel->targ = t; st.changed(); }
        }
        for (const CompileMessage& m : st.messages)
            if (m.graph == st.graph && m.node == sel->id)
                ImGui::TextColored(m.error ? ImVec4(1, 0.4f, 0.4f, 1) : ImVec4(1, 0.8f, 0.3f, 1), "%s", m.text.c_str());
    }
}

struct NodeRect {
    int    id;
    ImVec2 a, b;   // screen corners
};

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
    auto sx = [&](float world_x) { return st.view.to_screen_x(world_x, origin.x); };
    auto sy = [&](float world_y) { return st.view.to_screen_y(world_y, origin.y); };
    auto wx = [&](float screen_x) { return st.view.to_world_x(screen_x, origin.x); };
    auto wy = [&](float screen_y) { return st.view.to_world_y(screen_y, origin.y); };
    auto mouse_in = [&](ImVec2 a, ImVec2 b) {
        return io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y;
    };
    std::unordered_map<uint64_t, ImVec2> pin_pos;   // (node, pin) -> screen centre
    auto key = [](int node, int pin) { return (static_cast<uint64_t>(static_cast<uint32_t>(node)) << 32) | static_cast<uint32_t>(pin); };
    std::unordered_map<int, Shape> shapes;
    std::vector<NodeRect>          rects;
    PinRef hover_pin;
    int    hover_node = 0;
    bool   widget_active = false;
    std::set<int> ran;   // nodes that ran a moment ago on the watched object

    // ---- comment boxes, behind everything ----------------------------------
    // Only the title bar and the corner grip take the mouse: the body is
    // canvas, so a box selection can start inside a comment.
    int  hover_box = 0;
    bool hover_grip = false;
    for (const CommentBox& b : g.boxes) {
        const ImVec2 a(sx(b.x), sy(b.y)), c(a.x + b.w * z, a.y + b.h * z);
        const ImU32  fill = ImGui::ColorConvertFloat4ToU32(ImVec4(b.color[0], b.color[1], b.color[2], 0.16f));
        const ImU32  bar = ImGui::ColorConvertFloat4ToU32(ImVec4(b.color[0], b.color[1], b.color[2], 0.80f));
        dl->AddRectFilled(a, c, fill, 4.0f * z);
        dl->AddRectFilled(a, ImVec2(c.x, a.y + row), bar, 4.0f * z, ImDrawFlags_RoundCornersTop);
        dl->PushClipRect(a, ImVec2(c.x, a.y + row), true);
        dl->AddText(ImVec2(a.x + 8.0f * z, a.y + 3.0f * z), IM_COL32(255, 255, 255, 255), b.text.c_str());
        dl->PopClipRect();
        dl->AddRect(a, c, b.id == st.sel_box ? IM_COL32(255, 200, 60, 255) : bar, 4.0f * z, 0, b.id == st.sel_box ? 2.0f : 1.0f);
        const float gs = 14.0f * z;
        dl->AddTriangleFilled(ImVec2(c.x - gs, c.y), ImVec2(c.x, c.y - gs), c, bar);
        if (hovered && mouse_in(ImVec2(c.x - gs, c.y - gs), c)) {
            hover_box = b.id;
            hover_grip = true;
        } else if (hovered && mouse_in(a, ImVec2(c.x, a.y + row))) {
            hover_box = b.id;
            hover_grip = false;
        }
    }

    // ---- nodes -------------------------------------------------------------
    for (Node& n : g.nodes) {
        Shape sh;
        std::string why;
        const bool ok = shape_of(st.reg, st.script, g, n, sh, &why);
        const NodeDef* def = st.reg.find(n.def);
        const bool knot = ok && def && def->builtin == Builtin::Reroute;   // drawn as a knot on the wire
        const std::string title = ok && def ? node_title(*def, n) : ("? " + n.def);
        std::vector<int> ins, outs;
        if (ok)
            for (int i = 0; i < static_cast<int>(sh.pins.size()); ++i) (sh.pins[static_cast<size_t>(i)].input ? ins : outs).push_back(i);
        auto has_editor = [&](const Pin& p) {
            const bool linked = std::any_of(g.links.begin(), g.links.end(), [&](const Link& l) { return l.to == n.id && l.to_pin == p.name; });
            return !knot && !linked && !p.type.is_exec() && !p.type.is_any() && !p.type.array && p.type.kind != Kind::Entity &&
                   p.type.kind != Kind::Transform;
        };
        // Width: title, or the widest input label + widest output label.
        float w = 26.0f * z, h = row;
        if (!knot) {
            float w_in = 0.0f, w_out = 0.0f;
            for (int i : ins) {
                const Pin& p = sh.pins[static_cast<size_t>(i)];
                float pw = ImGui::CalcTextSize(p.name.c_str()).x + 22.0f * z;
                if (has_editor(p))
                    pw += (p.type.kind == Kind::Bool ? 26.0f : p.type.kind == Kind::Vec3 ? 150.0f : p.type.kind == Kind::String ? 100.0f : 64.0f) * z;
                w_in = std::max(w_in, pw);
            }
            for (int i : outs) w_out = std::max(w_out, ImGui::CalcTextSize(sh.pins[static_cast<size_t>(i)].name.c_str()).x + 22.0f * z);
            w = std::max(ImGui::CalcTextSize(title.c_str()).x + 20.0f * z, w_in + w_out + 12.0f * z);
            h = row * (1.0f + static_cast<float>(std::max(ins.size(), outs.size()))) + 6.0f * z;
        }
        const ImVec2 p0(sx(n.x), sy(n.y));
        const ImVec2 p1(p0.x + w, p0.y + h);
        rects.push_back(NodeRect{n.id, p0, p1});

        bool has_error = !ok;
        std::string err_text = why;
        for (const CompileMessage& m : st.messages)
            if (m.graph == st.graph && m.node == n.id && m.error) { has_error = true; err_text = m.text; }

        const bool  is_sel = st.is_selected(n.id);
        const ImU32 edge = has_error ? IM_COL32(255, 70, 70, 255) : is_sel ? IM_COL32(255, 200, 60, 255) : IM_COL32(20, 20, 20, 255);
        const float edge_w = has_error || is_sel ? 2.0f : 1.0f;
        if (knot) {
            dl->AddRectFilled(p0, p1, IM_COL32(62, 62, 70, 240), h * 0.5f);
            dl->AddRect(p0, p1, edge, h * 0.5f, 0, edge_w);
        } else {
            dl->AddRectFilled(p0, p1, IM_COL32(48, 48, 54, 240), 5.0f * z);
            dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + row), def ? title_color(*def) : IM_COL32(120, 40, 40, 255), 5.0f * z, ImDrawFlags_RoundCornersTop);
            dl->AddText(ImVec2(p0.x + 8.0f * z, p0.y + 3.0f * z), IM_COL32(255, 255, 255, 255), title.c_str());
            dl->AddRect(p0, p1, edge, 5.0f * z, 0, edge_w);
        }
        if (!n.comment.empty()) {   // the node's own note, above it
            const ImVec2 ts = ImGui::CalcTextSize(n.comment.c_str());
            const ImVec2 c0(p0.x, p0.y - ts.y - 10.0f * z), c1(p0.x + ts.x + 12.0f * z, p0.y - 4.0f * z);
            dl->AddRectFilled(c0, c1, IM_COL32(74, 70, 40, 235), 4.0f * z);
            dl->AddText(ImVec2(c0.x + 6.0f * z, c0.y + 3.0f * z), IM_COL32(255, 240, 180, 255), n.comment.c_str());
        }
        // The running game: a glow on what just ran, a marker where it is stopped.
        if (st.dbg) {
            const double since = st.dbg->seconds_since_run(st.debug_entity, st.graph, n.id);
            if (since >= 0.0 && since < 0.6) {
                ran.insert(n.id);
                const int alpha = static_cast<int>(255.0 * (1.0 - since / 0.6));
                dl->AddRect(ImVec2(p0.x - 3.0f, p0.y - 3.0f), ImVec2(p1.x + 3.0f, p1.y + 3.0f), IM_COL32(110, 215, 255, alpha),
                            (knot ? h * 0.5f : 5.0f * z) + 3.0f, 0, 3.0f);
            }
        }
        if (st.stopped_here && st.stopped_graph == st.graph && st.stopped_node == n.id) {
            dl->AddRect(ImVec2(p0.x - 4.0f, p0.y - 4.0f), ImVec2(p1.x + 4.0f, p1.y + 4.0f), IM_COL32(255, 150, 40, 255), 5.0f * z + 4.0f, 0, 4.0f);
            const float ay = p0.y + row * 0.5f, as = 9.0f * z;
            dl->AddTriangleFilled(ImVec2(p0.x - 8.0f - as * 1.6f, ay - as), ImVec2(p0.x - 8.0f, ay), ImVec2(p0.x - 8.0f - as * 1.6f, ay + as),
                                  IM_COL32(255, 150, 40, 255));
        }
        if (st.bps.count({st.graph, n.id})) {
            const ImVec2 c(p0.x + 1.0f, p0.y + 1.0f);
            dl->AddCircleFilled(c, 7.0f * z, IM_COL32(225, 45, 45, 255));
            dl->AddCircle(c, 7.0f * z, IM_COL32(20, 20, 20, 255), 0, 1.5f);
        }
        const bool over_node = hovered && mouse_in(p0, p1);
        if (over_node) hover_node = n.id;
        if (over_node && has_error && !err_text.empty()) ImGui::SetTooltip("%s", err_text.c_str());

        auto draw_pin = [&](int idx, bool input, int slot) {
            const Pin& p = sh.pins[static_cast<size_t>(idx)];
            const float cy = knot ? p0.y + h * 0.5f : p0.y + row * (1.5f + static_cast<float>(slot));
            const ImVec2 c(input ? p0.x : p1.x, cy);
            pin_pos[key(n.id, idx)] = c;
            const ImU32 col = type_color(p.type);
            if (p.type.is_exec()) dl->AddTriangleFilled(ImVec2(c.x - pin_r, c.y - pin_r), ImVec2(c.x + pin_r, c.y), ImVec2(c.x - pin_r, c.y + pin_r), col);
            else if (p.type.array) dl->AddRectFilled(ImVec2(c.x - pin_r, c.y - pin_r), ImVec2(c.x + pin_r, c.y + pin_r), col);
            else dl->AddCircleFilled(c, pin_r, col);
            const float dx = io.MousePos.x - c.x, dy = io.MousePos.y - c.y;
            if (hovered && dx * dx + dy * dy <= (pin_r * 2.2f) * (pin_r * 2.2f)) hover_pin = PinRef{n.id, idx, input};
            if (knot) return;
            const ImVec2 ts = ImGui::CalcTextSize(p.name.c_str());
            if (input) {
                dl->AddText(ImVec2(c.x + 10.0f * z, cy - ts.y * 0.5f), IM_COL32(220, 220, 220, 255), p.name.c_str());
                if (has_editor(p)) {
                    Value v = p.def;
                    // The editor's frame is sized to the row, whatever the theme's padding is.
                    const float pad_y = 2.0f * z;
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * z, pad_y));
                    ImGui::SetCursorScreenPos(ImVec2(c.x + 14.0f * z + ts.x, cy - ImGui::GetTextLineHeight() * 0.5f - pad_y));
                    const float ww = (p.type.kind == Kind::Vec3 ? 140.0f : p.type.kind == Kind::String ? 92.0f : 56.0f) * z;
                    const std::string wid = std::to_string(n.id) + ":" + p.name;
                    if (value_editor(wid.c_str(), p.type, v, ww)) n.set_default(p.name, v);
                    if (ImGui::IsItemActive()) widget_active = true;
                    if (ImGui::IsItemDeactivatedAfterEdit()) st.changed();
                    ImGui::PopStyleVar();
                }
            } else {
                dl->AddText(ImVec2(c.x - 10.0f * z - ts.x, cy - ts.y * 0.5f), IM_COL32(220, 220, 220, 255), p.name.c_str());
            }
        };
        for (size_t i = 0; i < ins.size(); ++i) draw_pin(ins[i], true, static_cast<int>(i));
        for (size_t i = 0; i < outs.size(); ++i) draw_pin(outs[i], false, static_cast<int>(i));
        if (ok) shapes.emplace(n.id, std::move(sh));
    }

    // ---- wires -------------------------------------------------------------
    auto wire = [&](ImVec2 a, ImVec2 b, ImU32 col, float thick) {
        const float d = std::max(40.0f * z, std::fabs(b.x - a.x) * 0.5f);
        dl->AddBezierCubic(a, ImVec2(a.x + d, a.y), ImVec2(b.x - d, b.y), b, col, thick);
    };
    // How near the mouse is to a wire: the curve is walked in short pieces.
    auto near_wire = [&](ImVec2 a, ImVec2 b) {
        const float  d = std::max(40.0f * z, std::fabs(b.x - a.x) * 0.5f);
        const ImVec2 c1(a.x + d, a.y), c2(b.x - d, b.y);
        ImVec2       prev = a;
        for (int s = 1; s <= 24; ++s) {
            const float  t = static_cast<float>(s) / 24.0f, u = 1.0f - t;
            const float  k0 = u * u * u, k1 = 3.0f * u * u * t, k2 = 3.0f * u * t * t, k3 = t * t * t;
            const ImVec2 q(k0 * a.x + k1 * c1.x + k2 * c2.x + k3 * b.x, k0 * a.y + k1 * c1.y + k2 * c2.y + k3 * b.y);
            const float  ex = q.x - prev.x, ey = q.y - prev.y, len2 = ex * ex + ey * ey;
            float        f = len2 > 0.0f ? ((io.MousePos.x - prev.x) * ex + (io.MousePos.y - prev.y) * ey) / len2 : 0.0f;
            f = std::clamp(f, 0.0f, 1.0f);
            const float  mx = io.MousePos.x - (prev.x + ex * f), my = io.MousePos.y - (prev.y + ey * f);
            if (mx * mx + my * my <= 36.0f) return true;
            prev = q;
        }
        return false;
    };
    int hover_wire = -1;
    for (size_t li = 0; li < g.links.size(); ++li) {
        const Link& l = g.links[li];
        const auto fs = shapes.find(l.from), ts = shapes.find(l.to);
        if (fs == shapes.end() || ts == shapes.end()) continue;
        const int fp = fs->second.find(l.from_pin, false), tp = ts->second.find(l.to_pin, true);
        if (fp < 0 || tp < 0) continue;
        const Type   t = fs->second.pins[static_cast<size_t>(fp)].type;
        const ImVec2 a = pin_pos[key(l.from, fp)], b = pin_pos[key(l.to, tp)];
        const bool   hot = hovered && !hover_pin.valid() && !hover_node && hover_wire < 0 && st.drag == Drag::None &&
                           !st.link_from.valid() && near_wire(a, b);
        if (hot) hover_wire = static_cast<int>(li);
        // A flow wire whose two ends both just ran is the path the run took.
        const bool taken = t.is_exec() && ran.count(l.from) && ran.count(l.to);
        wire(a, b, taken ? IM_COL32(110, 215, 255, 255) : type_color(t), (t.is_exec() ? 3.0f : 2.0f) * z + (hot ? 2.0f : 0.0f) + (taken ? 1.5f : 0.0f));
    }
    if (hover_wire >= 0) ImGui::SetTooltip("double-click: add a knot   Alt-click: cut");

    // What is on a pin, while the game runs: an output's own value, an input's
    // source. A pure node keeps nothing (it is computed where it is used).
    if (st.dbg && hover_pin.valid() && !st.link_from.valid()) {
        int         src_node = hover_pin.node, src_pin = hover_pin.pin;
        const auto  hs = shapes.find(hover_pin.node);
        std::string label = hs != shapes.end() ? hs->second.pins[static_cast<size_t>(hover_pin.pin)].name : std::string();
        bool        have = hs != shapes.end() && !hs->second.pins[static_cast<size_t>(hover_pin.pin)].type.is_exec();
        if (have && hover_pin.input) {
            have = false;
            for (const Link& l : g.links)
                if (l.to == hover_pin.node && l.to_pin == label) {
                    const auto ss = shapes.find(l.from);
                    const int  sp = ss != shapes.end() ? ss->second.find(l.from_pin, false) : -1;
                    if (sp >= 0) { src_node = l.from; src_pin = sp; have = true; }
                }
        }
        if (have) {
            const Shape& ss = shapes[src_node];
            int          slot = 0;
            for (int i = 0; i < src_pin; ++i)
                if (!ss.pins[static_cast<size_t>(i)].input && !ss.pins[static_cast<size_t>(i)].type.is_exec()) ++slot;
            std::string text;
            if (st.dbg->output_text(st.debug_entity, st.graph, src_node, slot, text)) ImGui::SetTooltip("%s = %s", label.c_str(), text.c_str());
            else if (ss.pure) ImGui::SetTooltip("%s: worked out each time it is used", label.c_str());
        }
    }
    if (st.want_center) {
        for (const NodeRect& r : rects)
            if (r.id == st.want_center) {
                st.view.pan_x += (origin.x + size.x * 0.5f - (r.a.x + r.b.x) * 0.5f) / z;
                st.view.pan_y += (origin.y + size.y * 0.5f - (r.a.y + r.b.y) * 0.5f) / z;
            }
        st.want_center = 0;
    }

    // ---- what the keys and menus do ----------------------------------------
    auto world_rect = [&](const NodeRect& r, float& x0, float& y0, float& x1, float& y1, bool& any) {
        const float ax = wx(r.a.x), ay = wy(r.a.y), bx = wx(r.b.x), by = wy(r.b.y);
        if (!any) { x0 = ax; y0 = ay; x1 = bx; y1 = by; any = true; return; }
        x0 = std::min(x0, ax); y0 = std::min(y0, ay); x1 = std::max(x1, bx); y1 = std::max(y1, by);
    };
    auto do_delete = [&]() {
        bool any = false;
        for (int id : st.sel) { g.remove(id); any = true; }
        if (st.sel_box) {
            g.boxes.erase(std::remove_if(g.boxes.begin(), g.boxes.end(), [&](const CommentBox& b) { return b.id == st.sel_box; }), g.boxes.end());
            any = true;
        }
        st.clear_selection();
        if (any) st.changed();
    };
    auto toggle_breakpoints = [&]() {
        // A stop happens where a node with flow pins starts to run; a pure
        // node has no such moment.
        bool any = false;
        for (int id : st.sel) {
            const auto s = shapes.find(id);
            if (s == shapes.end() || s->second.pure) continue;
            const std::pair<int, int> k{st.graph, id};
            if (!st.bps.erase(k)) st.bps.insert(k);
            any = true;
        }
        if (any) st.push_breakpoints();
        else if (!st.sel.empty()) st.status = "a breakpoint needs a node with flow pins";
    };
    auto do_copy = [&]() {
        if (st.sel.empty()) return;
        ImGui::SetClipboardText(selection_text(g, st.sel).c_str());
        st.status = std::to_string(st.sel.size()) + (st.sel.size() == 1 ? " node copied" : " nodes copied");
    };
    auto do_paste_text = [&](const std::string& text, float x, float y) {
        const std::vector<int> made = paste_selection(g, text, x, y);
        if (made.empty()) return false;
        st.sel = made;
        st.selected = made.back();
        st.sel_box = 0;
        st.changed();
        return true;
    };
    auto do_paste = [&](float x, float y) {
        const char* clip = ImGui::GetClipboardText();
        if (!clip || !do_paste_text(clip, x, y)) st.status = "nothing to paste (copy some nodes first)";
    };
    auto do_duplicate = [&]() {
        if (st.sel.empty()) return;
        float minx = 0.0f, miny = 0.0f;
        bool  any = false;
        for (int id : st.sel)
            if (const Node* n = g.find(id)) {
                minx = any ? std::min(minx, n->x) : n->x;
                miny = any ? std::min(miny, n->y) : n->y;
                any = true;
            }
        if (any) do_paste_text(selection_text(g, st.sel), minx + 30.0f, miny + 30.0f);
    };
    auto add_comment = [&](float at_x, float at_y) {
        CommentBox b;
        b.id = g.next_id++;
        b.text = "Comment";
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool  any = false;
        for (const NodeRect& r : rects)
            if (st.is_selected(r.id)) world_rect(r, x0, y0, x1, y1, any);
        if (any) {   // wrap the selection, with room for the title
            const float bar = row / z;
            b.x = x0 - 24.0f;
            b.y = y0 - 24.0f - bar;
            b.w = (x1 - x0) + 48.0f;
            b.h = (y1 - y0) + 48.0f + bar;
        } else {
            b.x = at_x;
            b.y = at_y;
        }
        g.boxes.push_back(b);
        st.clear_selection();
        st.sel_box = b.id;
        st.changed();
    };
    auto frame = [&](bool only_selected) {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool  any = false;
        for (const NodeRect& r : rects)
            if (!only_selected || st.is_selected(r.id)) world_rect(r, x0, y0, x1, y1, any);
        if (!only_selected)
            for (const CommentBox& b : g.boxes) world_rect(NodeRect{0, ImVec2(sx(b.x), sy(b.y)), ImVec2(sx(b.x + b.w), sy(b.y + b.h))}, x0, y0, x1, y1, any);
        if (!any) return;
        const float bw = std::max(1.0f, x1 - x0), bh = std::max(1.0f, y1 - y0);
        const float nz = std::clamp(std::min(size.x / (bw + 120.0f), size.y / (bh + 120.0f)), 0.3f, 1.0f);
        st.view.zoom = nz;
        st.view.pan_x = (size.x / nz - bw) * 0.5f - x0;
        st.view.pan_y = (size.y / nz - bh) * 0.5f - y0;
    };

    // ---- mouse -------------------------------------------------------------
    const bool additive = io.KeyShift || io.KeyCtrl;
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
            if (additive) st.toggle(hover_node);
            else if (!st.is_selected(hover_node)) st.select_only(hover_node);
            else { st.selected = hover_node; st.sel_box = 0; }   // keeps the group, for dragging it
            st.drag = Drag::Nodes;
            st.drag_moved = false;
            st.press_additive = additive;
            st.press_node = hover_node;
        } else if (hover_wire >= 0 && io.KeyAlt) {
            g.links.erase(g.links.begin() + hover_wire);
            st.changed();
        } else if (hover_wire >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            // A knot on the wire, to route it around something.
            const Link l = g.links[static_cast<size_t>(hover_wire)];
            const auto fs = shapes.find(l.from);
            const int  fp = fs != shapes.end() ? fs->second.find(l.from_pin, false) : -1;
            if (fp >= 0) {
                const Type t = fs->second.pins[static_cast<size_t>(fp)].type;
                Node& kn = g.add("flow.reroute", wx(io.MousePos.x) - 13.0f, wy(io.MousePos.y) - row / z * 0.5f);
                kn.targ = t;
                const int kid = kn.id;
                g.links.erase(g.links.begin() + hover_wire);
                g.link(l.from, l.from_pin, kid, "In");
                g.link(kid, "Out", l.to, l.to_pin);
                st.select_only(kid);
                st.changed();
            }
        } else if (hover_box) {
            st.clear_selection();
            st.sel_box = hover_box;
            st.drag = hover_grip ? Drag::ResizeBox : Drag::MoveBox;
            st.drag_moved = false;
            st.carry.clear();
            if (const CommentBox* b = g.find_box(hover_box))
                for (const Node& n : g.nodes)
                    if (n.x >= b->x && n.x <= b->x + b->w && n.y >= b->y && n.y <= b->y + b->h) st.carry.push_back(n.id);
        } else {
            if (!additive) st.clear_selection();
            st.sel_box = 0;
            st.drag = Drag::Band;
            st.band_x = wx(io.MousePos.x);
            st.band_y = wy(io.MousePos.y);
            st.band_base = st.sel;
        }
    }

    const bool  lmb_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const float mdx = io.MouseDelta.x / z, mdy = io.MouseDelta.y / z;
    const bool  moved_now = mdx != 0.0f || mdy != 0.0f;
    switch (st.drag) {
        case Drag::None: break;
        case Drag::Nodes:
            if (lmb_down) {
                if (moved_now) {
                    st.drag_moved = true;
                    for (int id : st.sel)
                        if (Node* n = g.find(id)) { n->x += mdx; n->y += mdy; }
                }
            } else {
                // A plain click on one node of a group picks just that node.
                if (!st.drag_moved && !st.press_additive && st.sel.size() > 1) st.select_only(st.press_node);
                st.drag = Drag::None;
                if (st.drag_moved) st.changed();
            }
            break;
        case Drag::MoveBox:
        case Drag::ResizeBox:
            if (lmb_down) {
                CommentBox* b = g.find_box(st.sel_box);
                if (b && moved_now) {
                    st.drag_moved = true;
                    if (st.drag == Drag::MoveBox) {
                        b->x += mdx;
                        b->y += mdy;
                        for (int id : st.carry)
                            if (Node* n = g.find(id)) { n->x += mdx; n->y += mdy; }
                    } else {
                        b->w = std::max(120.0f, b->w + mdx);
                        b->h = std::max(60.0f, b->h + mdy);
                    }
                }
            } else {
                st.drag = Drag::None;
                if (st.drag_moved) st.changed();
            }
            break;
        case Drag::Band:
            if (lmb_down) {
                const ImVec2 s0(sx(st.band_x), sy(st.band_y));
                const ImVec2 a(std::min(s0.x, io.MousePos.x), std::min(s0.y, io.MousePos.y));
                const ImVec2 b(std::max(s0.x, io.MousePos.x), std::max(s0.y, io.MousePos.y));
                if (b.x - a.x > 3.0f || b.y - a.y > 3.0f) {
                    dl->AddRectFilled(a, b, IM_COL32(255, 200, 60, 30));
                    dl->AddRect(a, b, IM_COL32(255, 200, 60, 200));
                    st.sel = st.band_base;
                    for (const NodeRect& r : rects)
                        if (r.a.x <= b.x && r.b.x >= a.x && r.a.y <= b.y && r.b.y >= a.y && !st.is_selected(r.id)) st.sel.push_back(r.id);
                    st.selected = st.sel.empty() ? 0 : st.sel.back();
                }
            } else {
                st.drag = Drag::None;
            }
            break;
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
                st.palette_x = wx(io.MousePos.x);
                st.palette_y = wy(io.MousePos.y);
                st.search[0] = 0;
                ImGui::OpenPopup("##palette");
            }
            st.link_from = PinRef{};
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        st.palette_x = wx(io.MousePos.x);
        st.palette_y = wy(io.MousePos.y);
        if (hover_node) {
            if (!st.is_selected(hover_node)) st.select_only(hover_node);
            ImGui::OpenPopup("##nodemenu");
        } else if (hover_box) {
            st.clear_selection();
            st.sel_box = hover_box;
            ImGui::OpenPopup("##boxmenu");
        } else {
            st.palette_for = PinRef{};
            st.search[0] = 0;
            ImGui::OpenPopup("##palette");
        }
    }

    // ---- keys --------------------------------------------------------------
    if (hovered && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) do_delete();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) do_duplicate();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) do_copy();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X)) { do_copy(); do_delete(); }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) do_paste(wx(io.MousePos.x), wy(io.MousePos.y));
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
            st.sel.clear();
            for (const Node& n : g.nodes) st.sel.push_back(n.id);
            st.selected = st.sel.empty() ? 0 : st.sel.back();
            st.sel_box = 0;
        }
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) add_comment(wx(io.MousePos.x), wy(io.MousePos.y));
        if (ImGui::IsKeyPressed(ImGuiKey_F9)) toggle_breakpoints();
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) frame(false);
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) frame(!st.sel.empty());
    }

    ImGui::SetWindowFontScale(1.0f);
    if (ImGui::BeginPopup("##nodemenu")) {
        const bool many = st.sel.size() > 1;
        if (ImGui::MenuItem(many ? "Delete nodes" : "Delete node", "Del")) do_delete();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) do_duplicate();
        if (ImGui::MenuItem("Copy", "Ctrl+C")) do_copy();
        if (ImGui::MenuItem("Cut", "Ctrl+X")) { do_copy(); do_delete(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Break all wires")) {
            g.links.erase(std::remove_if(g.links.begin(), g.links.end(), [&](const Link& l) { return st.is_selected(l.from) || st.is_selected(l.to); }),
                          g.links.end());
            st.changed();
        }
        if (ImGui::MenuItem("Comment box around", "C")) add_comment(st.palette_x, st.palette_y);
        ImGui::Separator();
        if (ImGui::MenuItem("Toggle breakpoint", "F9")) toggle_breakpoints();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##boxmenu")) {
        if (ImGui::MenuItem("Delete comment box", "Del")) do_delete();
        ImGui::TextDisabled("its text and colour are in the side panel");
        ImGui::EndPopup();
    }
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
        if (!filter_pin && !st.search[0]) {
            if (ImGui::Selectable("  Paste")) {
                do_paste(st.palette_x, st.palette_y);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Selectable("  Comment box")) {
                add_comment(st.palette_x, st.palette_y);
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
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
                st.select_only(new_id);
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

std::string new_logic_script_text(const std::string& name) {
    Script s;
    s.name = name;
    Graph& g = s.add_graph(GraphKind::Event, "EventGraph");
    g.add("event.begin_play", 60.0f, 60.0f);
    g.add("event.tick", 60.0f, 220.0f);
    return to_text(s);
}

void open_logic_script(const std::string& path, bool* open_flag) {
    open_doc(path);
    if (open_flag) *open_flag = true;
}

void set_logic_entity_namer(std::function<std::string(uint32_t)> name_of) { entity_namer() = std::move(name_of); }

void add_logic_script_breakpoint(const std::string& path, int graph, int node) {
    PanelState& st = open_doc(path);
    st.bps.insert({graph, node});
    st.push_breakpoints();
}

bool logic_scripts_unsaved() {
    for (const auto& s : docs().list)
        if (s->dirty) return true;
    return false;
}

void draw_logic_script_panel(bool* open) {
    Docs& d = docs();
    ImGui::SetNextWindowSize(ImVec2(1100.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Logic Script", open, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }

    // A script that stops at a breakpoint is brought up, at the node it stopped at.
    LogicDebugger*   dbg = logic_debugger();
    LogicDebugTarget stop_at;
    int              stop_graph = -1, stop_node = 0;
    const bool       stopped = dbg && dbg->paused(&stop_at, &stop_graph, &stop_node);
    static bool      s_shown_stop = false;
    if (stopped && !s_shown_stop && !stop_at.path.empty()) {
        PanelState& ps = open_doc(stop_at.path);
        if (stop_graph >= 0 && stop_graph < static_cast<int>(ps.script.graphs.size())) ps.graph = stop_graph;
        ps.select_only(stop_node);
        ps.want_center = stop_node;
        ps.debug_entity = stop_at.entity;
    }
    s_shown_stop = stopped;

    // One tab per open script.
    int close = -1;
    const int want = d.select;
    d.select = -1;
    if (ImGui::BeginTabBar("##lg_docs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int i = 0; i < static_cast<int>(d.list.size()); ++i) {
            PanelState&       s = *d.list[static_cast<size_t>(i)];
            bool              keep = true;
            ImGuiTabItemFlags f = s.dirty ? ImGuiTabItemFlags_UnsavedDocument : 0;
            if (want == i) f |= ImGuiTabItemFlags_SetSelected;
            const std::string label = s.tab_name() + "###lgdoc" + std::to_string(s.uid);
            if (ImGui::BeginTabItem(label.c_str(), &keep, f)) {
                d.active = i;
                ImGui::EndTabItem();
            }
            if (!s.path.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.path.c_str());
            if (!keep) close = i;
        }
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
            d.list.push_back(std::make_unique<PanelState>());
            d.select = static_cast<int>(d.list.size()) - 1;
        }
        ImGui::EndTabBar();
    }
    if (want >= 0 && want < static_cast<int>(d.list.size())) d.active = want;
    if (close >= 0) {
        if (d.list[static_cast<size_t>(close)]->dirty) {
            d.closing = close;
            ImGui::OpenPopup("Unsaved changes##lg");
        } else {
            close_doc(close);
        }
    }
    if (ImGui::BeginPopupModal("Unsaved changes##lg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        PanelState* c = d.closing >= 0 && d.closing < static_cast<int>(d.list.size()) ? d.list[static_cast<size_t>(d.closing)].get() : nullptr;
        if (!c) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("%s has changes that are not saved.", c->tab_name().c_str());
            if (ImGui::Button("Save")) {
                if (c->path.empty()) {
                    c->status = "give the script a file name, then save";
                    d.select = d.closing;
                } else if (c->save()) {
                    close_doc(d.closing);
                }
                d.closing = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard")) {
                close_doc(d.closing);
                d.closing = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                d.closing = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    d.active = std::clamp(d.active, 0, static_cast<int>(d.list.size()) - 1);
    PanelState& st = *d.list[static_cast<size_t>(d.active)];
    ImGui::PushID(st.uid);
    if (ImGui::Button("Open...")) {
        d.found = find_logic_scripts();
        d.open_buf[0] = 0;
        ImGui::OpenPopup("##lg_open");
    }
    if (ImGui::BeginPopup("##lg_open")) {
        ImGui::SetNextItemWidth(300.0f);
        const bool enter = ImGui::InputTextWithHint("##p", "a path, or pick one below", d.open_buf, sizeof d.open_buf,
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Open") || enter) && d.open_buf[0]) {
            open_doc(d.open_buf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        if (d.found.empty()) ImGui::TextDisabled("no .lgraph files under assets/ yet");
        for (const std::string& f : d.found)
            if (ImGui::Selectable(f.c_str())) open_doc(f);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputTextWithHint("##path", "assets/scripts/my_script.lgraph", st.path_buf, sizeof st.path_buf);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The file this script is saved to.");
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

    // ---- the running game ------------------------------------------------
    st.dbg = nullptr;
    st.stopped_here = false;
    std::vector<uint32_t> running;   // the objects that run this file right now
    if (dbg && !st.path.empty()) {
        const std::string& fk = st.file_key();
        for (const LogicDebugTarget& tg : dbg->targets())
            if (tg.path == fk) running.push_back(tg.entity);
        if (!running.empty()) {
            dbg->want_tracing();
            if (std::find(running.begin(), running.end(), st.debug_entity) == running.end()) st.debug_entity = running.front();
            st.dbg = dbg;
            if (stopped && stop_at.path == fk && stop_at.entity == st.debug_entity) {
                st.stopped_here = true;
                st.stopped_graph = stop_graph;
                st.stopped_node = stop_node;
            }
        }
    }
    if (st.dbg || stopped) {
        auto go = [&](LogicStep how) {
            dbg->resume(how);
            s_shown_stop = false;   // the next stop is brought up again, wherever it is
        };
        if (st.dbg) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Running on");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##dbgobj", entity_label(st.debug_entity).c_str())) {
                for (uint32_t e : running)
                    if (ImGui::Selectable(entity_label(e).c_str(), e == st.debug_entity)) st.debug_entity = e;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
        }
        if (stopped) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), st.stopped_here ? "Stopped at a breakpoint" : "Stopped in another script or object");
            ImGui::SameLine();
            const bool keys = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsAnyItemActive();
            if (ImGui::Button("Continue")) go(LogicStep::Continue);
            ImGui::SameLine();
            if (ImGui::Button("Step") || (keys && ImGui::IsKeyPressed(ImGuiKey_F10))) go(LogicStep::Over);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run this node and stop at the next one (F10)");
            ImGui::SameLine();
            if (ImGui::Button("Step Into") || (keys && !ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_F11))) go(LogicStep::Into);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Like Step, but goes into a function or an event this node calls (F11)");
            ImGui::SameLine();
            if (ImGui::Button("Step Out") || (keys && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_F11))) go(LogicStep::Out);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run to the end of this function or event (Shift+F11)");
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("F9 on a node: breakpoint.  Hover a pin: its value.");
        }
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
                st.select_only(m.node);
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
    ImGui::PopID();
    ImGui::End();
}

}  // namespace schizo::editor
