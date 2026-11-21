// animhooks_check: the animation hooks and inertialization (editor/include/anim_hooks.h),
// headless. Clip events are gameplay: a bolt that leaves its port twice, a bite that
// never lands or a footstep heard on the wrong loop is a bug no one sees in a still
// frame. The spring decides whether a change of clip pops or flows.
#include "anim_hooks.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace schizo::editor::anim;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    // ---- the spring --------------------------------------------------------------
    {
        // From rest, (1 + 2 ln 2) / 4 of an offset is left after one half-life (the
        // critically damped spring's closed form), whatever the step size.
        for (float step : {1.0f / 30, 1.0f / 60, 1.0f / 144, 0.1f}) {
            glm::vec3 x(1.0f, 0, 0), v(0.0f);
            float t = 0;
            while (t + step <= 0.2f + 1e-6f) { decay(x, v, 0.2f, step); t += step; }
            decay(x, v, 0.2f, 0.2f - t);
            char what[96];
            std::snprintf(what, sizeof what, "decay: 59.7%% left after one half-life at dt %.4f (%.4f)", step, x.x);
            check(std::abs(x.x - (1 + 2 * 0.69314718f) / 4) < 1e-3f, what);
        }
        // Critically damped: from rest it never swings past zero, and it is all but gone.
        glm::vec3 x(1.0f, -2.0f, 0.5f), v(0.0f);
        bool crossed = false;
        for (int i = 0; i < 400; ++i) {
            decay(x, v, 0.05f, 1.0f / 60);
            crossed = crossed || x.x < -1e-4f || x.y > 1e-4f || x.z < -1e-4f;
        }
        check(!crossed, "decay: no overshoot from rest");
        check(glm::length(x) < 1e-4f, "decay: gone after many half-lives");
        // Moving away, it carries on a little, then comes back (the old motion flows on).
        glm::vec3 y(0.1f, 0, 0), w(3.0f, 0, 0);
        decay(y, w, 0.1f, 1.0f / 60);
        check(y.x > 0.1f, "decay: an offset moving away goes on a little first");
        for (int i = 0; i < 300; ++i) decay(y, w, 0.1f, 1.0f / 60);
        check(std::abs(y.x) < 1e-3f, "decay: ...and still comes to nothing");
        // Exact for any dt: one big step = many small ones.
        glm::vec3 a(1.0f, 0, 0), av(0.5f, 0, 0), b = a, bv = av;
        decay(a, av, 0.1f, 0.3f);
        for (int i = 0; i < 300; ++i) decay(b, bv, 0.1f, 0.001f);
        check(std::abs(a.x - b.x) < 1e-3f && std::abs(av.x - bv.x) < 1e-2f, "decay: one big step equals many small ones");
        check(std::abs(halflife_for_blend(0.2f) - 0.05f) < 1e-6f && halflife_for_blend(0.0f) > 0.0f, "halflife_for_blend: a quarter, never zero");
    }
    // ---- rotations as vectors ------------------------------------------------------------
    {
        const glm::quat q = glm::angleAxis(1.2f, glm::normalize(glm::vec3(0.3f, 1.0f, -0.4f)));
        const glm::quat r = from_scaled_axis(to_scaled_axis(q));
        check(std::abs(glm::dot(q, r)) > 0.99999f, "scaled axis: there and back");
        check(std::abs(glm::length(to_scaled_axis(-q)) - 1.2f) < 1e-4f, "scaled axis: q and -q are the same turn, the short way");
        check(glm::length(to_scaled_axis(glm::quat(1, 0, 0, 0))) < 1e-6f, "scaled axis: no turn is zero");
    }
    // ---- the events file -----------------------------------------------------------------
    const std::vector<std::string> clips = {"idle", "bite", "dorsal_salvo", "Armature|walk_cycle"};
    const char* text =
        "# a comment\n"
        "bite 0.45 bite_open\n"
        "dorsal_salvo 1.20 bolt 1   # the second port\n"
        "dorsal_salvo 1.05 bolt 0\n"
        "\n"
        "walk 0.0 footstep left foot\n"
        "nonesuch 1 nothing\n"
        "bite oops\n";
    ClipEvents events;
    std::vector<std::string> warnings;
    std::istringstream in(text);
    const int read = parse_clip_events(in, [&](const std::string& c) { return find_clip(clips, c); }, events, &warnings);
    check(read == 4, "events: four lines read");
    check(warnings.size() == 2, "events: an unknown clip and a bad line are warned about");
    check(events[2].size() == 2 && events[2][0].param == "0" && events[2][1].param == "1", "events: sorted by time, params kept");
    check(events[3].size() == 1 && events[3][0].param == "left foot", "events: a clip found by part of its name; a param of several words");
    check(events[1].size() == 1 && events[1][0].name == "bite_open" && std::abs(events[1][0].time - 0.45f) < 1e-6f, "events: name and time");
    check(find_clip(clips, "BITE") == 1 && find_clip(clips, "run") == -1, "find_clip: any case; -1 when none");
    // ---- which events a step passes --------------------------------------------------------
    {
        std::vector<ClipEvent> list = {{0.0f, "start", ""}, {0.5f, "mid", ""}, {1.0f, "end", ""}};
        std::vector<size_t> out;
        events_passed(list, -1e-4f, 0.1f, 1.0f, false, out);
        check(out.size() == 1 && out[0] == 0, "passed: the first step hears an event at 0");
        out.clear();
        events_passed(list, 0.1f, 0.5f, 1.0f, false, out);
        check(out.size() == 1 && out[0] == 1, "passed: an event exactly at the step's end is heard");
        out.clear();
        events_passed(list, 0.5f, 0.9f, 1.0f, false, out);
        check(out.empty(), "passed: ...and not again on the next step");
        out.clear();
        events_passed(list, 0.9f, 3.0f, 1.0f, false, out);
        check(out.size() == 1 && out[0] == 2, "passed: a clip played once passes its end once, however long the step");
        out.clear();
        events_passed(list, 0.9f, 1.6f, 1.0f, true, out);
        check(out.size() == 3, "passed: a loop wraps: end, start and mid of the next pass");
        out.clear();
        events_passed(list, 0.2f, 0.2f, 1.0f, true, out);
        check(out.empty(), "passed: no time, no events");
        // A long run of frames hears each event of a loop once per pass: 10 passes.
        const std::vector<ClipEvent> steps = {{0.25f, "left", ""}, {0.5f, "breath", ""}, {0.75f, "right", ""}};
        int heard = 0;
        float t = -1e-4f;
        for (int i = 0; i < 600; ++i) { out.clear(); const float n = (i + 1) / 60.0f; events_passed(steps, t, n, 1.0f, true, out); heard += static_cast<int>(out.size()); t = n; }
        char loop_what[96];
        std::snprintf(loop_what, sizeof loop_what, "passed: 10 s of a 1 s loop at 60 fps: each event 10 times (%d of 30)", heard);
        check(heard == 30, loop_what);
        // The Leviathan's salvo: nine ports, each once, at 60 fps and at 20 fps.
        std::vector<ClipEvent> ports;
        const float at[9] = {1.05f, 1.20f, 1.35f, 1.45f, 1.60f, 1.75f, 1.90f, 2.05f, 2.15f};
        for (int i = 0; i < 9; ++i) ports.push_back({at[i], "bolt", std::to_string(i)});
        for (float fps : {60.0f, 20.0f, 7.0f}) {
            int fired = 0;
            float was = -1e-4f;
            for (float now = 1.0f / fps; was < 3.6f; now += 1.0f / fps) { out.clear(); events_passed(ports, was, now, 3.6f, false, out); fired += static_cast<int>(out.size()); was = now; }
            char what[80];
            std::snprintf(what, sizeof what, "passed: a salvo fires nine bolts at %.0f fps", fps);
            check(fired == 9, what);
        }
    }
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
