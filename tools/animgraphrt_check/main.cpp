// animgraphrt_check: the animation graph runtime (anim_graph_runtime.h), headless.
// Blend spaces (Delaunay triangles reproduce the point they are asked at; outside,
// the edge along the way from the origin and how far past), sync markers (a
// phase between foot plants, there and back), and a graph on a fake model:
// a blend state's pose, transitions on bools, triggers and exit times, any-state
// interrupts, a masked upper layer, montages on a slot coming and going, phase
// carried between states of one sync group. A graph fails quietly -- the
// character just does not do the thing -- so it is asserted here.
#include "anim_graph_runtime.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace schizo::renderer;
using namespace schizo::renderer::animgraph;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
std::string f2(float x) { char b[32]; std::snprintf(b, sizeof b, "%.3f", x); return b; }

// A model of 4 bones (0 root > 1 hips > 2 spine > 3 head). Clip c sets every bone's
// position x to its value (idle 0, walk 1, run 2, aim 5, shot 7, die 9) and y to its time.
struct Fake : ClipSource {
    struct C { float dur, value; std::vector<float> l, r; };
    std::vector<C> clips = {
        {1.0f, 0.0f, {}, {}},                  // 0 idle
        {1.0f, 1.0f, {0.2f}, {0.7f}},          // 1 walk: left down at 0.2, right at 0.7
        {0.6f, 2.0f, {0.1f}, {0.4f}},          // 2 run: left at 0.1, right at 0.4
        {1.0f, 5.0f, {}, {}},                  // 3 aim
        {0.3f, 7.0f, {}, {}},                  // 4 shot
        {0.8f, 9.0f, {}, {}},                  // 5 die
    };
    Pose bind_pose = Pose(4);
    int clip_count() const override { return static_cast<int>(clips.size()); }
    float duration(int c) const override { return clips[static_cast<size_t>(c)].dur; }
    void sample(int c, float t, bool, Pose& pose) const override {
        for (auto& b : pose) b.position = glm::vec3(clips[static_cast<size_t>(c)].value, t, 0.0f);
    }
    std::vector<SyncMarkers> sync = [this] {
        std::vector<SyncMarkers> m;
        for (const auto& k : clips) m.push_back(SyncMarkers::from_contacts(k.dur, k.l, k.r));
        return m;
    }();
    const SyncMarkers& markers(int c) const override { return sync[static_cast<size_t>(c)]; }
    const Pose& bind() const override { return bind_pose; }
};
}  // namespace

