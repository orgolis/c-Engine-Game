#include "vscript/registry.h"

#include <algorithm>

namespace schizo::vs {

NodeDef& Registry::add(NodeDef def) {
    auto it = by_id_.find(def.id);
    if (it != by_id_.end()) {
        *defs_[it->second] = std::move(def);
        return *defs_[it->second];
    }
    by_id_.emplace(def.id, defs_.size());
    defs_.push_back(std::make_unique<NodeDef>(std::move(def)));
    return *defs_.back();
}

const NodeDef* Registry::find(std::string_view id) const {
    const auto it = by_id_.find(std::string(id));
    return it == by_id_.end() ? nullptr : defs_[it->second].get();
}

std::string Registry::default_name(size_t i, bool input, size_t count) {
    if (!input) return count == 1 ? std::string("Result") : "Result " + std::to_string(i);
    // A, B, C ... the names a maths node would have.
    return std::string(1, static_cast<char>('A' + (i % 26)));
}

namespace {

Pin make_pin(std::string name, Type type, bool input, Value def = Value()) {
    Pin p;
    p.name = std::move(name);
    p.type = type;
    p.input = input;
    if (input && !type.is_exec()) p.def = def.type == type ? std::move(def) : Value::default_of(type);
    return p;
}

void add_params(Shape& s, const std::vector<Param>& params, bool input) {
    for (const Param& p : params) s.pins.push_back(make_pin(p.name, p.type, input, p.def));
}

/// The Custom Event node called `name`, in any event graph of the script.
const Node* find_custom_event(const Registry& reg, const Script& script, const std::string& name) {
    for (const Graph& g : script.graphs) {
        if (g.kind != GraphKind::Event) continue;
        for (const Node& n : g.nodes) {
            if (n.name != name) continue;
            const NodeDef* d = reg.find(n.def);
            if (d && d->builtin == Builtin::CustomEvent) return &n;
        }
    }
    return nullptr;
}

bool fail(std::string* error, std::string text) {
    if (error) *error = std::move(text);
    return false;
}

}  // namespace

bool shape_of(const Registry& reg, const Script& script, const Graph& graph, const Node& node, Shape& out,
              std::string* error) {
    out = Shape{};
    const NodeDef* def = reg.find(node.def);
    if (!def) return fail(error, "unknown node kind '" + node.def + "'");
    out.def = def;
    out.pure = def->pure;

    // The definition's own pins first, inputs and outputs in declared order.
    for (const PinDef& pd : def->pins) {
        Pin p = make_pin(pd.name, pd.type, pd.input, pd.def);
        p.wildcard = pd.type.is_any();
        out.pins.push_back(std::move(p));
    }

    const int extra2 = std::max(2, node.extra);
    switch (def->builtin) {
        case Builtin::Sequence:
            for (int i = 0; i < extra2; ++i) out.pins.push_back(make_pin("Then " + std::to_string(i), kExec, false));
            break;
        case Builtin::MultiGate:
            for (int i = 0; i < extra2; ++i) out.pins.push_back(make_pin("Out " + std::to_string(i), kExec, false));
            break;
        case Builtin::SwitchInt:
        case Builtin::SwitchString:
            for (const std::string& c : node.cases) out.pins.push_back(make_pin(c, kExec, false));
            out.pins.push_back(make_pin("Default", kExec, false));
            break;
        case Builtin::Select: {
            for (int i = 0; i < extra2; ++i) {
                Pin p = make_pin("Option " + std::to_string(i), kAny, true);
                p.wildcard = true;
                out.pins.push_back(std::move(p));
            }
            Pin r = make_pin("Result", kAny, false);
            r.wildcard = true;
            out.pins.push_back(std::move(r));
            break;
        }
        case Builtin::MakeArray: {
            const int n = std::max(1, node.extra);
            for (int i = 0; i < n; ++i) {
                Pin p = make_pin(std::to_string(i), kAny, true);
                p.wildcard = true;
                out.pins.push_back(std::move(p));
            }
            Pin r = make_pin("Array", Type(Kind::Any, true), false);
            r.wildcard = true;
            out.pins.push_back(std::move(r));
            break;
        }
        case Builtin::VarGet:
        case Builtin::VarSet: {
            const Variable* v = script.find_variable(node.name);
            if (!v) return fail(error, "no variable named '" + node.name + "'");
            if (def->builtin == Builtin::VarSet) {
                // In / Then are in the definition; the value pins go around them.
                out.pins.insert(out.pins.begin() + 1, make_pin("Value", v->type, true, v->def));
            }
            out.pins.push_back(make_pin("Value", v->type, false));
            break;
        }
        case Builtin::LocalGet:
        case Builtin::LocalSet: {
            const Variable* v = graph.find_local(node.name);
            if (!v) return fail(error, "no local variable named '" + node.name + "' in this function");
            if (def->builtin == Builtin::LocalSet)
                out.pins.insert(out.pins.begin() + 1, make_pin("Value", v->type, true, v->def));
            out.pins.push_back(make_pin("Value", v->type, false));
            break;
        }
        case Builtin::FnEntry:
            if (graph.kind != GraphKind::Function) return fail(error, "a function entry outside a function");
            if (!graph.pure) out.pins.push_back(make_pin("Then", kExec, false));
            add_params(out, graph.inputs, false);
            break;
        case Builtin::FnReturn:
            if (graph.kind != GraphKind::Function) return fail(error, "a return node outside a function");
            if (!graph.pure) out.pins.push_back(make_pin("In", kExec, true));
            add_params(out, graph.outputs, true);
            break;
        case Builtin::FnCall: {
            const Graph* f = script.find_function(node.name);
            if (!f) return fail(error, "no function named '" + node.name + "'");
            out.pure = f->pure;
            if (!f->pure) out.pins.push_back(make_pin("In", kExec, true));
            add_params(out, f->inputs, true);
            if (!f->pure) out.pins.push_back(make_pin("Then", kExec, false));
            add_params(out, f->outputs, false);
            break;
        }
        case Builtin::CustomEvent:
            add_params(out, node.params, false);
            break;
        case Builtin::Reroute:
            // On a flow wire it is a flow node that passes control along.
            if (node.targ.is_exec()) out.pure = false;
            break;
        case Builtin::SendEvent: {
            // In, Target are in the definition; the sender's own parameter
            // list follows Target, Then stays last.
            std::vector<Pin> params;
            for (const Param& p : node.params) params.push_back(make_pin(p.name, p.type, true, p.def));
            out.pins.insert(out.pins.begin() + 2, params.begin(), params.end());
            break;
        }
        case Builtin::SwitchEnum: {
            // One flow output per choice of the enum the node was given.
            if (const EnumDef* e = script.enum_of(node.targ))
                for (const std::string& en : e->entries) out.pins.push_back(make_pin(en, kExec, false));
            break;
        }
        case Builtin::StructMake:
        case Builtin::StructBreak:
        case Builtin::StructSet: {
            const StructDef* sd = script.struct_of(node.targ);
            if (!sd) return fail(error, "pick the struct this node works on");
            if (def->builtin == Builtin::StructMake) {
                add_params(out, sd->fields, true);
                out.pins.push_back(make_pin("Struct", node.targ, false));
            } else if (def->builtin == Builtin::StructBreak) {
                out.pins.push_back(make_pin("Struct", node.targ, true));
                add_params(out, sd->fields, false);
            } else {
                // In, Struct, the one member being set, Then, the struct again.
                out.pins.insert(out.pins.begin() + 1, make_pin("Struct", node.targ, true));
                const Param* member = nullptr;
                for (const Param& p : sd->fields)
                    if (p.name == node.name) member = &p;
                if (!member) return fail(error, node.name.empty() ? "pick the member to set" : "the struct has no member '" + node.name + "'");
                out.pins.insert(out.pins.begin() + 2, make_pin(member->name, member->type, true, member->def));
                out.pins.push_back(make_pin("Struct", node.targ, false));
            }
            break;
        }
        case Builtin::DispatchCall: {
            const Dispatcher* d = script.find_dispatcher(node.name);
            if (!d) return fail(error, "no dispatcher named '" + node.name + "'");
            std::vector<Pin> params;
            for (const Param& p : d->params) params.push_back(make_pin(p.name, p.type, true, p.def));
            out.pins.insert(out.pins.begin() + 1, params.begin(), params.end());
            break;
        }
        case Builtin::RemoteCall: {
            // In, Target; then what the caller hands over; Then; then what comes back.
            std::vector<Pin> params;
            for (const Param& p : node.params) params.push_back(make_pin(p.name, p.type, true, p.def));
            out.pins.insert(out.pins.begin() + 2, params.begin(), params.end());
            add_params(out, node.results, false);
            break;
        }
        case Builtin::CallEvent: {
            const Node* ev = find_custom_event(reg, script, node.name);
            if (!ev) return fail(error, "no custom event named '" + node.name + "'");
            // In is in the definition; parameters follow it, Then stays last.
            std::vector<Pin> params;
            for (const Param& p : ev->params) params.push_back(make_pin(p.name, p.type, true, p.def));
            out.pins.insert(out.pins.begin() + 1, params.begin(), params.end());
            break;
        }
        default: break;
    }

    // Wildcards take the node's resolved type; instance literals override defaults.
    for (Pin& p : out.pins) {
        if (p.wildcard && !node.targ.is_none()) {
            p.type = p.type.array ? node.targ.element().as_array() : node.targ;
            if (p.input) p.def = Value::default_of(p.type);
        }
        if (p.input && !p.type.is_exec()) {
            if (const Value* lit = node.find_default(p.name)) {
                if (lit->type == p.type) p.def = *lit;
                else if (!p.type.is_any()) convert(*lit, p.type, p.def);
                else p.def = *lit;
            }
        }
    }
    return true;
}

}  // namespace schizo::vs
