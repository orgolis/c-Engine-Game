// ====================
// vscript_check -- headless verification of the visual scripting core.
//
// Builds graphs in memory, compiles them and runs them on the VM. What is
// asserted is what an author relies on without seeing it: which wire runs
// first, when a pure node is evaluated again, what a Delay remembers, that an
// array read from a variable is the variable's array, and that a broken graph
// is reported at the node that is wrong instead of crashing at run time.
//
// Pure CPU, no device. Prints "vscript_check: ALL OK (N checks)" and exits 0.
// ====================

#include "vscript/io.h"
#include "vscript/program.h"
#include "vscript/vm.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace schizo::vs;

namespace {

int g_pass = 0, g_fail = 0;
void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "OK" : "FAIL", what.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

struct TestHost : Host {
    std::vector<std::string> out, errors;
    void print(const Instance&, std::string_view t) override { out.emplace_back(t); }
    void script_error(const Instance&, int, int node, std::string_view t) override {
        errors.push_back("#" + std::to_string(node) + " " + std::string(t));
    }
    std::string joined() const {
        std::string s;
        for (const auto& o : out) { if (!s.empty()) s += ","; s += o; }
        return s;
    }
};

struct Rig {
    Registry reg;
    Script   script;
    Program  prog;
    TestHost host;
    VM       vm{&host};
    std::vector<CompileMessage> msgs;
    std::unique_ptr<Instance>   inst;

    Rig() { register_standard_library(reg); script.name = "test"; }
    Graph& events() {
        if (Graph* g = script.find_graph("EventGraph")) return *g;
        return script.add_graph(GraphKind::Event, "EventGraph");
    }
    bool build() {
        msgs.clear();
        const bool ok = compile(reg, script, prog, msgs);
        if (ok) inst = vm.instantiate(prog, 7);
        return ok;
    }
    bool has_error(const std::string& part) const {
        for (const auto& m : msgs) if (m.error && m.text.find(part) != std::string::npos) return true;
        return false;
    }
    std::string first_error() const {
        for (const auto& m : msgs) if (m.error) return m.text;
        return "";
    }
    std::string run(const char* event = "event.begin_play") {
        host.out.clear();
        vm.fire(*inst, event);
        return host.joined();
    }
};

/// Fires Begin Play three times, in order. (a() + a() + a() does not promise an order.)
std::string run3(Rig& r) {
    std::string s = r.run();
    s += r.run();
    s += r.run();
    return s;
}

// Small helpers so a test graph reads like its picture.
int node(Graph& g, const char* def, const char* name = "") {
    Node& n = g.add(def);
    n.name = name;
    return n.id;
}
void lit(Graph& g, int id, const char* pin, Value v) { g.find(id)->set_default(pin, std::move(v)); }
int print_node(Graph& g, const char* text = nullptr) {
    const int p = node(g, "print");
    if (text) lit(g, p, "Text", Value::string(text));
    return p;
}
// begin -> print(text)... chained by exec in order
void chain(Graph& g, std::initializer_list<int> ids, const char* out = "Then", const char* in = "In") {
    int prev = 0;
    for (int id : ids) {
        if (prev) g.link(prev, out, id, in);
        prev = id;
    }
}

void test_values() {
    std::printf("-- values\n");
    check(sizeof(Value) == 32, "a Value is 32 bytes");
    Value v;
    check(parse_literal(kVec3, "1,2.5,-3", v) && v.as_vec3() == glm::vec3(1, 2.5f, -3), "a vec3 literal parses");
    check(literal_text(Value::number(0.1)) == "0.1" && literal_text(Value::number(2.0)) == "2.0",
          "numbers print shortest and never look like integers");
    const Value arr = Value::array(kString, {Value::string("a|b"), Value::string(""), Value::string("c\\")});
    Value back;
    check(parse_literal(Type(Kind::String, true), literal_text(arr), back) && equals(arr, back),
          "a string array survives the text form, separators and empty items included");
    Value conv;
    check(convert(Value::integer(3), kFloat, conv) && conv.as_float() == 3.0, "Int converts to Float");
    check(!can_convert(kString, kInt), "String does not silently become Int");
    Value a = Value::array(kInt, {Value::integer(1)});
    Value b = a;
    b.items().push_back(Value::integer(2));
    check(a.items().size() == 2, "copying an array value shares it");
    Value c = clone(a);
    c.items().clear();
    check(a.items().size() == 2, "clone() does not");
    Type t;
    check(parse_type("vec3[]", t) && t == Type(Kind::Vec3, true) && type_name(t) == "vec3[]", "type names round-trip");
}

void test_flow() {
    std::printf("-- flow\n");
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), br = node(g, "flow.branch");
        const int t = print_node(g, "yes"), f = print_node(g, "no");
        const int lt = node(g, "math.less");
        lit(g, lt, "A", Value::integer(2)); lit(g, lt, "B", Value::number(2.5));
        g.find(lt)->targ = kFloat;
        g.link(ev, "Then", br, "In"); g.link(lt, "Result", br, "Condition");
        g.link(br, "True", t, "In"); g.link(br, "False", f, "In");
        check(r.build() && r.run() == "yes", "Branch takes True, and an Int literal on a Float pin is converted");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), sq = node(g, "flow.sequence");
        g.find(sq)->extra = 3;
        const int a = print_node(g, "a"), a2 = print_node(g, "a2"), b = print_node(g, "b"), c = print_node(g, "c");
        g.link(ev, "Then", sq, "In");
        g.link(sq, "Then 0", a, "In"); g.link(a, "Then", a2, "In");
        g.link(sq, "Then 2", c, "In"); g.link(sq, "Then 1", b, "In");
        check(r.build() && r.run() == "a,a2,b,c", "Sequence finishes each chain before starting the next");
    }
    {
        // for i in 2..5: print(i * 10)   then "done"
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), fl = node(g, "flow.for");
        lit(g, fl, "First Index", Value::integer(2)); lit(g, fl, "Last Index", Value::integer(5));
        const int mul = node(g, "math.mul"); lit(g, mul, "B", Value::integer(10));
        const int p = print_node(g), d = print_node(g, "done");
        g.link(ev, "Then", fl, "In"); g.link(fl, "Index", mul, "A"); g.link(mul, "Result", p, "Text");
        g.link(fl, "Loop Body", p, "In"); g.link(fl, "Completed", d, "In");
        check(r.build() && r.run() == "20,30,40,50,done",
              "For Loop runs inclusive bounds; the wildcard * takes Int from its wire; Int prints as text");
    }
    {
        Rig r; Graph& g = r.events();
        r.script.add_variable("n", kInt, Value::integer(0));
        const int ev = node(g, "event.begin_play"), wh = node(g, "flow.while");
        const int get = node(g, "var.get", "n"), lt = node(g, "math.less"); lit(g, lt, "B", Value::integer(3));
        const int add = node(g, "math.add"); lit(g, add, "B", Value::integer(1));
        const int set = node(g, "var.set", "n"), p = print_node(g);
        g.link(ev, "Then", wh, "In"); g.link(get, "Value", lt, "A"); g.link(lt, "Result", wh, "Condition");
        g.link(wh, "Loop Body", set, "In"); g.link(get, "Value", add, "A"); g.link(add, "Result", set, "Value");
        g.link(set, "Then", p, "In"); g.link(set, "Value", p, "Text");
        check(r.build() && r.run() == "1,2,3", "While re-evaluates its pure condition every pass");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), mk = node(g, "array.make"), fe = node(g, "flow.for_each");
        g.find(mk)->extra = 3;
        lit(g, mk, "0", Value::string("x")); lit(g, mk, "1", Value::string("y")); lit(g, mk, "2", Value::string("z"));
        g.find(mk)->targ = kString;
        const int p = print_node(g);
        g.link(ev, "Then", fe, "In"); g.link(mk, "Array", fe, "Array");
        g.link(fe, "Loop Body", p, "In"); g.link(fe, "Element", p, "Text");
        check(r.build() && r.run() == "x,y,z", "For Each takes its element type from the array wired in");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), fl = node(g, "flow.for_break");
        lit(g, fl, "Last Index", Value::integer(9));
        const int p = print_node(g), br = node(g, "flow.branch"), eq = node(g, "math.equal");
        lit(g, eq, "B", Value::integer(2));
        g.link(ev, "Then", fl, "In"); g.link(fl, "Loop Body", p, "In"); g.link(fl, "Index", p, "Text");
        g.link(p, "Then", br, "In"); g.link(fl, "Index", eq, "A"); g.link(eq, "Result", br, "Condition");
        g.link(br, "True", fl, "Break");
        check(r.build() && r.run() == "0,1,2", "Break stops the loop after the current pass");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), sw = node(g, "flow.switch_string");
        g.find(sw)->cases = {"red", "green"};
        lit(g, sw, "Selection", Value::string("green"));
        const int a = print_node(g, "R"), b = print_node(g, "G"), d = print_node(g, "?");
        g.link(ev, "Then", sw, "In"); g.link(sw, "red", a, "In"); g.link(sw, "green", b, "In"); g.link(sw, "Default", d, "In");
        check(r.build() && r.run() == "G", "Switch on String picks the matching case");
        lit(g, sw, "Selection", Value::string("blue"));
        check(r.build() && r.run() == "?", "and Default when none matches");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), sel = node(g, "flow.select");
        g.find(sel)->extra = 3; g.find(sel)->targ = kString;
        lit(g, sel, "Option 0", Value::string("zero")); lit(g, sel, "Option 2", Value::string("two"));
        lit(g, sel, "Index", Value::integer(7));
        const int p = print_node(g);
        g.link(ev, "Then", p, "In"); g.link(sel, "Result", p, "Text");
        check(r.build() && r.run() == "two", "Select clamps an index past the end to the last option");
    }
}

