#pragma once

// ============================================================================
// Scene logic graph — a Blueprint-style node graph of events -> actions.
// ============================================================================
//
// Author per-scene logic as nodes: EVENT nodes (On Start / On Event / On Key)
// fire, and every ACTION node wired from them runs (Emit Event / Set Flag / Log).
// It composes over the existing substrate: actions publish to the GameplayEventBus
// (which drives triggers, quests, combat, …) or write WorldFlags (G9 world state),
// so a scene can react to gameplay without code. The runtime is bus-driven +
// deterministic; the visual editor authors the same data.

#include "world.h"
#include "gameplay_events.h"
#include "gameplay_savegame.h"   // WorldFlags (Set Flag action)
#include "component_serialize.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace schizo::ecs {

enum class LogicNodeKind : uint32_t {
    // ---- events (sources, id < 100) — runtime-fired, output pin only ----
    OnStart    = 0,   // fires once when the graph starts
    OnEvent    = 1,   // fires when a named bus event is published (param = event name)
    OnKey      = 2,   // fires when a key is pressed (param = GLFW key code), fed by the editor
    OnKeyUp    = 3,   // fires when a key is released (param = GLFW key code)
    OnTick     = 4,   // fires every `param` seconds (repeating timer)
    OnFlag     = 5,   // fires on the rising edge of a flag condition (param = key, param2 = cond)
    // ---- actions (sinks, 100..199) — have input AND output pins (chainable) ----
    EmitEvent  = 100, // publish a bus event (param = event name)
    SetFlag    = 101, // set a WorldFlag (param = key, param2 = int value / data pin 1)
    Log        = 102, // log param (or data pin 1 as string)
    ClearFlag  = 103, // set a WorldFlag to 0 (param = key)
    ToggleFlag = 104, // flip a WorldFlag 0<->1 (param = key)
    SetVar     = 105, // write a graph variable (param = name; value from data pin 1)
    // ---- flow (200+) ----
    Branch     = 200, // propagate downstream only if the condition holds (data pin 1, else param=key/param2=cond)
    DoOnce     = 201, // propagate the FIRST time it's hit each run, then block (reset on restart)
    Delay      = 202, // propagate downstream after `param` seconds (driven by the runtime tick)
    // ---- data / pure (300+): no exec pins, evaluated on demand into an input pin ----
    LiteralBool   = 300, // param = 0/1
    LiteralInt    = 301, // param = integer literal
    LiteralFloat  = 302, // param = float literal
    LiteralString = 303, // param = string literal
    GetVar        = 310, // read a graph variable (param = name)
    Add           = 320, // pin1 + pin2 (numeric)
    Sub           = 321, // pin1 - pin2
    Mul           = 322, // pin1 * pin2
    Div           = 323, // pin1 / pin2 (0 guard)
    Compare       = 330, // pin1 <op> pin2 -> bool (param2 = op: == != > < >= <=)
    AndNode       = 331, // pin1 && pin2 -> bool
    OrNode        = 332, // pin1 || pin2 -> bool
    NotNode       = 333, // !pin1 -> bool
};
inline bool logic_is_event(LogicNodeKind k)  { return static_cast<uint32_t>(k) < 100; }
inline bool logic_is_branch(LogicNodeKind k) { return k == LogicNodeKind::Branch; }
// Data / pure nodes (300+) have no exec pins — they are pulled on demand to feed a
// value into another node's data input pin, never wired into the exec chain.
inline bool logic_is_data(LogicNodeKind k)   { return static_cast<uint32_t>(k) >= 300; }
// Events are fired by the runtime (no input pin); actions/flow have an exec input.
// Every exec node can drive downstream (exec output). Data nodes have neither.
inline bool logic_has_input(LogicNodeKind k) { return !logic_is_event(k) && !logic_is_data(k); }

struct LogicNode {
    int           id = 0;
    LogicNodeKind kind = LogicNodeKind::OnStart;
    std::string   param;    // event name / key / flag key / message / interval / literal / var name
    std::string   param2;   // flag value (SetFlag) / condition (Branch, OnFlag, Compare)
    float         x = 0.0f, y = 0.0f;   // editor canvas position
};

