// ============================================================================
// doc_io.cpp — text serialisation for the three authoring documents. See header.
// ============================================================================

#include "doc_io.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

using gws::anim::Curve;
using gws::anim::CurveKey;
using gws::shadergraph::Link;
using gws::shadergraph::MaterialGraph;
using gws::shadergraph::Node;
using gws::shadergraph::NodeId;
using gws::shadergraph::NodeKind;
using gws::shadergraph::kInvalidNode;

namespace schizo::editor {
namespace {

// ---------------------------------------------------------------------------
// Shared line plumbing
// ---------------------------------------------------------------------------

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

float parse_float(const std::string& s, float fallback) {
    try { return std::stof(s); } catch (...) { return fallback; }
}
int parse_int(const std::string& s, int fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}

/// Ids need their OWN parse, and this is not a nicety. Every id in these
/// documents is a uint32_t, and two of them are sentinels at the TOP of that
/// range -- kAnyState (0xFFFFFFFF) and kInvalidNode. std::stoi throws
/// out_of_range on those, so an int-based parse silently returned the fallback:
/// every "interrupt from anywhere" transition loaded as from-state 0, matched no
/// state, and was DROPPED. Nothing errored -- the death animation simply stopped
/// playing. Caught by docio_check, which is why that assertion exists.
uint32_t parse_uint(const std::string& s, uint32_t fallback) {
    try { return static_cast<uint32_t>(std::stoul(s)); } catch (...) { return fallback; }
}
bool parse_bool(const std::string& s) { return trim(s) == "1" || trim(s) == "true"; }

std::vector<float> parse_floats(const std::string& s) {
    std::vector<float> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        const std::string t = trim(part);
        if (!t.empty()) out.push_back(parse_float(t, 0.0f));
    }
    return out;
}

/// Split on the FIRST '=' only. A state called "half = full" is legal, and
/// splitting on every '=' would silently truncate its name.
bool split_kv(const std::string& line, std::string& key, std::string& value) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    key   = trim(line.substr(0, eq));
    value = line.substr(eq + 1);          // NOT trimmed: a trailing space may be part of a name
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
    return !key.empty();
}

/// "NODE.3.KIND" -> prefix "NODE", index 3, field "KIND". False when the key is
/// not an indexed one.
bool split_indexed(const std::string& key, std::string& prefix, int& index, std::string& field) {
    const size_t d1 = key.find('.');
    if (d1 == std::string::npos) return false;
    const size_t d2 = key.find('.', d1 + 1);
    if (d2 == std::string::npos) return false;
    prefix = key.substr(0, d1);
    const std::string num = key.substr(d1 + 1, d2 - d1 - 1);
    if (num.empty() || num.find_first_not_of("0123456789") != std::string::npos) return false;
    index = parse_int(num, -1);
    field = key.substr(d2 + 1);
    return index >= 0;
}

/// Curves are written as a flat comma list -- time,value,in,out per key -- the
/// same shape vfx_io uses, because the format is line-based and a nested block
/// would need a parser rather than a split.
std::string curve_to_text(const Curve& c) {
    std::ostringstream o;
    for (size_t i = 0; i < c.keys().size(); ++i) {
        const CurveKey& k = c.keys()[i];
        if (i) o << ",";
        o << k.time << "," << k.value << "," << k.in_tangent << "," << k.out_tangent;
    }
    return o.str();
}

Curve curve_from_text(const std::string& s) {
    Curve c;
    const std::vector<float> v = parse_floats(s);
    // A trailing partial key is dropped rather than padded with zeros: a key at
    // time 0 with value 0 is a real, visible edit to a curve, and inventing one
    // from a truncated file would move the animation.
    for (size_t i = 0; i + 3 < v.size(); i += 4)
        c.add(CurveKey{v[i], v[i + 1], v[i + 2], v[i + 3]});
    return c;
}

