// ============================================================================
// vscript/compiler.cpp -- Script -> Program.
// ============================================================================
//
// How a graph becomes code.
//
// EXEC FLOW. Every (node, exec input pin) is a block with a label. A block
// evaluates the node's data inputs, does the node's work and jumps to the
// block wired to its exec output, or emits OP_END when nothing is wired.
// Blocks are emitted from a worklist starting at the event nodes (or the
// function entry), so only reachable nodes produce code.
//
// DATA. An input wired to a node WITH exec pins reads the register that node
// wrote when it ran. An input wired to a PURE node makes the compiler emit the
// pure node's code right there, before the consumer. A pure node is therefore
// evaluated once per consumer that uses it, every time that consumer runs --
// the rule Blueprint authors already know. An unwired input is a constant.
//
// REGISTERS. "Persistent" registers hold what must survive from one block to
// another: outputs of nodes with exec pins, loop counters, event parameters,
// locals. "Temporary" registers are scratch for one block and are reused by
// the next. Nothing may read a temporary after control has left the block and
// come back (a loop body returning to its header), so the loop code below
// reloads what it needs after each continuation label.

#include "vscript/program.h"

#include <algorithm>
#include <cstdint>
#include <charconv>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace schizo::vs {

namespace {

constexpr int kTempBase = 1 << 24;   // register numbers at or above this are temporaries until finalize()

enum : uint8_t { oN, oR, oK, oL, oI, oV, oS, oT, oF, oE, oX };
struct OpInfo {
    const char* name;
    uint8_t     a, b, c;
};
// One row per Op, in enum order.
const OpInfo kOps[OP_COUNT_] = {
    {"NOP", oN, oN, oN},     {"LOADK", oR, oK, oN},   {"MOVE", oR, oR, oN},    {"COPY", oR, oR, oN},
    {"GETV", oR, oV, oN},
    {"SETV", oV, oR, oN},    {"SELF", oR, oN, oN},    {"CONV", oR, oR, oT},    {"JMP", oL, oN, oN},
    {"JMPT", oR, oL, oN},    {"JMPF", oR, oL, oN},    {"PUSHC", oL, oN, oN},   {"END", oN, oN, oN},
    {"CALLN", oX, oR, oR},   {"CALLF", oF, oR, oR},   {"CALLE", oE, oR, oN},   {"RET", oN, oN, oN},
    {"STSET", oS, oI, oN},   {"STLOAD", oR, oS, oN},  {"STSTORE", oS, oR, oN}, {"JSTEQ", oS, oI, oL},
    {"LATENT", oR, oL, oS},  {"ALEN", oR, oR, oN},    {"AGET", oR, oR, oR},    {"MKARR", oR, oR, oT},
    {"SELECT", oR, oR, oR},  {"EQ", oR, oR, oR},      {"NE", oR, oR, oR},
    {"ADDI", oR, oR, oR},    {"SUBI", oR, oR, oR},    {"MULI", oR, oR, oR},    {"DIVI", oR, oR, oR},
    {"MODI", oR, oR, oR},    {"NEGI", oR, oR, oN},
    {"LTI", oR, oR, oR},     {"LEI", oR, oR, oR},     {"GTI", oR, oR, oR},     {"GEI", oR, oR, oR},
    {"EQI", oR, oR, oR},     {"NEI", oR, oR, oR},
    {"ADDF", oR, oR, oR},    {"SUBF", oR, oR, oR},    {"MULF", oR, oR, oR},    {"DIVF", oR, oR, oR},
    {"NEGF", oR, oR, oN},
    {"LTF", oR, oR, oR},     {"LEF", oR, oR, oR},     {"GTF", oR, oR, oR},     {"GEF", oR, oR, oR},
    {"EQF", oR, oR, oR},     {"NEF", oR, oR, oR},
    {"NOT", oR, oR, oN},     {"AND", oR, oR, oR},     {"OR", oR, oR, oR},      {"XOR", oR, oR, oR},
};

struct Endpoint {
    int node = 0;
    int pin = -1;   // index into the node's Shape::pins
};

uint64_t pin_key(int node, int pin) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(node)) << 32) | static_cast<uint32_t>(pin);
}

/// One graph, analysed: shapes with wildcards resolved, and its wires indexed.
struct GraphInfo {
    const Graph* g = nullptr;
    int          index = -1;
    std::unordered_map<int, const Node*>    nodes;
    std::unordered_map<int, Shape>          shapes;
    std::unordered_map<uint64_t, Endpoint>  data_in;    // (node, input pin)  -> the output feeding it
    std::unordered_map<uint64_t, Endpoint>  exec_out;   // (node, output pin) -> the exec input it leads to

    const Shape* shape(int node) const {
        const auto it = shapes.find(node);
        return it == shapes.end() ? nullptr : &it->second;
    }
};

class Compiler {
public:
    Compiler(const Registry& reg, const Script& script, Program& prog, std::vector<CompileMessage>& messages)
        : reg_(reg), script_(script), prog_(prog), messages_(messages) {}

    bool run();

private:
    // ---- messages ------------------------------------------------------
    void error(int graph, int node, std::string text) {
        messages_.push_back(CompileMessage{true, graph, node, std::move(text)});
        failed_ = true;
    }
    void warn(int graph, int node, std::string text) {
        messages_.push_back(CompileMessage{false, graph, node, std::move(text)});
    }

    // ---- analysis ------------------------------------------------------
    void analyse(GraphInfo& gi);

    // ---- tables --------------------------------------------------------
    int const_index(const Value& v) {
        for (size_t i = 0; i < prog_.consts.size(); ++i)
            if (prog_.consts[i].type == v.type && equals(prog_.consts[i], v)) return static_cast<int>(i);
        prog_.consts.push_back(v);
        return static_cast<int>(prog_.consts.size() - 1);
    }
    int type_index(Type t) {
        for (size_t i = 0; i < prog_.types.size(); ++i)
            if (prog_.types[i] == t) return static_cast<int>(i);
        prog_.types.push_back(t);
        return static_cast<int>(prog_.types.size() - 1);
    }
    int native_index(const NodeDef* d) {
        for (size_t i = 0; i < prog_.natives.size(); ++i)
            if (prog_.natives[i] == d) return static_cast<int>(i);
        prog_.natives.push_back(d);
        return static_cast<int>(prog_.natives.size() - 1);
    }