// A typed value that flows along a data pin. One variant, four types; every type
// coerces to every other so a graph never hard-errors on a mismatched wire.
enum class LogicValueType : uint8_t { Bool = 0, Int = 1, Float = 2, String = 3 };
struct LogicValue {
    LogicValueType type = LogicValueType::Int;
    int         i = 0;
    float       f = 0.0f;
    std::string s;

    static LogicValue make_bool(bool b)             { LogicValue v; v.type = LogicValueType::Bool;   v.i = b ? 1 : 0; return v; }
    static LogicValue make_int(int x)               { LogicValue v; v.type = LogicValueType::Int;    v.i = x;         return v; }
    static LogicValue make_float(float x)           { LogicValue v; v.type = LogicValueType::Float;  v.f = x;         return v; }
    static LogicValue make_string(std::string x)    { LogicValue v; v.type = LogicValueType::String; v.s = std::move(x); return v; }

    bool  as_bool()  const {
        switch (type) { case LogicValueType::Bool: case LogicValueType::Int: return i != 0;
                        case LogicValueType::Float: return f != 0.0f;
                        case LogicValueType::String: return !s.empty() && s != "0" && s != "false"; }
        return false;
    }
    int   as_int()   const {
        switch (type) { case LogicValueType::Bool: case LogicValueType::Int: return i;
                        case LogicValueType::Float: return static_cast<int>(f);
                        case LogicValueType::String: return std::atoi(s.c_str()); }
        return 0;
    }
    float as_float() const {
        switch (type) { case LogicValueType::Bool: case LogicValueType::Int: return static_cast<float>(i);
                        case LogicValueType::Float: return f;
                        case LogicValueType::String: return static_cast<float>(std::atof(s.c_str())); }
        return 0.0f;
    }
    std::string as_string() const {
        switch (type) { case LogicValueType::Bool: return i ? "true" : "false";
                        case LogicValueType::Int:   return std::to_string(i);
                        case LogicValueType::Float: return std::to_string(f);
                        case LogicValueType::String: return s; }
        return {};
    }
    bool is_numeric() const { return type != LogicValueType::String; }
};

struct LogicVariable { std::string name; LogicValue value; };
using LogicVarStore = std::vector<LogicVariable>;
inline LogicValue* logic_find_var(LogicVarStore& vars, const std::string& name) {
    for (auto& v : vars) if (v.name == name) return &v.value;
    return nullptr;
}

// A link carries a value from an output pin to an input pin. Exec links use pin 0
// on both ends (the default). Data links target a specific input slot (1,2,…) on
// the receiver; from_pin is the source node's output slot (0 for single-output data nodes).
struct LogicLink { int from = 0; int to = 0; int from_pin = 0; int to_pin = 0; };
inline bool logic_link_is_exec(const LogicLink& l) { return l.from_pin == 0 && l.to_pin == 0; }
inline bool logic_link_is_data(const LogicLink& l) { return l.to_pin != 0; }

struct LogicGraph {
    std::vector<LogicNode>     nodes;
    std::vector<LogicLink>     links;
    std::vector<LogicVariable> variables;   // graph-scoped typed variables (Blueprint-style)
    int next_id = 1;

    int add_node(LogicNodeKind k, float x, float y) {
        LogicNode n; n.id = next_id++; n.kind = k; n.x = x; n.y = y;
        nodes.push_back(n); return n.id;
    }
    LogicNode* find(int id) { for (auto& n : nodes) if (n.id == id) return &n; return nullptr; }
    void remove_node(int id) {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const LogicNode& n){ return n.id == id; }), nodes.end());
        links.erase(std::remove_if(links.begin(), links.end(), [&](const LogicLink& l){ return l.from == id || l.to == id; }), links.end());
    }
    void link(int from, int to) {   // exec link
        for (const auto& l : links) if (l.from == from && l.to == to && logic_link_is_exec(l)) return;
        links.push_back({from, to, 0, 0});
    }
    // Wire a data node's output into a receiver's input slot. Only one wire per
    // input slot: a new one replaces the old (Blueprint semantics for input pins).
    void link_data(int from, int from_pin, int to, int to_pin) {
        links.erase(std::remove_if(links.begin(), links.end(),
                    [&](const LogicLink& l){ return l.to == to && l.to_pin == to_pin && l.to_pin != 0; }), links.end());
        LogicLink l; l.from = from; l.from_pin = from_pin; l.to = to; l.to_pin = to_pin;
        links.push_back(l);
    }
    LogicVariable* find_var(const std::string& name) {
        for (auto& v : variables) if (v.name == name) return &v;
        return nullptr;
    }
    bool empty() const { return nodes.empty(); }
};

