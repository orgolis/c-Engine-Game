#pragma once
// ============================================================================
// anim_graph_build.h -- the .animgraph document (anim_graph.h) and the graph a
// character runs (anim_graph_runtime.h), each made from the other:
//
//   build_runtime_graph: a document -> a runtime graph for one model (clips by
//     name, blend samples where their clips were measured to move, parameters a
//     condition names but the document does not declare added by how they are
//     used); what could not be resolved is listed, never fatal.
//   document_from_graph: a runtime graph -> a document to save and edit (the
//     graph a character runs when it names none, as a starting point).
// ============================================================================
#include "anim_graph.h"
#include "anim_graph_runtime.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::editor {

struct RuntimeGraphBuild {
    std::shared_ptr<schizo::renderer::animgraph::GraphDef> def;
    std::vector<std::string> problems;
};

inline schizo::renderer::animgraph::Op runtime_op(AnimGraphCondOp op) {
    using schizo::renderer::animgraph::Op;
    switch (op) {
        case AnimGraphCondOp::Greater:      return Op::Greater;
        case AnimGraphCondOp::GreaterEqual: return Op::GreaterEqual;
        case AnimGraphCondOp::Less:         return Op::Less;
        case AnimGraphCondOp::LessEqual:    return Op::LessEqual;
        case AnimGraphCondOp::Equals:       return Op::Equals;
        case AnimGraphCondOp::NotEquals:    return Op::NotEquals;
        case AnimGraphCondOp::True:         return Op::True;
        case AnimGraphCondOp::False:        return Op::False;
        case AnimGraphCondOp::TriggerSet:   return Op::Triggered;
    }
    return Op::True;
}
inline AnimGraphCondOp document_op(schizo::renderer::animgraph::Op op) {
    using schizo::renderer::animgraph::Op;
    switch (op) {
        case Op::Greater:      return AnimGraphCondOp::Greater;
        case Op::GreaterEqual: return AnimGraphCondOp::GreaterEqual;
        case Op::Less:         return AnimGraphCondOp::Less;
        case Op::LessEqual:    return AnimGraphCondOp::LessEqual;
        case Op::Equals:       return AnimGraphCondOp::Equals;
        case Op::NotEquals:    return AnimGraphCondOp::NotEquals;
        case Op::True:         return AnimGraphCondOp::True;
        case Op::False:        return AnimGraphCondOp::False;
        case Op::Triggered:    return AnimGraphCondOp::TriggerSet;
    }
    return AnimGraphCondOp::True;
}