void test_state_nodes() {
    std::printf("-- nodes that remember\n");
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), once = node(g, "flow.do_once"), p = print_node(g, "once");
        const int rs = node(g, "event.custom", "reset");
        g.link(ev, "Then", once, "In"); g.link(once, "Completed", p, "In"); g.link(rs, "Then", once, "Reset");
        check(r.build(), "Do Once compiles");
        std::string s = r.run(); s += "|" + r.run();
        r.vm.fire(*r.inst, "reset");
        s += "|" + r.run();
        check(s == "once||once", "Do Once passes once, blocks, and passes again after Reset");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), ff = node(g, "flow.flip_flop");
        const int a = print_node(g, "A"), b = print_node(g, "B");
        g.link(ev, "Then", ff, "In"); g.link(ff, "A", a, "In"); g.link(ff, "B", b, "In");
        check(r.build() && run3(r) == "ABA", "Flip Flop alternates, starting with A");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), gate = node(g, "flow.gate"), p = print_node(g, "through");
        lit(g, gate, "Start Closed", Value::boolean(true));
        const int op = node(g, "event.custom", "open");
        g.link(ev, "Then", gate, "Enter"); g.link(gate, "Exit", p, "In"); g.link(op, "Then", gate, "Open");
        check(r.build(), "Gate compiles");
        std::string s = r.run();
        r.vm.fire(*r.inst, "open");
        s += "|" + r.run();
        check(s == "|through", "a Gate that starts closed passes only after Open");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), dn = node(g, "flow.do_n"), p = print_node(g);
        lit(g, dn, "N", Value::integer(2));
        g.link(ev, "Then", dn, "In"); g.link(dn, "Exit", p, "In"); g.link(dn, "Counter", p, "Text");
        check(r.build() && run3(r) == "12", "Do N passes N times and reports the count");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), mg = node(g, "flow.multi_gate");
        g.find(mg)->extra = 2; lit(g, mg, "Loop", Value::boolean(true));
        const int a = print_node(g, "0"), b = print_node(g, "1");
        g.link(ev, "Then", mg, "In"); g.link(mg, "Out 0", a, "In"); g.link(mg, "Out 1", b, "In");
        check(r.build() && run3(r) == "010", "Multi Gate steps through its outputs and loops");
    }
    {
        // Two instances of one program do not share node state or variables.
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), ff = node(g, "flow.flip_flop");
        const int a = print_node(g, "A"), b = print_node(g, "B");
        g.link(ev, "Then", ff, "In"); g.link(ff, "A", a, "In"); g.link(ff, "B", b, "In");
        r.build();
        auto other = r.vm.instantiate(r.prog, 8);
        r.run();
        r.host.out.clear();
        r.vm.fire(*other, "event.begin_play");
        check(r.host.joined() == "A", "each instance has its own node state");
    }
}