int main() {
    {
        // A locomotion space: idle in the middle, walks and runs ahead, back and to the sides.
        BlendSpace bs;
        const glm::vec2 at[] = {{0, 0}, {0, 1.2f}, {0, 2.7f}, {0, -1.0f}, {0, -2.0f}, {1.0f, 0}, {-1.0f, 0}, {2.0f, 0}, {-2.0f, 0}};
        for (int i = 0; i < 9; ++i) bs.samples.push_back({i, at[i], 1.0f});
        bs.build();
        check(!bs.line && bs.tris.size() >= 8 && bs.holds_origin(), "a 2D space triangulates (" + std::to_string(bs.tris.size()) + " triangles) around its origin");
        std::vector<BlendWeight> w;
        float worst = 0.0f;
        for (glm::vec2 p : {glm::vec2(0.3f, 0.5f), glm::vec2(-0.7f, 1.4f), glm::vec2(1.2f, -0.6f), glm::vec2(0.0f, 2.0f), glm::vec2(-1.5f, -0.2f)}) {
            bs.weights(p, true, w);
            glm::vec2 got(0.0f);
            float sum = 0.0f;
            for (const auto& x : w) { got += bs.samples[static_cast<size_t>(x.sample)].at * x.w; sum += x.w; }
            worst = std::max({worst, glm::length(got - p), std::abs(sum - 1.0f)});
        }
        check(worst < 1e-4f, "inside, the weights sum to 1 and put the samples back on the point asked (" + f2(worst) + " off)");
        bs.weights(glm::vec2(0.0f, 1.2f), true, w);
        check(w.size() == 1 && w[0].sample == 1 && std::abs(w[0].w - 1.0f) < 1e-4f, "on a sample: that sample alone");
        float over = 0.0f;
        bs.weights(glm::vec2(0.0f, 5.0f), true, w, &over);
        check(w.size() == 1 && w[0].sample == 2 && std::abs(over - 5.0f / 2.7f) < 1e-3f, "5 m/s ahead past a 2.7 run: the run, " + f2(over) + " times past it (1.852)");
        bs.weights(glm::vec2(3.0f, 3.0f), true, w, &over);
        glm::vec2 edge(0.0f);
        for (const auto& x : w) edge += bs.samples[static_cast<size_t>(x.sample)].at * x.w;
        const float cross = edge.x * 3.0f - edge.y * 3.0f;
        check(std::abs(cross) < 1e-3f && over > 1.0f && std::abs(glm::length(edge) * over - glm::length(glm::vec2(3, 3))) < 1e-3f,
              "a diagonal past the edge: the edge along the same way (" + f2(edge.x) + ", " + f2(edge.y) + "), " + f2(over) + " times past");
        bs.weights(glm::vec2(3.0f, 3.0f), false, w, &over);
        check(!w.empty() && over == 1.0f, "(not radial: the nearest point of the edge)");

        BlendSpace line;
        line.samples = {{0, {0, 0}, 1}, {1, {1.2f, 0}, 1}, {2, {2.7f, 0}, 1}};
        line.build();
        line.weights(glm::vec2(2.0f, 0.0f), true, w, &over);
        float v = 0.0f;
        for (const auto& x : w) v += line.samples[static_cast<size_t>(x.sample)].at.x * x.w;
        check(line.line && std::abs(v - 2.0f) < 1e-4f && over == 1.0f, "a 1D space: 2.0 between the 1.2 walk and the 2.7 run");
        line.weights(glm::vec2(4.0f, 0.0f), true, w, &over);
        check(w.size() == 1 && w[0].sample == 2 && std::abs(over - 4.0f / 2.7f) < 1e-3f, "past its end: the run, " + f2(over) + " times past");
    }
    {
        const SyncMarkers m = SyncMarkers::from_contacts(0.6f, {0.1f}, {0.4f});
        check(std::abs(m.phase_of(0.1f)) < 1e-4f && std::abs(m.phase_of(0.4f) - 0.5f) < 1e-4f && std::abs(m.phase_of(0.25f) - 0.25f) < 1e-4f &&
                  std::abs(m.phase_of(0.55f) - 0.75f) < 1e-4f && std::abs(m.phase_of(0.05f) - 0.9166667f) < 1e-3f,
              "sync markers: the left foot down is 0, the right 0.5, between them in proportion (wrapping)");
        check(std::abs(m.time_of(0.75f, 0.0f) - 0.55f) < 1e-4f && std::abs(m.time_of(0.0f, 0.3f) - 0.1f) < 1e-4f, "and back from a phase to the clip's time");
        const SyncMarkers two = SyncMarkers::from_contacts(1.0f, {0.0f, 0.5f}, {0.25f, 0.75f});
        check(two.strides() == 2 && std::abs(two.phase_of(0.6f) - 0.2f) < 1e-4f && std::abs(two.time_of(0.2f, 0.62f) - 0.6f) < 1e-4f &&
                  std::abs(two.time_of(0.2f, 0.05f) - 0.1f) < 1e-4f,
              "a clip of two strides: the phase runs twice; back to the stride nearest the time it was at");
        const SyncMarkers same = SyncMarkers::from_contacts(1.0f, {0.1f, 0.3f}, {0.6f});
        check(!same.valid() && std::abs(same.phase_of(0.25f) - 0.25f) < 1e-4f, "feet that do not alternate are no markers: the phase is the clip's fraction");
    }
    {
        Fake model;
        auto def = std::make_shared<GraphDef>();
        const int speed = def->add_param("speed", ParamType::Float);
        const int dead = def->add_param("dead", ParamType::Bool);
        const int aiming = def->add_param("aiming", ParamType::Bool);
        const int jump = def->add_param("jump", ParamType::Trigger);
        const int feet = def->sync_group("feet");
        LayerDef base;
        base.name = "Base";
        StateDef loco;
        loco.name = "Locomotion";
        loco.kind = NodeKind::Blend1D;
        loco.space.samples = {{0, {0, 0}, 1}, {1, {1.2f, 0}, 1}, {2, {2.7f, 0}, 1}};
        loco.space.build();
        loco.param_x = speed;
        loco.radial = true;
        loco.sync = feet;
        base.states.push_back(loco);
        StateDef die;
        die.name = "Dead";
        die.clip = 5;
        die.loop = false;
        base.states.push_back(die);
        StateDef hop;
        hop.name = "Hop";
        hop.clip = 4;
        hop.loop = false;
        base.states.push_back(hop);
        StateDef run;
        run.name = "Run";
        run.clip = 2;
        run.sync = feet;
        base.states.push_back(run);
        base.transitions.push_back({-1, 1, {{dead, Op::True, 0}}, 0.12f, -1.0f});
        base.transitions.push_back({1, 0, {{dead, Op::False, 0}}, 0.0f, -1.0f});
        base.transitions.push_back({0, 2, {{jump, Op::Triggered, 0}}, 0.1f, -1.0f});
        base.transitions.push_back({2, 0, {}, 0.2f, 1.0f});
        base.transitions.push_back({0, 3, {{speed, Op::Greater, 9.0f}}, 0.2f, -1.0f});
        def->layers.push_back(base);
        LayerDef upper;
        upper.name = "Upper";
        upper.mask_bone = "spine";
        StateDef none;
        none.name = "None";
        none.kind = NodeKind::None;
        upper.states.push_back(none);
        StateDef aim;
        aim.name = "Aim";
        aim.clip = 3;
        upper.states.push_back(aim);
        upper.transitions.push_back({0, 1, {{aiming, Op::True, 0}}, 0.15f, -1.0f});
        upper.transitions.push_back({1, 0, {{aiming, Op::False, 0}}, 0.2f, -1.0f});
        def->layers.push_back(upper);
        def->slots.push_back({"upper", "spine"});
        def->slots.push_back({"full", ""});

        GraphInstance g;
        const std::vector<int> parent = {-1, 0, 1, 2};
        g.init(def, parent, [](const std::string& n) { return n == "spine" ? 2 : -1; });
        Pose pose;
        g.set(speed, 2.0f);
        g.update(1.0f / 60.0f, model);
        g.evaluate(model, pose);
        const float want = (1.0f * (0.7f / 1.5f) + 2.0f * (0.8f / 1.5f));   // 2.0 is 0.8 of the way from 1.2 to 2.7
        check(g.state_name(0) == "Locomotion" && std::abs(pose[0].position.x - want) < 1e-3f,
              "a blend state at 2.0 m/s poses walk and run by their weights (" + f2(pose[0].position.x) + ", " + f2(want) + ")");
        // The phase: at 2.0 a stride every (walk 1.0 s, run 0.6 s) weighted: pace 0.467*1 + 0.533*1.667.
        const float pace = (0.7f / 1.5f) * 1.0f + (0.8f / 1.5f) * (1.0f / 0.6f);
        GraphInstance g2;
        g2.init(def, parent, [](const std::string& n) { return n == "spine" ? 2 : -1; });
        g2.set(speed, 2.0f);
        for (int i = 0; i < 30; ++i) g2.update(1.0f / 60.0f, model);
        g2.evaluate(model, pose);
        // Both clips at the same phase: walk's and run's times are where their left feet were as far along.
        const float ph = SyncMarkers::wrap(0.5f * pace, 1.0f);
        const float tw = model.markers(1).time_of(ph, 0.0f), tr = model.markers(2).time_of(ph, 0.0f);
        check(std::abs(model.markers(1).phase_of(tw) - model.markers(2).phase_of(tr)) < 1e-4f && std::abs(ph - 0.5f * pace) < 1e-3f,
              "half a second in: one phase for both (" + f2(ph) + " of a stride, at " + f2(pace) + " strides a second)");
        auto changes = g.take_changes();
        check(changes.empty(), "no change yet: nothing to smooth");

        g.set_bool(dead, true);
        g.update(1.0f / 60.0f, model);
        changes = g.take_changes();
        check(g.state_name(0) == "Dead" && changes.size() == 1 && changes[0].mask == nullptr && std::abs(changes[0].blend - 0.12f) < 1e-6f,
              "dead: from any state to Dead, a change over 0.12 s of the whole body");
        g.update(1.0f / 60.0f, model);
        check(g.state_name(0) == "Dead" && g.take_changes().empty(), "and it stays (any-state does not re-enter itself)");
        g.set_bool(dead, false);
        g.update(1.0f / 60.0f, model);
        g.take_changes();
        check(g.state_name(0) == "Locomotion", "alive again: back to Locomotion");

        g.trigger(jump);
        g.update(1.0f / 60.0f, model);
        check(g.state_name(0) == "Hop", "a trigger fires its transition");
        g.update(0.1f, model);
        check(g.state_name(0) == "Hop", "a once state with an exit time of 1 waits for its end (0.1 of 0.3 s)");
        g.update(0.25f, model);
        g.update(1.0f / 60.0f, model);
        check(g.state_name(0) == "Locomotion", "and leaves at its end; the trigger was used (no second hop)");

        g.take_changes();
        g.set_bool(aiming, true);
        g.update(1.0f / 60.0f, model);
        changes = g.take_changes();
        g.evaluate(model, pose);
        check(g.state_name(1) == "Aim" && changes.size() == 1 && changes[0].mask && (*changes[0].mask)[2] && (*changes[0].mask)[3] && !(*changes[0].mask)[1] &&
                  pose[2].position.x == 5.0f && pose[3].position.x == 5.0f && pose[1].position.x != 5.0f,
              "aiming: the upper layer's Aim on the spine and head only, the change masked to them");

        g.play_montage(0, 4, 1.0f, 0.06f, 0.2f);
        g.update(1.0f / 60.0f, model);
        changes = g.take_changes();
        g.evaluate(model, pose);
        check(changes.size() == 1 && std::abs(changes[0].blend - 0.06f) < 1e-6f && pose[2].position.x == 7.0f && pose[1].position.x != 7.0f &&
                  g.montage_clip(0) == 4,
              "a montage on the upper slot: the shot over the aim, on its bones, coming in over 0.06 s");
        bool stopped = false;
        for (int i = 0; i < 30 && !stopped; ++i) {
            g.update(1.0f / 60.0f, model);
            for (const auto& c : g.take_changes()) stopped = stopped || std::abs(c.blend - 0.2f) < 1e-6f;
        }
        g.evaluate(model, pose);
        check(stopped && g.montage_clip(0) < 0 && pose[2].position.x == 5.0f, "at its end it goes (over 0.2 s) and the aim is back");

        g.play_montage(1, 5, 1.0f, 0.1f, 0.2f);
        g.update(1.0f / 60.0f, model);
        g.evaluate(model, pose);
        check(pose[0].position.x == 9.0f && pose[3].position.x == 9.0f, "a montage on the full-body slot poses every bone");
        g.stop_montage(1);
        g.take_changes();

        // In step: from the blend state (walk and run) to the Run state of the same group.
        GraphInstance g3;
        g3.init(def, parent, [](const std::string&) { return -1; });
        g3.set(speed, 2.0f);
        for (int i = 0; i < 20; ++i) g3.update(1.0f / 60.0f, model);
        float ph_before = 0.0f;
        {
            g3.update(0.0f, model);
            ph_before = SyncMarkers::wrap(20.0f / 60.0f * pace, 1.0f);
        }
        g3.set(speed, 10.0f);
        g3.update(0.0f, model);
        const float ph_after = model.markers(2).phase_of(g3.state_time(0));
        check(g3.state_name(0) == "Run" && std::abs(ph_after - ph_before) < 2e-3f,
              "to Run in the same sync group: it starts at the same point of the stride (" + f2(ph_before) + " -> " + f2(ph_after) + ")");

        // Events: what plays and its time window.
        GraphInstance g4;
        g4.init(def, parent, [](const std::string&) { return -1; });
        g4.set(speed, 1.2f);
        g4.update(0.1f, model);
        const auto& act = g4.active();
        check(act.size() == 1 && act[0].clip == 1 && act[0].fresh && act[0].now > act[0].was, "the playing clip and its window (the walk at 1.2 m/s, fresh)");
    }

    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
