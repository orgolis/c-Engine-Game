#include "link.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace schizo::vs {

std::string parent_name(const std::string& name) { return "Parent::" + name; }

std::string event_key(const Registry& reg, const Node& node) {
    const NodeDef* d = reg.find(node.def);
    if (!d) return std::string();
    if (d->builtin == Builtin::CustomEvent) return node.name;
    if (d->builtin == Builtin::Event) return d->named ? d->id + ":" + node.name : d->id;
    return std::string();
}

namespace {

/// A literal's type, and the types inside it. Boxes are shared between copies
/// of a value, so one that has a box is copied before it is touched: the
/// script this one was copied from must not change.
void retype(Value& v, const std::function<void(Type&)>& fn) {
    if (v.type.array || v.type.kind == Kind::Struct) v = clone(v);
    fn(v.type);
    if (v.type.array) {
        for (Value& item : v.items()) retype(item, fn);
    } else if (v.type.kind == Kind::Struct) {
        for (Value& f : v.fields()) retype(f, fn);
    }
}

void retype(Param& p, const std::function<void(Type&)>& fn) {
    fn(p.type);
    retype(p.def, fn);
}

void retype(Variable& v, const std::function<void(Type&)>& fn) {
    fn(v.type);
    retype(v.def, fn);
}

bool fail(std::string* error, std::string text) {
    if (error) *error = std::move(text);
    return false;
}

/// Rename a function everywhere a script calls it.
void rename_function(Script& s, const std::string& from, const std::string& to) {
    for (Graph& g : s.graphs) {
        if (g.kind == GraphKind::Function && g.name == from) g.name = to;
        for (Node& n : g.nodes)
            if (n.def == "fn.call" && n.name == from) n.name = to;
    }
}

/// Rename a custom event and the nodes that call it.
void rename_custom_event(Script& s, const std::string& from, const std::string& to) {
    for (Graph& g : s.graphs)
        for (Node& n : g.nodes)
            if ((n.def == "event.custom" || n.def == "event.call") && n.name == from) n.name = to;
}

bool has_function(const Script& s, const std::string& name) { return s.find_function(name) != nullptr; }

bool has_custom_event(const Script& s, const std::string& name) {
    for (const Graph& g : s.graphs)
        for (const Node& n : g.nodes)
            if (n.def == "event.custom" && n.name == name) return true;
    return false;
}

/// Free the name `name` in `s` by pushing whatever holds it one "Parent::"
/// further down (a grandparent's version, when the parent already replaced it).
void push_down_function(Script& s, const std::string& name, bool with_callers = false) {
    if (!has_function(s, name)) return;
    push_down_function(s, parent_name(name), true);
    if (with_callers) {
        // "Parent::x" is only ever called on purpose: those calls follow it down.
        rename_function(s, name, parent_name(name));
    } else {
        // The name the child took over. The parent's own calls to it keep the
        // name, and so reach the child's: that is what replacing it means.
        for (Graph& g : s.graphs)
            if (g.kind == GraphKind::Function && g.name == name) g.name = parent_name(name);
    }
}
void push_down_event(Script& s, const std::string& name) {
    if (!has_custom_event(s, name)) return;
    push_down_event(s, parent_name(name));
    rename_custom_event(s, name, parent_name(name));
}

/// Renumber the types of a script that is about to be added after `ne` enums
/// and `ns` structs.
void shift_types(Script& s, size_t ne, size_t ns) {
    if (ne == 0 && ns == 0) return;
    for_each_type(s, [&](Type& t) {
        if (t.sub == 0) return;
        if (t.kind == Kind::Enum) t.sub = static_cast<uint16_t>(t.sub + ne);
        else if (t.kind == Kind::Struct) t.sub = static_cast<uint16_t>(t.sub + ns);
    });
}

void merge_parent(const Registry& reg, Linked& out, Linked parent, const std::string& file) {
    Script& me = out.script;
    Script& ps = parent.script;
    shift_types(ps, me.enums.size(), me.structs.size());
    me.enums.insert(me.enums.end(), ps.enums.begin(), ps.enums.end());
    me.structs.insert(me.structs.end(), ps.structs.begin(), ps.structs.end());

    // Variables: the parent's first, in its order; one this script declares
    // again takes the parent's place (its own type and default).
    std::vector<Variable> vars;
    for (const Variable& pv : ps.variables) {
        const Variable* own = me.find_variable(pv.name);
        vars.push_back(own ? *own : pv);
    }
    for (const Variable& v : me.variables)
        if (!ps.find_variable(v.name)) vars.push_back(v);
    me.variables = std::move(vars);

    for (const Dispatcher& d : ps.dispatchers)
        if (!me.find_dispatcher(d.name)) me.dispatchers.push_back(d);

    // What this script defines itself: those names are its own.
    std::vector<std::string> own_functions, own_events;
    for (const Graph& g : me.graphs) {
        if (g.kind == GraphKind::Function) own_functions.push_back(g.name);
        for (const Node& n : g.nodes) {
            const std::string k = event_key(reg, n);
            if (!k.empty()) own_events.push_back(k);
        }
    }
    for (const std::string& f : own_functions) push_down_function(ps, f);
    for (const std::string& key : own_events) {
        // A custom event of that name moves down as it is. A host event of
        // that name becomes a custom event, so that only a call runs it; its
        // pins keep their names, so its wires stay.
        push_down_event(ps, parent_name(key));
        for (Graph& g : ps.graphs) {
            for (Node& n : g.nodes) {
                if (event_key(reg, n) != key) continue;
                const NodeDef* d = reg.find(n.def);
                if (d && d->builtin == Builtin::Event) {
                    Shape sh;
                    std::vector<Param> params;
                    if (shape_of(reg, ps, g, n, sh))
                        for (const Pin& p : sh.pins)
                            if (!p.input && !p.type.is_exec()) params.push_back(Param{p.name, p.type, Value::default_of(p.type)});
                    n.def = "event.custom";
                    n.params = std::move(params);
                }
                n.name = parent_name(key);
            }
            // (A node of the parent that calls this event by name is left
            // alone: it now reaches this script's, which is what replacing
            // an event means.)
        }
    }

    for (size_t i = 0; i < ps.graphs.size(); ++i) {
        Graph g = ps.graphs[i];
        if (g.kind == GraphKind::Event) {
            // Event graphs are told apart only for the reader.
            const std::string base = g.name;
            int               n = 0;
            while (me.find_graph(g.name)) g.name = base + " (parent" + (n++ ? " " + std::to_string(n) : std::string()) + ")";
        }
        me.graphs.push_back(std::move(g));
        GraphOrigin o = i < parent.origins.size() ? parent.origins[i] : GraphOrigin{};
        if (o.file.empty()) {
            o.file = file;
            o.graph = static_cast<int>(i);
        }
        out.origins.push_back(std::move(o));
    }
    out.files.push_back(file);
    out.files.insert(out.files.end(), parent.files.begin(), parent.files.end());
}

void merge_library(Linked& out, Linked lib, const std::string& file) {
    Script& me = out.script;
    Script& ls = lib.script;
    shift_types(ls, me.enums.size(), me.structs.size());
    me.enums.insert(me.enums.end(), ls.enums.begin(), ls.enums.end());
    me.structs.insert(me.structs.end(), ls.structs.begin(), ls.structs.end());
    for (size_t i = 0; i < ls.graphs.size(); ++i) {
        const Graph& g = ls.graphs[i];
        if (g.kind != GraphKind::Function || has_function(me, g.name)) continue;   // the script's own function of that name wins
        me.graphs.push_back(g);
        GraphOrigin o = i < lib.origins.size() ? lib.origins[i] : GraphOrigin{};
        if (o.file.empty()) {
            o.file = file;
            o.graph = static_cast<int>(i);
        }
        out.origins.push_back(std::move(o));
    }
    out.files.push_back(file);
    out.files.insert(out.files.end(), lib.files.begin(), lib.files.end());
}

bool link_one(const Registry& reg, const Script& root, const ScriptLoader& load, Linked& out, std::vector<std::string>& chain,
              std::string* error) {
    out = Linked{};
    out.script = root;
    for (size_t i = 0; i < root.graphs.size(); ++i) out.origins.push_back(GraphOrigin{std::string(), static_cast<int>(i)});

    auto other = [&](const std::string& path, const char* what, Linked& into) {
        if (std::find(chain.begin(), chain.end(), path) != chain.end())
            return fail(error, std::string("the scripts name each other in a circle, through '") + path + "'");
        Script s;
        if (!load || !load(path, s)) return fail(error, std::string("cannot read the ") + what + " '" + path + "'");
        chain.push_back(path);
        const bool ok = link_one(reg, s, load, into, chain, error);
        chain.pop_back();
        return ok;
    };
    if (!root.parent.empty()) {
        Linked p;
        if (!other(root.parent, "parent script", p)) return false;
        merge_parent(reg, out, std::move(p), root.parent);
    }
    for (const std::string& lib : root.imports) {
        if (lib.empty()) continue;
        Linked l;
        if (!other(lib, "library", l)) return false;
        merge_library(out, std::move(l), lib);
    }
    return true;
}

}  // namespace

void for_each_type(Script& s, const std::function<void(Type&)>& fn) {
    for (StructDef& sd : s.structs)
        for (Param& p : sd.fields) retype(p, fn);
    for (Variable& v : s.variables) retype(v, fn);
    for (Dispatcher& d : s.dispatchers)
        for (Param& p : d.params) retype(p, fn);
    for (Graph& g : s.graphs) {
        for (Param& p : g.inputs) retype(p, fn);
        for (Param& p : g.outputs) retype(p, fn);
        for (Variable& v : g.locals) retype(v, fn);
        for (Node& n : g.nodes) {
            fn(n.targ);
            for (Param& p : n.params) retype(p, fn);
            for (Param& p : n.results) retype(p, fn);
            for (auto& d : n.defaults) retype(d.second, fn);
        }
    }
}

bool link(const Registry& reg, const Script& root, const ScriptLoader& load, Linked& out, std::string* error) {
    std::vector<std::string> chain;
    return link_one(reg, root, load, out, chain, error);
}

}  // namespace schizo::vs