void test_pure_and_variables() {
    std::printf("-- data\n");
    {
        // A pure node feeding two consumers is evaluated for each of them.
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), rnd = node(g, "float.random");
        const int p1 = print_node(g), p2 = print_node(g);
        g.link(ev, "Then", p1, "In"); g.link(p1, "Then", p2, "In");
        g.link(rnd, "Result", p1, "Text"); g.link(rnd, "Result", p2, "Text");
        r.build(); r.run();
        check(r.host.out.size() == 2 && r.host.out[0] != r.host.out[1],
              "a pure node is evaluated again for each consumer");
    }
    {
        // ... but once per consumer, even if that consumer uses it twice.
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), rnd = node(g, "float.random"), eq = node(g, "math.equal");
        const int p = print_node(g);
        g.link(rnd, "Result", eq, "A"); g.link(rnd, "Result", eq, "B");
        g.link(ev, "Then", p, "In"); g.link(eq, "Result", p, "Text");
        check(r.build() && r.run() == "true", "and once per consumer, however many of its pins it feeds");
    }
    {
        Rig r; Graph& g = r.events();
        r.script.add_variable("list", Type(Kind::Int, true));
        r.script.add_variable("copy", Type(Kind::Int, true));
        const int ev = node(g, "event.begin_play"), get = node(g, "var.get", "list"), add = node(g, "array.add");
        lit(g, add, "Item", Value::integer(5));
        const int set = node(g, "var.set", "copy"), add2 = node(g, "array.add"); lit(g, add2, "Item", Value::integer(6));
        const int len = node(g, "array.length"), getc = node(g, "var.get", "copy"), lenc = node(g, "array.length");
        const int p = print_node(g), pc = print_node(g);
        chain(g, {ev, add, set, add2, p, pc});
        g.link(get, "Value", add, "Array"); g.link(get, "Value", set, "Value"); g.link(get, "Value", add2, "Array");
        g.link(get, "Value", len, "Array"); g.link(len, "Length", p, "Text");
        g.link(getc, "Value", lenc, "Array"); g.link(lenc, "Length", pc, "Text");
        check(r.build() && r.run() == "2,1",
              "Add on a variable's array changes the variable; Set copies, so the copy stays as it was");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), get = node(g, "array.get"), mk = node(g, "array.make");
        lit(g, mk, "0", Value::integer(4)); g.find(mk)->targ = kInt;
        lit(g, get, "Index", Value::integer(3));
        const int p = print_node(g);
        g.link(ev, "Then", p, "In"); g.link(mk, "Array", get, "Array"); g.link(get, "Element", p, "Text");
        check(r.build() && r.run() == "0" && r.host.errors.size() == 1 && r.host.errors[0].find("outside") != std::string::npos,
              "reading past the end of an array gives the default and reports the node");
    }
}

void test_functions_and_events() {
    std::printf("-- functions and events\n");
    {
        // factorial(n) = n <= 1 ? 1 : n * factorial(n - 1), as an impure function
        Rig r;
        Graph& f = r.script.add_graph(GraphKind::Function, "factorial");
        f.inputs.push_back(Param{"n", kInt, Value::integer(0)});
        f.outputs.push_back(Param{"result", kInt, Value::integer(0)});
        const int en = node(f, "fn.entry"), br = node(f, "flow.branch"), le = node(f, "math.less_equal");
        lit(f, le, "B", Value::integer(1));
        const int r1 = node(f, "fn.return"); lit(f, r1, "result", Value::integer(1));
        const int sub = node(f, "math.sub"); lit(f, sub, "B", Value::integer(1));
        const int call = node(f, "fn.call", "factorial"), mul = node(f, "math.mul"), r2 = node(f, "fn.return");
        f.link(en, "Then", br, "In"); f.link(en, "n", le, "A"); f.link(le, "Result", br, "Condition");
        f.link(br, "True", r1, "In"); f.link(br, "False", call, "In");
        f.link(en, "n", sub, "A"); f.link(sub, "Result", call, "n");
        f.link(en, "n", mul, "A"); f.link(call, "result", mul, "B");
        f.link(call, "Then", r2, "In"); f.link(mul, "Result", r2, "result");
        Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), c = node(g, "fn.call", "factorial"), p = print_node(g);
        lit(g, c, "n", Value::integer(10));
        chain(g, {ev, c, p}); g.link(c, "result", p, "Text");
        check(r.build() && r.run() == "3628800", "a function calls itself with a fresh frame each time");
        std::vector<Value> out;
        const Value arg = Value::integer(5);
        check(r.vm.call(*r.inst, "factorial", &arg, 1, &out) && out.size() == 1 && out[0].as_int() == 120,
              "the host can call a script function and read its result");
    }
    {
        // pure function with a local: hyp(a, b) = sqrt(a*a + b*b)
        Rig r;
        Graph& f = r.script.add_graph(GraphKind::Function, "hyp");
        f.pure = true;
        f.inputs = {Param{"a", kFloat, Value::number(0)}, Param{"b", kFloat, Value::number(0)}};
        f.outputs = {Param{"h", kFloat, Value::number(0)}};
        const int en = node(f, "fn.entry"), aa = node(f, "math.mul"), bb = node(f, "math.mul"), add = node(f, "math.add");
        const int sq = node(f, "float.sqrt"), ret = node(f, "fn.return");
        f.link(en, "a", aa, "A"); f.link(en, "a", aa, "B"); f.link(en, "b", bb, "A"); f.link(en, "b", bb, "B");
        f.link(aa, "Result", add, "A"); f.link(bb, "Result", add, "B"); f.link(add, "Result", sq, "A");
        f.link(sq, "Result", ret, "h");
        Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), c = node(g, "fn.call", "hyp"), p = print_node(g);
        lit(g, c, "a", Value::number(3)); lit(g, c, "b", Value::number(4));
        g.link(ev, "Then", p, "In"); g.link(c, "h", p, "Text");
        check(r.build() && r.run() == "5.0", "a pure function is used from a data wire, with no flow pins");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), ce = node(g, "event.custom", "hit");
        g.find(ce)->params = {Param{"damage", kFloat, Value::number(0)}, Param{"who", kString, Value()}};
        const int call = node(g, "event.call", "hit"), after = print_node(g, "after");
        lit(g, call, "damage", Value::number(12.5)); lit(g, call, "who", Value::string("orc"));
        const int p1 = print_node(g), p2 = print_node(g);
        chain(g, {ev, call, after}); chain(g, {ce, p1, p2});
        g.link(ce, "who", p1, "Text"); g.link(ce, "damage", p2, "Text");
        check(r.build() && r.run() == "orc,12.5,after", "a custom event receives its arguments and returns to the caller");
        const Value args[2] = {Value::number(1.0), Value::string("host")};
        r.host.out.clear();
        check(r.vm.fire(*r.inst, "hit", args, 2) && r.host.joined() == "host,1.0", "the host can fire a custom event");
        check(!r.vm.fire(*r.inst, "nothing"), "firing an event the script does not have reports false");
    }
    {
        Rig r; Graph& g = r.events();
        const int tick = node(g, "event.tick"), p = print_node(g);
        g.link(tick, "Then", p, "In"); g.link(tick, "Delta Seconds", p, "Text");
        r.build();
        const Value dt = Value::number(0.25);
        r.vm.fire(*r.inst, "event.tick", &dt, 1);
        check(r.host.joined() == "0.25", "a host event passes its parameters to the event node's pins");
    }
}

