// ====================
// docio_check — the three authoring documents survive a save/load (4.10)
// ====================
//
// MaterialGraph, AnimGraph and Sequence were built, reachable and tested, and
// none of them could be saved. This check is the guard on the fix.
//
// THE ASSERTION STYLE MATTERS MORE THAN THE COUNT. A round-trip test that
// builds a default document and compares it to itself passes against a writer
// that emits nothing at all: both sides are default. That is not hypothetical —
// item 3.7 found particle emitters and NPC agents were **never written to the
// scene file**, so configuring one, saving and reopening silently restored every
// default, and it survived because nothing asserted a non-default value.
//
// So every field here is set to a distinctive NON-DEFAULT value before saving,
// and each is checked individually after loading. A field the writer forgets
// comes back as its default and fails loudly.
//
// The strongest assertions are behavioural rather than field-by-field:
//   * a MaterialGraph must generate BYTE-IDENTICAL GLSL after a round trip —
//     that covers node kinds, literals, links, input indices and topological
//     order in one comparison, and it is the property the renderer depends on;
//   * a Sequence must evaluate to the same values at the same times;
//   * an AnimGraph must report the same validation problems and the same
//     unreachable states.
// A document can differ from its original in ways no getter reveals; it cannot
// differ in what it produces.

#include "doc_io.h"
#include "anim_graph_build.h"
#include "humanoid_graph.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace schizo::editor;
using namespace gws::shadergraph;
using gws::anim::CurveKey;

static int g_failures = 0;
static void check(const char* name, bool ok) {
    std::printf("  [%s] %s\n", ok ? "OK  " : "FAIL", name);
    if (!ok) ++g_failures;
}
static bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

