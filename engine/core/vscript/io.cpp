#include "vscript/io.h"

#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace schizo::vs {

namespace {

std::string esc(std::string_view s) {
    std::string o;
    for (char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\t') o += "\\t";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}

std::string unesc(std::string_view s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            o += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            o += s[i];
        }
    }
    return o;
}

void line(std::string& out, std::initializer_list<std::string> fields) {
    bool first = true;
    for (const std::string& f : fields) {
        if (!first) out += '\t';
        first = false;
        out += f;
    }
    out += '\n';
}

std::string num(float x) { return number_text(static_cast<double>(x)); }

}  // namespace

std::string to_text(const Script& s) {
    std::string o = "VSCRIPT 2\n";
    line(o, {"name", esc(s.name)});
    if (!s.parent.empty()) line(o, {"parent", esc(s.parent)});
    for (const Variable& v : s.variables)
        line(o, {"var", esc(v.name), type_name(v.type), v.exposed ? "1" : "0", esc(literal_text(v.def)),
                 esc(v.category), esc(v.tooltip)});
    for (const Graph& g : s.graphs) {
        line(o, {"graph", g.kind == GraphKind::Function ? "function" : "event", esc(g.name), g.pure ? "1" : "0",
                 std::to_string(g.next_id)});
        for (const Param& p : g.inputs) line(o, {"input", esc(p.name), type_name(p.type), esc(literal_text(p.def))});
        for (const Param& p : g.outputs) line(o, {"output", esc(p.name), type_name(p.type), esc(literal_text(p.def))});
        for (const Variable& v : g.locals) line(o, {"local", esc(v.name), type_name(v.type), esc(literal_text(v.def))});
        for (const Node& n : g.nodes) {
            line(o, {"node", std::to_string(n.id), esc(n.def), num(n.x), num(n.y), esc(n.name), type_name(n.targ),
                     std::to_string(n.extra), esc(n.comment)});
            for (const std::string& c : n.cases) line(o, {"case", std::to_string(n.id), esc(c)});
            for (const Param& p : n.params)
                line(o, {"param", std::to_string(n.id), esc(p.name), type_name(p.type), esc(literal_text(p.def))});
            for (const auto& d : n.defaults)
                line(o, {"pin", std::to_string(n.id), esc(d.first), type_name(d.second.type), esc(literal_text(d.second))});
        }
        for (const Link& l : g.links)
            line(o, {"link", std::to_string(l.from), esc(l.from_pin), std::to_string(l.to), esc(l.to_pin)});
        for (const CommentBox& b : g.boxes)
            line(o, {"box", std::to_string(b.id), num(b.x), num(b.y), num(b.w), num(b.h), esc(b.text), num(b.color[0]),
                     num(b.color[1]), num(b.color[2])});
        line(o, {"end"});
    }
    return o;
}