/// Several scripts on one VM, each on its own entity (1, 2, ...), the way a
/// scene has them.
struct World : TestHost {
    struct Unit {
        Script                    script;
        Program                   prog;
        std::unique_ptr<Instance> inst;
    };
    Registry reg;
    VM       vm{this};
    std::vector<std::unique_ptr<Unit>> units;
    std::vector<CompileMessage>        msgs;

    World() { register_standard_library(reg); }
    Graph& add() {
        units.push_back(std::make_unique<Unit>());
        units.back()->script.name = "script" + std::to_string(units.size());
        return units.back()->script.add_graph(GraphKind::Event, "EventGraph");
    }
    Script& script(int entity) { return units[static_cast<size_t>(entity - 1)]->script; }
    Instance& inst(int entity) { return *units[static_cast<size_t>(entity - 1)]->inst; }
    bool build() {
        msgs.clear();
        bool ok = true;
        for (size_t i = 0; i < units.size(); ++i) {
            if (!compile(reg, units[i]->script, units[i]->prog, msgs)) { ok = false; continue; }
            units[i]->inst = vm.instantiate(units[i]->prog, static_cast<uint32_t>(i + 1));
        }
        return ok;
    }
    Instance* find_instance(uint32_t e) override {
        return e >= 1 && e <= units.size() ? units[e - 1]->inst.get() : nullptr;
    }
    std::string fire(int entity, const char* event, const Value* args = nullptr, int nargs = 0) {
        out.clear();
        vm.fire(inst(entity), event, args, nargs);
        return joined();
    }
    bool has_error(const std::string& part) const {
        for (const auto& m : msgs) if (m.error && m.text.find(part) != std::string::npos) return true;
        return false;
    }
};