// Evaluate a flag condition: `cond` is an optional operator (==,!=,>,<,>=,<=)
// followed by an integer; empty means "== 1" (truthy). Reads flag `key`.
inline bool logic_eval_condition(World& w, Entity flags_entity,
                                 const std::string& key, const std::string& cond) {
    int cur = 0;
    if (auto* wf = w.try_get<WorldFlags>(flags_entity)) cur = world_flag(*wf, key);
    std::string c = cond;
    const size_t b = c.find_first_not_of(" \t");
    c = (b == std::string::npos) ? std::string() : c.substr(b);
    std::string op = "==";
    if      (c.rfind(">=", 0) == 0) { op = ">="; c = c.substr(2); }
    else if (c.rfind("<=", 0) == 0) { op = "<="; c = c.substr(2); }
    else if (c.rfind("==", 0) == 0) { op = "=="; c = c.substr(2); }
    else if (c.rfind("!=", 0) == 0) { op = "!="; c = c.substr(2); }
    else if (c.rfind(">",  0) == 0) { op = ">";  c = c.substr(1); }
    else if (c.rfind("<",  0) == 0) { op = "<";  c = c.substr(1); }
    const int val = c.empty() ? 1 : std::atoi(c.c_str());
    if (op == "==") return cur == val;
    if (op == "!=") return cur != val;
    if (op == ">")  return cur >  val;
    if (op == "<")  return cur <  val;
    if (op == ">=") return cur >= val;
    if (op == "<=") return cur <= val;
    return cur == val;
}

// ---------------- data-pin evaluation (pure, pull-based) ----------------
// Evaluate a data node to a value, following its input wires. `guard` is the DFS
// path (passed by value so siblings don't see each other — only a true cycle, a
// node reachable from itself, short-circuits to a default). Needs the runtime
// variable store for GetVar.
inline LogicValue logic_eval_value(LogicGraph& g, LogicVarStore& vars, int node_id, std::vector<int> guard);

// Pull the value wired into input `slot` of `node_id`. Returns false (out
// untouched) when nothing is wired there, so callers fall back to the node's param.
inline bool logic_input_value(LogicGraph& g, LogicVarStore& vars, int node_id, int slot, LogicValue& out) {
    for (const auto& l : g.links) {
        if (l.to == node_id && l.to_pin == slot && l.to_pin != 0) {
            out = logic_eval_value(g, vars, l.from, std::vector<int>{});
            return true;
        }
    }
    return false;
}