    // ---- one compilation unit (the event unit, or one function) ---------
    struct BlockKey {
        int graph, node, pin;
    };
    struct Unit {
        FnInfo             info;
        std::vector<Instr> code;
        std::vector<Where> where;
        std::vector<int>   label_pc;
        std::vector<Type>  persistent;        // type of each persistent register
        std::vector<Value> persistent_init;   // and its initial value
        int                temp_top = 0;      // temporaries in use by the current block
        int                temp_max = 0;
        bool               is_function = false;
        std::deque<BlockKey> pending;
        std::unordered_map<std::string, int> blocks;                   // "graph:node:pin" -> label
        std::unordered_map<std::string, int> out_regs;                 // "graph:node"     -> first output register
        std::unordered_map<std::string, int> local_regs;               // local variable name -> register
        std::unordered_map<std::string, int> pure_memo;                // "graph:node" -> first output register (per block)
        std::unordered_set<std::string>      pure_active;              // pure nodes being evaluated (cycle detection)
        Where cur;
    };

    static std::string nkey(int graph, int node) { return std::to_string(graph) + ":" + std::to_string(node); }

    void begin_unit(Unit& u) { unit_ = &u; }
    int  new_persistent(Type t, const Value* init = nullptr) {
        Unit& u = *unit_;
        u.persistent.push_back(t);
        u.persistent_init.push_back(init && init->type == t ? *init : Value::default_of(t));
        return static_cast<int>(u.persistent.size() - 1);
    }
    int new_persistent_block(const std::vector<Type>& types) {
        const int base = static_cast<int>(unit_->persistent.size());
        for (Type t : types) new_persistent(t);
        return base;
    }
    int new_temps(int n) {
        Unit& u = *unit_;
        const int base = u.temp_top;
        u.temp_top += n;
        u.temp_max = std::max(u.temp_max, u.temp_top);
        return kTempBase + base;
    }
    int  new_temp() { return new_temps(1); }
    int  new_label() {
        unit_->label_pc.push_back(-1);
        return static_cast<int>(unit_->label_pc.size() - 1);
    }
    void place(int label) { unit_->label_pc[label] = static_cast<int>(unit_->code.size()); }
    void emit(Op op, int a = 0, int b = 0, int c = 0, int d = 0) {
        Instr i;
        i.op = op;
        i.a = a;
        i.b = b;
        i.c = c;
        i.d = static_cast<uint16_t>(d);
        unit_->code.push_back(i);
        unit_->where.push_back(unit_->cur);
    }
    void start_block_scope() {
        unit_->temp_top = 0;
        unit_->pure_memo.clear();
        unit_->pure_active.clear();
    }

    int  block_label(const GraphInfo& gi, int node, int pin);
    void go(const GraphInfo& gi, int node, const std::string& out_pin);
    void go_pin(const GraphInfo& gi, int node, int out_pin);
    int  output_block(const GraphInfo& gi, int node);
    int  data_output_slot(const Shape& s, int pin) const;
    int  input_reg(const GraphInfo& gi, int node, int pin, int into = -1);
    int  input_named(const GraphInfo& gi, int node, const char* name, int into = -1);
    int  eval_pure(const GraphInfo& gi, int node);
    void emit_block(const GraphInfo& gi, int node, int pin);
    void emit_native(const GraphInfo& gi, const Node& n, const Shape& s, int dst_base, bool is_pure);
    void drain(Unit& u);
    void finalize(Unit& u);
    void compile_event_unit();
    void compile_function(int graph_index, int fn_index);
    int  load_const(const Value& v) {
        const int r = new_temp();
        emit(OP_LOADK, r, const_index(v));
        return r;
    }
    const GraphInfo& info(int graph) const { return infos_[static_cast<size_t>(graph)]; }
    int pin_index(const GraphInfo& gi, int node, const char* name, bool input) {
        const Shape* s = gi.shape(node);
        const int    p = s ? s->find(name, input) : -1;
        if (p < 0) error(gi.index, node, std::string("internal: node has no pin '") + name + "'");
        return p;
    }

    const Registry&              reg_;
    const Script&                script_;
    Program&                     prog_;
    std::vector<CompileMessage>& messages_;
    bool                         failed_ = false;
    std::vector<GraphInfo>       infos_;
    Unit*                        unit_ = nullptr;
    std::unordered_map<std::string, int> fn_index_;      // function name -> Program::fns index
    std::unordered_map<std::string, int> event_index_;   // event name    -> Program::events index
    std::unordered_map<std::string, int> event_of_node_; // "graph:node"  -> Program::events index
};