void test_between_scripts() {
    std::printf("-- scripts talking to each other\n");
    {
        // Entity 1 sends "hit" to entity 2, which prints what it was given.
        World w;
        Graph& a = w.add();
        const int go = node(a, "event.custom", "go"), send = node(a, "event.send", "hit"), sent = print_node(a, "sent");
        a.find(send)->params = {Param{"amount", kInt, Value::integer(0)}, Param{"who", kString, Value()}};
        lit(a, send, "Target", Value::entity(2));
        lit(a, send, "amount", Value::integer(5)); lit(a, send, "who", Value::string("orc"));
        chain(a, {go, send, sent});
        const int miss = node(a, "event.custom", "miss"), send2 = node(a, "event.send", "hit"), sent2 = print_node(a, "nobody");
        lit(a, send2, "Target", Value::entity(9));
        chain(a, {miss, send2, sent2});
        const int other = node(a, "event.custom", "other"), send3 = node(a, "event.send", "no such event"), sent3 = print_node(a, "ignored");
        lit(a, send3, "Target", Value::entity(2));
        chain(a, {other, send3, sent3});

        Graph& b = w.add();
        const int hit = node(b, "event.custom", "hit"), p1 = print_node(b), p2 = print_node(b);
        b.find(hit)->params = {Param{"amount", kFloat, Value::number(0)}, Param{"who", kString, Value()}};
        chain(b, {hit, p1, p2});
        b.link(hit, "who", p1, "Text"); b.link(hit, "amount", p2, "Text");

        check(w.build() && w.fire(1, "go") == "orc,5.0,sent",
              "Send Event runs the other entity's event at once, then the sender continues; an Int arrives as the receiver's Float");
        check(w.fire(1, "miss") == "nobody" && w.errors.empty(), "sending to an entity with no script does nothing, without an error");
        check(w.fire(1, "other") == "ignored" && w.errors.empty(), "sending an event the receiver does not have does nothing");
        const Value wrong[2] = {Value::string("x"), Value::string("elf")};
        check(w.fire(2, "hit", wrong, 2) == "elf,0.0", "an argument of the wrong type leaves the parameter at its default");
        w.fire(2, "hit");
        check(w.out.size() == 2 && w.out[0].empty() && w.out[1] == "0.0",
              "arguments that were not given are defaults, not what the last call left behind");

        // The node and its parameter list survive the file.
        Script again;
        check(from_text(to_text(w.script(1)), again) && to_text(again) == to_text(w.script(1)), "a Send Event node round-trips through the file");
    }
    {
        // Reading and writing another script's variable.
        World w;
        Graph& a = w.add();
        const int poke = node(a, "event.custom", "poke"), set = node(a, "var.remote_set", "hp");
        a.find(set)->targ = kInt;
        lit(a, set, "Target", Value::entity(2)); lit(a, set, "Value", Value::integer(42));
        const int get = node(a, "var.remote_get", "hp"), p = print_node(a);
        a.find(get)->targ = kFloat;
        lit(a, get, "Target", Value::entity(2));
        chain(a, {poke, set, p}); a.link(get, "Value", p, "Text");
        const int peek = node(a, "event.custom", "peek"), get2 = node(a, "var.remote_get", "nothing"), p2 = print_node(a);
        a.find(get2)->targ = kInt;
        lit(a, get2, "Target", Value::entity(2));
        a.link(peek, "Then", p2, "In"); a.link(get2, "Value", p2, "Text");
        const int bad = node(a, "event.custom", "bad"), set2 = node(a, "var.remote_set", "hp");
        a.find(set2)->targ = kString;
        lit(a, set2, "Target", Value::entity(2)); lit(a, set2, "Value", Value::string("lots"));
        a.link(bad, "Then", set2, "In");

        w.add();
        w.script(2).add_variable("hp", kFloat, Value::number(10.0));

        check(w.build() && w.fire(1, "poke") == "42.0", "a variable of another entity's script can be set and read back");
        const Value* hp = w.inst(2).variable("hp");
        check(hp && hp->type == kFloat && hp->as_float() == 42.0, "it keeps its own type: the Int became a Float");
        check(w.fire(1, "peek") == "0", "reading a variable the other script does not have gives the default");
        w.fire(1, "bad");
        check(hp->type == kFloat && hp->as_float() == 42.0 && w.errors.empty(), "a value that cannot become the variable's type is not written");
    }
    {
        // Two scripts that send to each other for ever are stopped.
        World w;
        Graph& a = w.add();
        const int e1 = node(a, "event.custom", "ping"), s1 = node(a, "event.send", "ping");
        lit(a, s1, "Target", Value::entity(2)); chain(a, {e1, s1});
        const int ok1 = node(a, "event.custom", "ok"), okp = print_node(a, "fine");
        chain(a, {ok1, okp});
        Graph& b = w.add();
        const int e2 = node(b, "event.custom", "ping"), s2 = node(b, "event.send", "ping");
        lit(b, s2, "Target", Value::entity(1)); chain(b, {e2, s2});
        w.build();
        w.fire(1, "ping");
        check(w.errors.size() == 1 && w.errors[0].find("too deep") != std::string::npos, "two scripts sending to each other without end are stopped with an error");
        check(w.fire(1, "ok") == "fine", "and the scripts still run afterwards");
    }
    {
        // A dispatcher: the door announces, the lamp and the bell listen.
        World w;
        Graph& door = w.add();
        w.script(1).dispatchers.push_back(Dispatcher{"opened", {Param{"who", kString, Value()}}});
        const int open = node(door, "event.custom", "open"), call = node(door, "dispatch.call", "opened"), after = print_node(door, "after");
        lit(door, call, "who", Value::string("door"));
        chain(door, {open, call, after});
        auto listener = [&](const char* tag) {
            Graph& g = w.add();
            const int bp = node(g, "event.begin_play"), bind = node(g, "dispatch.bind", "opened");
            lit(g, bind, "Target", Value::entity(1)); lit(g, bind, "Event", Value::string("heard"));
            const int again = node(g, "dispatch.bind", "opened");   // binding twice is binding once
            lit(g, again, "Target", Value::entity(1)); lit(g, again, "Event", Value::string("heard"));
            chain(g, {bp, bind, again});
            const int heard = node(g, "event.custom", "heard"), cat = node(g, "math.add"), p = print_node(g);
            g.find(heard)->params = {Param{"who", kString, Value()}};
            g.find(cat)->targ = kString;
            lit(g, cat, "A", Value::string(tag));
            g.link(heard, "who", cat, "B"); g.link(cat, "Result", p, "Text"); g.link(heard, "Then", p, "In");
            const int leave = node(g, "event.custom", "leave"), unbind = node(g, "dispatch.unbind", "opened");
            lit(g, unbind, "Target", Value::entity(1)); lit(g, unbind, "Event", Value::string("heard"));
            chain(g, {leave, unbind});
        };
        listener("lamp:");
        listener("bell:");
        const bool built = w.build();
        check(built && w.fire(1, "open") == "after", "calling a dispatcher nobody bound to does nothing");
        w.fire(2, "event.begin_play");
        w.fire(3, "event.begin_play");
        check(w.fire(1, "open") == "lamp:door,bell:door,after",
              "every listener's event runs, in the order they bound, with the call's arguments; then the caller continues");
        w.fire(2, "leave");
        check(w.fire(1, "open") == "bell:door,after", "a listener that unbound is not called");
        w.units[2]->inst.reset();   // the bell's object is gone
        check(w.fire(1, "open") == "after" && w.errors.empty(), "a listener that no longer exists is skipped");
        Script again;
        check(from_text(to_text(w.script(1)), again) && again.dispatchers.size() == 1 && again.dispatchers[0].params.size() == 1 &&
                  to_text(again) == to_text(w.script(1)),
              "a dispatcher and its parameters survive the file");
    }
    {
        // Calling a function of another object's script, and getting its results.
        World w;
        Graph& a = w.add();
        const int ask = node(a, "event.custom", "ask"), call = node(a, "fn.call_remote", "area"), p = print_node(a);
        a.find(call)->params = {Param{"w", kInt, Value::integer(0)}, Param{"h", kInt, Value::integer(0)}};
        a.find(call)->results = {Param{"a", kInt, Value::integer(0)}};
        lit(a, call, "Target", Value::entity(2)); lit(a, call, "w", Value::integer(3)); lit(a, call, "h", Value::integer(4));
        chain(a, {ask, call, p}); a.link(call, "a", p, "Text");
        const int miss = node(a, "event.custom", "miss"), call2 = node(a, "fn.call_remote", "nothing"), p2 = print_node(a);
        a.find(call2)->results = {Param{"r", kString, Value()}, Param{"n", kInt, Value::integer(0)}};
        lit(a, call2, "Target", Value::entity(2));
        chain(a, {miss, call2, p2}); a.link(call2, "n", p2, "Text");
        w.add();
        Graph& f = w.script(2).add_graph(GraphKind::Function, "area");
        f.inputs = {Param{"w", kFloat, Value::number(0)}, Param{"h", kFloat, Value::number(0)}};
        f.outputs = {Param{"a", kFloat, Value::number(0)}, Param{"extra", kString, Value::string("x")}};   // one more than the caller asks for
        const int en = node(f, "fn.entry"), mul = node(f, "math.mul"), ret = node(f, "fn.return");
        f.find(mul)->targ = kFloat;
        f.link(en, "Then", ret, "In"); f.link(en, "w", mul, "A"); f.link(en, "h", mul, "B"); f.link(mul, "Result", ret, "a");
        check(w.build() && w.fire(1, "ask") == "12",
              "a function of another script is called with converted arguments, and its result comes back as the caller's type");
        check(w.fire(1, "miss") == "0" && w.errors.empty(), "calling a function the other script does not have gives the defaults");
        Script again;
        check(from_text(to_text(w.script(1)), again) && to_text(again) == to_text(w.script(1)), "the node's results survive the file");
    }
    {
        World w;
        Graph& a = w.add();
        const int ev = node(a, "event.begin_play"), s = node(a, "event.send"), g = node(a, "var.remote_get"), p = print_node(a);
        chain(a, {ev, s, p}); a.link(g, "Value", p, "Text");
        check(!w.build() && w.has_error("name the event") && w.has_error("name the variable"),
              "a Send Event with no event name and a remote Get with no variable name are compile errors");
    }
}

void test_waits() {
    std::printf("-- waits\n");
    {
        Rig r; Graph& g = r.events();
        r.script.add_variable("who", kString, Value::string("first"));
        const int ev = node(g, "event.begin_play"), a = print_node(g, "start"), d = node(g, "flow.delay");
        lit(g, d, "Duration", Value::number(1.0));
        const int get = node(g, "var.get", "who"), b = print_node(g);
        chain(g, {ev, a, d}); g.link(d, "Completed", b, "In"); g.link(get, "Value", b, "Text");
        r.build();
        std::string s = r.run();
        r.vm.tick(0.6);
        s += "|" + r.host.joined();
        *r.inst->variable("who") = Value::string("later");
        r.host.out.clear();
        r.vm.fire(*r.inst, "event.begin_play");   // while waiting: prints start, the Delay ignores it
        r.vm.tick(0.6);
        check(s == "start|start" && r.host.joined() == "start,later" && r.vm.pending_waits() == 0,
              "Delay resumes once, after its time, and reads values as they are then");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), d = node(g, "flow.retriggerable_delay"), p = print_node(g, "fired");
        lit(g, d, "Duration", Value::number(1.0));
        g.link(ev, "Then", d, "In"); g.link(d, "Completed", p, "In");
        r.build(); r.run();
        r.vm.tick(0.8); r.vm.fire(*r.inst, "event.begin_play"); r.vm.tick(0.8);
        const bool not_yet = r.host.out.empty();
        r.vm.tick(0.3);
        check(not_yet && r.host.joined() == "fired", "a Retriggerable Delay starts over when run again");
    }
    {
        Rig r;
        Graph& f = r.script.add_graph(GraphKind::Function, "bad");
        const int en = node(f, "fn.entry"), d = node(f, "flow.delay");
        f.link(en, "Then", d, "In");
        check(!r.build() && r.has_error("cannot wait"), "a Delay inside a function is a compile error that says why");
    }
}

