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
    SetFlag    = 101, // set a WorldFlag (param = key, param2 = int value)
    Log        = 102, // log param
    ClearFlag  = 103, // set a WorldFlag to 0 (param = key)
    ToggleFlag = 104, // flip a WorldFlag 0<->1 (param = key)
    // ---- flow (200+) ----
    Branch     = 200, // propagate downstream only if a flag condition holds (param=key, param2=cond)
    DoOnce     = 201, // propagate the FIRST time it's hit each run, then block (reset on restart)
    Delay      = 202, // propagate downstream after `param` seconds (driven by the runtime tick)
};
inline bool logic_is_event(LogicNodeKind k)  { return static_cast<uint32_t>(k) < 100; }
inline bool logic_is_branch(LogicNodeKind k) { return k == LogicNodeKind::Branch; }
// Events are fired by the runtime (no input pin); everything else can be wired
// into (input pin). Every node can drive downstream (output pin), so actions and
// branches chain: On Event -> Branch -> Set Flag -> Log.
inline bool logic_has_input(LogicNodeKind k) { return !logic_is_event(k); }

struct LogicNode {
    int           id = 0;
    LogicNodeKind kind = LogicNodeKind::OnStart;
    std::string   param;    // event name / key / flag key / message / interval seconds
    std::string   param2;   // flag value (SetFlag) / condition (Branch, OnFlag)
    float         x = 0.0f, y = 0.0f;   // editor canvas position
};
struct LogicLink { int from = 0; int to = 0; };   // node id -> node id (outputs -> inputs)

struct LogicGraph {
    std::vector<LogicNode> nodes;
    std::vector<LogicLink> links;
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
    void link(int from, int to) {
        for (const auto& l : links) if (l.from == from && l.to == to) return;
        links.push_back({from, to});
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

// Execute a single ACTION node's effect (no propagation).
inline void logic_do_action(World& w, GameplayEventBus& bus, Entity flags_entity, LogicNode& a) {
    auto ensure_flags = [&]() -> WorldFlags* {
        if (flags_entity == null_entity) return nullptr;
        if (!w.has<WorldFlags>(flags_entity)) w.add<WorldFlags>(flags_entity, WorldFlags{});
        return w.try_get<WorldFlags>(flags_entity);
    };
    switch (a.kind) {
        case LogicNodeKind::EmitEvent:  { GameplayEvent ev; ev.name = a.param; ev.target = flags_entity; bus.publish(ev); break; }
        case LogicNodeKind::SetFlag:    { if (auto* wf = ensure_flags()) set_world_flag(*wf, a.param, a.param2.empty() ? 1 : std::atoi(a.param2.c_str())); break; }
        case LogicNodeKind::Log:        { GameplayEvent ev; ev.name = "logic.log"; ev.param = a.param; bus.publish(ev); break; }
        case LogicNodeKind::ClearFlag:  { if (auto* wf = ensure_flags()) set_world_flag(*wf, a.param, 0); break; }
        case LogicNodeKind::ToggleFlag: { if (auto* wf = ensure_flags()) set_world_flag(*wf, a.param, world_flag(*wf, a.param) ? 0 : 1); break; }
        default: break;
    }
}

// Per-run state needed by stateful flow nodes. Supplied by the LogicRuntime; the
// free logic_fire() passes an empty ctx, so Do Once / Delay act as pass-throughs
// in the stateless case (e.g. simple unit tests).
struct LogicActivationCtx {
    std::vector<std::pair<int, float>>* pending       = nullptr;  // Delay: (delay-node id, remaining sec)
    std::vector<int>*                   once_consumed = nullptr;  // Do Once: node ids already fired this run
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
        case LogicNodeKind::Branch:
            proceed = logic_eval_condition(w, flags_entity, n->param, n->param2);
            break;
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
            if (!logic_is_event(n->kind)) logic_do_action(w, bus, flags_entity, *n);
            break;
    }
    if (!proceed) return;
    for (const auto& l : g.links)
        if (l.from == node_id) logic_activate(g, w, bus, flags_entity, l.to, visited, ctx);
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

    LogicActivationCtx actx() { return LogicActivationCtx{&pending, &once_consumed}; }
    // Fire a node's downstream chain with this run's flow state (Do Once / Delay).
    void fire(int node_id) {
        std::vector<int> visited;
        logic_activate(*graph, *world, *bus, flags_entity, node_id, visited, actx());
    }

    void start(LogicGraph& g, World& w, GameplayEventBus& b, Entity flags_e) {
        stop();
        graph = &g; world = &w; bus = &b; flags_entity = flags_e;
        tick_accum.clear(); flag_state.clear(); pending.clear(); once_consumed.clear();
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
                    if (l.from == delay_id)
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
        subs.clear(); tick_accum.clear(); flag_state.clear(); pending.clear(); once_consumed.clear();
        graph = nullptr; world = nullptr; bus = nullptr;
    }
    bool running() const { return graph != nullptr; }
};

// ---------------- text serialization (scene sidecar) ----------------
inline std::string logic_to_text(const LogicGraph& g) {
    std::string s = "LOGIC " + std::to_string(g.next_id) + "\n";
    for (const auto& n : g.nodes)
        s += "N\t" + std::to_string(n.id) + "\t" + std::to_string(static_cast<uint32_t>(n.kind)) + "\t"
           + std::to_string(n.x) + "\t" + std::to_string(n.y) + "\t" + n.param + "\t" + n.param2 + "\n";
    for (const auto& l : g.links)
        s += "L\t" + std::to_string(l.from) + "\t" + std::to_string(l.to) + "\n";
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
            g.links.push_back({std::atoi(f[1].c_str()), std::atoi(f[2].c_str())});
        }
    }
    return g;
}

}  // namespace schizo::ecs