inline LogicValue logic_eval_value(LogicGraph& g, LogicVarStore& vars, int node_id, std::vector<int> guard) {
    if (std::find(guard.begin(), guard.end(), node_id) != guard.end()) return LogicValue::make_int(0);
    guard.push_back(node_id);
    LogicNode* n = g.find(node_id);
    if (!n) return LogicValue::make_int(0);

    auto in = [&](int slot) -> LogicValue {
        for (const auto& l : g.links)
            if (l.to == node_id && l.to_pin == slot && l.to_pin != 0)
                return logic_eval_value(g, vars, l.from, guard);
        return LogicValue::make_int(0);
    };

    switch (n->kind) {
        case LogicNodeKind::LiteralBool:   return LogicValue::make_bool(std::atoi(n->param.c_str()) != 0 || n->param == "true");
        case LogicNodeKind::LiteralInt:    return LogicValue::make_int(std::atoi(n->param.c_str()));
        case LogicNodeKind::LiteralFloat:  return LogicValue::make_float(static_cast<float>(std::atof(n->param.c_str())));
        case LogicNodeKind::LiteralString: return LogicValue::make_string(n->param);
        case LogicNodeKind::GetVar: {
            if (auto* pv = logic_find_var(vars, n->param)) return *pv;
            return LogicValue::make_int(0);
        }
        case LogicNodeKind::Add: case LogicNodeKind::Sub:
        case LogicNodeKind::Mul: case LogicNodeKind::Div: {
            LogicValue a = in(1), b = in(2);
            const bool flt = a.type == LogicValueType::Float || b.type == LogicValueType::Float;
            if (flt) {
                const float x = a.as_float(), y = b.as_float();
                switch (n->kind) { case LogicNodeKind::Add: return LogicValue::make_float(x + y);
                                   case LogicNodeKind::Sub: return LogicValue::make_float(x - y);
                                   case LogicNodeKind::Mul: return LogicValue::make_float(x * y);
                                   default: return LogicValue::make_float(y != 0.0f ? x / y : 0.0f); }
            }
            const int x = a.as_int(), y = b.as_int();
            switch (n->kind) { case LogicNodeKind::Add: return LogicValue::make_int(x + y);
                               case LogicNodeKind::Sub: return LogicValue::make_int(x - y);
                               case LogicNodeKind::Mul: return LogicValue::make_int(x * y);
                               default: return LogicValue::make_int(y != 0 ? x / y : 0); }
        }
        case LogicNodeKind::Compare: {
            LogicValue a = in(1), b = in(2);
            const std::string& op = n->param2;
            if (a.type == LogicValueType::String || b.type == LogicValueType::String) {
                const std::string x = a.as_string(), y = b.as_string();
                if (op == "!=") return LogicValue::make_bool(x != y);
                return LogicValue::make_bool(x == y);   // == default for strings
            }
            const float x = a.as_float(), y = b.as_float();
            if      (op == "!=") return LogicValue::make_bool(x != y);
            else if (op == ">")  return LogicValue::make_bool(x >  y);
            else if (op == "<")  return LogicValue::make_bool(x <  y);
            else if (op == ">=") return LogicValue::make_bool(x >= y);
            else if (op == "<=") return LogicValue::make_bool(x <= y);
            return LogicValue::make_bool(x == y);
        }
        case LogicNodeKind::AndNode: return LogicValue::make_bool(in(1).as_bool() && in(2).as_bool());
        case LogicNodeKind::OrNode:  return LogicValue::make_bool(in(1).as_bool() || in(2).as_bool());
        case LogicNodeKind::NotNode: return LogicValue::make_bool(!in(1).as_bool());
        default: break;
    }
    return LogicValue::make_int(0);
}

// Execute a single ACTION node's effect (no propagation). `vars` (may be null) is
// the runtime variable store used to resolve data-pin inputs and SetVar writes.
inline void logic_do_action(World& w, GameplayEventBus& bus, Entity flags_entity, LogicNode& a,
                            LogicGraph* graph = nullptr, LogicVarStore* vars = nullptr) {
    auto ensure_flags = [&]() -> WorldFlags* {
        if (flags_entity == null_entity) return nullptr;
        if (!w.has<WorldFlags>(flags_entity)) w.add<WorldFlags>(flags_entity, WorldFlags{});
        return w.try_get<WorldFlags>(flags_entity);
    };
    // A data pin wired into input slot `slot` overrides the node's authored param.
    auto pin = [&](int slot, LogicValue& out) -> bool {
        return graph && vars && logic_input_value(*graph, *vars, a.id, slot, out);
    };
    switch (a.kind) {
        case LogicNodeKind::EmitEvent:  {
            LogicValue v; GameplayEvent ev;
            ev.name = pin(1, v) ? v.as_string() : a.param;
            ev.target = flags_entity; bus.publish(ev); break;
        }
        case LogicNodeKind::SetFlag:    {
            LogicValue v;
            const int value = pin(1, v) ? v.as_int() : (a.param2.empty() ? 1 : std::atoi(a.param2.c_str()));
            if (auto* wf = ensure_flags()) set_world_flag(*wf, a.param, value);
            break;
        }
        case LogicNodeKind::Log:        {
            LogicValue v; GameplayEvent ev; ev.name = "logic.log";
            ev.param = pin(1, v) ? v.as_string() : a.param; bus.publish(ev); break;
        }
        case LogicNodeKind::ClearFlag:  { if (auto* wf = ensure_flags()) set_world_flag(*wf, a.param, 0); break; }
        case LogicNodeKind::ToggleFlag: { if (auto* wf = ensure_flags()) set_world_flag(*wf, a.param, world_flag(*wf, a.param) ? 0 : 1); break; }
        case LogicNodeKind::SetVar:     {
            if (!vars || a.param.empty()) break;
            LogicValue v;
            if (!pin(1, v)) v = LogicValue::make_int(a.param2.empty() ? 0 : std::atoi(a.param2.c_str()));
            if (auto* pv = logic_find_var(*vars, a.param)) *pv = v;
            else vars->push_back({a.param, v});
            break;
        }
        default: break;
    }
}