void test_timeline() {
    std::printf("-- timeline\n");
    auto build = [](Rig& r, const char* curve, bool loop) {
        Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), tl = node(g, "flow.timeline", curve);
        const int up = print_node(g), fin = print_node(g, "done");
        lit(g, tl, "Duration", Value::number(2.0));
        lit(g, tl, "Loop", Value::boolean(loop));
        g.link(ev, "Then", tl, "Play from Start");
        g.link(tl, "Update", up, "In"); g.link(tl, "Alpha", up, "Text"); g.link(tl, "Finished", fin, "In");
        const int rev = node(g, "event.custom", "back"), stop = node(g, "event.custom", "halt"), go = node(g, "event.custom", "go");
        g.link(rev, "Then", tl, "Reverse"); g.link(stop, "Then", tl, "Stop"); g.link(go, "Then", tl, "Play");
        return tl;
    };
    {
        Rig r;
        build(r, "", false);
        const bool ok = r.build();
        r.run();
        const bool quiet = r.host.out.empty() && r.vm.running_timelines() == 1;
        for (int i = 0; i < 5; ++i) r.vm.tick(0.5);
        check(ok && quiet && r.host.joined() == "0.25,0.5,0.75,1.0,done" && r.vm.running_timelines() == 0,
              "a Timeline runs Update every frame with Alpha going 0 to 1, then Finished once, and stops");
        r.host.out.clear();
        r.vm.fire(*r.inst, "back");
        r.vm.tick(0.5); r.vm.tick(0.5);
        r.vm.fire(*r.inst, "halt");
        r.vm.tick(0.5);
        const bool held = r.host.joined() == "0.75,0.5";
        r.vm.fire(*r.inst, "go");
        r.vm.tick(0.5); r.vm.tick(0.5); r.vm.tick(0.5);
        check(held && r.host.joined() == "0.75,0.5,0.75,1.0,done", "Reverse runs it back from where it is, Stop holds it, Play goes on from there");
    }
    {
        Rig r;
        build(r, "ease in", false);
        r.build(); r.run();
        r.vm.tick(1.0);
        check(r.host.joined() == "0.25", "the curve shapes Alpha: ease in is at a quarter when half the time has passed");
    }
    {
        Rig r;
        build(r, "", true);
        r.build(); r.run();
        for (int i = 0; i < 6; ++i) r.vm.tick(0.5);
        check(r.host.joined() == "0.25,0.5,0.75,0.0,0.25,0.5" && r.vm.running_timelines() == 1,
              "a looping Timeline wraps round and never finishes");
        r.vm.forget(*r.inst);
        check(r.vm.running_timelines() == 0, "forgetting an instance stops its timelines");
    }
    {
        Rig r;
        Graph& f = r.script.add_graph(GraphKind::Function, "f");
        const int en = node(f, "fn.entry"), tl = node(f, "flow.timeline");
        f.link(en, "Then", tl, "Play");
        check(!r.build() && r.has_error("cannot wait"), "a Timeline in a function is a compile error, like a Delay");
    }
}

void test_errors() {
    std::printf("-- broken graphs\n");
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), br = node(g, "flow.branch"), mk = node(g, "vec3.make");
        g.link(ev, "Then", br, "In"); g.link(mk, "Result", br, "Condition");
        check(!r.build() && r.has_error("cannot connect vec3"), "a wire between incompatible types is refused: " + r.first_error());
    }
    {
        Rig r; Graph& g = r.events();
        node(g, "no.such.node");
        check(!r.build() && r.has_error("unknown node kind"), "an unknown node kind is an error, not a crash");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), a = print_node(g, "a"), b = print_node(g, "b");
        g.link(ev, "Then", a, "In"); g.link(ev, "Then", b, "In");
        check(!r.build() && r.has_error("two wires"), "two wires out of one flow pin are refused");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), a = node(g, "math.add"), b = node(g, "math.add"), p = print_node(g);
        g.link(a, "Result", b, "A"); g.link(b, "Result", a, "A");
        g.link(ev, "Then", p, "In"); g.link(a, "Result", p, "Text");
        check(!r.build() && r.has_error("loop"), "data wires that form a loop are refused");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), wh = node(g, "flow.while"), p = print_node(g, "x");
        lit(g, wh, "Condition", Value::boolean(true));
        g.link(ev, "Then", wh, "In"); g.link(wh, "Loop Body", p, "In");
        r.build();
        r.vm.instruction_budget = 5000;
        r.run();
        check(r.host.errors.size() == 1 && r.host.errors[0].find("never ends") != std::string::npos,
              "a loop that never ends is stopped and reported");
        r.host.errors.clear();
        check(r.vm.fire(*r.inst, "event.begin_play") && r.host.errors.size() == 1, "and the VM is usable afterwards");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), dv = node(g, "math.div"), p = print_node(g);
        g.find(dv)->targ = kInt; lit(g, dv, "A", Value::integer(5));
        g.link(ev, "Then", p, "In"); g.link(dv, "Result", p, "Text");
        check(r.build() && r.run() == "0" && r.host.errors.size() == 1, "dividing by zero gives 0 and reports the node");
    }
    {
        Rig r;
        Graph& f = r.script.add_graph(GraphKind::Function, "forever");
        const int en = node(f, "fn.entry"), c = node(f, "fn.call", "forever");
        f.link(en, "Then", c, "In");
        Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), c2 = node(g, "fn.call", "forever");
        g.link(ev, "Then", c2, "In");
        r.build(); r.run();
        check(r.host.errors.size() == 1 && r.host.errors[0].find("too deep") != std::string::npos,
              "a function that calls itself without end is stopped");
    }
}