// ---------------------------------------------------------------------------
// Enum <-> stable token. Names, never indices -- see the header.
// ---------------------------------------------------------------------------

struct KindName { NodeKind kind; const char* name; };
const KindName kKindNames[] = {
    {NodeKind::UV, "UV"},                       {NodeKind::Time, "Time"},
    {NodeKind::VertexColor, "VertexColor"},     {NodeKind::ConstFloat, "ConstFloat"},
    {NodeKind::ConstColor, "ConstColor"},       {NodeKind::TextureSample, "TextureSample"},
    {NodeKind::Add, "Add"},                     {NodeKind::Multiply, "Multiply"},
    {NodeKind::Lerp, "Lerp"},                   {NodeKind::OneMinus, "OneMinus"},
    {NodeKind::Saturate, "Saturate"},           {NodeKind::Dot, "Dot"},
    {NodeKind::Split, "Split"},                 {NodeKind::Combine, "Combine"},
    {NodeKind::Output, "Output"},
};

const char* kind_token(NodeKind k) {
    for (const KindName& e : kKindNames) if (e.kind == k) return e.name;
    return "ConstFloat";
}
NodeKind kind_from_token(const std::string& s) {
    for (const KindName& e : kKindNames) if (s == e.name) return e.kind;
    return NodeKind::ConstFloat;   // unknown kind degrades to an inert constant
}

struct TargetName { TrackTarget target; const char* name; };
const TargetName kTargetNames[] = {
    {TrackTarget::PositionX, "PositionX"},         {TrackTarget::PositionY, "PositionY"},
    {TrackTarget::PositionZ, "PositionZ"},         {TrackTarget::RotationYaw, "RotationYaw"},
    {TrackTarget::RotationPitch, "RotationPitch"}, {TrackTarget::RotationRoll, "RotationRoll"},
    {TrackTarget::ScaleUniform, "ScaleUniform"},
};

const char* target_token(TrackTarget t) {
    for (const TargetName& e : kTargetNames) if (e.target == t) return e.name;
    return "PositionX";
}
TrackTarget target_from_token(const std::string& s) {
    for (const TargetName& e : kTargetNames) if (s == e.name) return e.target;
    return TrackTarget::PositionX;
}

struct OpName { AnimGraphCondOp op; const char* name; };
const OpName kOpNames[] = {
    {AnimGraphCondOp::Greater, "Greater"},           {AnimGraphCondOp::GreaterEqual, "GreaterEqual"},
    {AnimGraphCondOp::Less, "Less"},                 {AnimGraphCondOp::LessEqual, "LessEqual"},
    {AnimGraphCondOp::Equals, "Equals"},             {AnimGraphCondOp::NotEquals, "NotEquals"},
    {AnimGraphCondOp::True, "True"},                 {AnimGraphCondOp::False, "False"},
    {AnimGraphCondOp::TriggerSet, "TriggerSet"},
};

const char* op_token(AnimGraphCondOp o) {
    for (const OpName& e : kOpNames) if (e.op == o) return e.name;
    return "Greater";
}
AnimGraphCondOp op_from_token(const std::string& s) {
    for (const OpName& e : kOpNames) if (s == e.name) return e.op;
    return AnimGraphCondOp::Greater;
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool write_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::trunc | std::ios::binary);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

}  // namespace

// ===========================================================================
// MaterialGraph
// ===========================================================================

std::string material_graph_to_text(const MaterialGraph& g) {
    std::ostringstream o;
    o << "MATGRAPH_VERSION=" << kDocFormatVersion << "\n";
    o << "OUTPUT=" << g.output_node() << "\n";

    size_t n = 0;
    for (const Node& node : g.nodes()) {
        o << "NODE." << n << ".ID="    << node.id << "\n";
        o << "NODE." << n << ".KIND="  << kind_token(node.kind) << "\n";
        o << "NODE." << n << ".VALUE=" << node.value[0] << "," << node.value[1] << ","
                                       << node.value[2] << "," << node.value[3] << "\n";
        o << "NODE." << n << ".PARAM=" << node.param_index << "\n";
        o << "NODE." << n << ".POS="   << node.x << "," << node.y << "\n";
        ++n;
    }
    size_t l = 0;
    for (const Link& link : g.links()) {
        o << "LINK." << l << ".SRC="   << link.src_node << "\n";
        o << "LINK." << l << ".DST="   << link.dst_node << "\n";
        o << "LINK." << l << ".INPUT=" << link.dst_input << "\n";
        ++l;
    }
    return o.str();
}

