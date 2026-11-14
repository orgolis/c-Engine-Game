#pragma once

// ============================================================================
// Effects -- short-lived particle bursts and streams for gameplay: a gun's
// muzzle, its tracer and what its rounds hit, an ability's wind-up, flight,
// beam and blast, a creature's attack.
// ============================================================================
//
// An effect is DATA: `effect` blocks in a project's .items files, each a few
// layers of particles. The engine has a built-in set (below) so every gun and
// ability looks like something with no data at all; a project replaces any of
// them by defining an effect of the same name.
//
//   effect firebolt_trail
//     # layer <name> then key / values, any order:
//     layer core   rate 90 life 0.10 0.16 size 0.45 0.15 color 4 1.8 0.5 1 to 2 0.4 0.1 0
//     layer embers rate 60 life 0.3 0.6 speed 0.5 2 spread 180 size 0.08 0.02 color 3 1.2 0.3 1 gravity -1 drag 2
//     layer smoke  rate 20 life 0.6 1.1 speed 0.2 0.6 spread 180 size 0.2 0.7 color 0.2 0.2 0.2 0.45 blend gravity -0.6
//   end
//
// Layer keys:
//   count N           a burst of N when the effect starts
//   rate N            N a second while it streams (trails: along the path flown)
//   path N            N a metre along the line to `reach` (beams)
//   life a [b]        seconds, random between a and b
//   speed a [b]       metres a second
//   dir along|back|normal|up|any   which way they go (default along)
//   spread deg        cone around that way (180: every way)
//   size a [b]        width at birth, at death
//   color r g b a     at birth (above 1 glows); `to r g b a` at death (default: the same, clear)
//   add | blend       add light (default) or cover what is behind (smoke, dust)
//   gravity g         metres a second squared down (below 0 rises)
//   drag d            slows by this share a second
//   streak k          stretched along their motion: length = speed x k (sparks, tracers)
//   jitter r          born up to r metres from the point
//   ring r            born on a level ring of radius r, moving outward (novas)
//   delay s           starts s seconds later (a blast's smoke after its flash)
//   reach             lives until it has travelled to the effect's reach (tracers)
//
// Areas pass a scale (their radius over 3 m): distances and speeds grow with
// it, sizes with its square root.
//
// Names the engine looks up (each falls back to the built-in one):
//   guns      <weapon>_muzzle, <weapon>_tracer, <weapon>_impact, <weapon>_hit
//             then muzzle, tracer, impact, impact_actor
//   abilities <vfx>_<stage>, then <element>_<stage>; stages windup (while it
//             is cast), cast (it goes off), trail (in flight), beam (aimed,
//             instant), hit (where it lands), area (around the caster), self;
//             elements by damage type: fire, cold, shock, poison, physical,
//             explosive, magic -- and heal for abilities that only restore.
//
// Checked by tools/vfx_check.