void test_text() {
    std::printf("-- text format\n");
    Rig r; Graph& g = r.events();
    r.script.add_variable("hp", kFloat, Value::number(100)).exposed = true;
    r.script.add_variable("names", Type(Kind::String, true), Value::array(kString, {Value::string("a\tb")}));
    const int ev = node(g, "event.begin_play"), sw = node(g, "flow.switch_int");
    g.find(sw)->cases = {"1", "2"};
    const int ce = node(g, "event.custom", "go");
    g.find(ce)->params = {Param{"v", kVec3, Value::vec3({1, 2, 3})}};
    const int p = print_node(g, "line one\nline two");
    g.find(p)->comment = "says hello";
    g.link(ev, "Then", sw, "In"); g.link(sw, "2", p, "In"); g.link(ce, "Then", p, "In");
    Graph& f = r.script.add_graph(GraphKind::Function, "f");
    f.inputs = {Param{"x", kInt, Value::integer(3)}};
    f.locals.push_back(Variable{"tmp", kString, Value::string("t"), false, "", ""});
    node(f, "fn.entry");

    const std::string text = to_text(r.script);
    Script back;
    std::string err;
    const bool ok = from_text(text, back, &err);
    check(ok, "a script reads back from its own text" + (ok ? std::string() : ": " + err));
    check(to_text(back) == text, "and writes the same text again");
    Program a, b;
    std::vector<CompileMessage> m;
    const bool ca = compile(r.reg, r.script, a, m), cb = compile(r.reg, back, b, m);
    check(ca && cb && disassemble(a) == disassemble(b), "the reloaded script compiles to the same program");
    Script junk;
    check(!from_text("LOGIC 3\n", junk, &err) && !err.empty(), "text that is not a script is refused with a reason");
}

void test_editing() {
    std::printf("-- editing: knots, comment boxes, copy and paste\n");
    {
        // A knot on a flow wire passes control along; one on a data wire passes the value.
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), k1 = node(g, "flow.reroute"), p = print_node(g);
        g.find(k1)->targ = kExec;
        const int add = node(g, "math.add"), k2 = node(g, "flow.reroute");
        g.find(add)->targ = kInt; g.find(k2)->targ = kInt;
        lit(g, add, "A", Value::integer(2)); lit(g, add, "B", Value::integer(3));
        g.link(ev, "Then", k1, "In"); g.link(k1, "Out", p, "In");
        g.link(add, "Result", k2, "In"); g.link(k2, "Out", p, "Text");
        check(r.build() && r.run() == "5", "a knot passes flow on a flow wire and the value on a data wire");
    }
    {
        Rig r; Graph& g = r.events();
        const int ev = node(g, "event.begin_play"), p = print_node(g, "x");
        chain(g, {ev, p});
        CommentBox b;
        b.id = g.next_id++;
        b.x = 10; b.y = -20.5f; b.w = 400; b.h = 220;
        b.text = "what this\tgroup does\nsecond line";
        b.color[0] = 0.9f; b.color[1] = 0.2f; b.color[2] = 0.1f;
        g.boxes.push_back(b);
        Script back;
        const std::string text = to_text(r.script);
        const bool read = from_text(text, back);
        const bool same = read && back.graphs.size() == 1 && back.graphs[0].boxes.size() == 1 &&
                          back.graphs[0].boxes[0].text == b.text && back.graphs[0].boxes[0].y == b.y &&
                          back.graphs[0].boxes[0].w == 400 && back.graphs[0].boxes[0].color[0] == 0.9f && to_text(back) == text;
        check(same, "a comment box survives the file: place, size, colour, text with a tab and a line break");
        check(back.graphs[0].next_id > b.id, "its id is not handed out again after loading");
        check(r.build() && r.run() == "x", "and the compiler does not see it");
    }
    {
        // Copy two wired nodes out of one graph and paste them into another script.
        Rig a; Graph& g = a.events();
        const int ev = node(g, "event.begin_play"), add = node(g, "math.add"), p = print_node(g);
        g.find(add)->targ = kInt;
        lit(g, add, "A", Value::integer(40)); lit(g, add, "B", Value::integer(2));
        g.find(add)->x = 300; g.find(add)->y = 100; g.find(p)->x = 500; g.find(p)->y = 80;
        chain(g, {ev, p}); g.link(add, "Result", p, "Text");
        const std::string clip = selection_text(g, {add, p});

        Rig b; Graph& h = b.events();
        const int ev2 = node(h, "event.begin_play");
        node(h, "math.add"); node(h, "math.add");   // so the ids in the target differ from the source's
        const std::vector<int> made = paste_selection(h, clip, 1000.0f, 2000.0f);
        bool placed = made.size() == 2;
        int  new_print = 0;
        if (placed) {
            const Node* n0 = h.find(made[0]);
            const Node* n1 = h.find(made[1]);
            placed = n0 && n1 && n0->x == 1000.0f && n0->y == 2020.0f && n1->x == 1200.0f && n1->y == 2000.0f && made[0] != add;
            new_print = n1 && n1->def == "print" ? n1->id : 0;
        }
        check(placed, "pasted nodes get new ids and keep their layout, top-left corner where asked");
        if (new_print) h.link(ev2, "Then", new_print, "In");
        check(b.build() && b.run() == "42", "the wire between the copied nodes and their literals came along; the wire to a node left behind did not");
        check(paste_selection(h, "hello", 0, 0).empty() && paste_selection(h, to_text(a.script), 0, 0).empty(),
              "other clipboard text, or a whole script, is not pasted");
        const std::vector<int> twice = paste_selection(h, clip, 0, 0);
        check(twice.size() == 2 && twice[0] != made[0] && twice[1] != made[1], "pasting again makes another copy");
    }
    {
        Rig r;
        Graph& f = r.script.add_graph(GraphKind::Function, "f");
        const int en = node(f, "fn.entry"), ret = node(f, "fn.return");
        const size_t before = f.nodes.size();
        const std::vector<int> made = paste_selection(f, selection_text(f, {en, ret}), 0, 0);
        check(made.size() == 1 && f.nodes.size() == before + 1, "a function's entry node is not duplicated by paste");
    }
}

