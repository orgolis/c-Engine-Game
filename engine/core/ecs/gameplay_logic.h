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
#include <vector>

namespace schizo::ecs {

enum class LogicNodeKind : uint32_t {
    // ---- events (sources) ----
    OnStart   = 0,   // fires once when the graph starts
    OnEvent   = 1,   // fires when a named bus event is published (param = event name)
    OnKey     = 2,   // fires when a key is pressed (param = GLFW key code), fed by the editor
    // ---- actions (sinks) ----
    EmitEvent = 100, // publish a bus event (param = event name)
    SetFlag   = 101, // set a WorldFlag (param = key, param2 = int value)
    Log       = 102, // log param
};
inline bool logic_is_event(LogicNodeKind k) { return static_cast<uint32_t>(k) < 100; }

struct LogicNode {
    int           id = 0;
    LogicNodeKind kind = LogicNodeKind::OnStart;
    std::string   param;    // event name / key / flag key / message
    std::string   param2;   // flag value (SetFlag)
    float         x = 0.0f, y = 0.0f;   // editor canvas position
};
struct LogicLink { int from = 0; int to = 0; };   // event node id -> action node id

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

// Run every action wired from `event_node_id`.
inline void logic_fire(LogicGraph& g, World& w, GameplayEventBus& bus, Entity flags_entity, int event_node_id) {
    for (const auto& l : g.links) {
        if (l.from != event_node_id) continue;
        LogicNode* a = g.find(l.to);
        if (!a || logic_is_event(a->kind)) continue;
        switch (a->kind) {
            case LogicNodeKind::EmitEvent: { GameplayEvent ev; ev.name = a->param; ev.target = flags_entity; bus.publish(ev); break; }
            case LogicNodeKind::SetFlag: {
                if (flags_entity != null_entity && !w.has<WorldFlags>(flags_entity)) w.add<WorldFlags>(flags_entity, WorldFlags{});
                if (auto* wf = w.try_get<WorldFlags>(flags_entity)) set_world_flag(*wf, a->param, a->param2.empty() ? 1 : std::atoi(a->param2.c_str()));
                break;
            }
            case LogicNodeKind::Log: { GameplayEvent ev; ev.name = "logic.log"; ev.param = a->param; bus.publish(ev); break; }
            default: break;
        }
    }
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

    void start(LogicGraph& g, World& w, GameplayEventBus& b, Entity flags_e) {
        stop();
        graph = &g; world = &w; bus = &b; flags_entity = flags_e;
        for (auto& n : g.nodes) if (n.kind == LogicNodeKind::OnStart) logic_fire(g, w, b, flags_e, n.id);
        for (auto& n : g.nodes) if (n.kind == LogicNodeKind::OnEvent) {
            const int nid = n.id;
            subs.push_back(b.subscribe(n.param, [this, nid](const GameplayEvent&) {
                if (graph) logic_fire(*graph, *world, *bus, flags_entity, nid);
            }));
        }
    }
    void on_key(int key) {
        if (!graph) return;
        const std::string k = std::to_string(key);
        for (auto& n : graph->nodes) if (n.kind == LogicNodeKind::OnKey && n.param == k)
            logic_fire(*graph, *world, *bus, flags_entity, n.id);
    }
    void stop() {
        if (bus) for (auto s : subs) bus->unsubscribe(s);
        subs.clear(); graph = nullptr; world = nullptr; bus = nullptr;
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
