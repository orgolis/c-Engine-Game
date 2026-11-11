#pragma once

// ============================================================================
// vscript/graph.h -- what an author builds: a script of graphs of nodes.
// ============================================================================
//
// A Script is the asset. It owns variables and graphs:
//   - event graphs: nodes wired from event nodes (Begin Play, Tick, custom
//     events). All event graphs of a script compile into one unit that shares
//     one persistent set of registers per instance, so a Delay can resume later
//     with every pin value intact.
//   - functions: a signature, local variables, one entry and any number of
//     return nodes. They get a fresh frame per call and may recurse.
//
// A Node names its definition by a stable string id ("flow.branch",
// "math.add", "var.get"). Links name pins by NAME, not index, so a definition
// can gain or reorder pins without breaking saved graphs.
//
// This header has no behaviour beyond editing the data. What pins a node has
// is answered by shape_of() in registry.h, and what it does by the compiler.

#include "vscript/value.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace schizo::vs {

struct Param {
    std::string name;
    Type        type;
    Value       def;
};

struct Variable {
    std::string name;
    Type        type;
    Value       def;
    bool        exposed = false;   // editable per instance in the inspector
    std::string category;
    std::string tooltip;
};

struct Node {
    int         id = 0;
    std::string def;          // registry id
    std::string name;         // the variable / function / event this node refers to
    float       x = 0.0f, y = 0.0f;
    Type        targ;         // the type a wildcard node was resolved to (None = not yet)
    int         extra = 0;    // number of variadic pins: Sequence outputs, Make Array inputs
    std::vector<std::string> cases;                        // Switch: one label per case output
    std::vector<Param>       params;                       // Custom Event: its parameters
    std::vector<std::pair<std::string, Value>> defaults;   // literals on unlinked input pins
    std::string comment;

    const Value* find_default(const std::string& pin) const {
        for (const auto& d : defaults)
            if (d.first == pin) return &d.second;
        return nullptr;
    }
    void set_default(const std::string& pin, Value v) {
        for (auto& d : defaults)
            if (d.first == pin) {
                d.second = std::move(v);
                return;
            }
        defaults.emplace_back(pin, std::move(v));
    }
};

struct Link {
    int         from = 0;
    std::string from_pin;
    int         to = 0;
    std::string to_pin;
};

enum class GraphKind : uint8_t { Event = 0, Function = 1 };

struct Graph {
    std::string       name;
    GraphKind         kind = GraphKind::Event;
    std::vector<Node> nodes;
    std::vector<Link> links;
    int               next_id = 1;

    // Functions only.
    std::vector<Param>    inputs;
    std::vector<Param>    outputs;
    std::vector<Variable> locals;
    bool                  pure = false;   // no exec pins; callable from a data wire
    std::string           category;
    std::string           tooltip;

    Node& add(std::string def, float x = 0.0f, float y = 0.0f) {
        Node n;
        n.id = next_id++;
        n.def = std::move(def);
        n.x = x;
        n.y = y;
        nodes.push_back(std::move(n));
        return nodes.back();
    }
    Node* find(int id) {
        for (auto& n : nodes)
            if (n.id == id) return &n;
        return nullptr;
    }
    const Node* find(int id) const {
        for (const auto& n : nodes)
            if (n.id == id) return &n;
        return nullptr;
    }
    void remove(int id) {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const Node& n) { return n.id == id; }), nodes.end());
        links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& l) { return l.from == id || l.to == id; }),
                    links.end());
    }
    /// Adds a wire. Does not enforce pin rules (one wire into a data input, one
    /// out of an exec output): the editor does that with the node shapes, and
    /// the compiler reports a graph that breaks them.
    void link(int from, std::string from_pin, int to, std::string to_pin) {
        for (const auto& l : links)
            if (l.from == from && l.to == to && l.from_pin == from_pin && l.to_pin == to_pin) return;
        links.push_back(Link{from, std::move(from_pin), to, std::move(to_pin)});
    }
    void unlink_input(int to, const std::string& to_pin) {
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [&](const Link& l) { return l.to == to && l.to_pin == to_pin; }),
                    links.end());
    }
    Variable* find_local(const std::string& n) {
        for (auto& v : locals)
            if (v.name == n) return &v;
        return nullptr;
    }
    const Variable* find_local(const std::string& n) const {
        for (const auto& v : locals)
            if (v.name == n) return &v;
        return nullptr;
    }
};

struct Script {
    std::string           name;
    std::string           parent;      // reserved: the script this one extends
    std::vector<Variable> variables;
    std::vector<Graph>    graphs;

    Graph& add_graph(GraphKind kind, std::string graph_name) {
        Graph g;
        g.kind = kind;
        g.name = std::move(graph_name);
        graphs.push_back(std::move(g));
        return graphs.back();
    }
    Graph* find_graph(const std::string& n) {
        for (auto& g : graphs)
            if (g.name == n) return &g;
        return nullptr;
    }
    const Graph* find_graph(const std::string& n) const {
        for (const auto& g : graphs)
            if (g.name == n) return &g;
        return nullptr;
    }
    const Graph* find_function(const std::string& n) const {
        for (const auto& g : graphs)
            if (g.kind == GraphKind::Function && g.name == n) return &g;
        return nullptr;
    }
    Variable* find_variable(const std::string& n) {
        for (auto& v : variables)
            if (v.name == n) return &v;
        return nullptr;
    }
    const Variable* find_variable(const std::string& n) const {
        for (const auto& v : variables)
            if (v.name == n) return &v;
        return nullptr;
    }
    Variable& add_variable(std::string var_name, Type type, Value def = Value()) {
        Variable v;
        v.name = std::move(var_name);
        v.type = type;
        v.def = def.type == type ? std::move(def) : Value::default_of(type);
        variables.push_back(std::move(v));
        return variables.back();
    }
};

}  // namespace schizo::vs