// ---------------------------------------------------------------------------
// Analysis: shapes, wildcard types, wires.
// ---------------------------------------------------------------------------
void Compiler::analyse(GraphInfo& gi) {
    const Graph& g = *gi.g;
    std::unordered_map<int, Type> targs;
    for (const Node& n : g.nodes) {
        if (gi.nodes.count(n.id)) {
            error(gi.index, n.id, "two nodes share the id " + std::to_string(n.id));
            continue;
        }
        gi.nodes[n.id] = &n;
        targs[n.id] = n.targ;
    }

    auto build_shape = [&](const Node& n, bool report) -> bool {
        Node copy = n;
        copy.targ = targs[n.id];
        Shape       s;
        std::string why;
        if (!shape_of(reg_, script_, g, copy, s, &why)) {
            if (report) error(gi.index, n.id, why);
            gi.shapes.erase(n.id);
            return false;
        }
        gi.shapes[n.id] = std::move(s);
        return true;
    };
    for (const Node& n : g.nodes)
        if (gi.nodes[n.id] == &n) build_shape(n, true);

    // Wildcards: a node whose type is open takes it from what it is wired to.
    // Repeat until nothing changes, because one resolution can enable the next.
    for (size_t round = 0; round <= g.nodes.size(); ++round) {
        bool changed = false;
        for (const Link& l : g.links) {
            const Shape* fs = gi.shape(l.from);
            const Shape* ts = gi.shape(l.to);
            if (!fs || !ts) continue;
            const int fp = fs->find(l.from_pin, false);
            const int tp = ts->find(l.to_pin, true);
            if (fp < 0 || tp < 0) continue;
            const Pin& a = fs->pins[static_cast<size_t>(fp)];
            const Pin& b = ts->pins[static_cast<size_t>(tp)];
            if (a.type.is_exec() || b.type.is_exec()) continue;
            const bool a_open = a.type.is_any(), b_open = b.type.is_any();
            if (a_open == b_open) continue;
            const Pin& open = a_open ? a : b;
            const Pin& known = a_open ? b : a;
            // An array wildcard pin wired to T[] makes the node's type T.
            Type t = known.type;
            if (open.type.array) {
                if (!t.array) continue;   // reported as a type mismatch below
                t = t.element();
            }
            const int id = a_open ? l.from : l.to;
            targs[id] = t;
            build_shape(*gi.nodes[id], false);
            changed = true;
        }
        if (!changed) break;
    }
    // Still open: fall back to the definition's default, if it has one.
    for (const Node& n : g.nodes) {
        const Shape* s = gi.shape(n.id);
        if (!s || !targs[n.id].is_none()) continue;
        bool open = false;
        for (const Pin& p : s->pins)
            if (p.type.is_any()) open = true;
        if (open && !s->def->default_any.is_none()) {
            targs[n.id] = s->def->default_any;
            build_shape(n, false);
        }
    }

    // Wires.
    for (const Link& l : g.links) {
        const Shape* fs = gi.shape(l.from);
        const Shape* ts = gi.shape(l.to);
        if (!gi.nodes.count(l.from) || !gi.nodes.count(l.to)) {
            error(gi.index, 0, "a wire is attached to a node that does not exist");
            continue;
        }
        if (!fs || !ts) continue;   // the node itself already has an error
        const int fp = fs->find(l.from_pin, false);
        const int tp = ts->find(l.to_pin, true);
        if (fp < 0) {
            error(gi.index, l.from, "no output pin named '" + l.from_pin + "'");
            continue;
        }
        if (tp < 0) {
            error(gi.index, l.to, "no input pin named '" + l.to_pin + "'");
            continue;
        }
        const Pin& a = fs->pins[static_cast<size_t>(fp)];
        const Pin& b = ts->pins[static_cast<size_t>(tp)];
        if (a.type.is_exec() != b.type.is_exec()) {
            error(gi.index, l.to, "a wire joins a flow pin and a data pin ('" + l.from_pin + "' to '" + l.to_pin + "')");
            continue;
        }
        if (a.type.is_exec()) {
            const uint64_t k = pin_key(l.from, fp);
            if (gi.exec_out.count(k)) {
                error(gi.index, l.from, "the flow output '" + l.from_pin + "' has two wires; use a Sequence node");
                continue;
            }
            gi.exec_out[k] = Endpoint{l.to, tp};
        } else {
            const uint64_t k = pin_key(l.to, tp);
            if (gi.data_in.count(k)) {
                error(gi.index, l.to, "the input '" + l.to_pin + "' has two wires");
                continue;
            }
            if (a.type.is_any() || b.type.is_any()) {
                error(gi.index, a.type.is_any() ? l.from : l.to,
                      "this node's type could not be worked out; wire it to something with a type");
                continue;
            }
            bool lossy = false;
            if (!can_convert(a.type, b.type, &lossy)) {
                error(gi.index, l.to, "cannot connect " + type_name(a.type) + " to '" + l.to_pin + "' (" +
                                          type_name(b.type) + ")");
                continue;
            }
            if (lossy)
                warn(gi.index, l.to, "'" + l.to_pin + "' receives a " + type_name(a.type) +
                                         " and truncates it to " + type_name(b.type));
            gi.data_in[k] = Endpoint{l.from, fp};
        }
    }
}

// ---------------------------------------------------------------------------
// Code generation helpers
// ---------------------------------------------------------------------------
int Compiler::block_label(const GraphInfo& gi, int node, int pin) {
    Unit&             u = *unit_;
    const std::string k = nkey(gi.index, node) + ":" + std::to_string(pin);
    const auto        it = u.blocks.find(k);
    if (it != u.blocks.end()) return it->second;
    const int label = new_label();
    u.blocks.emplace(k, label);
    u.pending.push_back(BlockKey{gi.index, node, pin});
    return label;
}

void Compiler::go_pin(const GraphInfo& gi, int node, int out_pin) {
    const auto it = out_pin < 0 ? gi.exec_out.end() : gi.exec_out.find(pin_key(node, out_pin));
    if (it == gi.exec_out.end()) {
        emit(OP_END);
        return;
    }
    emit(OP_JMP, block_label(gi, it->second.node, it->second.pin));
}

void Compiler::go(const GraphInfo& gi, int node, const std::string& out_pin) {
    const Shape* s = gi.shape(node);
    go_pin(gi, node, s ? s->find(out_pin, false) : -1);
}

/// Index of a data output among the node's data outputs (its register offset).
int Compiler::data_output_slot(const Shape& s, int pin) const {
    int slot = 0;
    for (int i = 0; i < pin; ++i)
        if (!s.pins[static_cast<size_t>(i)].input && !s.pins[static_cast<size_t>(i)].type.is_exec()) ++slot;
    return slot;
}

/// First persistent register of a node's data outputs, allocated on first use.
int Compiler::output_block(const GraphInfo& gi, int node) {
    Unit&             u = *unit_;
    const std::string k = nkey(gi.index, node);
    const auto        it = u.out_regs.find(k);
    if (it != u.out_regs.end()) return it->second;
    const Shape*      s = gi.shape(node);
    std::vector<Type> types;
    if (s)
        for (const Pin& p : s->pins)
            if (!p.input && !p.type.is_exec()) types.push_back(p.type);
    const int base = new_persistent_block(types);
    u.out_regs.emplace(k, base);
    return base;
}

int Compiler::input_named(const GraphInfo& gi, int node, const char* name, int into) {
    return input_reg(gi, node, pin_index(gi, node, name, true), into);
}

/// The register holding the value of a data input, evaluating whatever feeds
/// it. With `into` >= 0 the value is placed in that register.
int Compiler::input_reg(const GraphInfo& gi, int node, int pin, int into) {
    const Shape* s = gi.shape(node);
    if (!s || pin < 0) return into >= 0 ? into : new_temp();
    const Pin& p = s->pins[static_cast<size_t>(pin)];
    if (p.type.is_any()) {
        error(gi.index, node, "the type of '" + p.name + "' could not be worked out; wire it or pick a type");
        return into >= 0 ? into : new_temp();
    }
    const auto it = gi.data_in.find(pin_key(node, pin));
    if (it == gi.data_in.end()) {
        const int r = into >= 0 ? into : new_temp();
        emit(OP_LOADK, r, const_index(p.def.type == p.type ? p.def : Value::default_of(p.type)));
        return r;
    }
    const Endpoint src = it->second;
    const Shape*   ss = gi.shape(src.node);
    if (!ss) return into >= 0 ? into : new_temp();
    const Pin& sp = ss->pins[static_cast<size_t>(src.pin)];
    int        r = 0;
    if (ss->pure) {
        r = eval_pure(gi, src.node) + data_output_slot(*ss, src.pin);
    } else {
        r = output_block(gi, src.node) + data_output_slot(*ss, src.pin);
    }
    if (sp.type != p.type) {
        const int t = into >= 0 ? into : new_temp();
        emit(OP_CONV, t, r, type_index(p.type));
        return t;
    }
    if (into >= 0 && into != r) {
        emit(OP_MOVE, into, r);
        return into;
    }
    return r;
}