/// `find_clip`: a clip of the model by name (-1: none); `measured(clip, fallback)`:
/// where a locomotion clip goes (m/s, model x and z), else the fallback.
inline RuntimeGraphBuild build_runtime_graph(const AnimGraph& doc, const std::function<int(const std::string&)>& find_clip,
                                             const std::function<glm::vec2(int, glm::vec2)>& measured) {
    using namespace schizo::renderer::animgraph;
    RuntimeGraphBuild out;
    auto def = std::make_shared<GraphDef>();
    for (const auto& p : doc.params())
        def->add_param(p.name, p.type == 1 ? ParamType::Bool : p.type == 2 ? ParamType::Trigger : ParamType::Float, p.init);
    auto param = [&](const std::string& n, ParamType how) {
        if (n.empty()) return -1;
        return def->add_param(n, how);   // (declared: kept as declared)
    };
    // Layers: 0 the whole body, then the document's.
    int layers = 1 + static_cast<int>(doc.layers().size());
    for (const auto& s : doc.states()) layers = std::max(layers, s.layer + 1);
    def->layers.resize(static_cast<size_t>(layers));
    def->layers[0].name = "Base";
    for (int l = 1; l < layers; ++l) {
        if (static_cast<size_t>(l - 1) < doc.layers().size()) {
            def->layers[static_cast<size_t>(l)].name = doc.layers()[static_cast<size_t>(l - 1)].name;
            def->layers[static_cast<size_t>(l)].mask_bone = doc.layers()[static_cast<size_t>(l - 1)].mask_bone;
        } else {
            def->layers[static_cast<size_t>(l)].name = "Layer " + std::to_string(l);
        }
    }
    // States: by layer; ids to (layer, index).
    std::unordered_map<uint32_t, std::pair<int, int>> where;
    auto clip_of = [&](const std::string& name, int index, const std::string& state) {
        if (!name.empty()) {
            const int c = find_clip(name);
            if (c < 0) out.problems.push_back("'" + state + "': no clip '" + name + "' in the model");
            return c;
        }
        return index;
    };
    for (const auto& s : doc.states()) {
        LayerDef& L = def->layers[static_cast<size_t>(std::max(0, s.layer))];
        StateDef sd;
        sd.name = s.name;
        sd.loop = s.loop;
        sd.rate = s.speed;
        sd.rate_param = param(s.rate_param, ParamType::Float);
        sd.sync = s.sync.empty() ? -1 : def->sync_group(s.sync);
        switch (s.kind) {
            case AnimGraphStateKind::None: sd.kind = NodeKind::None; break;
            case AnimGraphStateKind::Clip:
                sd.kind = NodeKind::Clip;
                sd.clip = clip_of(s.clip, s.clip_index, s.name);
                if (sd.clip < 0) sd.kind = NodeKind::None;
                break;
            case AnimGraphStateKind::Blend1D:
            case AnimGraphStateKind::Blend2D: {
                const bool two = s.kind == AnimGraphStateKind::Blend2D;
                sd.kind = two ? NodeKind::Blend2D : NodeKind::Blend1D;
                sd.param_x = param(s.param_x, ParamType::Float);
                sd.param_y = two ? param(s.param_y, ParamType::Float) : -1;
                sd.radial = s.radial;
                for (const auto& sm : s.samples) {
                    BlendSample b;
                    b.clip = sm.clip == "-" ? -1 : clip_of(sm.clip, -1, s.name);
                    if (b.clip < 0 && sm.clip != "-") continue;
                    b.at = glm::vec2(sm.x, two ? sm.y : 0.0f);
                    if (sm.measured && b.clip >= 0) {
                        const glm::vec2 m = measured(b.clip, b.at);
                        b.at = two ? m : glm::vec2(glm::length(m), 0.0f);
                    }
                    b.measured = sm.measured;
                    sd.space.samples.push_back(b);
                }
                sd.space.build();
                if (sd.space.samples.empty()) {
                    out.problems.push_back("'" + s.name + "': a blend space with no clips");
                    sd.kind = NodeKind::None;
                }
                break;
            }
        }
        where[s.id] = {static_cast<int>(&L - def->layers.data()), static_cast<int>(L.states.size())};
        L.states.push_back(std::move(sd));
    }
    for (int l = 0; l < layers; ++l) {
        const auto e = where.find(doc.layer_entry(l));
        def->layers[static_cast<size_t>(l)].entry = e != where.end() && e->second.first == l ? e->second.second : 0;
        if (def->layers[static_cast<size_t>(l)].states.empty()) {   // (an empty layer shows nothing)
            StateDef none;
            none.name = "None";
            none.kind = NodeKind::None;
            def->layers[static_cast<size_t>(l)].states.push_back(none);
        }
    }
    for (const auto& t : doc.transitions()) {
        const auto to = where.find(t.to);
        if (to == where.end()) continue;
        int from = -1;
        if (t.from != kAnyState) {
            const auto f = where.find(t.from);
            if (f == where.end()) continue;
            if (f->second.first != to->second.first) {
                out.problems.push_back("a transition between two layers is left out");
                continue;
            }
            from = f->second.second;
        }
        TransitionDef td;
        td.from = from;
        td.to = to->second.second;
        td.blend = t.blend_duration;
        td.exit = t.has_exit_time ? t.exit_time : -1.0f;
        for (const auto& c : t.conditions) {
            const ParamType how = c.op == AnimGraphCondOp::TriggerSet ? ParamType::Trigger
                                : (c.op == AnimGraphCondOp::True || c.op == AnimGraphCondOp::False) ? ParamType::Bool : ParamType::Float;
            td.when.push_back({param(c.param, how), runtime_op(c.op), c.threshold});
        }
        def->layers[static_cast<size_t>(to->second.first)].transitions.push_back(std::move(td));
    }
    for (const auto& sl : doc.slots()) def->slots.push_back({sl.name, sl.mask_bone});
    out.def = def;
    return out;
}