int main() {
    std::printf("docio_check — persistence for the Phase 4 authoring documents\n");

    const fs::path root = fs::temp_directory_path() / "gws_docio_check";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // =======================================================================
    // MaterialGraph
    // =======================================================================
    std::printf("\n[group] material graph — structure and literals\n");
    {
        MaterialGraph g;
        const NodeId uv    = g.add_node(NodeKind::UV, 10.0f, 20.0f);
        const NodeId tex   = g.add_node(NodeKind::TextureSample, 120.0f, 30.0f);
        const NodeId tint  = g.add_node(NodeKind::ConstColor, 130.0f, 200.0f);
        const NodeId mul   = g.add_node(NodeKind::Multiply, 260.0f, 90.0f);
        const NodeId rough = g.add_node(NodeKind::ConstFloat, 270.0f, 300.0f);
        const NodeId split = g.add_node(NodeKind::Split, 380.0f, 120.0f);

        g.node(tint)->value[0] = 0.25f; g.node(tint)->value[1] = 0.5f;
        g.node(tint)->value[2] = 0.75f; g.node(tint)->value[3] = 0.125f;
        g.node(rough)->value[0] = 0.375f;
        g.node(tex)->param_index   = 3;      // a non-zero texture slot
        g.node(split)->param_index = 2;      // channel Z

        g.connect(uv, tex, 0);
        g.connect(tex, mul, 0);
        g.connect(tint, mul, 1);
        g.connect(mul, g.output_node(), static_cast<uint32_t>(OutputSlot::BaseColor));
        g.connect(rough, g.output_node(), static_cast<uint32_t>(OutputSlot::Roughness));

        const std::string before_glsl = g.generate().glsl;
        const size_t before_nodes = g.nodes().size();
        const size_t before_links = g.links().size();

        const fs::path p = root / "test.matgraph";
        check("saves", save_material_graph(p.string(), g));

        MaterialGraph r;
        check("loads", load_material_graph(p.string(), r));

        // The constructor seeds an Output node. A loader that did not account
        // for it would leave two, and generate() would follow the wrong one.
        check("node count is preserved (no duplicated Output)", r.nodes().size() == before_nodes);
        check("link count is preserved", r.links().size() == before_links);

        int outputs = 0;
        for (const Node& n : r.nodes()) if (n.kind == NodeKind::Output) ++outputs;
        check("exactly one Output node", outputs == 1);

        // Literals, individually — the fields a forgetful writer drops.
        const Node* rtint = nullptr; const Node* rtex = nullptr;
        const Node* rrough = nullptr; const Node* rsplit = nullptr;
        for (const Node& n : r.nodes()) {
            if (n.kind == NodeKind::ConstColor)   rtint  = &n;
            if (n.kind == NodeKind::TextureSample) rtex  = &n;
            if (n.kind == NodeKind::ConstFloat)   rrough = &n;
            if (n.kind == NodeKind::Split)        rsplit = &n;
        }
        check("ConstColor survives all four channels",
              rtint && approx(rtint->value[0], 0.25f) && approx(rtint->value[1], 0.5f) &&
              approx(rtint->value[2], 0.75f) && approx(rtint->value[3], 0.125f));
        check("ConstFloat literal survives", rrough && approx(rrough->value[0], 0.375f));
        check("texture slot survives", rtex && rtex->param_index == 3);
        check("split channel survives", rsplit && rsplit->param_index == 2);
        check("canvas position survives", rtint && approx(rtint->x, 130.0f) && approx(rtint->y, 200.0f));

        // The assertion that covers everything at once.
        check("generates byte-identical GLSL", r.generate().glsl == before_glsl);
        check("and the GLSL is not empty (so the comparison means something)",
              !before_glsl.empty());
    }

    std::printf("\n[group] material graph — a link into a missing node\n");
    {
        // Hand-edited or truncated files happen. A link naming a node that is
        // not in the file must be DROPPED: reconnecting it to something else
        // would silently rewire the material into one that still compiles.
        const std::string text =
            "MATGRAPH_VERSION=1\n"
            "OUTPUT=0\n"
            "NODE.0.ID=0\nNODE.0.KIND=Output\nNODE.0.POS=500,100\n"
            "NODE.1.ID=1\nNODE.1.KIND=ConstFloat\nNODE.1.VALUE=0.5,0,0,0\nNODE.1.POS=100,100\n"
            "LINK.0.SRC=1\nLINK.0.DST=0\nLINK.0.INPUT=1\n"
            "LINK.1.SRC=99\nLINK.1.DST=0\nLINK.1.INPUT=2\n";
        MaterialGraph r;
        material_graph_from_text(text, r);
        check("the valid link is kept", r.links().size() == 1);
        check("the dangling link is dropped, not remapped",
              r.links().empty() || r.links()[0].dst_input == 1);
    }

    std::printf("\n[group] material graph — garbage never throws\n");
    {
        MaterialGraph r;
        material_graph_from_text("not a graph\n\x01\x02\x03\nNODE.x.ID=zzz\n=novalue\n", r);
        // A damaged file yields a usable empty document, because there is no
        // error path in the editor for "your material graph is corrupt".
        check("survives garbage with a usable graph", r.output_node() != kInvalidNode);
        check("and holds only the Output node", r.nodes().size() == 1);
    }

    // =======================================================================
    // AnimGraph
    // =======================================================================
    std::printf("\n[group] anim graph — states, transitions, conditions\n");
    {
        AnimGraph g;
        g.clear();
        const uint32_t idle = g.add_state("Idle", 40.0f, 50.0f);
        const uint32_t walk = g.add_state("Walk Forward", 200.0f, 50.0f);
        const uint32_t die  = g.add_state("Death", 200.0f, 260.0f);
        g.set_entry_state(walk);            // deliberately NOT the first state

        g.state(walk)->clip_index        = 7;
        g.state(walk)->loop              = false;
        g.state(walk)->speed             = 1.75f;
        g.state(walk)->apply_root_motion = true;

        g.add_transition(idle, walk);
        g.transitions().back().blend_duration = 0.45f;
        g.transitions().back().has_exit_time  = true;
        g.transitions().back().exit_time      = 0.8f;
        g.transitions().back().conditions.push_back({"Speed", AnimGraphCondOp::GreaterEqual, 2.5f});
        g.transitions().back().conditions.push_back({"Grounded", AnimGraphCondOp::True, 0.0f});

        // Any State is a pseudo-state with no table entry; a loader that
        // remapped it like a normal id would drop every interrupt transition,
        // and nobody notices until a death animation stops playing.
        g.add_transition(kAnyState, die);
        g.transitions().back().conditions.push_back({"Dead", AnimGraphCondOp::TriggerSet, 0.0f});

        const auto before_problems   = g.validate();
        const auto before_unreached  = g.unreachable_states();

        const fs::path p = root / "test.animgraph";
        check("saves", save_anim_graph(p.string(), g));

        AnimGraph r;
        check("loads", load_anim_graph(p.string(), r));

        // The constructor seeds an "Idle" state; without clear() on load it
        // would sit alongside the file's three, gaining one orphan per open.
        check("state count is exact (no leftover seeded state)", r.states().size() == 3);
        check("transition count is exact", r.transitions().size() == 2);

        const AnimGraphState* rw = nullptr;
        for (const AnimGraphState& s : r.states()) if (s.name == "Walk Forward") rw = &s;
        check("a name with a space survives", rw != nullptr);
        check("clip index survives",   rw && rw->clip_index == 7);
        check("loop=false survives",   rw && rw->loop == false);
        check("speed survives",        rw && approx(rw->speed, 1.75f));
        check("root motion survives",  rw && rw->apply_root_motion == true);
        check("canvas position survives", rw && approx(rw->x, 200.0f) && approx(rw->y, 50.0f));
        check("entry state follows the remap, not the index",
              rw && r.entry_state() == rw->id);

        const AnimGraphTransition* t_idle = nullptr;
        const AnimGraphTransition* t_any  = nullptr;
        for (const AnimGraphTransition& t : r.transitions()) {
            if (t.from == kAnyState) t_any = &t; else t_idle = &t;
        }
        check("the Any-State transition survives", t_any != nullptr);
        check("and still points at Death",
              t_any && r.states().size() == 3 &&
              [&]{ for (const AnimGraphState& s : r.states())
                       if (s.id == t_any->to) return s.name == "Death";
                   return false; }());
        check("blend duration survives",  t_idle && approx(t_idle->blend_duration, 0.45f));
        check("has_exit_time survives",   t_idle && t_idle->has_exit_time == true);
        check("exit time survives",       t_idle && approx(t_idle->exit_time, 0.8f));
        check("both conditions survive",  t_idle && t_idle->conditions.size() == 2);
        check("condition param survives", t_idle && t_idle->conditions.size() == 2 &&
                                          t_idle->conditions[0].param == "Speed");
        check("condition op survives",    t_idle && t_idle->conditions.size() == 2 &&
                                          t_idle->conditions[0].op == AnimGraphCondOp::GreaterEqual);
        check("condition threshold survives", t_idle && t_idle->conditions.size() == 2 &&
                                          approx(t_idle->conditions[0].threshold, 2.5f));
        check("a threshold-less op survives", t_idle && t_idle->conditions.size() == 2 &&
                                          t_idle->conditions[1].op == AnimGraphCondOp::True);

        check("validation agrees",  r.validate().size() == before_problems.size());
        check("reachability agrees", r.unreachable_states().size() == before_unreached.size());
    }

    std::printf("\n[group] anim graph — the runtime graph's parts: layers, blend spaces, slots\n");
    {
        // The graph a soldier runs when it names none, from its clips.
        HumanoidClips hc;
        hc.idle = 0; hc.walk = 1; hc.run = 2; hc.aim = 5; hc.walk_back = 22; hc.strafe_left = 14; hc.strafe_right = 15;
        hc.jump = 23; hc.fall = 24; hc.land = 25; hc.run_back = 28; hc.strafe_run_left = 29; hc.strafe_run_right = 30; hc.death = 10;
        hc.crouch = 3; hc.crouch_walk = 4; hc.melee_idle = 27;
        auto measured = [](int clip, glm::vec2 fb) {
            switch (clip) {
                case 1: return glm::vec2(0.0f, 1.18f);
                case 2: return glm::vec2(0.0f, 2.67f);
                case 22: return glm::vec2(0.0f, -0.95f);
                case 28: return glm::vec2(0.0f, -2.03f);
                case 14: return glm::vec2(1.02f, 0.0f);
                case 15: return glm::vec2(-1.02f, 0.0f);
                case 29: return glm::vec2(2.01f, 0.0f);
                case 30: return glm::vec2(-2.01f, 0.0f);
                default: return fb;
            }
        };
        const HumanoidGraph h = build_humanoid_graph(hc, measured, "spine");
        check("the humanoid graph: a base and an upper layer, two slots", h.def && h.def->layers.size() == 2 && h.def->slots.size() == 2 &&
                                                                            h.def->layers[1].mask_bone == "spine");
        const auto& loco = h.def->layers[0].states[static_cast<size_t>(h.s_loco)];
        check("its locomotion: a 2D space of 8 clips (and the middle) at their measured velocities",
              loco.kind == schizo::renderer::animgraph::NodeKind::Blend2D && loco.space.samples.size() == 9 && !loco.space.line &&
                  loco.space.samples[2].at == glm::vec2(0.0f, 2.67f));
        float slow = 0.0f, turn = 0.0f;
        const glm::vec2 q = humanoid_loco_query(h, glm::vec2(0.0f, 0.6f), &slow, &turn);
        check("half a walk's speed is asked at the walk (0, 1.18), its clips half as quick", approx(q.y, 1.18f) && approx(slow, 0.6f / 1.18f) && turn == 0.0f);
        const glm::vec2 qd = humanoid_loco_query(h, glm::vec2(0.5f, 0.5f), &slow, &turn);
        check("a slow diagonal: out to the ring between walk and strafe", qd.x > 0.5f && qd.y > 0.5f && slow < 1.0f && turn == 0.0f);
        HumanoidClips fwd_only;
        fwd_only.idle = 0; fwd_only.walk = 1; fwd_only.run = 2;
        const HumanoidGraph hf = build_humanoid_graph(fwd_only, measured, "");
        humanoid_loco_query(hf, glm::vec2(2.0f, 0.0f), &slow, &turn);
        check("no strafes: going sideways walks ahead with the legs turned (50 degrees at most)", std::abs(turn) > 0.85f && std::abs(turn) < 0.88f);

        // To a document, a file, a document again, and a runtime graph again.
        auto name_of = [](int c) { return std::string("clip_") + std::to_string(c); };
        const AnimGraph doc = document_from_graph(*h.def, name_of);
        const fs::path gp = root / "humanoid.animgraph";
        check("the humanoid graph saves as a document", save_anim_graph(gp.string(), doc));
        AnimGraph back;
        check("and loads", load_anim_graph(gp.string(), back));
        check("states, transitions, parameters, layers and slots all come back",
              back.states().size() == doc.states().size() && back.transitions().size() == doc.transitions().size() &&
                  back.params().size() == doc.params().size() && back.layers().size() == 1 && back.layers()[0].mask_bone == "spine" &&
                  back.slots().size() == 2 && back.slots()[0].name == "upper");
        const AnimGraphState* bl = nullptr;
        for (const auto& st : back.states()) if (st.name == "Locomotion") bl = &st;
        check("a blend state's kind, parameters, sync, rate and samples come back",
              bl && bl->kind == AnimGraphStateKind::Blend2D && bl->param_x == "vx" && bl->param_y == "vz" && bl->radial && bl->sync == "feet" &&
                  bl->rate_param == "loco_rate" && bl->samples.size() == 9 && bl->samples[2].clip == "clip_2" && bl->samples[2].measured);
        for (const auto& pr : back.validate()) std::printf("    (problem: %s)\n", pr.c_str());
        check("no problems with it", back.validate().empty());
        auto find = [](const std::string& n) { return n.rfind("clip_", 0) == 0 ? std::stoi(n.substr(5)) : -1; };
        const RuntimeGraphBuild rb = build_runtime_graph(back, find, measured);
        bool same = rb.def && rb.problems.empty() && rb.def->layers.size() == h.def->layers.size();
        for (size_t l = 0; same && l < rb.def->layers.size(); ++l) {
            const auto& a = rb.def->layers[l];
            const auto& b = h.def->layers[l];
            same = a.states.size() == b.states.size() && a.transitions.size() == b.transitions.size() && a.entry == b.entry && a.mask_bone == b.mask_bone;
            for (size_t i = 0; same && i < a.states.size(); ++i)
                same = a.states[i].name == b.states[i].name && a.states[i].clip == b.states[i].clip && a.states[i].kind == b.states[i].kind &&
                       a.states[i].space.samples.size() == b.states[i].space.samples.size() && (a.states[i].sync >= 0) == (b.states[i].sync >= 0);
            for (size_t i = 0; same && i < a.transitions.size(); ++i)
                same = a.transitions[i].from == b.transitions[i].from && a.transitions[i].to == b.transitions[i].to &&
                       a.transitions[i].when.size() == b.transitions[i].when.size() && approx(a.transitions[i].blend, b.transitions[i].blend);
        }
        check("built again it is the same graph (states, clips, kinds, samples, transitions, entries, masks)", same);
        AnimGraph bad = back;
        for (auto& st : bad.states()) if (st.name == "Idle") st.clip = "no such clip";
        const RuntimeGraphBuild rbad = build_runtime_graph(bad, find, measured);
        check("a clip the model lacks is listed as a problem, not fatal", rbad.def && rbad.problems.size() == 1);
    }

    std::printf("\n[group] anim graph — enums are names, not indices\n");
    {
        // The file must not encode operators positionally. If it did, inserting
        // one into the enum would silently change every saved graph, with no
        // error, because every index still parses.
        AnimGraph g;
        g.clear();
        const uint32_t a = g.add_state("A", 0, 0);
        const uint32_t b = g.add_state("B", 0, 0);
        g.add_transition(a, b);
        g.transitions().back().conditions.push_back({"P", AnimGraphCondOp::NotEquals, 1.0f});
        const std::string text = anim_graph_to_text(g);
        check("the operator is written by name",
              text.find("NotEquals") != std::string::npos);
        check("and the condition line carries the param",
              text.find("|P") != std::string::npos);
    }

    std::printf("\n[group] anim graph — garbage never throws\n");
    {
        AnimGraph r;
        anim_graph_from_text("garbage\nSTATE.q.NAME=x\nTRANS.0.COND=nonsense\n", r);
        check("survives garbage", r.states().empty() && r.transitions().empty());
    }

    // =======================================================================
    // Sequence
    // =======================================================================
    std::printf("\n[group] sequence — tracks and curve shape\n");
    {
        Sequence s;
        s.set_duration(12.5f);

        Track& t1 = s.add_track(42, TrackTarget::RotationYaw);
        t1.curve.add(CurveKey{0.0f, 0.0f, 0.0f, 0.0f});
        t1.curve.add(CurveKey{1.0f, 90.0f, 0.25f, -0.5f});   // non-zero tangents
        t1.curve.add(CurveKey{2.5f, 45.0f, 0.0f, 0.0f});

        Track& t2 = s.add_track(7, TrackTarget::ScaleUniform);
        t2.enabled = false;                                   // non-default
        t2.curve.add(CurveKey{0.0f, 1.0f, 0.0f, 0.0f});
        t2.curve.add(CurveKey{3.0f, 2.0f, 0.0f, 0.0f});

        const fs::path p = root / "test.seq";
        check("saves", save_sequence(p.string(), s));

        Sequence r;
        check("loads", load_sequence(p.string(), r));
        check("duration survives", approx(r.duration(), 12.5f));
        check("track count survives", r.track_count() == 2);

        const Track* r1 = nullptr; const Track* r2 = nullptr;
        for (const Track& t : r.tracks()) {
            if (t.target == TrackTarget::RotationYaw)   r1 = &t;
            if (t.target == TrackTarget::ScaleUniform)  r2 = &t;
        }
        check("entity id survives", r1 && r1->entity_id == 42);
        check("target enum survives", r1 != nullptr);
        check("enabled=false survives", r2 && r2->enabled == false);
        check("key count survives", r1 && r1->curve.size() == 3);

        // Tangents are the field most likely to be silently dropped, and losing
        // them changes the EASING without changing any keyed value — the curve
        // still passes through every point, so it looks correct in the editor.
        bool tangents_ok = false;
        if (r1 && r1->curve.size() == 3) {
            const CurveKey& k = r1->curve.keys()[1];
            tangents_ok = approx(k.in_tangent, 0.25f) && approx(k.out_tangent, -0.5f);
        }
        check("tangents survive (easing, not just the keyed points)", tangents_ok);

        // The behavioural assertion: same values at the same times.
        bool same = true;
        for (float t = 0.0f; t <= 3.0f; t += 0.1f) {
            const auto a = s.evaluate(t);
            const auto b = r.evaluate(t);
            if (a.size() != b.size()) { same = false; break; }
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].entity_id != b[i].entity_id || a[i].target != b[i].target ||
                    !approx(a[i].value, b[i].value)) { same = false; break; }
            if (!same) break;
        }
        check("evaluates identically across the whole timeline", same);
    }

    std::printf("\n[group] sequence — a hand-edited duplicate track\n");
    {
        // add_track enforces one curve per entity+target. A file naming one
        // twice must not produce two, or whichever won would be arbitrary and
        // the loser invisible in the UI.
        const std::string text =
            "SEQUENCE_VERSION=1\nDURATION=4\n"
            "TRACK.0.ENTITY=5\nTRACK.0.TARGET=PositionX\nTRACK.0.CURVE=0,1,0,0\n"
            "TRACK.1.ENTITY=5\nTRACK.1.TARGET=PositionX\nTRACK.1.CURVE=0,9,0,0\n";
        Sequence r;
        sequence_from_text(text, r);
        check("a duplicated track collapses to one", r.track_count() == 1);
    }

    std::printf("\n[group] sequence — garbage never throws\n");
    {
        Sequence r;
        sequence_from_text("???\nTRACK.0.CURVE=1,2\nDURATION=abc\n", r);
        check("survives garbage", r.track_count() <= 1);
        check("a truncated curve key is dropped, not padded",
              r.tracks().empty() || r.tracks()[0].curve.empty());
    }

    // =======================================================================
    // Cross-cutting
    // =======================================================================
    std::printf("\n[group] a missing file is reported, an empty one is not an error\n");
    {
        MaterialGraph mg; AnimGraph ag; Sequence sq;
        check("load_material_graph reports a missing file",
              !load_material_graph((root / "nope.matgraph").string(), mg));
        check("load_anim_graph reports a missing file",
              !load_anim_graph((root / "nope.animgraph").string(), ag));
        check("load_sequence reports a missing file",
              !load_sequence((root / "nope.seq").string(), sq));

        // Absent and unparseable are different: only the first is a false
        // return, or a missing asset becomes a silently blank one.
        const fs::path empty = root / "empty.seq";
        { std::ofstream(empty.string()); }
        Sequence e;
        check("an empty file loads (absent != unparseable)", load_sequence(empty.string(), e));
    }

    std::printf("\n[group] the version stamp is written\n");
    {
        MaterialGraph mg; AnimGraph ag; Sequence sq;
        check("material graph is stamped",
              material_graph_to_text(mg).rfind("MATGRAPH_VERSION=", 0) == 0);
        check("anim graph is stamped",
              anim_graph_to_text(ag).rfind("ANIMGRAPH_VERSION=", 0) == 0);
        check("sequence is stamped",
              sequence_to_text(sq).rfind("SEQUENCE_VERSION=", 0) == 0);
    }

    std::printf("\n[group] the starter documents the New menu writes\n");
    {
        // Loading THE ACTUAL template, not a copy. v0.8.0 shipped an
        // editor-extension template that could not run because its test built
        // its own version; one definition, pointed at from both places, is the
        // fix. If the New menu ever writes something the loader rejects, the
        // very first thing a person does with this feature fails.
        MaterialGraph mg;
        material_graph_from_text(starter_material_graph_text(), mg);
        check("the starter material graph loads", mg.nodes().size() == 2);
        const auto gen = mg.generate();
        check("and generates working GLSL", gen.ok && !gen.glsl.empty());
        check("with base colour actually driven", mg.links().size() == 1);

        AnimGraph ag;
        anim_graph_from_text(starter_anim_graph_text(), ag);
        check("the starter anim graph loads with a state", ag.states().size() == 1);
        check("and that state is the entry point",
              !ag.states().empty() && ag.entry_state() == ag.states()[0].id);
        check("so it validates clean", ag.validate().empty());

        Sequence sq;
        sequence_from_text(starter_sequence_text(), sq);
        check("the starter sequence loads", sq.duration() > 0.0f);

        // A starter that does not survive its OWN round trip would hand every
        // new document a one-way trip to disk.
        check("each starter round-trips to itself",
              material_graph_to_text(mg) == starter_material_graph_text() &&
              anim_graph_to_text(ag)     == starter_anim_graph_text() &&
              sequence_to_text(sq)       == starter_sequence_text());
    }

    fs::remove_all(root, ec);
    std::printf("\n%s — %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