/// Emit the code of a pure node (once per block) and return the first of the
/// registers its outputs are in.
int Compiler::eval_pure(const GraphInfo& gi, int node) {
    Unit&             u = *unit_;
    const std::string k = nkey(gi.index, node);
    const auto        memo = u.pure_memo.find(k);
    if (memo != u.pure_memo.end()) return memo->second;
    const Shape* s = gi.shape(node);
    const Node*  n = gi.nodes.at(node);
    if (!s) return new_temp();
    if (!u.pure_active.insert(k).second) {
        error(gi.index, node, "data wires form a loop through this node");
        return new_temp();
    }
    const Where saved = u.cur;
    u.cur = Where{gi.index, node};

    int       base = 0;
    const int n_out = static_cast<int>(std::count_if(
        s->pins.begin(), s->pins.end(), [](const Pin& p) { return !p.input && !p.type.is_exec(); }));

    switch (s->def->builtin) {
        case Builtin::VarGet: {
            const int vi = prog_.find_variable(n->name);
            base = new_temp();
            emit(OP_GETV, base, vi);
            break;
        }
        case Builtin::LocalGet: {
            const auto it = u.local_regs.find(n->name);
            if (it == u.local_regs.end()) {
                error(gi.index, node, "no local variable named '" + n->name + "'");
                base = new_temp();
            } else {
                base = it->second;   // read the local's own register
            }
            break;
        }
        case Builtin::Self:
            base = new_temp();
            emit(OP_SELF, base);
            break;
        case Builtin::Reroute: base = input_named(gi, node, "In"); break;
        case Builtin::MakeArray: {
            const int count = std::max(1, n->extra);
            const int items = new_temps(count);
            for (int i = 0; i < count; ++i) input_named(gi, node, std::to_string(i).c_str(), items + i);
            base = new_temp();
            const int out_pin = s->find("Array", false);
            const Type arr = out_pin >= 0 ? s->pins[static_cast<size_t>(out_pin)].type : Type();
            emit(OP_MKARR, base, items, type_index(arr.element()), count);
            break;
        }
        case Builtin::Select:
        case Builtin::SelectBool: {
            const bool by_bool = s->def->builtin == Builtin::SelectBool;
            const int  count = by_bool ? 2 : std::max(2, n->extra);
            const int  idx = input_named(gi, node, by_bool ? "Condition" : "Index");
            const int  opts = new_temps(count);
            if (by_bool) {
                input_named(gi, node, "False", opts + 0);
                input_named(gi, node, "True", opts + 1);
            } else {
                for (int i = 0; i < count; ++i)
                    input_named(gi, node, ("Option " + std::to_string(i)).c_str(), opts + i);
            }
            base = new_temp();
            emit(OP_SELECT, base, opts, idx, count);
            break;
        }
        case Builtin::FnCall: {
            const auto f = fn_index_.find(n->name);
            const Graph* fg = script_.find_function(n->name);
            const int np = fg ? static_cast<int>(fg->inputs.size()) : 0;
            const int args = new_temps(std::max(1, np));
            int       a = 0;
            for (size_t i = 0; i < s->pins.size(); ++i) {
                const Pin& p = s->pins[i];
                if (p.input && !p.type.is_exec()) input_reg(gi, node, static_cast<int>(i), args + a++);
            }
            base = new_temps(std::max(1, n_out));
            emit(OP_CALLF, f == fn_index_.end() ? 0 : f->second, args, base);
            break;
        }
        case Builtin::None:
            base = new_temps(std::max(1, n_out));
            emit_native(gi, *n, *s, base, true);
            break;
        default:
            error(gi.index, node, "internal: '" + s->def->id + "' cannot be evaluated as data");
            base = new_temp();
            break;
    }

    u.cur = saved;
    u.pure_active.erase(k);
    u.pure_memo.emplace(k, base);
    return base;
}

