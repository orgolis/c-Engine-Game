#pragma once
// The parts of character animation that need no GPU and no skeleton: the
// inertialization spring and the clip events (animation hooks). Kept apart from
// imported_skinned_actor.h so animhooks_check can test them headless.
//
// Inertialization (Bollo, "Inertialization: High-Performance Animation
// Transitions in Gears of War", GDC 2018): on a change of clip the new clip
// plays at once and the difference between what was showing and what it says
// now -- an offset per bone, with the velocity the old pose had -- decays to
// nothing. Unlike a crossfade nothing is sampled twice and the old motion
// carries on into the new one. The decay is a critically damped spring (Holden,
// "Spring-It-On: The Game Developer's Spring-Roll-Call"), exact for any dt.
//
// Clip events: a text file beside the model (`<model>.events`), a line per
// event: `clip seconds name [param]`. glTF has nowhere to keep them.
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <istream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::editor::anim {

// ---- inertialization ---------------------------------------------------------------
/// A rotation as a scaled angle-axis vector (2 log q), the form a spring can act on;
/// always the short way round.
inline glm::vec3 to_scaled_axis(glm::quat q) {
    if (q.w < 0.0f) q = -q;
    const glm::vec3 v(q.x, q.y, q.z);
    const float len = glm::length(v);
    if (len < 1e-8f) return 2.0f * v;
    return v * (2.0f * std::atan2(len, q.w) / len);
}
inline glm::quat from_scaled_axis(const glm::vec3& a) {
    const float angle = glm::length(a);
    if (angle < 1e-8f) return glm::normalize(glm::quat(1.0f, a.x * 0.5f, a.y * 0.5f, a.z * 0.5f));
    const glm::vec3 axis = a / angle;
    const float s = std::sin(angle * 0.5f);
    return glm::quat(std::cos(angle * 0.5f), axis.x * s, axis.y * s, axis.z * s);
}
/// An offset `x` moving at `v` decays toward zero, critically damped: it never
/// swings past zero from rest. `halflife` is the spring's (Holden's) half-life:
/// from rest (1 + 2 ln 2) / 4 = 60% of an offset is left after one, 20% after two,
/// 3% after four.
inline void decay(glm::vec3& x, glm::vec3& v, float halflife, float dt) {
    const float y = (4.0f * 0.69314718f) / (halflife + 1e-5f) * 0.5f;
    const glm::vec3 j1 = v + x * y;
    const float e = std::exp(-y * dt);
    x = e * (x + j1 * dt);
    v = e * (v - j1 * y * dt);
}
/// The spring's half-life for a transition asked for as a blend time: four
/// half-lives, so 3% of the offset is left at the end of that time.
inline float halflife_for_blend(float blend) { return std::max(0.01f, blend * 0.25f); }

// ---- clip events (animation hooks) -----------------------------------------------------
struct ClipEvent {
    float time = 0.0f;
    std::string name, param;
};
using ClipEvents = std::unordered_map<int, std::vector<ClipEvent>>;

/// Reads `clip seconds name [param]` lines (`#` starts a comment) into `out`, by the
/// clip index `resolve` gives for a clip's name (-1: no such clip, the line is
/// skipped with a warning). Each clip's events are sorted by time. Returns how many
/// were read.
inline int parse_clip_events(std::istream& in, const std::function<int(const std::string&)>& resolve, ClipEvents& out,
                             std::vector<std::string>* warnings = nullptr) {
    std::string line;
    int count = 0, lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::istringstream ss(line);
        std::string clip, name, param;
        float time = 0.0f;
        if (!(ss >> clip)) continue;
        if (!(ss >> time >> name)) {
            if (warnings) warnings->push_back("line " + std::to_string(lineno) + ": expected 'clip seconds name [param]'");
            continue;
        }
        std::getline(ss, param);
        while (!param.empty() && std::isspace(static_cast<unsigned char>(param.front()))) param.erase(param.begin());
        while (!param.empty() && std::isspace(static_cast<unsigned char>(param.back()))) param.pop_back();
        const int index = resolve(clip);
        if (index < 0) {
            if (warnings) warnings->push_back("line " + std::to_string(lineno) + ": no clip '" + clip + "'");
            continue;
        }
        out[index].push_back({time, name, param});
        ++count;
    }
    for (auto& [i, list] : out)
        std::stable_sort(list.begin(), list.end(), [](const ClipEvent& a, const ClipEvent& b) { return a.time < b.time; });
    return count;
}
/// A clip found by name as play() finds one: exact, else the first whose name
/// contains it (either way without regard to case for the second).
inline int find_clip(const std::vector<std::string>& names, const std::string& clip) {
    for (size_t i = 0; i < names.size(); ++i) if (names[i] == clip) return static_cast<int>(i);
    auto lower = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    const std::string want = lower(clip);
    for (size_t i = 0; i < names.size(); ++i) if (lower(names[i]).find(want) != std::string::npos) return static_cast<int>(i);
    return -1;
}

/// The events a clip passed as its time went from `was` to `now` (clip seconds;
/// `was` is a little below 0 on the clip's first step, so an event at 0 is heard):
/// each event of a clip that plays once at most once, of a looping clip on every
/// pass (at most twice in one step). Appends their indices in `list`.
inline void events_passed(const std::vector<ClipEvent>& list, float was, float now, float duration, bool loop,
                          std::vector<size_t>& out) {
    if (now <= was) return;
    for (size_t i = 0; i < list.size(); ++i) {
        const ClipEvent& ev = list[i];
        if (!loop || duration <= 1e-4f) {
            if (ev.time > was && ev.time <= now && ev.time <= duration + 1e-4f) out.push_back(i);
            continue;
        }
        const float first = std::floor(std::max(was, 0.0f) / duration) * duration + ev.time;
        for (float at = first; at <= now && at < first + 2.5f * duration; at += duration)
            if (at > was) out.push_back(i);
    }
}

}  // namespace schizo::editor::anim