/// A runtime graph as a document (clips by name); laid out a layer to a row band.
inline AnimGraph document_from_graph(const schizo::renderer::animgraph::GraphDef& def, const std::function<std::string(int)>& clip_name) {
    using namespace schizo::renderer::animgraph;
    AnimGraph doc;
    doc.clear();
    for (const auto& p : def.params)
        doc.params().push_back({p.name, p.type == ParamType::Bool ? 1 : p.type == ParamType::Trigger ? 2 : 0, p.init});
    std::vector<std::vector<uint32_t>> ids(def.layers.size());
    for (size_t l = 0; l < def.layers.size(); ++l) {
        const auto& L = def.layers[l];
        for (size_t i = 0; i < L.states.size(); ++i) {
            const StateDef& sd = L.states[i];
            const float x = 60.0f + 220.0f * static_cast<float>(i % 5);
            const float y = 60.0f + 320.0f * static_cast<float>(l) + 110.0f * static_cast<float>(i / 5);
            const uint32_t id = doc.add_state(sd.name, x, y);
            ids[l].push_back(id);
            AnimGraphState* s = doc.state(id);
            s->layer = static_cast<int>(l);
            s->loop = sd.loop;
            s->speed = sd.rate;
            s->rate_param = sd.rate_param >= 0 ? def.params[static_cast<size_t>(sd.rate_param)].name : "";
            s->sync = sd.sync >= 0 ? def.sync_groups[static_cast<size_t>(sd.sync)] : "";
            s->kind = sd.kind == NodeKind::None ? AnimGraphStateKind::None : sd.kind == NodeKind::Clip ? AnimGraphStateKind::Clip
                    : sd.kind == NodeKind::Blend1D ? AnimGraphStateKind::Blend1D : AnimGraphStateKind::Blend2D;
            if (sd.kind == NodeKind::Clip) { s->clip = clip_name(sd.clip); s->clip_index = std::max(0, sd.clip); }
            s->param_x = sd.param_x >= 0 ? def.params[static_cast<size_t>(sd.param_x)].name : "";
            s->param_y = sd.param_y >= 0 ? def.params[static_cast<size_t>(sd.param_y)].name : "";
            s->radial = sd.radial;
            for (const auto& b : sd.space.samples)
                s->samples.push_back({b.clip >= 0 ? clip_name(b.clip) : std::string("-"), b.at.x, b.at.y, b.measured});
        }
        if (l == 0) {
            if (!ids[0].empty()) doc.set_entry_state(ids[0][static_cast<size_t>(std::clamp(L.entry, 0, static_cast<int>(ids[0].size()) - 1))]);
        } else {
            AnimGraphLayer ly;
            ly.name = L.name;
            ly.mask_bone = L.mask_bone;
            ly.entry = ids[l].empty() ? 0 : ids[l][static_cast<size_t>(std::clamp(L.entry, 0, static_cast<int>(ids[l].size()) - 1))];
            doc.layers().push_back(ly);
        }
    }
    for (size_t l = 0; l < def.layers.size(); ++l)
        for (const auto& t : def.layers[l].transitions) {
            const uint32_t from = t.from < 0 ? kAnyState : ids[l][static_cast<size_t>(t.from)];
            const uint32_t to = ids[l][static_cast<size_t>(t.to)];
            AnimGraphTransition tr;
            tr.from = from;
            tr.to = to;
            tr.blend_duration = t.blend;
            tr.has_exit_time = t.exit >= 0.0f;
            tr.exit_time = t.exit >= 0.0f ? t.exit : 0.9f;
            for (const auto& c : t.when)
                tr.conditions.push_back({c.param >= 0 ? def.params[static_cast<size_t>(c.param)].name : "", document_op(c.op), c.threshold});
            doc.transitions().push_back(tr);   // (as built: add_transition would refuse an any-state one to itself only)
        }
    for (const auto& sl : def.slots) doc.slots().push_back({sl.name, sl.mask_bone});
    return doc;
}

}  // namespace schizo::editor