#include "vfx/particle_system.h"   // BillboardVertex

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::vfx {

struct EffectLayer {
    std::string name;
    int   count = 0;                 // burst
    float rate = 0.0f;               // a second, while streaming
    float per_metre = 0.0f;          // along a beam
    float life_min = 0.3f, life_max = 0.3f;
    float speed_min = 0.0f, speed_max = 0.0f;
    enum class Dir : uint8_t { Along, Back, Normal, Up, Any } dir = Dir::Along;
    float spread_deg = 0.0f;
    float size_start = 0.2f, size_end = 0.2f;
    glm::vec4 color_start{1.0f}, color_end{1.0f, 1.0f, 1.0f, 0.0f};
    bool  blend = false;             // covers (alpha) rather than adds light
    float gravity = 0.0f, drag = 0.0f, streak = 0.0f, jitter = 0.0f, ring = 0.0f, delay = 0.0f;
    bool  reach = false;
};

struct EffectDef {
    std::string id;
    std::vector<EffectLayer> layers;
};

// ---- parsing ---------------------------------------------------------------------------------
namespace effect_detail {
inline bool number(const std::string& s, float& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtof(s.c_str(), &end);
    return end && *end == '\0';
}
}  // namespace effect_detail

/// Read one `layer ...` line (after the word layer).
inline EffectLayer parse_effect_layer(std::istringstream& ls) {
    EffectLayer L;
    std::vector<std::string> tok;
    for (std::string t; ls >> t;) {
        if (t[0] == '#') break;
        tok.push_back(t);
    }
    size_t i = 0;
    if (i < tok.size()) {
        float probe = 0.0f;
        if (!effect_detail::number(tok[i], probe) && tok[i] != "count" && tok[i] != "rate") L.name = tok[i++];
    }
    auto num = [&](float& out) {
        float v = 0.0f;
        if (i < tok.size() && effect_detail::number(tok[i], v)) { out = v; ++i; return true; }
        return false;
    };
    auto pair = [&](float& a, float& b) {
        if (num(a)) { if (!num(b)) b = a; }
    };
    bool to_set = false;
    while (i < tok.size()) {
        const std::string k = tok[i++];
        float v = 0.0f;
        if (k == "count")        { if (num(v)) L.count = std::max(0, static_cast<int>(v)); }
        else if (k == "rate")    { num(L.rate); }
        else if (k == "path")    { num(L.per_metre); }
        else if (k == "life")    { pair(L.life_min, L.life_max); }
        else if (k == "speed")   { pair(L.speed_min, L.speed_max); }
        else if (k == "spread")  { num(L.spread_deg); }
        else if (k == "size")    { pair(L.size_start, L.size_end); }
        else if (k == "color")   { for (int c = 0; c < 4; ++c) num(L.color_start[c]); }
        else if (k == "to")      { for (int c = 0; c < 4; ++c) num(L.color_end[c]); to_set = true; }
        else if (k == "add")     { L.blend = false; }
        else if (k == "blend")   { L.blend = true; }
        else if (k == "gravity") { num(L.gravity); }
        else if (k == "drag")    { num(L.drag); }
        else if (k == "streak")  { num(L.streak); }
        else if (k == "jitter")  { num(L.jitter); }
        else if (k == "ring")    { num(L.ring); }
        else if (k == "delay")   { num(L.delay); }
        else if (k == "reach")   { L.reach = true; }
        else if (k == "dir" && i < tok.size()) {
            const std::string d = tok[i++];
            L.dir = d == "back" ? EffectLayer::Dir::Back : d == "normal" ? EffectLayer::Dir::Normal
                  : d == "up" ? EffectLayer::Dir::Up : d == "any" ? EffectLayer::Dir::Any : EffectLayer::Dir::Along;
        }
    }
    if (!to_set) L.color_end = glm::vec4(glm::vec3(L.color_start), 0.0f);
    if (L.life_max < L.life_min) std::swap(L.life_min, L.life_max);
    return L;
}

/// The `effect` blocks in a text (other blocks are skipped).
inline std::vector<EffectDef> parse_effects_text(const std::string& text) {
    std::vector<EffectDef> out;
    EffectDef cur;
    bool have = false, other = false;   // other: inside an item / weapon / ability / skill block
    std::istringstream in(text);
    std::string line;
    auto flush = [&] {
        if (have && !cur.id.empty()) out.push_back(cur);
        cur = EffectDef{};
        have = false;
    };
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        // Inside an ability, `effect Speed -3 3` is one of its lines, not a block.
        if (key == "effect" && !other) { flush(); ls >> cur.id; have = true; }
        else if (key == "item" || key == "weapon" || key == "ability" || key == "skill") { flush(); other = true; }
        else if (!have) { if (key == "end") other = false; continue; }
        else if (key == "end") { flush(); }
        else if (key == "layer") { cur.layers.push_back(parse_effect_layer(ls)); }
    }
    flush();
    return out;
}

// ---- the catalog -------------------------------------------------------------------------------
inline std::vector<EffectDef>& effect_registry() { static std::vector<EffectDef> r; return r; }
inline void register_effect(const EffectDef& d) {
    for (auto& e : effect_registry()) if (e.id == d.id) { e = d; return; }
    effect_registry().push_back(d);
}