void material_graph_from_text(const std::string& text, MaterialGraph& out) {
    struct RawNode { uint32_t id = 0; NodeKind kind = NodeKind::ConstFloat;
                     float value[4] = {0, 0, 0, 0}; uint32_t param = 0;
                     float x = 0, y = 0; bool seen = false; };
    struct RawLink { uint32_t src = 0, dst = 0, input = 0; bool seen = false; };

    std::unordered_map<int, RawNode> nodes;
    std::unordered_map<int, RawLink> links;
    uint32_t file_output = 0;
    bool     have_output = false;

    std::istringstream in(text);
    std::string line, key, value, prefix, field;
    int idx = 0;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (!split_kv(t, key, value)) continue;

        if (key == "OUTPUT") { file_output = parse_uint(trim(value), 0); have_output = true; continue; }
        if (!split_indexed(key, prefix, idx, field)) continue;

        if (prefix == "NODE") {
            RawNode& r = nodes[idx];
            r.seen = true;
            if      (field == "ID")    r.id    = parse_uint(trim(value), 0);
            else if (field == "KIND")  r.kind  = kind_from_token(trim(value));
            else if (field == "PARAM") r.param = parse_uint(trim(value), 0);
            else if (field == "VALUE") {
                const std::vector<float> v = parse_floats(value);
                for (size_t i = 0; i < 4 && i < v.size(); ++i) r.value[i] = v[i];
            } else if (field == "POS") {
                const std::vector<float> v = parse_floats(value);
                if (v.size() >= 2) { r.x = v[0]; r.y = v[1]; }
            }
        } else if (prefix == "LINK") {
            RawLink& r = links[idx];
            r.seen = true;
            if      (field == "SRC")   r.src   = parse_uint(trim(value), 0);
            else if (field == "DST")   r.dst   = parse_uint(trim(value), 0);
            else if (field == "INPUT") r.input = parse_uint(trim(value), 0);
        }
    }

    out.clear();   // leaves exactly the Output node the document must always have

    // File id -> live id. Rebuilt through add_node so the graph's own counter
    // stays ahead of everything it holds.
    std::unordered_map<uint32_t, NodeId> remap;

    // Index order, not map order, so a file's node order is preserved.
    std::vector<int> node_keys;
    node_keys.reserve(nodes.size());
    for (const auto& kv : nodes) if (kv.second.seen) node_keys.push_back(kv.first);
    std::sort(node_keys.begin(), node_keys.end());

    for (int k : node_keys) {
        const RawNode& r = nodes[k];
        NodeId live;
        if (r.kind == NodeKind::Output) {
            // The Output that clear() just made IS this node -- adding a second
            // would give the graph two outputs and generate() would follow the
            // wrong one.
            live = out.output_node();
        } else {
            live = out.add_node(r.kind, r.x, r.y);
        }
        remap[r.id] = live;
        if (Node* n = out.node(live)) {
            for (int i = 0; i < 4; ++i) n->value[i] = r.value[i];
            n->param_index = r.param;
            n->x = r.x;
            n->y = r.y;
        }
    }
    // A file naming an OUTPUT id that has no Output node still resolves, so an
    // older or hand-edited file opens instead of losing its links.
    if (have_output && remap.find(file_output) == remap.end())
        remap[file_output] = out.output_node();

    std::vector<int> link_keys;
    link_keys.reserve(links.size());
    for (const auto& kv : links) if (kv.second.seen) link_keys.push_back(kv.first);
    std::sort(link_keys.begin(), link_keys.end());

    for (int k : link_keys) {
        const RawLink& r = links[k];
        const auto s = remap.find(r.src);
        const auto d = remap.find(r.dst);
        // A link naming a node the file does not contain is DROPPED, not
        // guessed at: connecting it to something else would silently rewire the
        // material into one that still compiles.
        if (s == remap.end() || d == remap.end()) continue;
        out.connect(s->second, d->second, r.input);
    }
}