bool from_text(std::string_view text, Script& out, std::string* error) {
    out = Script{};
    Graph* g = nullptr;
    int    lineno = 0;
    bool   header = false;
    auto fail = [&](const std::string& what) {
        if (error) *error = "line " + std::to_string(lineno) + ": " + what;
        return false;
    };
    size_t start = 0;
    while (start <= text.size()) {
        const size_t     nl = text.find('\n', start);
        std::string_view ln = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        start = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        ++lineno;
        if (!ln.empty() && ln.back() == '\r') ln.remove_suffix(1);
        if (ln.empty()) continue;
        if (!header) {
            if (ln != "VSCRIPT 2") return fail("not a logic script (expected 'VSCRIPT 2')");
            header = true;
            continue;
        }
        std::vector<std::string> f;
        for (size_t p = 0; p <= ln.size();) {
            const size_t t = ln.find('\t', p);
            f.push_back(unesc(ln.substr(p, t == std::string_view::npos ? std::string_view::npos : t - p)));
            if (t == std::string_view::npos) break;
            p = t + 1;
        }
        auto field = [&](size_t i) -> const std::string& {
            static const std::string empty;
            return i < f.size() ? f[i] : empty;
        };
        auto type_at = [&](size_t i, Type& t) { return parse_type(field(i), t); };
        auto value_at = [&](size_t i, Type t, Value& v) {
            if (!parse_literal(t, field(i), v)) v = Value::default_of(t);
        };
        auto node_at = [&](size_t i) -> Node* { return g ? g->find(std::atoi(field(i).c_str())) : nullptr; };
        const std::string& k = f[0];

        if (k == "name") out.name = field(1);
        else if (k == "parent") out.parent = field(1);
        else if (k == "var") {
            Variable v;
            v.name = field(1);
            if (!type_at(2, v.type)) return fail("unknown type '" + field(2) + "'");
            v.exposed = field(3) == "1";
            value_at(4, v.type, v.def);
            v.category = field(5);
            v.tooltip = field(6);
            out.variables.push_back(std::move(v));
        } else if (k == "graph") {
            out.graphs.emplace_back();
            g = &out.graphs.back();
            g->kind = field(1) == "function" ? GraphKind::Function : GraphKind::Event;
            g->name = field(2);
            g->pure = field(3) == "1";
            g->next_id = std::max(1, std::atoi(field(4).c_str()));
        } else if (k == "end") {
            g = nullptr;
        } else if (!g) {
            return fail("'" + k + "' outside a graph");
        } else if (k == "input" || k == "output") {
            Param p;
            p.name = field(1);
            if (!type_at(2, p.type)) return fail("unknown type '" + field(2) + "'");
            value_at(3, p.type, p.def);
            (k == "input" ? g->inputs : g->outputs).push_back(std::move(p));
        } else if (k == "local") {
            Variable v;
            v.name = field(1);
            if (!type_at(2, v.type)) return fail("unknown type '" + field(2) + "'");
            value_at(3, v.type, v.def);
            g->locals.push_back(std::move(v));
        } else if (k == "node") {
            Node n;
            n.id = std::atoi(field(1).c_str());
            n.def = field(2);
            Value x, y;
            parse_literal(kFloat, field(3), x);
            parse_literal(kFloat, field(4), y);
            n.x = static_cast<float>(x.as_float());
            n.y = static_cast<float>(y.as_float());
            n.name = field(5);
            parse_type(field(6), n.targ);
            n.extra = std::atoi(field(7).c_str());
            n.comment = field(8);
            if (n.id <= 0) return fail("a node needs a positive id");
            g->nodes.push_back(std::move(n));
            g->next_id = std::max(g->next_id, g->nodes.back().id + 1);
        } else if (k == "case") {
            if (Node* n = node_at(1)) n->cases.push_back(field(2));
        } else if (k == "param") {
            Param p;
            p.name = field(2);
            if (!type_at(3, p.type)) return fail("unknown type '" + field(3) + "'");
            value_at(4, p.type, p.def);
            if (Node* n = node_at(1)) n->params.push_back(std::move(p));
        } else if (k == "pin") {
            Type t;
            if (!type_at(3, t)) return fail("unknown type '" + field(3) + "'");
            Value v;
            value_at(4, t, v);
            if (Node* n = node_at(1)) n->set_default(field(2), std::move(v));
        } else if (k == "link") {
            g->link(std::atoi(field(1).c_str()), field(2), std::atoi(field(3).c_str()), field(4));
        } else if (k == "box") {
            CommentBox b;
            b.id = std::atoi(field(1).c_str());
            float* const dst[7] = {&b.x, &b.y, &b.w, &b.h, &b.color[0], &b.color[1], &b.color[2]};
            const size_t at[7] = {2, 3, 4, 5, 7, 8, 9};
            for (int i = 0; i < 7; ++i) {
                Value v;
                if (!field(at[i]).empty() && parse_literal(kFloat, field(at[i]), v)) *dst[i] = static_cast<float>(v.as_float());
            }
            b.text = field(6);
            if (b.id <= 0) return fail("a comment box needs a positive id");
            g->boxes.push_back(std::move(b));
            g->next_id = std::max(g->next_id, g->boxes.back().id + 1);
        } else {
            return fail("unknown record '" + k + "'");
        }
    }
    if (!header) return fail("empty file");
    return true;
}

namespace {
/// The name a copied selection carries, so that other text is not taken for one.
const char* const kClipName = "#logic-script-selection";
}  // namespace

std::string selection_text(const Graph& g, const std::vector<int>& node_ids) {
    Script s;
    s.name = kClipName;
    Graph& c = s.add_graph(g.kind, "selection");
    auto in = [&](int id) { return std::find(node_ids.begin(), node_ids.end(), id) != node_ids.end(); };
    for (const Node& n : g.nodes)
        if (in(n.id)) c.nodes.push_back(n);
    for (const Link& l : g.links)
        if (in(l.from) && in(l.to)) c.links.push_back(l);
    c.next_id = g.next_id;
    return to_text(s);
}

std::vector<int> paste_selection(Graph& g, std::string_view text, float x, float y) {
    std::vector<int> made;
    Script           s;
    if (!from_text(text, s) || s.name != kClipName || s.graphs.empty() || s.graphs[0].nodes.empty()) return made;
    const Graph& c = s.graphs[0];
    float minx = c.nodes[0].x, miny = c.nodes[0].y;
    for (const Node& n : c.nodes) {
        minx = std::min(minx, n.x);
        miny = std::min(miny, n.y);
    }
    std::unordered_map<int, int> remap;
    for (Node n : c.nodes) {
        if (n.def == "fn.entry") continue;
        const int old = n.id;
        n.id = g.next_id++;
        n.x += x - minx;
        n.y += y - miny;
        remap[old] = n.id;
        made.push_back(n.id);
        g.nodes.push_back(std::move(n));
    }
    for (const Link& l : c.links) {
        const auto a = remap.find(l.from), b = remap.find(l.to);
        if (a != remap.end() && b != remap.end()) g.link(a->second, l.from_pin, b->second, l.to_pin);
    }
    return made;
}

}  // namespace schizo::vs