/// The built-in effects: what a gun or an ability looks like when the
/// project says nothing. Colours above 1 glow (bloom).
inline const char* builtin_effects_text() {
    return R"(
effect muzzle
  layer flash  count 2  life 0.04 0.06 size 0.34 0.12 color 6 3.6 1.4 1 to 3 1 0.2 0 jitter 0.02
  layer cone   count 6  life 0.04 0.07 speed 3 7 spread 10 size 0.14 0.03 color 5 2.6 0.8 1 to 2 0.6 0.1 0 streak 0.05
  layer sparks count 7  life 0.08 0.22 speed 7 18 spread 18 size 0.025 0.01 color 4 2.4 0.9 1 to 2 0.6 0.1 0 streak 0.03 gravity 4 drag 2
  layer smoke  count 4  life 0.5 1.0 speed 0.6 1.8 spread 25 size 0.06 0.45 color 0.55 0.53 0.5 0.32 to 0.6 0.6 0.6 0 blend drag 3.5 gravity -0.7
end
effect tracer
  layer round  count 1 reach speed 320 size 0.05 0.05 color 6 3.8 1.6 1 to 4 2 0.6 0.8 streak 0.022
end
effect impact
  layer flash  count 1 life 0.05 size 0.25 0.1 color 4 3 1.6 1 to 2 1 0.3 0
  layer sparks count 9 life 0.15 0.4 speed 3 9 dir normal spread 60 size 0.025 0.01 color 4 2.6 1 1 to 2 0.5 0.1 0 streak 0.03 gravity 9.8 drag 1
  layer dust   count 5 life 0.5 1.1 speed 0.6 2.2 dir normal spread 45 size 0.07 0.55 color 0.42 0.38 0.32 0.55 to 0.45 0.42 0.38 0 blend drag 3.5 gravity -0.2
  layer chips  count 4 life 0.4 0.7 speed 2 5 dir normal spread 50 size 0.04 0.03 color 0.2 0.18 0.15 0.9 to 0.2 0.18 0.15 0.6 blend gravity 9.8
end
effect impact_actor
  layer burst  count 1 life 0.06 size 0.3 0.12 color 3 0.6 0.4 1 to 1 0.1 0.1 0
  layer spray  count 12 life 0.25 0.55 speed 1.5 4.5 dir normal spread 55 size 0.06 0.03 color 0.45 0.05 0.04 0.95 to 0.3 0.02 0.02 0.6 blend gravity 9.8 drag 0.8
  layer mist   count 3 life 0.3 0.6 speed 0.4 1.2 dir normal spread 60 size 0.12 0.45 color 0.5 0.06 0.05 0.45 to 0.4 0.05 0.05 0 blend drag 3
end

effect fire_windup
  layer swirl  rate 70 life 0.18 0.32 speed 0.6 1.4 dir any size 0.14 0.03 color 4 1.6 0.4 1 to 2 0.4 0.1 0 jitter 0.18 drag 4
  layer core   rate 40 life 0.06 0.1 size 0.2 0.08 color 5 2.2 0.6 1 to 3 0.8 0.2 0
end
effect fire_cast
  layer flash  count 2 life 0.06 0.1 size 0.5 0.2 color 6 2.8 0.8 1 to 3 0.8 0.2 0
  layer embers count 10 life 0.2 0.45 speed 2 6 spread 35 size 0.07 0.02 color 4 1.6 0.4 1 to 2 0.4 0.1 0 streak 0.02 drag 2
end
effect fire_trail
  layer core   rate 140 life 0.06 0.1 size 0.32 0.12 color 3.2 1.4 0.35 1 to 2 0.4 0.1 0 jitter 0.03
  layer flame  rate 80 life 0.14 0.26 speed 0.3 1 dir any size 0.2 0.04 color 2.6 0.9 0.2 1 to 1.2 0.2 0.04 0 jitter 0.06 gravity -2 drag 2
  layer embers rate 45 life 0.3 0.6 speed 0.5 2 dir any size 0.05 0.015 color 3 1.2 0.3 1 to 1.5 0.3 0.08 0 gravity -1.5 drag 1.5
  layer smoke  rate 22 life 0.5 1 speed 0.2 0.6 dir any size 0.12 0.45 color 0.16 0.14 0.13 0.4 to 0.22 0.21 0.2 0 blend gravity -0.8 drag 2
end
effect fire_beam
  layer bolt   path 7 life 0.08 0.16 size 0.3 0.08 color 5 2 0.5 1 to 2 0.5 0.1 0 jitter 0.08
end
effect fire_hit
  layer flash  count 3 life 0.07 0.12 size 1.2 0.4 color 7 3 0.8 1 to 3 0.8 0.2 0 jitter 0.1
  layer flame  count 26 life 0.25 0.55 speed 1.5 5 dir any size 0.45 0.1 color 4 1.5 0.35 1 to 1.2 0.25 0.05 0 drag 3 gravity -2
  layer embers count 22 life 0.4 0.9 speed 3 9 dir any size 0.06 0.02 color 4 1.8 0.4 1 to 2 0.4 0.1 0 streak 0.02 gravity 6 drag 1
  layer smoke  count 10 life 0.9 1.8 speed 0.4 1.6 dir any size 0.4 1.6 color 0.16 0.15 0.14 0.55 to 0.22 0.21 0.2 0 blend gravity -1 drag 2 delay 0.05
end
effect fire_area
  layer ring   count 48 life 0.35 0.6 speed 6 9 size 0.5 0.15 color 4 1.6 0.4 1 to 1.5 0.3 0.05 0 ring 0.6 drag 3
  layer flash  count 2 life 0.08 0.14 size 2 0.6 color 5 2.2 0.6 1 to 2 0.5 0.1 0
end

effect cold_windup
  layer frost  rate 60 life 0.2 0.35 speed 0.4 1 dir any size 0.1 0.03 color 1.2 2.4 4 1 to 0.5 1 2 0 jitter 0.2 drag 4
end
effect cold_cast
  layer flash  count 2 life 0.06 0.1 size 0.45 0.2 color 1.5 3 5 1 to 0.6 1.2 2.5 0
end
effect cold_trail
  layer core   rate 120 life 0.08 0.14 size 0.4 0.15 color 1.6 3 5 1 to 0.6 1.2 2.5 0
  layer shards rate 60 life 0.3 0.6 speed 0.5 2 dir any size 0.06 0.02 color 1.5 2.5 4 1 to 0.6 1 2 0 gravity 2 drag 1
  layer mist   rate 20 life 0.5 1 speed 0.1 0.4 dir any size 0.2 0.6 color 0.7 0.85 1 0.35 to 0.8 0.9 1 0 blend drag 2
end
effect cold_beam
  layer bolt   path 7 life 0.1 0.18 size 0.24 0.06 color 1.5 3 5 1 to 0.6 1.2 2.5 0 jitter 0.06
end
effect cold_hit
  layer flash  count 2 life 0.08 0.12 size 1 0.3 color 2 3.5 6 1 to 0.8 1.4 3 0
  layer shards count 26 life 0.4 0.8 speed 3 8 dir any size 0.08 0.03 color 1.5 2.6 4 1 to 0.6 1 2 0 streak 0.02 gravity 8 drag 1
  layer mist   count 10 life 0.8 1.5 speed 0.4 1.4 dir any size 0.4 1.4 color 0.75 0.88 1 0.4 to 0.8 0.9 1 0 blend drag 2
end
effect cold_area
  layer ring   count 64 life 0.45 0.7 speed 7 10 size 0.45 0.2 color 1.4 2.6 4.5 1 to 0.5 1 2 0 ring 0.6 drag 3
  layer shards count 40 life 0.4 0.8 speed 2 6 dir up spread 70 size 0.09 0.03 color 1.6 2.8 4.5 1 to 0.6 1 2 0 streak 0.02 gravity 9 ring 1
  layer mist   count 24 life 0.8 1.5 speed 2 4 size 0.6 1.6 color 0.75 0.88 1 0.4 to 0.8 0.9 1 0 blend ring 0.8 drag 3
end

effect shock_windup
  layer arcs   rate 80 life 0.04 0.08 speed 2 5 dir any size 0.06 0.02 color 3 2.6 6 1 to 1.5 1 3 0 jitter 0.2 streak 0.05
end
effect shock_cast
  layer flash  count 2 life 0.04 0.08 size 0.5 0.2 color 4 3.5 8 1 to 2 1.5 4 0
end
effect shock_trail
  layer core   rate 120 life 0.05 0.1 size 0.35 0.12 color 4 3.5 8 1 to 2 1.5 4 0 jitter 0.06
  layer arcs   rate 80 life 0.05 0.1 speed 3 7 dir any size 0.05 0.02 color 3 2.6 6 1 to 1.5 1 3 0 streak 0.04
end
effect shock_beam
  layer bolt   path 9 life 0.08 0.14 size 0.16 0.06 color 5 4.5 10 1 to 2 1.5 5 0 jitter 0.22
  layer glow   path 2 life 0.12 0.2 size 0.6 0.3 color 1.2 1 2.6 1 to 0.4 0.3 1 0 jitter 0.1
end
effect shock_hit
  layer flash  count 2 life 0.05 0.1 size 1.1 0.3 color 6 5 12 1 to 2 1.5 5 0
  layer arcs   count 24 life 0.08 0.2 speed 4 12 dir any size 0.05 0.02 color 4 3.5 8 1 to 2 1.5 4 0 streak 0.04
end
effect shock_area
  layer ring   count 56 life 0.2 0.35 speed 9 13 size 0.25 0.08 color 4 3.5 8 1 to 2 1.5 4 0 ring 0.6 streak 0.03
end

effect poison_windup
  layer bubbles rate 40 life 0.25 0.45 speed 0.3 0.9 dir up spread 60 size 0.1 0.04 color 0.7 2.4 0.4 1 to 0.3 1 0.2 0 jitter 0.25
end
effect poison_cast
  layer splash count 10 life 0.15 0.3 speed 2 5 spread 30 size 0.1 0.04 color 0.25 0.6 0.12 0.9 to 0.2 0.5 0.1 0 blend gravity 6
end
effect poison_trail
  layer glob   rate 110 life 0.1 0.16 size 0.32 0.14 color 0.9 2.6 0.5 1 to 0.4 1.2 0.2 0
  layer drips  rate 45 life 0.3 0.6 speed 0.2 1 dir any size 0.07 0.03 color 0.25 0.55 0.1 0.9 to 0.2 0.45 0.08 0 blend gravity 6
  layer fume   rate 18 life 0.5 1 speed 0.1 0.4 dir any size 0.2 0.6 color 0.35 0.6 0.2 0.35 to 0.4 0.6 0.25 0 blend gravity -0.5
end
effect poison_beam
  layer bolt   path 6 life 0.1 0.18 size 0.25 0.08 color 0.9 2.6 0.5 1 to 0.4 1.2 0.2 0 jitter 0.05
end
effect poison_hit
  layer splash count 22 life 0.3 0.6 speed 2 5 dir normal spread 70 size 0.12 0.04 color 0.25 0.6 0.12 0.95 to 0.2 0.5 0.1 0.4 blend gravity 9
  layer glow   count 2 life 0.08 0.14 size 0.8 0.3 color 1.2 3 0.6 1 to 0.4 1 0.2 0
  layer fume   count 8 life 0.8 1.5 speed 0.3 1 dir any size 0.4 1.2 color 0.35 0.6 0.2 0.4 to 0.4 0.6 0.25 0 blend gravity -0.4 drag 2
end
effect poison_area
  layer ring   count 40 life 0.5 0.8 speed 4 6 size 0.6 0.3 color 0.35 0.6 0.2 0.5 to 0.4 0.6 0.25 0 blend ring 0.8 drag 2
end

effect physical_windup
  layer glint  rate 30 life 0.1 0.2 size 0.12 0.04 color 2 1.8 1.5 1 to 1 0.8 0.6 0 jitter 0.25
end
effect physical_cast
  layer whoosh count 8 life 0.08 0.16 speed 4 8 spread 25 size 0.06 0.02 color 1.6 1.5 1.4 0.8 to 1 0.9 0.8 0 streak 0.05
end
effect physical_trail
  layer core   rate 80 life 0.08 0.12 size 0.25 0.1 color 1.4 1.3 1.2 0.7 to 0.8 0.7 0.6 0
end
effect physical_beam
  layer streak path 4 life 0.06 0.1 size 0.12 0.05 color 2 1.9 1.7 1 to 1 0.9 0.8 0
end
effect physical_hit
  layer slash  count 10 life 0.1 0.22 speed 3 8 dir any size 0.05 0.02 color 2.5 2.2 1.8 1 to 1 0.8 0.6 0 streak 0.04
  layer dust   count 4 life 0.4 0.8 speed 0.5 1.5 dir any size 0.15 0.5 color 0.45 0.42 0.38 0.45 to 0.45 0.42 0.38 0 blend drag 3
end
effect physical_area
  layer dust   count 40 life 0.5 0.9 speed 4 7 size 0.4 1.2 color 0.45 0.42 0.38 0.5 to 0.45 0.42 0.38 0 blend ring 0.6 drag 3
end

effect explosive_windup
  layer fuse   rate 50 life 0.1 0.2 speed 1 3 dir up spread 40 size 0.05 0.02 color 4 2.4 0.8 1 to 2 0.6 0.1 0 streak 0.03
end
effect explosive_cast
  layer whoosh count 6 life 0.1 0.2 speed 2 5 spread 30 size 0.06 0.02 color 1.5 1.4 1.3 0.8 to 1 0.9 0.8 0 streak 0.04
end
effect explosive_trail
  layer fuse   rate 70 life 0.08 0.16 size 0.12 0.04 color 5 2.6 0.8 1 to 2 0.6 0.1 0
  layer smoke  rate 30 life 0.4 0.8 speed 0.1 0.4 dir any size 0.08 0.35 color 0.3 0.3 0.3 0.4 to 0.35 0.35 0.35 0 blend
end
effect explosive_beam
  layer streak path 4 life 0.08 0.12 size 0.15 0.05 color 4 2.4 0.8 1 to 2 0.6 0.1 0
end
effect explosive_hit
  layer flash  count 4 life 0.08 0.14 size 3 1 color 8 4.5 1.5 1 to 4 1.2 0.2 0 jitter 0.3
  layer fire   count 44 life 0.3 0.7 speed 3 9 dir any size 0.9 0.2 color 5 2 0.4 1 to 1.2 0.25 0.05 0 drag 3.5 gravity -2.5
  layer debris count 30 life 0.6 1.3 speed 5 14 dir up spread 70 size 0.1 0.06 color 0.15 0.13 0.1 1 to 0.15 0.13 0.1 0.8 blend gravity 14 drag 0.6
  layer sparks count 34 life 0.5 1.1 speed 6 16 dir any size 0.05 0.02 color 5 2.8 0.9 1 to 2 0.6 0.1 0 streak 0.025 gravity 9 drag 0.8
  layer smoke  count 22 life 1.4 2.8 speed 0.8 3 dir any size 0.9 3.2 color 0.14 0.13 0.12 0.62 to 0.22 0.21 0.2 0 blend gravity -1.2 drag 2.2 delay 0.06
  layer dust   count 26 life 0.8 1.4 speed 6 9 size 0.6 1.8 color 0.42 0.38 0.32 0.5 to 0.45 0.42 0.38 0 blend ring 0.5 drag 3.5
end
effect explosive_area
  layer flash  count 3 life 0.08 0.14 size 3 1 color 8 4.5 1.5 1 to 4 1.2 0.2 0
  layer fire   count 40 life 0.3 0.6 speed 6 9 size 0.8 0.2 color 5 2 0.4 1 to 1.2 0.25 0.05 0 ring 0.6 drag 3.5
end

effect magic_windup
  layer swirl  rate 60 life 0.2 0.35 speed 0.5 1.2 dir any size 0.1 0.03 color 2.4 1.2 4 1 to 1 0.4 2 0 jitter 0.2 drag 4
end
effect magic_cast
  layer flash  count 2 life 0.06 0.1 size 0.45 0.2 color 3 1.5 5 1 to 1.2 0.6 2.5 0
end
effect magic_trail
  layer core   rate 120 life 0.08 0.14 size 0.4 0.15 color 3 1.5 5 1 to 1.2 0.6 2.5 0
  layer motes  rate 50 life 0.3 0.6 speed 0.3 1.2 dir any size 0.06 0.02 color 2.4 1.2 4 1 to 1 0.4 2 0 drag 1.5
end
effect magic_beam
  layer bolt   path 7 life 0.1 0.16 size 0.24 0.08 color 3 1.5 5 1 to 1.2 0.6 2.5 0 jitter 0.06
end
effect magic_hit
  layer flash  count 2 life 0.08 0.12 size 1 0.3 color 4 2 6 1 to 1.5 0.8 3 0
  layer motes  count 24 life 0.3 0.7 speed 2 6 dir any size 0.07 0.02 color 2.4 1.2 4 1 to 1 0.4 2 0 streak 0.02 drag 1.5
end
effect magic_area
  layer ring   count 56 life 0.35 0.6 speed 6 9 size 0.4 0.12 color 2.4 1.2 4 1 to 1 0.4 2 0 ring 0.6 drag 3
end

effect heal_windup
  layer motes  rate 50 life 0.4 0.8 speed 0.3 0.8 dir up spread 30 size 0.08 0.02 color 0.8 3 1.2 1 to 0.3 1.2 0.5 0 jitter 0.45
end
effect heal_self
  layer rise   count 40 life 0.7 1.3 speed 0.8 2 dir up spread 20 size 0.1 0.03 color 0.9 3.2 1.3 1 to 0.3 1.2 0.5 0 ring 0.55 drag 0.5
  layer glow   count 3 life 0.25 0.4 size 1.6 0.6 color 0.5 1.6 0.7 1 to 0.2 0.6 0.3 0
end
effect heal_hit
  layer rise   count 24 life 0.6 1.1 speed 0.6 1.6 dir up spread 25 size 0.09 0.03 color 0.9 3.2 1.3 1 to 0.3 1.2 0.5 0 ring 0.4
end
effect magic_self
  layer rise   count 30 life 0.6 1.1 speed 0.6 1.6 dir up spread 25 size 0.09 0.03 color 2.4 1.2 4 1 to 1 0.4 2 0 ring 0.5
end
)";
}