bool load_material_graph(const std::string& path, MaterialGraph& out) {
    std::string text;
    if (!read_file(path, text)) return false;
    material_graph_from_text(text, out);
    return true;
}

bool save_material_graph(const std::string& path, const MaterialGraph& g) {
    return write_file(path, material_graph_to_text(g));
}

// ===========================================================================
// AnimGraph
// ===========================================================================

std::string anim_graph_to_text(const AnimGraph& g) {
    std::ostringstream o;
    o << "ANIMGRAPH_VERSION=" << kDocFormatVersion << "\n";
    o << "ENTRY=" << g.entry_state() << "\n";

    size_t n = 0;
    for (const AnimGraphState& s : g.states()) {
        o << "STATE." << n << ".ID="         << s.id << "\n";
        o << "STATE." << n << ".NAME="       << s.name << "\n";
        o << "STATE." << n << ".CLIP="       << s.clip_index << "\n";
        o << "STATE." << n << ".LOOP="       << (s.loop ? 1 : 0) << "\n";
        o << "STATE." << n << ".SPEED="      << s.speed << "\n";
        o << "STATE." << n << ".ROOTMOTION=" << (s.apply_root_motion ? 1 : 0) << "\n";
        o << "STATE." << n << ".POS="        << s.x << "," << s.y << "\n";
        ++n;
    }

    size_t t = 0;
    for (const AnimGraphTransition& tr : g.transitions()) {
        o << "TRANS." << t << ".FROM="     << tr.from << "\n";
        o << "TRANS." << t << ".TO="       << tr.to << "\n";
        o << "TRANS." << t << ".BLEND="    << tr.blend_duration << "\n";
        o << "TRANS." << t << ".HASEXIT="  << (tr.has_exit_time ? 1 : 0) << "\n";
        o << "TRANS." << t << ".EXITTIME=" << tr.exit_time << "\n";
        // Conditions are flattened one-per-line rather than nested: the format
        // is line-based, and "param|op|threshold" keeps a transition's whole
        // condition list readable in a diff.
        for (size_t c = 0; c < tr.conditions.size(); ++c) {
            const AnimGraphCondition& cond = tr.conditions[c];
            o << "TRANS." << t << ".COND=" << op_token(cond.op) << "|"
              << cond.threshold << "|" << cond.param << "\n";
        }
        ++t;
    }
    return o.str();
}

