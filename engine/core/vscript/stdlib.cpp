// ============================================================================
// vscript/stdlib.cpp -- the nodes that need no engine.
// ============================================================================
//
// Flow control, variables, functions, events, maths, vectors, strings, arrays.
// A host registers its own nodes (entities, physics, gameplay) on top.

#include "vscript/program.h"
#include "vscript/vm.h"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cmath>

namespace schizo::vs {

namespace {

NodeDef builtin(const char* id, const char* category, const char* title, Builtin b, bool pure = false) {
    NodeDef d;
    d.id = id;
    d.category = category;
    d.title = title;
    d.builtin = b;
    d.pure = pure;
    return d;
}

void flow(Registry& r) {
    const char* C = "Flow Control";
    r.add(builtin("flow.branch", C, "Branch", Builtin::Branch))
        .in("In", kExec).in("Condition", kBool).out("True", kExec).out("False", kExec)
        .tip("Runs True when the condition holds, False otherwise.").keys("if");
    r.add(builtin("flow.sequence", C, "Sequence", Builtin::Sequence))
        .in("In", kExec).tip("Runs each output in order, one after the other.");
    r.add(builtin("flow.for", C, "For Loop", Builtin::ForLoop))
        .in("In", kExec).in("First Index", kInt).in("Last Index", kInt)
        .out("Loop Body", kExec).out("Index", kInt).out("Completed", kExec)
        .tip("Runs Loop Body once for each index from First to Last, inclusive.");
    r.add(builtin("flow.for_break", C, "For Loop with Break", Builtin::ForLoopBreak))
        .in("In", kExec).in("First Index", kInt).in("Last Index", kInt).in("Break", kExec)
        .out("Loop Body", kExec).out("Index", kInt).out("Completed", kExec);
    r.add(builtin("flow.for_each", C, "For Each", Builtin::ForEach))
        .in("In", kExec).in("Array", Type(Kind::Any, true))
        .out("Loop Body", kExec).out("Element", kAny).out("Index", kInt).out("Completed", kExec)
        .tip("Runs Loop Body once for each element of the array.");
    r.add(builtin("flow.for_each_break", C, "For Each with Break", Builtin::ForEachBreak))
        .in("In", kExec).in("Array", Type(Kind::Any, true)).in("Break", kExec)
        .out("Loop Body", kExec).out("Element", kAny).out("Index", kInt).out("Completed", kExec);
    r.add(builtin("flow.while", C, "While Loop", Builtin::While))
        .in("In", kExec).in("Condition", kBool).out("Loop Body", kExec).out("Completed", kExec)
        .tip("Runs Loop Body for as long as the condition holds.");
    r.add(builtin("flow.do_once", C, "Do Once", Builtin::DoOnce))
        .in("In", kExec).in("Reset", kExec).in("Start Closed", kBool).out("Completed", kExec)
        .tip("Lets the flow through once, then blocks until Reset.");
    r.add(builtin("flow.do_n", C, "Do N", Builtin::DoN))
        .in("In", kExec).in("N", kInt, Value::integer(1)).in("Reset", kExec).out("Exit", kExec).out("Counter", kInt)
        .tip("Lets the flow through N times, then blocks until Reset.");
    r.add(builtin("flow.flip_flop", C, "Flip Flop", Builtin::FlipFlop))
        .in("In", kExec).out("A", kExec).out("B", kExec).out("Is A", kBool)
        .tip("Alternates between A and B each time it runs.");
    r.add(builtin("flow.gate", C, "Gate", Builtin::Gate))
        .in("Enter", kExec).in("Open", kExec).in("Close", kExec).in("Toggle", kExec).in("Start Closed", kBool)
        .out("Exit", kExec).tip("Passes Enter through to Exit only while open.");
    r.add(builtin("flow.multi_gate", C, "Multi Gate", Builtin::MultiGate))
        .in("In", kExec).in("Reset", kExec).in("Loop", kBool).in("Start Index", kInt)
        .tip("Each time it runs, the next output fires.");
    r.add(builtin("flow.switch_int", C, "Switch on Int", Builtin::SwitchInt))
        .in("In", kExec).in("Selection", kInt);
    r.add(builtin("flow.switch_string", C, "Switch on String", Builtin::SwitchString))
        .in("In", kExec).in("Selection", kString);
    {
        NodeDef d = builtin("flow.select", C, "Select", Builtin::Select, true);
        d.default_any = kFloat;
        r.add(std::move(d)).in("Index", kInt).tip("Picks one of the options by index.");
    }
    {
        NodeDef d = builtin("flow.select_bool", C, "Select (Bool)", Builtin::SelectBool, true);
        d.default_any = kFloat;
        r.add(std::move(d)).in("Condition", kBool).in("False", kAny).in("True", kAny).out("Result", kAny);
    }
    {
        NodeDef d = builtin("flow.delay", C, "Delay", Builtin::Delay);
        d.latent = true;
        r.add(std::move(d)).in("In", kExec).in("Duration", kFloat, Value::number(0.2)).out("Completed", kExec)
            .tip("Continues after Duration seconds. Running it again while it waits does nothing.");
    }
    {
        NodeDef d = builtin("flow.retriggerable_delay", C, "Retriggerable Delay", Builtin::RetriggerableDelay);
        d.latent = true;
        r.add(std::move(d)).in("In", kExec).in("Duration", kFloat, Value::number(0.2)).out("Completed", kExec)
            .tip("Continues after Duration seconds. Running it again starts the wait over.");
    }
    {
        NodeDef d = builtin("flow.timeline", C, "Timeline", Builtin::Timeline);
        d.latent = true;
        r.add(std::move(d))
            .in("Play", kExec).in("Play from Start", kExec).in("Stop", kExec).in("Reverse", kExec).in("Reverse from End", kExec)
            .in("Duration", kFloat, Value::number(1.0)).in("Loop", kBool)
            .out("Update", kExec).out("Finished", kExec).out("Alpha", kFloat).out("Time", kFloat)
            .tip("Runs a value from 0 to 1 over Duration seconds. Update runs every frame while it moves, with Alpha (0 to 1, "
                 "shaped by the curve) and Time (seconds); Finished runs when it arrives. Reverse runs it back. Feed Alpha to a "
                 "Lerp to move, fade or turn something.")
            .keys("animate tween curve lerp over time");
    }
    r.add(builtin("flow.reroute", C, "Reroute", Builtin::Reroute, true)).in("In", kAny).out("Out", kAny);

    r.add(builtin("var.get", "Variables", "Get", Builtin::VarGet, true));
    r.add(builtin("var.set", "Variables", "Set", Builtin::VarSet)).in("In", kExec).out("Then", kExec);
    r.add(builtin("local.get", "Variables", "Get Local", Builtin::LocalGet, true));
    r.add(builtin("local.set", "Variables", "Set Local", Builtin::LocalSet)).in("In", kExec).out("Then", kExec);
    r.add(builtin("array.make", "Array", "Make Array", Builtin::MakeArray, true)).default_any = kFloat;
    r.add(builtin("self", "Utilities", "Self", Builtin::Self, true)).out("Self", kEntity)
        .tip("The entity this script is on.");

    r.add(builtin("fn.entry", "Functions", "Function Entry", Builtin::FnEntry));
    r.add(builtin("fn.return", "Functions", "Return", Builtin::FnReturn));
    r.add(builtin("fn.call", "Functions", "Call Function", Builtin::FnCall));
    r.add(builtin("event.custom", "Events", "Custom Event", Builtin::CustomEvent)).out("Then", kExec);
    r.add(builtin("event.call", "Events", "Call Event", Builtin::CallEvent)).in("In", kExec).out("Then", kExec);
    r.add(builtin("event.send", "Events", "Send Event to Entity", Builtin::SendEvent))
        .in("In", kExec).in("Target", kEntity).out("Then", kExec)
        .tip("Runs the custom event with this name on another entity's logic script, now. Nothing happens if that entity has no such event. Arguments are matched to the event's parameters by position.");
    {
        NodeDef d = builtin("var.remote_get", "Variables", "Get Variable of Entity", Builtin::RemoteGet, true);
        d.default_any = kFloat;
        r.add(std::move(d)).in("Target", kEntity).out("Value", kAny)
            .tip("Reads a variable of another entity's logic script. Gives the type's default if there is none.");
    }
    {
        NodeDef d = builtin("var.remote_set", "Variables", "Set Variable of Entity", Builtin::RemoteSet);
        d.default_any = kFloat;
        r.add(std::move(d)).in("In", kExec).in("Target", kEntity).in("Value", kAny).out("Then", kExec);
    }
    r.add(builtin("dispatch.call", "Events", "Call Dispatcher", Builtin::DispatchCall)).in("In", kExec).out("Then", kExec)
        .tip("Runs the event every listener bound to this dispatcher, in the order they bound, then continues.");
    {
        NodeDef d = builtin("dispatch.bind", "Events", "Bind to Dispatcher", Builtin::DispatchBind);
        d.named = true;
        d.name_label = "Dispatcher";
        r.add(std::move(d)).in("In", kExec).in("Target", kEntity).in("Event", kString).out("Then", kExec)
            .tip("From now on, when Target's script calls that dispatcher, the custom event of THIS script named in Event runs. "
                 "Target left unwired means this object. Binding the same event twice does nothing.")
            .keys("listen subscribe delegate");
    }
    {
        NodeDef d = builtin("dispatch.unbind", "Events", "Unbind from Dispatcher", Builtin::DispatchUnbind);
        d.named = true;
        d.name_label = "Dispatcher";
        r.add(std::move(d)).in("In", kExec).in("Target", kEntity).in("Event", kString).out("Then", kExec)
            .keys("stop listening unsubscribe");
    }
    r.add(builtin("fn.call_remote", "Functions", "Call Function of Entity", Builtin::RemoteCall))
        .in("In", kExec).in("Target", kEntity).out("Then", kExec)
        .tip("Calls the function with this name in another object's logic script and waits for it. The node lists what it hands "
             "over and what it expects back; both are matched by position and converted. If the object has no such function "
             "the results are their defaults.");
    r.add(builtin("event.begin_play", "Events", "Begin Play", Builtin::Event)).out("Then", kExec)
        .tip("Runs once when play starts for this entity.");
    r.add(builtin("event.tick", "Events", "Tick", Builtin::Event)).out("Then", kExec).out("Delta Seconds", kFloat)
        .tip("Runs every frame.");
    r.add(builtin("event.end_play", "Events", "End Play", Builtin::Event)).out("Then", kExec);
}

// A wildcard operator node: A op B -> Result, one implementation per type.
NodeDef op2(const char* id, const char* category, const char* title, Type result = kAny) {
    NodeDef d;
    d.id = id;
    d.category = category;
    d.title = title;
    d.pure = true;
    d.compact = true;
    d.default_any = kFloat;
    d.in("A", kAny).in("B", kAny).out("Result", result);
    return d;
}

template <class F> NativeFn vec_binary(F f) {
    return [f](Context&, Value* a, Value* o) {
        Value r = a[0];
        const int n = a[0].type.kind == Kind::Vec2 ? 2 : a[0].type.kind == Kind::Vec3 ? 3 : 4;
        for (int i = 0; i < n; ++i) r.u.v[i] = f(a[0].u.v[i], a[1].u.v[i]);
        o[0] = r;
    };
}

void math(Registry& r) {
    const char* M = "Math";
    auto add_vec = [](NodeDef& d, NativeFn f) {
        d.overload(kVec2, f).overload(kVec3, f).overload(kVec4, f);
    };
    {
        NodeDef& d = r.add(op2("math.add", M, "+")).keys("add plus sum");
        d.overload(kInt, nullptr, OP_ADDI).overload(kFloat, nullptr, OP_ADDF);
        add_vec(d, vec_binary([](float a, float b) { return a + b; }));
        d.overload(kString, [](Context&, Value* a, Value* o) { o[0] = Value::string(a[0].str() + a[1].str()); });
    }
    {
        NodeDef& d = r.add(op2("math.sub", M, "-")).keys("subtract minus");
        d.overload(kInt, nullptr, OP_SUBI).overload(kFloat, nullptr, OP_SUBF);
        add_vec(d, vec_binary([](float a, float b) { return a - b; }));
    }
    {
        NodeDef& d = r.add(op2("math.mul", M, "*")).keys("multiply times");
        d.overload(kInt, nullptr, OP_MULI).overload(kFloat, nullptr, OP_MULF);
        add_vec(d, vec_binary([](float a, float b) { return a * b; }));
    }
    {
        NodeDef& d = r.add(op2("math.div", M, "/")).keys("divide");
        d.overload(kInt, nullptr, OP_DIVI).overload(kFloat, nullptr, OP_DIVF);
        add_vec(d, vec_binary([](float a, float b) { return b != 0.0f ? a / b : 0.0f; }));
    }
    r.add(op2("math.mod", M, "%")).keys("modulo remainder").overload(kInt, nullptr, OP_MODI)
        .overload(kFloat, [](Context&, Value* a, Value* o) {
            o[0] = Value::number(a[1].u.f != 0.0 ? std::fmod(a[0].u.f, a[1].u.f) : 0.0);
        });
    r.add(op2("math.less", M, "<", kBool)).overload(kInt, nullptr, OP_LTI).overload(kFloat, nullptr, OP_LTF);
    r.add(op2("math.less_equal", M, "<=", kBool)).overload(kInt, nullptr, OP_LEI).overload(kFloat, nullptr, OP_LEF);
    r.add(op2("math.greater", M, ">", kBool)).overload(kInt, nullptr, OP_GTI).overload(kFloat, nullptr, OP_GTF);
    r.add(op2("math.greater_equal", M, ">=", kBool)).overload(kInt, nullptr, OP_GEI).overload(kFloat, nullptr, OP_GEF);
    // Equality works on every type: no overload list, one opcode.
    r.add(op2("math.equal", M, "==", kBool)).keys("equals same").op = OP_EQ;
    r.add(op2("math.not_equal", M, "!=", kBool)).keys("different").op = OP_NE;
    {
        NodeDef d;
        d.id = "math.negate"; d.category = M; d.title = "Negate"; d.pure = true; d.default_any = kFloat;
        d.in("A", kAny).out("Result", kAny);
        d.overload(kInt, nullptr, OP_NEGI).overload(kFloat, nullptr, OP_NEGF);
        r.add(std::move(d));
    }

    const char* B = "Math|Bool";
    r.pure("bool.and", B, "AND", [](bool a, bool b) { return a && b; }).as_compact().op = OP_AND;
    r.pure("bool.or", B, "OR", [](bool a, bool b) { return a || b; }).as_compact().op = OP_OR;
    r.pure("bool.xor", B, "XOR", [](bool a, bool b) { return a != b; }).as_compact().op = OP_XOR;
    r.pure("bool.not", B, "NOT", [](bool a) { return !a; }).as_compact().op = OP_NOT;

    const char* F = "Math|Float";
    r.pure("float.abs", F, "Abs", [](double a) { return std::fabs(a); });
    r.pure("float.sqrt", F, "Sqrt", [](double a) { return a > 0.0 ? std::sqrt(a) : 0.0; });
    r.pure("float.pow", F, "Power", [](double a, double b) { return std::pow(a, b); }, {"Base", "Exponent"});
    r.pure("float.min", F, "Min", [](double a, double b) { return std::min(a, b); });
    r.pure("float.max", F, "Max", [](double a, double b) { return std::max(a, b); });
    r.pure("float.clamp", F, "Clamp", [](double v, double lo, double hi) { return std::min(std::max(v, lo), hi); },
           {"Value", "Min", "Max"});
    r.pure("float.lerp", F, "Lerp", [](double a, double b, double t) { return a + (b - a) * t; }, {"A", "B", "Alpha"});
    r.pure("float.floor", F, "Floor", [](double a) { return static_cast<int64_t>(std::floor(a)); });
    r.pure("float.ceil", F, "Ceil", [](double a) { return static_cast<int64_t>(std::ceil(a)); });
    r.pure("float.round", F, "Round", [](double a) { return static_cast<int64_t>(std::llround(a)); });
    r.pure("float.truncate", F, "Truncate", [](double a) { return static_cast<int64_t>(std::trunc(a)); });
    r.pure("float.sin", F, "Sin", [](double a) { return std::sin(a); }, {"Radians"});
    r.pure("float.cos", F, "Cos", [](double a) { return std::cos(a); }, {"Radians"});
    r.pure("float.tan", F, "Tan", [](double a) { return std::tan(a); }, {"Radians"});
    r.pure("float.atan2", F, "Atan2", [](double y, double x) { return std::atan2(y, x); }, {"Y", "X"});
    r.pure("float.to_radians", F, "Degrees to Radians", [](double d) { return d * 0.017453292519943295; });
    r.pure("float.to_degrees", F, "Radians to Degrees", [](double d) { return d * 57.29577951308232; });
    r.pure("float.sign", F, "Sign", [](double a) { return a > 0.0 ? 1.0 : a < 0.0 ? -1.0 : 0.0; });
    r.pure("float.nearly_equal", F, "Nearly Equal",
           [](double a, double b, double tol) { return std::fabs(a - b) <= tol; }, {"A", "B", "Tolerance"});
    r.pure("float.in_range", F, "In Range", [](double v, double lo, double hi) { return v >= lo && v <= hi; },
           {"Value", "Min", "Max"});
    r.pure("float.map_range", F, "Map Range",
           [](double v, double a0, double a1, double b0, double b1) {
               return a1 != a0 ? b0 + (v - a0) / (a1 - a0) * (b1 - b0) : b0;
           },
           {"Value", "In Min", "In Max", "Out Min", "Out Max"});
    r.pure("float.random", F, "Random Float", [](Context& c) { return c.vm->random_unit(); })
        .tip("A number from 0 up to, not including, 1.");
    r.pure("float.random_range", F, "Random Float in Range",
           [](Context& c, double lo, double hi) { return lo + (hi - lo) * c.vm->random_unit(); }, {"Min", "Max"});

    const char* I = "Math|Int";
    r.pure("int.abs", I, "Abs (Int)", [](int64_t a) { return a < 0 ? -a : a; });
    r.pure("int.min", I, "Min (Int)", [](int64_t a, int64_t b) { return std::min(a, b); });
    r.pure("int.max", I, "Max (Int)", [](int64_t a, int64_t b) { return std::max(a, b); });
    r.pure("int.clamp", I, "Clamp (Int)",
           [](int64_t v, int64_t lo, int64_t hi) { return std::min(std::max(v, lo), hi); }, {"Value", "Min", "Max"});
    r.pure("int.random_range", I, "Random Int in Range",
           [](Context& c, int64_t lo, int64_t hi) {
               if (hi < lo) std::swap(lo, hi);
               const uint64_t span = static_cast<uint64_t>(hi - lo) + 1;
               return span == 0 ? lo : lo + static_cast<int64_t>(c.vm->next_random() % span);
           },
           {"Min", "Max"}).tip("A whole number from Min to Max, both included.");

    const char* V = "Math|Vector";
    r.pure("vec3.make", V, "Make Vector",
           [](double x, double y, double z) { return glm::vec3(float(x), float(y), float(z)); }, {"X", "Y", "Z"});
    r.pure("vec3.break", V, "Break Vector",
           [](glm::vec3 v) { return std::tuple<double, double, double>(v.x, v.y, v.z); }, {"Vector", "X", "Y", "Z"});
    r.pure("vec2.make", V, "Make Vector 2", [](double x, double y) { return glm::vec2(float(x), float(y)); }, {"X", "Y"});
    r.pure("vec2.break", V, "Break Vector 2", [](glm::vec2 v) { return std::tuple<double, double>(v.x, v.y); },
           {"Vector", "X", "Y"});
    r.pure("vec3.scale", V, "Scale Vector", [](glm::vec3 v, double s) { return v * float(s); }, {"Vector", "Scale"});
    r.pure("vec3.dot", V, "Dot", [](glm::vec3 a, glm::vec3 b) { return double(glm::dot(a, b)); });
    r.pure("vec3.cross", V, "Cross", [](glm::vec3 a, glm::vec3 b) { return glm::cross(a, b); });
    r.pure("vec3.length", V, "Length", [](glm::vec3 a) { return double(glm::length(a)); });
    r.pure("vec3.distance", V, "Distance", [](glm::vec3 a, glm::vec3 b) { return double(glm::distance(a, b)); });
    r.pure("vec3.normalize", V, "Normalize",
           [](glm::vec3 a) { const float l = glm::length(a); return l > 1e-8f ? a / l : glm::vec3(0.0f); });
    r.pure("vec3.lerp", V, "Lerp (Vector)", [](glm::vec3 a, glm::vec3 b, double t) { return glm::mix(a, b, float(t)); },
           {"A", "B", "Alpha"});
    r.pure("quat.from_euler", "Math|Rotation", "Rotation from Euler",
           [](glm::vec3 deg) { return glm::quat(glm::radians(deg)); }, {"Degrees"});
    r.pure("quat.to_euler", "Math|Rotation", "Rotation to Euler",
           [](glm::quat q) { return glm::degrees(glm::eulerAngles(q)); }, {"Rotation", "Degrees"});
    r.pure("quat.rotate", "Math|Rotation", "Rotate Vector", [](glm::quat q, glm::vec3 v) { return q * v; },
           {"Rotation", "Vector"});
    r.pure("quat.combine", "Math|Rotation", "Combine Rotations", [](glm::quat a, glm::quat b) { return b * a; },
           {"First", "Then"});
    r.pure("transform.make", "Math|Transform", "Make Transform",
           [](glm::vec3 p, glm::quat q, glm::vec3 s) { return TransformData{p, q, s}; },
           {"Position", "Rotation", "Scale"});
    r.pure("transform.break", "Math|Transform", "Break Transform",
           [](TransformData t) { return std::make_tuple(t.position, t.rotation, t.scale); },
           {"Transform", "Position", "Rotation", "Scale"});
}

void strings(Registry& r) {
    const char* S = "String";
    r.pure("string.length", S, "Length", [](std::string s) { return static_cast<int64_t>(s.size()); });
    r.pure("string.contains", S, "Contains",
           [](std::string s, std::string sub) { return s.find(sub) != std::string::npos; }, {"String", "Substring"});
    r.pure("string.find", S, "Find",
           [](std::string s, std::string sub) {
               const size_t p = s.find(sub);
               return p == std::string::npos ? int64_t(-1) : static_cast<int64_t>(p);
           },
           {"String", "Substring", "Index"});
    r.pure("string.substring", S, "Substring",
           [](std::string s, int64_t start, int64_t len) {
               if (start < 0) start = 0;
               if (len < 0 || static_cast<size_t>(start) >= s.size()) return std::string();
               return s.substr(static_cast<size_t>(start), static_cast<size_t>(len));
           },
           {"String", "Start", "Length"});
    r.pure("string.replace", S, "Replace",
           [](std::string s, std::string from, std::string to) {
               if (from.empty()) return s;
               for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
               return s;
           },
           {"String", "From", "To"});
    r.pure("string.upper", S, "To Upper", [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    });
    r.pure("string.lower", S, "To Lower", [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    });
    r.pure("string.split", S, "Split",
           [](std::string s, std::string sep) {
               std::vector<std::string> out;
               if (sep.empty()) { out.push_back(s); return out; }
               size_t start = 0;
               for (size_t p; (p = s.find(sep, start)) != std::string::npos; start = p + sep.size())
                   out.push_back(s.substr(start, p - start));
               out.push_back(s.substr(start));
               return out;
           },
           {"String", "Separator", "Parts"});
    r.pure("string.join", S, "Join",
           [](std::vector<std::string> parts, std::string sep) {
               std::string out;
               for (size_t i = 0; i < parts.size(); ++i) { if (i) out += sep; out += parts[i]; }
               return out;
           },
           {"Parts", "Separator"});
    r.pure("string.to_int", S, "String to Int", [](std::string s) {
        Value v;
        return parse_literal(kInt, s, v) ? v.as_int() : int64_t(0);
    });
    r.pure("string.to_float", S, "String to Float", [](std::string s) {
        Value v;
        return parse_literal(kFloat, s, v) ? v.as_float() : 0.0;
    });
    r.fn("print", "Utilities", "Print",
         [](Context& c, std::string text) { if (c.host && c.instance) c.host->print(*c.instance, text); }, {"Text"})
        .keys("log debug").tip("Writes a line to the output log.");
}

Value& array_arg(Value* a) { return a[0]; }

NodeDef array_node(const char* id, const char* title, bool pure) {
    NodeDef d;
    d.id = id;
    d.category = "Array";
    d.title = title;
    d.pure = pure;
    d.default_any = kFloat;
    if (!pure) d.in("In", kExec);
    d.in("Array", Type(Kind::Any, true));
    return d;
}

void arrays(Registry& r) {
    const Type A(Kind::Any, true);
    r.add(array_node("array.length", "Length (Array)", true)).out("Length", kInt).fn =
        [](Context&, Value* a, Value* o) { o[0] = Value::integer(static_cast<int64_t>(a[0].items().size())); };
    r.add(array_node("array.get", "Get (Array)", true)).in("Index", kInt).out("Element", kAny).fn =
        [](Context& c, Value* a, Value* o) {
            const auto&   it = a[0].items();
            const int64_t i = a[1].as_int();
            if (i < 0 || i >= static_cast<int64_t>(it.size())) {
                c.error("array index " + std::to_string(i) + " is outside 0.." + std::to_string(it.size()) + "; the default is used");
                o[0] = Value::default_of(a[0].type.element());
            } else {
                o[0] = it[static_cast<size_t>(i)];
            }
        };
    r.add(array_node("array.contains", "Contains (Array)", true)).in("Item", kAny).out("Result", kBool).fn =
        [](Context&, Value* a, Value* o) {
            bool found = false;
            for (const Value& v : a[0].items()) if (equals(v, a[1])) { found = true; break; }
            o[0] = Value::boolean(found);
        };
    r.add(array_node("array.find", "Find (Array)", true)).in("Item", kAny).out("Index", kInt).fn =
        [](Context&, Value* a, Value* o) {
            int64_t at = -1;
            const auto& it = a[0].items();
            for (size_t i = 0; i < it.size(); ++i) if (equals(it[i], a[1])) { at = static_cast<int64_t>(i); break; }
            o[0] = Value::integer(at);
        };
    r.add(array_node("array.add", "Add", false)).in("Item", kAny).out("Then", kExec).out("Index", kInt).fn =
        [](Context&, Value* a, Value* o) {
            auto& it = array_arg(a).items();
            it.push_back(clone(a[1]));
            o[0] = Value::integer(static_cast<int64_t>(it.size()) - 1);
        };
    r.add(array_node("array.set", "Set Element", false)).in("Index", kInt).in("Item", kAny).out("Then", kExec).fn =
        [](Context& c, Value* a, Value*) {
            auto&         it = array_arg(a).items();
            const int64_t i = a[1].as_int();
            if (i < 0 || i >= static_cast<int64_t>(it.size())) c.error("array index " + std::to_string(i) + " is out of range; nothing was set");
            else it[static_cast<size_t>(i)] = clone(a[2]);
        };
    r.add(array_node("array.remove_at", "Remove Index", false)).in("Index", kInt).out("Then", kExec).fn =
        [](Context& c, Value* a, Value*) {
            auto&         it = array_arg(a).items();
            const int64_t i = a[1].as_int();
            if (i < 0 || i >= static_cast<int64_t>(it.size())) c.error("array index " + std::to_string(i) + " is out of range; nothing was removed");
            else it.erase(it.begin() + static_cast<std::ptrdiff_t>(i));
        };
    r.add(array_node("array.remove", "Remove Item", false)).in("Item", kAny).out("Then", kExec).out("Removed", kBool).fn =
        [](Context&, Value* a, Value* o) {
            auto&        it = array_arg(a).items();
            const size_t before = it.size();
            it.erase(std::remove_if(it.begin(), it.end(), [&](const Value& v) { return equals(v, a[1]); }), it.end());
            o[0] = Value::boolean(it.size() != before);
        };
    r.add(array_node("array.clear", "Clear", false)).out("Then", kExec).fn =
        [](Context&, Value* a, Value*) { array_arg(a).items().clear(); };
    (void)A;
}

}  // namespace

void register_standard_library(Registry& reg) {
    flow(reg);
    math(reg);
    strings(reg);
    arrays(reg);
}

}  // namespace schizo::vs