inline std::vector<EffectDef>& builtin_effect_registry() {
    static std::vector<EffectDef> r = parse_effects_text(builtin_effects_text());
    return r;
}

/// A project's effect of that name, else the built-in one; null when neither.
inline const EffectDef* find_effect(const std::string& id) {
    if (id.empty()) return nullptr;
    for (const auto& e : effect_registry()) if (e.id == id) return &e;
    for (const auto& e : builtin_effect_registry()) if (e.id == id) return &e;
    return nullptr;
}
/// The first of several names that exists.
inline const EffectDef* find_effect_of(std::initializer_list<std::string> names) {
    for (const auto& n : names)
        if (const EffectDef* d = find_effect(n)) return d;
    return nullptr;
}

/// The element of a damage type, for an ability's built-in effects.
inline std::string effect_element(const std::string& damage_type, bool restores_only) {
    if (restores_only) return "heal";
    if (damage_type == "fire" || damage_type == "burn") return "fire";
    if (damage_type == "cold" || damage_type == "frost" || damage_type == "ice") return "cold";
    if (damage_type == "shock" || damage_type == "lightning" || damage_type == "electric") return "shock";
    if (damage_type == "poison" || damage_type == "acid") return "poison";
    if (damage_type == "physical" || damage_type == "kinetic" || damage_type == "slash") return "physical";
    if (damage_type == "explosive" || damage_type == "blast") return "explosive";
    return "magic";
}