// Per-run state needed by stateful flow nodes. Supplied by the LogicRuntime; the
// free logic_fire() passes an empty ctx, so Do Once / Delay act as pass-throughs
// in the stateless case (e.g. simple unit tests).
struct LogicActivationCtx {
    std::vector<std::pair<int, float>>* pending       = nullptr;  // Delay: (delay-node id, remaining sec)
    std::vector<int>*                   once_consumed = nullptr;  // Do Once: node ids already fired this run
    LogicGraph*                         graph         = nullptr;  // for data-pin evaluation
    LogicVarStore*                      vars          = nullptr;  // runtime variable store (GetVar/SetVar/data pins)
};

// Activate a node: run its action (if any), then propagate to every downstream
// node — UNLESS a flow node stops it (Branch condition false / Do Once already
// fired / Delay defers). `visited` guards against link cycles (each node
// activates at most once per fire).
inline void logic_activate(LogicGraph& g, World& w, GameplayEventBus& bus,
                           Entity flags_entity, int node_id, std::vector<int>& visited,
                           const LogicActivationCtx& ctx = {}) {
    if (std::find(visited.begin(), visited.end(), node_id) != visited.end()) return;
    visited.push_back(node_id);
    LogicNode* n = g.find(node_id);
    if (!n) return;

    bool proceed = true;
    switch (n->kind) {
        case LogicNodeKind::Branch: {
            LogicValue v;
            if (ctx.graph && ctx.vars && logic_input_value(*ctx.graph, *ctx.vars, node_id, 1, v))
                proceed = v.as_bool();                                  // data-pin condition (Blueprint style)
            else
                proceed = logic_eval_condition(w, flags_entity, n->param, n->param2);  // flag fallback
            break;
        }
        case LogicNodeKind::DoOnce:
            if (ctx.once_consumed) {
                if (std::find(ctx.once_consumed->begin(), ctx.once_consumed->end(), node_id) != ctx.once_consumed->end())
                    proceed = false;
                else
                    ctx.once_consumed->push_back(node_id);
            }
            break;
        case LogicNodeKind::Delay:
            if (ctx.pending) {   // scheduled by the runtime; stop propagating now
                ctx.pending->push_back({node_id, std::max(0.0f, static_cast<float>(std::atof(n->param.c_str())))});
                proceed = false;
            }
            break;
        default:
            if (logic_is_data(n->kind)) { proceed = false; break; }   // data nodes are pulled, never activated
            if (!logic_is_event(n->kind)) logic_do_action(w, bus, flags_entity, *n, ctx.graph, ctx.vars);
            break;
    }
    if (!proceed) return;
    for (const auto& l : g.links)
        if (l.from == node_id && logic_link_is_exec(l)) logic_activate(g, w, bus, flags_entity, l.to, visited, ctx);
}

// Fire an event/source node: activate its downstream chain (stateless — Do Once
// and Delay pass through; use LogicRuntime for their real behavior).
inline void logic_fire(LogicGraph& g, World& w, GameplayEventBus& bus, Entity flags_entity, int event_node_id) {
    std::vector<int> visited;
    logic_activate(g, w, bus, flags_entity, event_node_id, visited);
}