/// A native node: evaluate inputs, then one VM opcode when the definition has
/// an equivalent, otherwise a call. Outputs go to dst_base...
void Compiler::emit_native(const GraphInfo& gi, const Node& n, const Shape& s, int dst_base, bool is_pure) {
    const NodeDef*  def = s.def;
    const NativeFn* fn = &def->fn;
    uint16_t        op = def->op;
    (void)is_pure;
    if (s.has_wildcard()) {
        // A wildcard node runs the overload written for its resolved type.
        Type t;
        for (const Pin& p : s.pins)
            if (p.wildcard) {
                t = p.type.array ? p.type.element() : p.type;
                break;
            }
        const Overload* ov = def->find_overload(t);
        if (!ov && !def->overloads.empty()) {
            error(gi.index, n.id, "'" + def->title + "' does not work on " + type_name(t));
            return;
        }
        if (ov) {
            fn = &ov->fn;
            op = ov->op;
        }
    }
    std::vector<int> ins;
    for (size_t i = 0; i < s.pins.size(); ++i)
        if (s.pins[i].input && !s.pins[i].type.is_exec()) ins.push_back(static_cast<int>(i));

    if (op != 0) {
        // Unary or binary operator straight on the operand registers.
        const int a = ins.size() > 0 ? input_reg(gi, n.id, ins[0]) : 0;
        const int b = ins.size() > 1 ? input_reg(gi, n.id, ins[1]) : 0;
        emit(static_cast<Op>(op), dst_base, a, b);
        return;
    }
    if (!*fn) {
        error(gi.index, n.id, "'" + def->title + "' has no implementation");
        return;
    }
    const int args = new_temps(std::max<int>(1, static_cast<int>(ins.size())));
    for (size_t i = 0; i < ins.size(); ++i) input_reg(gi, n.id, ins[i], args + static_cast<int>(i));
    // The overload's function is found again at run time through the node
    // definition and the type, so the program stores which one was chosen.
    const int native = native_index(def);
    int which = 0;
    if (fn != &def->fn) {
        for (size_t i = 0; i < def->overloads.size(); ++i)
            if (&def->overloads[i].fn == fn) which = static_cast<int>(i) + 1;
    }
    emit(OP_CALLN, native, args, dst_base, which);
}

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------
void Compiler::emit_block(const GraphInfo& gi, int node, int pin) {
    Unit& u = *unit_;
    start_block_scope();
    u.cur = Where{gi.index, node};
    const Shape* sp = gi.shape(node);
    const Node*  np = gi.nodes.count(node) ? gi.nodes.at(node) : nullptr;
    if (!sp || !np) {
        emit(OP_END);
        return;
    }
    const Shape&      s = *sp;
    const Node&       n = *np;
    const std::string in_name = s.pins[static_cast<size_t>(pin)].name;
    const Builtin     b = s.def->builtin;

    if (s.def->latent && u.is_function) {
        error(gi.index, node, "'" + s.def->title + "' waits, and a function cannot wait; use it in an event graph");
        emit(OP_END);
        return;
    }

    // State nodes that start open or closed share this: state 0 = not yet
    // decided, 1 = open, 2 = closed.
    auto emit_open_closed_init = [&](int slot) {
        const int l_ready = new_label(), l_closed = new_label();
        emit(OP_JSTEQ, slot, 1, l_ready);
        emit(OP_JSTEQ, slot, 2, l_ready);
        const int sc = input_named(gi, node, "Start Closed");
        emit(OP_JMPT, sc, l_closed);
        emit(OP_STSET, slot, 1);
        emit(OP_JMP, l_ready);
        place(l_closed);
        emit(OP_STSET, slot, 2);
        place(l_ready);
    };
    // One state slot per node, shared by all of its exec inputs.
    auto node_state = [&](int count) -> int {
        const std::string k = "state:" + nkey(gi.index, node);
        const auto        it = u.out_regs.find(k);
        if (it != u.out_regs.end()) return it->second;
        const int slot = prog_.nstate;
        prog_.nstate += count;
        u.out_regs.emplace(k, slot);
        return slot;
    };
    // A persistent register private to this node (loop bound, break flag ...).
    auto node_reg = [&](const char* what, Type t) -> int {
        const std::string k = std::string(what) + ":" + nkey(gi.index, node);
        const auto        it = u.out_regs.find(k);
        if (it != u.out_regs.end()) return it->second;
        const int r = new_persistent(t);
        u.out_regs.emplace(k, r);
        return r;
    };

    switch (b) {
        case Builtin::Branch: {
            const int c = input_named(gi, node, "Condition");
            const int l_false = new_label();
            emit(OP_JMPF, c, l_false);
            go(gi, node, "True");
            place(l_false);
            go(gi, node, "False");
            break;
        }
        case Builtin::Sequence: {
            const int count = std::max(2, n.extra);
            for (int i = 0; i < count; ++i) {
                const std::string name = "Then " + std::to_string(i);
                const int         out = s.find(name, false);
                const bool        wired = out >= 0 && gi.exec_out.count(pin_key(node, out));
                if (i == count - 1) {
                    go_pin(gi, node, out);
                } else if (wired) {
                    const int l_next = new_label();
                    emit(OP_PUSHC, l_next);
                    go_pin(gi, node, out);
                    place(l_next);
                }
            }
            break;
        }
        case Builtin::ForLoop:
        case Builtin::ForLoopBreak: {
            const bool has_break = b == Builtin::ForLoopBreak;
            const int  idx = output_block(gi, node) + data_output_slot(s, s.find("Index", false));
            const int  last = node_reg("last", kInt);
            const int  brk = has_break ? node_reg("break", kBool) : 0;
            if (in_name == "Break") {
                emit(OP_LOADK, brk, const_index(Value::boolean(true)));
                emit(OP_END);
                break;
            }
            input_named(gi, node, "First Index", idx);
            input_named(gi, node, "Last Index", last);
            if (has_break) emit(OP_LOADK, brk, const_index(Value::boolean(false)));
            const int l_top = new_label(), l_inc = new_label(), l_done = new_label();
            place(l_top);
            {
                const int t = new_temp();
                emit(OP_LEI, t, idx, last);
                emit(OP_JMPF, t, l_done);
                if (has_break) emit(OP_JMPT, brk, l_done);
            }
            emit(OP_PUSHC, l_inc);
            go(gi, node, "Loop Body");
            place(l_inc);
            {
                // Temporaries did not survive the loop body: load the 1 again.
                const int one = new_temp();
                emit(OP_LOADK, one, const_index(Value::integer(1)));
                emit(OP_ADDI, idx, idx, one);
                emit(OP_JMP, l_top);
            }
            place(l_done);
            go(gi, node, "Completed");
            break;
        }
        case Builtin::ForEach:
        case Builtin::ForEachBreak: {
            const bool has_break = b == Builtin::ForEachBreak;
            const int  outs = output_block(gi, node);
            const int  elem = outs + data_output_slot(s, s.find("Element", false));
            const int  idx = outs + data_output_slot(s, s.find("Index", false));
            const int  arr_pin = s.find("Array", true);
            const int  arr = node_reg("array", arr_pin >= 0 ? s.pins[static_cast<size_t>(arr_pin)].type : Type());
            const int  brk = has_break ? node_reg("break", kBool) : 0;
            if (in_name == "Break") {
                emit(OP_LOADK, brk, const_index(Value::boolean(true)));
                emit(OP_END);
                break;
            }
            input_named(gi, node, "Array", arr);
            emit(OP_LOADK, idx, const_index(Value::integer(0)));
            if (has_break) emit(OP_LOADK, brk, const_index(Value::boolean(false)));
            const int l_top = new_label(), l_inc = new_label(), l_done = new_label();
            place(l_top);
            {
                const int len = new_temp(), t = new_temp();
                emit(OP_ALEN, len, arr);
                emit(OP_LTI, t, idx, len);
                emit(OP_JMPF, t, l_done);
                if (has_break) emit(OP_JMPT, brk, l_done);
                emit(OP_AGET, elem, arr, idx);
            }
            emit(OP_PUSHC, l_inc);
            go(gi, node, "Loop Body");
            place(l_inc);
            {
                const int one = new_temp();
                emit(OP_LOADK, one, const_index(Value::integer(1)));
                emit(OP_ADDI, idx, idx, one);
                emit(OP_JMP, l_top);
            }
            place(l_done);
            go(gi, node, "Completed");
            break;
        }
        case Builtin::While: {
            // The block's own label is the loop header: the condition is
            // evaluated again each time the body's chain ends.
            const int l_top = new_label(), l_done = new_label();
            place(l_top);
            start_block_scope();
            const int c = input_named(gi, node, "Condition");
            emit(OP_JMPF, c, l_done);
            emit(OP_PUSHC, l_top);
            go(gi, node, "Loop Body");
            place(l_done);
            go(gi, node, "Completed");
            break;
        }
        case Builtin::DoOnce: {
            const int slot = node_state(1);
            if (in_name == "Reset") {
                emit(OP_STSET, slot, 1);
                emit(OP_END);
                break;
            }
            emit_open_closed_init(slot);
            const int l_blocked = new_label();
            emit(OP_JSTEQ, slot, 2, l_blocked);
            emit(OP_STSET, slot, 2);
            go(gi, node, "Completed");
            place(l_blocked);
            emit(OP_END);
            break;
        }
        case Builtin::DoN: {
            const int slot = node_state(1);
            if (in_name == "Reset") {
                emit(OP_STSET, slot, 0);
                emit(OP_END);
                break;
            }
            const int limit = input_named(gi, node, "N");
            const int cnt = new_temp(), t = new_temp(), one = new_temp();
            const int l_blocked = new_label();
            emit(OP_STLOAD, cnt, slot);
            emit(OP_GEI, t, cnt, limit);
            emit(OP_JMPT, t, l_blocked);
            emit(OP_LOADK, one, const_index(Value::integer(1)));
            emit(OP_ADDI, cnt, cnt, one);
            emit(OP_STSTORE, slot, cnt);
            emit(OP_MOVE, output_block(gi, node) + data_output_slot(s, s.find("Counter", false)), cnt);
            go(gi, node, "Exit");
            place(l_blocked);
            emit(OP_END);
            break;
        }
        case Builtin::FlipFlop: {
            const int slot = node_state(1);
            const int is_a = output_block(gi, node) + data_output_slot(s, s.find("Is A", false));
            const int l_b = new_label();
            emit(OP_JSTEQ, slot, 1, l_b);
            emit(OP_STSET, slot, 1);
            emit(OP_LOADK, is_a, const_index(Value::boolean(true)));
            go(gi, node, "A");
            place(l_b);
            emit(OP_STSET, slot, 0);
            emit(OP_LOADK, is_a, const_index(Value::boolean(false)));
            go(gi, node, "B");
            break;
        }
        case Builtin::Gate: {
            const int slot = node_state(1);
            if (in_name == "Open") {
                emit(OP_STSET, slot, 1);
                emit(OP_END);
            } else if (in_name == "Close") {
                emit(OP_STSET, slot, 2);
                emit(OP_END);
            } else if (in_name == "Toggle") {
                emit_open_closed_init(slot);
                const int l_was_open = new_label();
                emit(OP_JSTEQ, slot, 1, l_was_open);
                emit(OP_STSET, slot, 1);
                emit(OP_END);
                place(l_was_open);
                emit(OP_STSET, slot, 2);
                emit(OP_END);
            } else {   // Enter
                emit_open_closed_init(slot);
                const int l_blocked = new_label();
                emit(OP_JSTEQ, slot, 2, l_blocked);
                go(gi, node, "Exit");
                place(l_blocked);
                emit(OP_END);
            }
            break;
        }
        case Builtin::MultiGate: {
            // state = index of the next output + 1; 0 = not started.
            const int slot = node_state(1);
            if (in_name == "Reset") {
                emit(OP_STSET, slot, 0);
                emit(OP_END);
                break;
            }
            const int count = std::max(2, n.extra);
            const int l_go = new_label(), l_init = new_label(), l_end = new_label();
            emit(OP_JSTEQ, slot, 0, l_init);
            place(l_go);
            std::vector<int> l_out(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                l_out[static_cast<size_t>(i)] = new_label();
                emit(OP_JSTEQ, slot, i + 1, l_out[static_cast<size_t>(i)]);
            }
            {
                // Past the last output: start over if Loop is on, else do nothing.
                const int loop = input_named(gi, node, "Loop");
                emit(OP_JMPF, loop, l_end);
                emit(OP_STSET, slot, 1);
                emit(OP_JMP, l_go);
            }
            place(l_end);
            emit(OP_END);
            for (int i = 0; i < count; ++i) {
                place(l_out[static_cast<size_t>(i)]);
                emit(OP_STSET, slot, i + 2);
                go(gi, node, "Out " + std::to_string(i));
            }
            place(l_init);
            {
                const int start = input_named(gi, node, "Start Index");
                const int one = new_temp(), next = new_temp();
                emit(OP_LOADK, one, const_index(Value::integer(1)));
                emit(OP_ADDI, next, start, one);
                emit(OP_STSTORE, slot, next);
                emit(OP_JMP, l_go);
            }
            break;
        }
        case Builtin::SwitchInt:
        case Builtin::SwitchString: {
            const bool by_int = b == Builtin::SwitchInt;
            const int  sel = input_named(gi, node, "Selection");
            std::vector<int> labels;
            for (const std::string& c : n.cases) {
                Value k;
                if (by_int) {
                    int64_t    v = 0;
                    const auto r = std::from_chars(c.data(), c.data() + c.size(), v);
                    if (r.ec != std::errc() || r.ptr != c.data() + c.size()) {
                        error(gi.index, node, "the case '" + c + "' is not a whole number");
                        continue;
                    }
                    k = Value::integer(v);
                } else {
                    k = Value::string(c);
                }
                const int kr = load_const(k);
                const int t = new_temp();
                const int l = new_label();
                emit(by_int ? OP_EQI : OP_EQ, t, sel, kr);
                emit(OP_JMPT, t, l);
                labels.push_back(l);
            }
            go(gi, node, "Default");
            size_t li = 0;
            for (const std::string& c : n.cases) {
                if (li >= labels.size()) break;
                place(labels[li++]);
                go(gi, node, c);
            }
            break;
        }
        case Builtin::Delay:
        case Builtin::RetriggerableDelay: {
            const int slot = node_state(1);
            const int secs = input_named(gi, node, "Duration");
            const int l_resume = new_label();
            emit(OP_LATENT, secs, l_resume, slot, b == Builtin::RetriggerableDelay ? 1 : 0);
            place(l_resume);
            go(gi, node, "Completed");
            break;
        }
        case Builtin::VarSet: {
            const int vi = prog_.find_variable(n.name);
            const int v = input_named(gi, node, "Value");
            emit(OP_SETV, vi, v);
            emit(OP_GETV, output_block(gi, node), vi);
            go(gi, node, "Then");
            break;
        }
        case Builtin::LocalSet: {
            const auto it = u.local_regs.find(n.name);
            if (it == u.local_regs.end()) {
                error(gi.index, node, "no local variable named '" + n.name + "'");
                emit(OP_END);
                break;
            }
            // A deep copy for arrays, so a local never aliases the array it
            // was assigned from.
            const int v = input_named(gi, node, "Value");
            const int vp = s.find("Value", true);
            const Type t = vp >= 0 ? s.pins[static_cast<size_t>(vp)].type : Type();
            if (t.array || t.kind == Kind::Struct) emit(OP_COPY, it->second, v);
            else emit(OP_MOVE, it->second, v);
            emit(OP_MOVE, output_block(gi, node), it->second);
            go(gi, node, "Then");
            break;
        }
        case Builtin::FnCall: {
            const auto   f = fn_index_.find(n.name);
            const Graph* fg = script_.find_function(n.name);
            const int    np2 = fg ? static_cast<int>(fg->inputs.size()) : 0;
            const int    args = new_temps(std::max(1, np2));
            int          a = 0;
            for (size_t i = 0; i < s.pins.size(); ++i)
                if (s.pins[i].input && !s.pins[i].type.is_exec()) input_reg(gi, node, static_cast<int>(i), args + a++);
            emit(OP_CALLF, f == fn_index_.end() ? 0 : f->second, args, output_block(gi, node));
            go(gi, node, "Then");
            break;
        }
        case Builtin::CallEvent: {
            const auto e = event_index_.find(n.name);
            if (e == event_index_.end()) {
                error(gi.index, node, "no custom event named '" + n.name + "'");
                emit(OP_END);
                break;
            }
            std::vector<int> ins;
            for (size_t i = 0; i < s.pins.size(); ++i)
                if (s.pins[i].input && !s.pins[i].type.is_exec()) ins.push_back(static_cast<int>(i));
            const int args = new_temps(std::max<int>(1, static_cast<int>(ins.size())));
            for (size_t i = 0; i < ins.size(); ++i) input_reg(gi, node, ins[i], args + static_cast<int>(i));
            emit(OP_CALLE, e->second, args);
            go(gi, node, "Then");
            break;
        }
        case Builtin::FnReturn: {
            int r = 0;
            for (size_t i = 0; i < s.pins.size(); ++i)
                if (s.pins[i].input && !s.pins[i].type.is_exec())
                    input_reg(gi, node, static_cast<int>(i), u.info.result_base + r++);
            emit(OP_RET);
            break;
        }
        case Builtin::None: {
            if (s.pure) {
                error(gi.index, node, "internal: a pure node was reached by a flow wire");
                emit(OP_END);
                break;
            }
            emit_native(gi, n, s, output_block(gi, node), false);
            go(gi, node, "Then");
            break;
        }
        default:
            error(gi.index, node, "'" + s.def->title + "' has no flow input to run");
            emit(OP_END);
            break;
    }
}