// ---- the runtime -------------------------------------------------------------------------------
/// Where and how an effect starts.
struct EffectSpawn {
    glm::vec3 pos{0.0f};
    glm::vec3 dir{0.0f, 0.0f, -1.0f};    // along: the shot, the flight
    glm::vec3 normal{0.0f, 1.0f, 0.0f};  // the surface hit
    float     scale = 1.0f;              // areas: radius / 3
    float     reach = 0.0f;              // tracers and beams: the distance to the end
};

/// Particles from effects, simulated on the CPU, drawn as billboards.
class EffectParticles {
public:
    /// Start an effect: its bursts (`count`, `path`) now or after their delay.
    void burst(const EffectDef& d, const EffectSpawn& at) {
        for (const auto& L : d.layers) {
            if (L.count <= 0 && L.per_metre <= 0.0f) continue;
            if (L.delay > 0.0f) pending_.push_back({L, at, L.delay});
            else spawn_layer(L, at);
        }
    }
    /// Stream an effect's `rate` layers along a path flown this frame.
    void stream(const EffectDef& d, const glm::vec3& from, const glm::vec3& to, float dt, float scale = 1.0f) {
        const glm::vec3 seg = to - from;
        const float len = glm::length(seg);
        EffectSpawn at;
        at.dir = len > 1e-5f ? seg / len : glm::vec3(0.0f, 1.0f, 0.0f);
        at.scale = scale;
        for (size_t li = 0; li < d.layers.size(); ++li) {
            const EffectLayer& L = d.layers[li];
            if (L.rate <= 0.0f) continue;
            float& acc = accum_[key(d, li)];
            acc += L.rate * dt;
            const int n = static_cast<int>(acc);
            acc -= static_cast<float>(n);
            for (int i = 0; i < n; ++i) {
                at.pos = from + seg * ((static_cast<float>(i) + rnd()) / static_cast<float>(n));
                spawn_one(L, at, -1.0f);
            }
        }
    }
    void update(float dt) {
        for (size_t i = 0; i < pending_.size();) {
            pending_[i].wait -= dt;
            if (pending_[i].wait <= 0.0f) {
                spawn_layer(pending_[i].layer, pending_[i].at);
                pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        for (auto& p : ps_) {
            p.age += dt;
            p.vel.y -= p.gravity * dt;
            p.vel *= std::max(0.0f, 1.0f - p.drag * dt);
            p.pos += p.vel * dt;
        }
        ps_.erase(std::remove_if(ps_.begin(), ps_.end(), [](const P& p) { return p.age >= p.life; }), ps_.end());
    }
    /// Append camera-facing quads, farthest first (smoke covers correctly).
    void build(const glm::vec3& cam_pos, std::vector<BillboardVertex>& out) const {
        order_.resize(ps_.size());
        for (size_t i = 0; i < ps_.size(); ++i) {
            const glm::vec3 d = ps_[i].pos - cam_pos;
            order_[i] = {glm::dot(d, d), static_cast<uint32_t>(i)};
        }
        std::sort(order_.begin(), order_.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        out.reserve(out.size() + ps_.size() * 4);
        for (const auto& [dist2, i] : order_) {
            const P& p = ps_[i];
            const float t = std::clamp(p.age / std::max(1e-4f, p.life), 0.0f, 1.0f);
            const float size = (p.size0 + (p.size1 - p.size0) * t) * 0.5f;
            glm::vec4 c = p.c0 + (p.c1 - p.c0) * t;
            // Fade out right in front of the eye: a particle a hand's width away
            // would fill the screen (a trail leaving the hand, muzzle smoke).
            c.a = std::max(0.0f, c.a) * std::clamp((std::sqrt(dist2) - 0.35f) / 1.1f, 0.0f, 1.0f);
            if (c.a <= 0.0f || size <= 0.0f) continue;
            if (p.blend) c.a = -c.a;   // the shader's mark for "covers"
            glm::vec3 view = cam_pos - p.pos;
            const float vd = glm::length(view);
            view = vd > 1e-5f ? view / vd : glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 r, u;
            const float sp = glm::length(p.vel);
            if (p.streak > 0.0f && sp > 1e-3f) {
                // Stretched along the motion as it is seen: a spark, a tracer.
                glm::vec3 axis = p.vel / sp;
                axis -= view * glm::dot(axis, view);
                const float al = glm::length(axis);
                if (al > 1e-4f) {
                    axis /= al;
                    const glm::vec3 side = glm::normalize(glm::cross(view, axis));
                    u = axis * std::max(size, 0.5f * sp * p.streak);
                    r = side * size;
                } else {
                    face(view, size, r, u);
                }
            } else {
                face(view, size, r, u);
            }
            out.push_back({p.pos - r - u, {0, 0}, c});
            out.push_back({p.pos + r - u, {1, 0}, c});
            out.push_back({p.pos + r + u, {1, 1}, c});
            out.push_back({p.pos - r + u, {0, 1}, c});
        }
    }
    size_t alive() const { return ps_.size(); }
    void clear() { ps_.clear(); pending_.clear(); accum_.clear(); }
    /// A floating-origin rebase moves every particle with the world.
    void apply_shift(const glm::vec3& d) {
        for (auto& p : ps_) p.pos += d;
        for (auto& q : pending_) q.at.pos += d;
    }

    static constexpr size_t kMaxParticles = 12000;

private:
    struct P {
        glm::vec3 pos, vel;
        float age = 0.0f, life = 1.0f, size0 = 0.1f, size1 = 0.1f, gravity = 0.0f, drag = 0.0f, streak = 0.0f;
        glm::vec4 c0, c1;
        bool blend = false;
    };
    struct Pending { EffectLayer layer; EffectSpawn at; float wait; };

    static void face(const glm::vec3& view, float size, glm::vec3& r, glm::vec3& u) {
        glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), view);
        if (glm::length(right) < 1e-4f) right = glm::vec3(1.0f, 0.0f, 0.0f);
        right = glm::normalize(right);
        r = right * size;
        u = glm::normalize(glm::cross(view, right)) * size;
    }
    static uint64_t key(const EffectDef& d, size_t layer) {
        return (reinterpret_cast<uintptr_t>(&d) << 6) ^ layer;
    }
    float rnd() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ & 0xFFFFFFu) / static_cast<float>(0x1000000);
    }
    float range(float a, float b) { return a + (b - a) * rnd(); }
    glm::vec3 any_dir() {
        const float z = range(-1.0f, 1.0f), a = range(0.0f, 6.2831853f), r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return glm::vec3(r * std::cos(a), z, r * std::sin(a));
    }
    glm::vec3 cone(const glm::vec3& axis, float deg) {
        if (deg >= 179.0f) return any_dir();
        const glm::vec3 a = glm::length(axis) > 1e-5f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 ref = std::fabs(a.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 x = glm::normalize(glm::cross(ref, a)), y = glm::cross(a, x);
        const float c = std::cos(glm::radians(deg) * std::sqrt(rnd())), s = std::sqrt(std::max(0.0f, 1.0f - c * c));
        const float phi = range(0.0f, 6.2831853f);
        return glm::normalize(a * c + (x * std::cos(phi) + y * std::sin(phi)) * s);
    }
    void spawn_layer(const EffectLayer& L, const EffectSpawn& at) {
        if (L.per_metre > 0.0f && at.reach > 0.0f) {
            const int n = std::min(400, static_cast<int>(L.per_metre * at.reach) + 1);
            for (int i = 0; i < n; ++i) {
                EffectSpawn s = at;
                s.pos = at.pos + at.dir * (at.reach * (static_cast<float>(i) + rnd()) / static_cast<float>(n));
                spawn_one(L, s, -1.0f);
            }
        }
        for (int i = 0; i < L.count; ++i) spawn_one(L, at, static_cast<float>(i) / static_cast<float>(std::max(1, L.count)));
    }
    void spawn_one(const EffectLayer& L, const EffectSpawn& at, float ring_at) {
        if (ps_.size() >= kMaxParticles) return;
        const float k = std::max(0.05f, at.scale), ks = std::sqrt(k);
        P p;
        p.pos = at.pos;
        glm::vec3 base;
        switch (L.dir) {
            case EffectLayer::Dir::Back:   base = -at.dir; break;
            case EffectLayer::Dir::Normal: base = at.normal; break;
            case EffectLayer::Dir::Up:     base = glm::vec3(0.0f, 1.0f, 0.0f); break;
            case EffectLayer::Dir::Any:    base = any_dir(); break;
            default:                       base = at.dir; break;
        }
        glm::vec3 dir = L.dir == EffectLayer::Dir::Any ? base : cone(base, L.spread_deg);
        if (L.ring > 0.0f) {
            // On a level ring around the point, moving out from it.
            const float a = ring_at >= 0.0f ? ring_at * 6.2831853f + range(-0.1f, 0.1f) : range(0.0f, 6.2831853f);
            const glm::vec3 out(std::cos(a), 0.0f, std::sin(a));
            p.pos += out * (L.ring * k);
            dir = L.dir == EffectLayer::Dir::Up ? cone(glm::vec3(0.0f, 1.0f, 0.0f), L.spread_deg)
                                                : glm::normalize(out + glm::vec3(0.0f, range(0.0f, 0.15f), 0.0f));
        }
        if (L.jitter > 0.0f) p.pos += any_dir() * (L.jitter * k * std::cbrt(rnd()));
        const float speed = range(L.speed_min, L.speed_max) * k;
        p.vel = dir * speed;
        p.life = L.reach ? (speed > 1e-3f ? std::max(0.01f, at.reach / speed) : 0.0f) : range(L.life_min, L.life_max);
        if (p.life <= 0.0f) return;
        p.size0 = L.size_start * ks;
        p.size1 = L.size_end * ks;
        p.c0 = L.color_start;
        p.c1 = L.color_end;
        p.gravity = L.gravity;
        p.drag = L.drag;
        p.streak = L.streak;
        p.blend = L.blend;
        ps_.push_back(p);
    }

    std::vector<P> ps_;
    std::vector<Pending> pending_;
    std::unordered_map<uint64_t, float> accum_;
    mutable std::vector<std::pair<float, uint32_t>> order_;
    uint32_t rng_ = 0x9E3779B9u;
};

}  // namespace schizo::vfx