// Runs a graph: On Start fires immediately; On Event nodes subscribe to the bus;
// On Key nodes are fired by the editor via on_key(). Bus-driven + re-entrancy-safe
// (actions that publish events are deferred by the bus to the next flush).
struct LogicRuntime {
    LogicGraph*       graph = nullptr;
    World*            world = nullptr;
    GameplayEventBus* bus   = nullptr;
    Entity            flags_entity = null_entity;
    std::vector<GameplayEventBus::SubId> subs;
    std::vector<std::pair<int, float>> tick_accum;    // OnTick node id -> elapsed seconds
    std::vector<std::pair<int, int>>   flag_state;     // OnFlag node id -> last condition truth
    std::vector<std::pair<int, float>> pending;        // Delay: (delay-node id, remaining seconds)
    std::vector<int>                   once_consumed;  // Do Once: node ids already fired this run
    LogicVarStore                      vars;           // live variable values (seeded from graph.variables)

    LogicActivationCtx actx() { return LogicActivationCtx{&pending, &once_consumed, graph, &vars}; }
    // Fire a node's downstream chain with this run's flow state (Do Once / Delay).
    void fire(int node_id) {
        std::vector<int> visited;
        logic_activate(*graph, *world, *bus, flags_entity, node_id, visited, actx());
    }

    void start(LogicGraph& g, World& w, GameplayEventBus& b, Entity flags_e) {
        stop();
        graph = &g; world = &w; bus = &b; flags_entity = flags_e;
        tick_accum.clear(); flag_state.clear(); pending.clear(); once_consumed.clear();
        vars = g.variables;   // reset live values to the authored defaults each run
        for (auto& n : g.nodes) if (n.kind == LogicNodeKind::OnStart) fire(n.id);
        for (auto& n : g.nodes) if (n.kind == LogicNodeKind::OnEvent) {
            const int nid = n.id;
            subs.push_back(b.subscribe(n.param, [this, nid](const GameplayEvent&) {
                if (graph) fire(nid);
            }));
        }
        // Seed repeating-timer accumulators and flag-edge baselines.
        for (auto& n : g.nodes) {
            if (n.kind == LogicNodeKind::OnTick)
                tick_accum.push_back({n.id, 0.0f});
            else if (n.kind == LogicNodeKind::OnFlag)
                flag_state.push_back({n.id, logic_eval_condition(w, flags_e, n.param, n.param2) ? 1 : 0});
        }
    }
    // Drive time-based events. Call once per frame while playing.
    void tick(float dt) {
        if (!graph) return;
        // 1) Fire any Delay nodes whose timer elapsed, then propagate THEIR
        //    downstream (fresh chain). Collect ready ids first so re-scheduling a
        //    chained Delay doesn't disturb this pass.
        std::vector<int> ready;
        for (auto& p : pending) { p.second -= dt; if (p.second <= 0.0f) ready.push_back(p.first); }
        if (!ready.empty()) {
            pending.erase(std::remove_if(pending.begin(), pending.end(),
                          [](const std::pair<int, float>& p) { return p.second <= 0.0f; }), pending.end());
            for (const int delay_id : ready) {
                std::vector<int> visited;
                for (const auto& l : graph->links)
                    if (l.from == delay_id && logic_link_is_exec(l))
                        logic_activate(*graph, *world, *bus, flags_entity, l.to, visited, actx());
            }
        }
        // 2) Repeating timers + flag-edge events.
        for (auto& n : graph->nodes) {
            if (n.kind == LogicNodeKind::OnTick) {
                const float interval = std::max(0.05f, static_cast<float>(std::atof(n.param.c_str())));
                for (auto& a : tick_accum) if (a.first == n.id) {
                    a.second += dt;
                    if (a.second >= interval) { a.second = 0.0f; fire(n.id); }
                }
            } else if (n.kind == LogicNodeKind::OnFlag) {
                const bool now = logic_eval_condition(*world, flags_entity, n.param, n.param2);
                for (auto& s : flag_state) if (s.first == n.id) {
                    if (now && s.second == 0) fire(n.id);  // rising edge
                    s.second = now ? 1 : 0;
                }
            }
        }
    }
    void on_key(int key) {
        if (!graph) return;
        const std::string k = std::to_string(key);
        for (auto& n : graph->nodes) if (n.kind == LogicNodeKind::OnKey && n.param == k) fire(n.id);
    }
    void on_key_up(int key) {
        if (!graph) return;
        const std::string k = std::to_string(key);
        for (auto& n : graph->nodes) if (n.kind == LogicNodeKind::OnKeyUp && n.param == k) fire(n.id);
    }
    void stop() {
        if (bus) for (auto s : subs) bus->unsubscribe(s);
        subs.clear(); tick_accum.clear(); flag_state.clear(); pending.clear(); once_consumed.clear(); vars.clear();
        graph = nullptr; world = nullptr; bus = nullptr;
    }
    // Read a live variable's value by name (editor debug / scripting bridge).
    LogicValue* variable(const std::string& name) { return logic_find_var(vars, name); }
    bool running() const { return graph != nullptr; }
};