void Compiler::drain(Unit& u) {
    while (!u.pending.empty()) {
        const BlockKey k = u.pending.front();
        u.pending.pop_front();
        const GraphInfo& gi = info(k.graph);
        place(u.blocks.at(nkey(k.graph, k.node) + ":" + std::to_string(k.pin)));
        emit_block(gi, k.node, k.pin);
    }
}

/// Turn labels into instruction indices and temporaries into real registers,
/// then append the unit to the program.
void Compiler::finalize(Unit& u) {
    const int npersist = static_cast<int>(u.persistent.size());
    auto reg = [&](int r) { return r >= kTempBase ? npersist + (r - kTempBase) : r; };
    const int base_pc = static_cast<int>(prog_.code.size());
    for (Instr& i : u.code) {
        const OpInfo& oi = kOps[i.op];
        int*          f[3] = {&i.a, &i.b, &i.c};
        const uint8_t kind[3] = {oi.a, oi.b, oi.c};
        for (int k = 0; k < 3; ++k) {
            if (kind[k] == oR) *f[k] = reg(*f[k]);
            else if (kind[k] == oL) *f[k] = base_pc + u.label_pc[static_cast<size_t>(*f[k])];
        }
    }
    u.info.entry += base_pc;
    u.info.proto = u.persistent_init;
    u.info.proto.resize(static_cast<size_t>(npersist + u.temp_max));
    u.info.boxed.clear();
    for (int i = 0; i < npersist; ++i)
        if (u.persistent[static_cast<size_t>(i)].array || u.persistent[static_cast<size_t>(i)].kind == Kind::Struct)
            u.info.boxed.push_back(i);
    prog_.code.insert(prog_.code.end(), u.code.begin(), u.code.end());
    prog_.where.insert(prog_.where.end(), u.where.begin(), u.where.end());
}