void anim_graph_from_text(const std::string& text, AnimGraph& out) {
    struct RawState { uint32_t id = 0; std::string name = "State"; int clip = 0;
                      bool loop = true; float speed = 1.0f; bool root = false;
                      float x = 0, y = 0; bool seen = false; };
    struct RawTrans { uint32_t from = 0, to = 0; float blend = 0.2f;
                      bool has_exit = false; float exit_time = 0.9f;
                      std::vector<AnimGraphCondition> conds; bool seen = false; };

    std::unordered_map<int, RawState> states;
    std::unordered_map<int, RawTrans> transitions;
    uint32_t entry = 0;
    bool     have_entry = false;

    std::istringstream in(text);
    std::string line, key, value, prefix, field;
    int idx = 0;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (!split_kv(t, key, value)) continue;

        if (key == "ENTRY") { entry = parse_uint(trim(value), 0); have_entry = true; continue; }
        if (!split_indexed(key, prefix, idx, field)) continue;

        if (prefix == "STATE") {
            RawState& r = states[idx];
            r.seen = true;
            if      (field == "ID")         r.id    = parse_uint(trim(value), 0);
            else if (field == "NAME")       r.name  = value;   // untrimmed: names may end in a space
            else if (field == "CLIP")       r.clip  = parse_int(trim(value), 0);
            else if (field == "LOOP")       r.loop  = parse_bool(value);
            else if (field == "SPEED")      r.speed = parse_float(trim(value), 1.0f);
            else if (field == "ROOTMOTION") r.root  = parse_bool(value);
            else if (field == "POS") {
                const std::vector<float> v = parse_floats(value);
                if (v.size() >= 2) { r.x = v[0]; r.y = v[1]; }
            }
        } else if (prefix == "TRANS") {
            RawTrans& r = transitions[idx];
            r.seen = true;
            if      (field == "FROM")     r.from      = parse_uint(trim(value), 0);
            else if (field == "TO")       r.to        = parse_uint(trim(value), 0);
            else if (field == "BLEND")    r.blend     = parse_float(trim(value), 0.2f);
            else if (field == "HASEXIT")  r.has_exit  = parse_bool(value);
            else if (field == "EXITTIME") r.exit_time = parse_float(trim(value), 0.9f);
            else if (field == "COND") {
                const size_t b1 = value.find('|');
                if (b1 == std::string::npos) continue;
                const size_t b2 = value.find('|', b1 + 1);
                if (b2 == std::string::npos) continue;
                AnimGraphCondition c;
                c.op        = op_from_token(trim(value.substr(0, b1)));
                c.threshold = parse_float(trim(value.substr(b1 + 1, b2 - b1 - 1)), 0.0f);
                c.param     = value.substr(b2 + 1);
                r.conds.push_back(std::move(c));
            }
        }
    }

    out.clear();

    std::unordered_map<uint32_t, uint32_t> remap;
    std::vector<int> state_keys;
    state_keys.reserve(states.size());
    for (const auto& kv : states) if (kv.second.seen) state_keys.push_back(kv.first);
    std::sort(state_keys.begin(), state_keys.end());

    for (int k : state_keys) {
        const RawState& r = states[k];
        const uint32_t live = out.add_state(r.name, r.x, r.y);
        remap[r.id] = live;
        if (AnimGraphState* s = out.state(live)) {
            s->clip_index        = r.clip;
            s->loop              = r.loop;
            s->speed             = r.speed;
            s->apply_root_motion = r.root;
        }
    }

    std::vector<int> trans_keys;
    trans_keys.reserve(transitions.size());
    for (const auto& kv : transitions) if (kv.second.seen) trans_keys.push_back(kv.first);
    std::sort(trans_keys.begin(), trans_keys.end());

    for (int k : trans_keys) {
        const RawTrans& r = transitions[k];
        // kAnyState is a pseudo-state with no entry in the table; it maps to
        // itself, and forgetting that would drop every "interrupt from
        // anywhere" transition on load -- the ones nobody notices missing until
        // a death animation stops playing.
        uint32_t from = kAnyState;
        if (r.from != kAnyState) {
            const auto f = remap.find(r.from);
            if (f == remap.end()) continue;
            from = f->second;
        }
        const auto t2 = remap.find(r.to);
        if (t2 == remap.end()) continue;
        if (!out.add_transition(from, t2->second)) continue;
        AnimGraphTransition& live = out.transitions().back();
        live.blend_duration = r.blend;
        live.has_exit_time  = r.has_exit;
        live.exit_time      = r.exit_time;
        live.conditions     = r.conds;
    }

    if (have_entry) {
        const auto e = remap.find(entry);
        if (e != remap.end()) out.set_entry_state(e->second);
    }
}

bool load_anim_graph(const std::string& path, AnimGraph& out) {
    std::string text;
    if (!read_file(path, text)) return false;
    anim_graph_from_text(text, out);
    return true;
}