// ---------------- text serialization (scene sidecar) ----------------
inline std::string logic_to_text(const LogicGraph& g) {
    std::string s = "LOGIC " + std::to_string(g.next_id) + "\n";
    for (const auto& n : g.nodes)
        s += "N\t" + std::to_string(n.id) + "\t" + std::to_string(static_cast<uint32_t>(n.kind)) + "\t"
           + std::to_string(n.x) + "\t" + std::to_string(n.y) + "\t" + n.param + "\t" + n.param2 + "\n";
    // Links keep the legacy 3-field form for exec wires (from_pin==to_pin==0) so
    // older readers still load them; data wires append the two pin fields.
    for (const auto& l : g.links) {
        s += "L\t" + std::to_string(l.from) + "\t" + std::to_string(l.to);
        if (l.from_pin != 0 || l.to_pin != 0)
            s += "\t" + std::to_string(l.from_pin) + "\t" + std::to_string(l.to_pin);
        s += "\n";
    }
    for (const auto& v : g.variables)
        s += "V\t" + v.name + "\t" + std::to_string(static_cast<int>(v.value.type)) + "\t"
           + std::to_string(v.value.i) + "\t" + std::to_string(v.value.f) + "\t" + v.value.s + "\n";
    return s;
}
inline LogicGraph logic_from_text(const std::string& text) {
    LogicGraph g;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.compare(0, 6, "LOGIC ") == 0) { g.next_id = std::atoi(line.c_str() + 6); continue; }
        // split by tabs
        std::vector<std::string> f; size_t p = 0;
        while (p <= line.size()) { size_t t = line.find('\t', p); f.push_back(line.substr(p, t == std::string::npos ? std::string::npos : t - p)); if (t == std::string::npos) break; p = t + 1; }
        if (f.empty()) continue;
        if (f[0] == "N" && f.size() >= 6) {
            LogicNode n; n.id = std::atoi(f[1].c_str()); n.kind = static_cast<LogicNodeKind>(std::atoi(f[2].c_str()));
            n.x = static_cast<float>(std::atof(f[3].c_str())); n.y = static_cast<float>(std::atof(f[4].c_str()));
            n.param = f[5]; n.param2 = f.size() > 6 ? f[6] : "";
            g.nodes.push_back(std::move(n));
        } else if (f[0] == "L" && f.size() >= 3) {
            LogicLink l; l.from = std::atoi(f[1].c_str()); l.to = std::atoi(f[2].c_str());
            if (f.size() >= 5) { l.from_pin = std::atoi(f[3].c_str()); l.to_pin = std::atoi(f[4].c_str()); }
            g.links.push_back(l);
        } else if (f[0] == "V" && f.size() >= 6) {
            LogicVariable v; v.name = f[1];
            v.value.type = static_cast<LogicValueType>(std::atoi(f[2].c_str()));
            v.value.i = std::atoi(f[3].c_str());
            v.value.f = static_cast<float>(std::atof(f[4].c_str()));
            v.value.s = f[5];
            g.variables.push_back(std::move(v));
        }
    }
    return g;
}

}  // namespace schizo::ecs