void test_debugger() {
    std::printf("-- pausing\n");
    Rig r; Graph& g = r.events();
    const int ev = node(g, "event.begin_play"), a = print_node(g, "a"), b = print_node(g, "b"), c = print_node(g, "c");
    chain(g, {ev, a, b, c});
    r.build();
    r.vm.set_breakpoint(&r.prog, 0, b, true);
    r.run();
    check(r.vm.state() == RunState::Paused && r.vm.paused_at().node == b && r.host.joined() == "a",
          "a breakpoint stops before its node runs");
    check(!r.vm.fire(*r.inst, "event.begin_play"), "nothing else runs while paused");
    r.vm.resume(VM::Step::Over);
    check(r.vm.state() == RunState::Paused && r.vm.paused_at().node == c && r.host.joined() == "a,b",
          "Step runs one node and stops at the next");
    r.vm.resume();
    check(r.vm.state() == RunState::Idle && r.host.joined() == "a,b,c", "Continue runs to the end");
    r.vm.clear_breakpoints();
    check(r.run() == "a,b,c", "with the breakpoint cleared the event runs through");

    // Stepping off the end of an event is "continue": the next event runs through.
    r.vm.set_breakpoint(&r.prog, 0, c, true);
    r.run();
    r.vm.resume(VM::Step::Over);
    r.vm.clear_breakpoints();
    check(r.vm.state() == RunState::Idle && r.run() == "a,b,c", "stepping past the last node does not stop the next event at its first");

    // A paused run can be dropped (the script is going away).
    r.vm.set_breakpoint(&r.prog, 0, b, true);
    r.run();
    r.vm.abort();
    r.vm.clear_breakpoints();
    check(r.vm.state() == RunState::Idle && r.run() == "a,b,c", "a paused run can be aborted, and the VM is usable afterwards");

    {
        // A node fed by pure nodes: its code is interleaved with theirs, and
        // it must still stop once, and step to the NEXT node.
        Rig q; Graph& h = q.events();
        const int e2 = node(h, "event.begin_play"), p1 = print_node(h), p2 = print_node(h, "z");
        const int add = node(h, "math.add"), mul = node(h, "math.mul"), cat = node(h, "math.add");
        h.find(add)->targ = kInt; h.find(mul)->targ = kInt; h.find(cat)->targ = kString;
        lit(h, add, "A", Value::integer(1)); lit(h, add, "B", Value::integer(2));
        lit(h, mul, "B", Value::integer(5));
        lit(h, cat, "A", Value::string("n="));
        chain(h, {e2, p1, p2});
        h.link(add, "Result", mul, "A"); h.link(mul, "Result", cat, "B"); h.link(cat, "Result", p1, "Text");
        const bool built = q.build();
        q.vm.set_breakpoint(&q.prog, 0, p1, true);
        q.run();
        const bool once = q.vm.state() == RunState::Paused && q.vm.paused_at().node == p1 && q.host.joined().empty();
        q.vm.resume(VM::Step::Over);
        check(built && once && q.vm.state() == RunState::Paused && q.vm.paused_at().node == p2 && q.host.joined() == "n=15",
              "a node fed by pure nodes stops once, before any of it runs, and Step goes to the next node");
        q.vm.resume();
        check(q.vm.state() == RunState::Idle && q.host.joined() == "n=15,z", "Continue from there finishes the run");
        q.vm.clear_breakpoints();
        q.vm.set_breakpoint(&q.prog, 0, e2, true);
        q.run();
        check(q.vm.state() == RunState::Paused && q.vm.paused_at().node == e2 && q.host.joined().empty(),
              "a breakpoint on the event node itself stops before anything runs");
        q.vm.resume(VM::Step::Over);
        check(q.vm.state() == RunState::Paused && q.vm.paused_at().node == p1, "and Step from it goes to the first node");
        q.vm.resume();
    }
    {
        // What is on a pin: a flow node's outputs are readable through the debug information.
        Rig q; Graph& h = q.events();
        q.script.add_variable("n", kInt, Value::integer(4));
        const int e2 = node(h, "event.custom", "go"), set = node(h, "var.set", "n"), p2 = print_node(h, "done");
        h.find(e2)->params = {Param{"amount", kInt, Value::integer(0)}};
        chain(h, {e2, set, p2}); h.link(e2, "amount", set, "Value");
        q.build();
        q.vm.set_breakpoint(&q.prog, 0, p2, true);
        const Value arg = Value::integer(31);
        q.vm.fire(*q.inst, "go", &arg, 1);
        const NodeOutputs* ev_out = q.prog.find_outputs(0, e2);
        const NodeOutputs* set_out = q.prog.find_outputs(0, set);
        const Value* amount = ev_out ? q.vm.frame_register(*q.inst, ev_out->fn, ev_out->base) : nullptr;
        const Value* stored = set_out ? q.vm.frame_register(*q.inst, set_out->fn, set_out->base) : nullptr;
        check(q.vm.state() == RunState::Paused && amount && amount->type == kInt && amount->as_int() == 31 && stored &&
                  stored->as_int() == 31,
              "while paused, the values on a flow node's output pins can be read");
        q.vm.resume();
        check(q.host.joined() == "done" && q.vm.frame_register(*q.inst, ev_out->fn, ev_out->base)->as_int() == 31,
              "and after the run too: an event graph keeps them");
    }
    {
        // Tracing: which nodes ran, and when.
        Rig q; Graph& h = q.events();
        const int e2 = node(h, "event.begin_play"), br = node(h, "flow.branch"), yes = print_node(h, "yes"), no = print_node(h, "no");
        lit(h, br, "Condition", Value::boolean(true));
        h.link(e2, "Then", br, "In"); h.link(br, "True", yes, "In"); h.link(br, "False", no, "In");
        q.build();
        q.run();
        check(q.vm.last_run(*q.inst, 0, yes) < 0.0, "nothing is traced while tracing is off");
        q.vm.trace = true;
        q.vm.trace_time = 5.0;
        q.run();
        q.vm.trace_time = 9.0;
        check(q.vm.last_run(*q.inst, 0, br) == 5.0 && q.vm.last_run(*q.inst, 0, yes) == 5.0 && q.vm.last_run(*q.inst, 0, no) < 0.0,
              "with tracing on, the nodes that ran are stamped and the branch not taken is not");
    }
}

void test_speed() {
    std::printf("-- speed (informational)\n");
    Rig r; Graph& g = r.events();
    r.script.add_variable("sum", kFloat);
    const int ev = node(g, "event.begin_play"), fl = node(g, "flow.for");
    lit(g, fl, "Last Index", Value::integer(199999));
    const int get = node(g, "var.get", "sum"), add = node(g, "math.add"), set = node(g, "var.set", "sum");
    g.link(ev, "Then", fl, "In"); g.link(fl, "Loop Body", set, "In");
    g.link(get, "Value", add, "A"); g.link(fl, "Index", add, "B"); g.link(add, "Result", set, "Value");
    r.build();
    r.vm.instruction_budget = 100'000'000;
    const auto t0 = std::chrono::steady_clock::now();
    r.run();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const double sum = r.inst->variable("sum")->as_float();
    check(sum == 199999.0 * 200000.0 / 2.0, "200,000 loop passes compute the right sum");
    std::printf("     200,000 passes (get, convert, add, set) in %.1f ms = %.0f ns per pass\n", ms, ms * 1e6 / 200000.0);
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== vscript_check: visual scripting core ===\n");
    test_values();
    test_flow();
    test_state_nodes();
    test_pure_and_variables();
    test_functions_and_events();
    test_between_scripts();
    test_waits();
    test_timeline();
    test_errors();
    test_text();
    test_editing();
    test_debugger();
    test_speed();
    if (g_fail) {
        std::printf("vscript_check: %d FAILED, %d passed\n", g_fail, g_pass);
        return 1;
    }
    std::printf("vscript_check: ALL OK (%d checks)\n", g_pass);
    return 0;
}