bool save_anim_graph(const std::string& path, const AnimGraph& g) {
    return write_file(path, anim_graph_to_text(g));
}

// ===========================================================================
// Sequence
// ===========================================================================

std::string sequence_to_text(const Sequence& s) {
    std::ostringstream o;
    o << "SEQUENCE_VERSION=" << kDocFormatVersion << "\n";
    o << "DURATION=" << s.duration() << "\n";

    size_t n = 0;
    for (const Track& t : s.tracks()) {
        o << "TRACK." << n << ".ENTITY="  << t.entity_id << "\n";
        o << "TRACK." << n << ".TARGET="  << target_token(t.target) << "\n";
        o << "TRACK." << n << ".ENABLED=" << (t.enabled ? 1 : 0) << "\n";
        o << "TRACK." << n << ".CURVE="   << curve_to_text(t.curve) << "\n";
        ++n;
    }
    return o.str();
}

void sequence_from_text(const std::string& text, Sequence& out) {
    struct RawTrack { uint32_t entity = 0; TrackTarget target = TrackTarget::PositionX;
                      bool enabled = true; Curve curve; bool seen = false; };
    std::unordered_map<int, RawTrack> tracks;
    float duration = 5.0f;

    std::istringstream in(text);
    std::string line, key, value, prefix, field;
    int idx = 0;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (!split_kv(t, key, value)) continue;

        if (key == "DURATION") { duration = parse_float(trim(value), 5.0f); continue; }
        if (!split_indexed(key, prefix, idx, field)) continue;
        if (prefix != "TRACK") continue;

        RawTrack& r = tracks[idx];
        r.seen = true;
        if      (field == "ENTITY")  r.entity  = parse_uint(trim(value), 0);
        else if (field == "TARGET")  r.target  = target_from_token(trim(value));
        else if (field == "ENABLED") r.enabled = parse_bool(value);
        else if (field == "CURVE")   r.curve   = curve_from_text(value);
    }

    out.tracks().clear();
    out.set_duration(duration);

    std::vector<int> keys;
    keys.reserve(tracks.size());
    for (const auto& kv : tracks) if (kv.second.seen) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());

    for (int k : keys) {
        const RawTrack& r = tracks[k];
        // Through add_track, so the "one curve per entity+target" rule the model
        // enforces also holds for a hand-edited file that names one twice.
        Track& t = out.add_track(r.entity, r.target);
        t.enabled = r.enabled;
        t.curve   = r.curve;
    }
}

bool load_sequence(const std::string& path, Sequence& out) {
    std::string text;
    if (!read_file(path, text)) return false;
    sequence_from_text(text, out);
    return true;
}

bool save_sequence(const std::string& path, const Sequence& s) {
    return write_file(path, sequence_to_text(s));
}

// ===========================================================================
// Starter documents
// ===========================================================================

std::string starter_material_graph_text() {
    // A bare Output node generates nothing, so a new graph would open as an
    // empty canvas beside a compile error. The starter wires a neutral colour
    // into base colour: the first thing a person sees is a graph that works.
    MaterialGraph g;
    const NodeId tint = g.add_node(NodeKind::ConstColor, 180.0f, 180.0f);
    if (Node* n = g.node(tint)) {
        n->value[0] = 0.80f; n->value[1] = 0.80f;
        n->value[2] = 0.85f; n->value[3] = 1.00f;
    }
    g.connect(tint, g.output_node(),
              static_cast<uint32_t>(gws::shadergraph::OutputSlot::BaseColor));
    return material_graph_to_text(g);
}

std::string starter_anim_graph_text() {
    // The constructor already seeds an entry state, which is the right starting
    // point: a machine with no states has nothing to drag a transition from.
    return anim_graph_to_text(AnimGraph{});
}

std::string starter_sequence_text() {
    return sequence_to_text(Sequence{});
}

}  // namespace schizo::editor