// ---------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------
void Compiler::compile_event_unit() {
    Unit u;
    u.info.name = "<events>";
    u.info.graph = -1;
    begin_unit(u);

    // Pass 1: every event node gets its parameter registers and a table entry,
    // so that a Call Event anywhere can refer to it.
    struct Entry {
        int graph, node, label;
    };
    std::vector<Entry> entries;
    for (const GraphInfo& gi : infos_) {
        if (gi.g->kind != GraphKind::Event) continue;
        for (const Node& n : gi.g->nodes) {
            const Shape* s = gi.shape(n.id);
            if (!s) continue;
            const Builtin b = s->def->builtin;
            if (b != Builtin::Event && b != Builtin::CustomEvent) continue;
            EventInfo e;
            e.custom = b == Builtin::CustomEvent;
            e.graph = gi.index;
            e.node = n.id;
            if (e.custom) {
                if (n.name.empty()) {
                    error(gi.index, n.id, "a custom event needs a name");
                    continue;
                }
                e.name = n.name;
            } else {
                e.name = n.name.empty() ? s->def->id : s->def->id + ":" + n.name;
            }
            if (event_index_.count(e.name)) {
                error(gi.index, n.id, "the event '" + (e.custom ? e.name : s->def->title) + "' appears twice in this script");
                continue;
            }
            const int base = output_block(gi, n.id);
            int       slot = 0;
            for (const Pin& p : s->pins)
                if (!p.input && !p.type.is_exec()) e.param_regs.push_back(base + slot++);
            const int label = new_label();
            entries.push_back(Entry{gi.index, n.id, label});
            event_index_[e.name] = static_cast<int>(prog_.events.size());
            event_of_node_[nkey(gi.index, n.id)] = static_cast<int>(prog_.events.size());
            prog_.events.push_back(std::move(e));
        }
    }
    // Pass 2: code. Each event's entry is a stub that follows its Then wire.
    for (const Entry& en : entries) {
        const GraphInfo& gi = info(en.graph);
        place(en.label);
        start_block_scope();
        u.cur = Where{en.graph, en.node};
        go(gi, en.node, "Then");
    }
    drain(u);
    if (u.code.empty()) {
        u.cur = Where{};
        emit(OP_END);
    }
    // Entries are label ids until the unit is laid out.
    const int base_pc = static_cast<int>(prog_.code.size());
    for (const Entry& en : entries) {
        const int ev = event_of_node_.at(nkey(en.graph, en.node));
        prog_.events[static_cast<size_t>(ev)].entry = base_pc + u.label_pc[static_cast<size_t>(en.label)];
    }
    u.info.entry = 0;
    finalize(u);
    prog_.fns[0] = std::move(u.info);
}

void Compiler::compile_function(int graph_index, int fn_index) {
    const GraphInfo& gi = info(graph_index);
    const Graph&     g = *gi.g;
    Unit             u;
    u.is_function = true;
    u.info.name = g.name;
    u.info.graph = graph_index;
    u.info.pure = g.pure;
    begin_unit(u);

    // Registers: parameters first, then results, then locals.
    u.info.nparams = static_cast<int>(g.inputs.size());
    for (const Param& p : g.inputs) new_persistent(p.type, &p.def);
    u.info.result_base = static_cast<int>(u.persistent.size());
    u.info.nresults = static_cast<int>(g.outputs.size());
    for (const Param& p : g.outputs) new_persistent(p.type, &p.def);
    for (const Variable& v : g.locals) {
        if (u.local_regs.count(v.name)) {
            error(graph_index, 0, "two local variables are named '" + v.name + "'");
            continue;
        }
        u.local_regs[v.name] = new_persistent(v.type, &v.def);
    }

    const Node* entry = nullptr;
    std::vector<const Node*> returns;
    for (const Node& n : g.nodes) {
        const Shape* s = gi.shape(n.id);
        if (!s) continue;
        if (s->def->builtin == Builtin::FnEntry) {
            if (entry) error(graph_index, n.id, "a function has one entry node; this is a second one");
            else entry = &n;
        } else if (s->def->builtin == Builtin::FnReturn) {
            returns.push_back(&n);
        }
    }
    if (entry) u.out_regs[nkey(graph_index, entry->id)] = 0;   // entry outputs ARE the parameter registers

    u.cur = Where{graph_index, entry ? entry->id : 0};
    start_block_scope();
    if (g.pure) {
        if (returns.size() != 1) {
            error(graph_index, 0, "a pure function needs exactly one return node");
        } else {
            const Shape* s = gi.shape(returns[0]->id);
            u.cur = Where{graph_index, returns[0]->id};
            int r = 0;
            for (size_t i = 0; s && i < s->pins.size(); ++i)
                if (s->pins[i].input && !s->pins[i].type.is_exec())
                    input_reg(gi, returns[0]->id, static_cast<int>(i), u.info.result_base + r++);
        }
        emit(OP_RET);
    } else {
        if (!entry) {
            error(graph_index, 0, "the function '" + g.name + "' has no entry node");
            emit(OP_RET);
        } else {
            go(gi, entry->id, "Then");
        }
        drain(u);
    }
    u.info.entry = 0;
    finalize(u);
    prog_.fns[static_cast<size_t>(fn_index)] = std::move(u.info);
}

bool Compiler::run() {
    prog_ = Program{};
    prog_.name = script_.name;

    // Variables.
    std::unordered_set<std::string> seen;
    for (const Variable& v : script_.variables) {
        if (v.name.empty()) {
            error(-1, 0, "a variable has no name");
            continue;
        }
        if (!seen.insert(v.name).second) {
            error(-1, 0, "two variables are named '" + v.name + "'");
            continue;
        }
        VarInfo vi;
        vi.name = v.name;
        vi.type = v.type;
        vi.def = v.def.type == v.type ? v.def : Value::default_of(v.type);
        vi.exposed = v.exposed;
        prog_.vars.push_back(std::move(vi));
    }

    // Function table first, so calls can be compiled in any order.
    prog_.fns.resize(1);
    seen.clear();
    for (size_t i = 0; i < script_.graphs.size(); ++i) {
        const Graph& g = script_.graphs[i];
        prog_.graph_names.push_back(g.name);
        if (g.kind != GraphKind::Function) continue;
        if (!seen.insert(g.name).second) {
            error(static_cast<int>(i), 0, "two functions are named '" + g.name + "'");
            continue;
        }
        fn_index_[g.name] = static_cast<int>(prog_.fns.size());
        prog_.fns.emplace_back();
    }

    infos_.resize(script_.graphs.size());
    for (size_t i = 0; i < script_.graphs.size(); ++i) {
        infos_[i].g = &script_.graphs[i];
        infos_[i].index = static_cast<int>(i);
        analyse(infos_[i]);
    }

    compile_event_unit();
    for (size_t i = 0; i < script_.graphs.size(); ++i) {
        const Graph& g = script_.graphs[i];
        if (g.kind != GraphKind::Function) continue;
        const auto it = fn_index_.find(g.name);
        if (it == fn_index_.end()) continue;
        if (prog_.fns[static_cast<size_t>(it->second)].name.empty()) compile_function(static_cast<int>(i), it->second);
    }
    return !failed_;
}

}  // namespace

bool compile(const Registry& reg, const Script& script, Program& out, std::vector<CompileMessage>& messages) {
    Compiler c(reg, script, out, messages);
    return c.run();
}

std::string disassemble(const Program& p) {
    std::string s;
    auto field = [&](uint8_t kind, int v) -> std::string {
        switch (kind) {
            case oR: return "r" + std::to_string(v);
            case oK: return "k" + std::to_string(v) + "(" + literal_text(p.consts[static_cast<size_t>(v)]) + ")";
            case oL: return "@" + std::to_string(v);
            case oI: return "#" + std::to_string(v);
            case oV: return "var" + std::to_string(v);
            case oS: return "st" + std::to_string(v);
            case oT: return type_name(p.types[static_cast<size_t>(v)]);
            case oF: return "fn" + std::to_string(v);
            case oE: return "ev" + std::to_string(v);
            case oX: return p.natives[static_cast<size_t>(v)]->id;
            default: return std::string();
        }
    };
    for (size_t pc = 0; pc < p.code.size(); ++pc) {
        const Instr&  i = p.code[pc];
        const OpInfo& oi = kOps[i.op];
        s += std::to_string(pc) + "\t" + oi.name;
        const uint8_t kind[3] = {oi.a, oi.b, oi.c};
        const int     v[3] = {i.a, i.b, i.c};
        for (int k = 0; k < 3; ++k)
            if (kind[k] != oN) s += " " + field(kind[k], v[k]);
        if (i.d) s += " d=" + std::to_string(i.d);
        s += "\n";
    }
    return s;
}

}  // namespace schizo::vs
