// ============================================================================
// movement_check -- every move of ecs/gameplay_movement.h against real
// geometry: boxes and a ground plane, a capsule approximated by an upright box
// that collides and slides axis by axis (the editor uses Jolt's capsule; the
// motor only sees rays and the body, so the moves are the same).
// ============================================================================
#include "ecs/gameplay_movement.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace schizo::ecs;

static int g_fail = 0, g_pass = 0;
static void check(const char* what, bool ok) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    (ok ? g_pass : g_fail)++;
}

struct Box { glm::vec3 lo, hi; bool climbable = false; };

struct Sim {
    std::vector<Box> boxes;
    bool      ground = true;           // a plane at y = 0
    glm::vec3 feet{0.0f};
    float     r = 0.3f, standing = 1.8f, h = 1.8f;
    bool      grounded = true;
    bool      in_water = false;
    glm::vec3 ground_normal{0.0f, 1.0f, 0.0f};
    Movement  mv;
    float     time = 0.0f;
    float     max_y = 0.0f;
    uint32_t  events = 0;              // every event seen since the last clear
    MoveMode  last_mode = MoveMode::Walk;
    std::vector<MoveMode> modes;       // every mode seen

    MoveHit ray(const glm::vec3& o, const glm::vec3& d0, float maxd) const {
        MoveHit best;
        const float l = glm::length(d0);
        if (l < 1e-6f) return best;
        const glm::vec3 d = d0 / l;
        float bt = maxd;
        auto take = [&](float t, glm::vec3 n, bool climb) {
            if (t < 0.0f || t > bt) return;
            bt = t; best.hit = true; best.distance = t; best.point = o + d * t; best.normal = n; best.climbable = climb;
        };
        if (ground && d.y < -1e-6f && o.y >= 0.0f) take(-o.y / d.y, glm::vec3(0, 1, 0), false);
        for (const Box& b : boxes) {
            float t0 = 0.0f, t1 = maxd;
            int axis = -1; float sign = 0.0f;
            bool inside = true, miss = false;
            for (int a = 0; a < 3; ++a) {
                if (o[a] < b.lo[a] || o[a] > b.hi[a]) inside = false;
                if (std::fabs(d[a]) < 1e-8f) { if (o[a] < b.lo[a] || o[a] > b.hi[a]) miss = true; continue; }
                float ta = (b.lo[a] - o[a]) / d[a], tb = (b.hi[a] - o[a]) / d[a];
                float s = -1.0f;
                if (ta > tb) { std::swap(ta, tb); s = 1.0f; }
                if (ta > t0) { t0 = ta; axis = a; sign = s; }
                t1 = std::min(t1, tb);
            }
            if (miss) continue;
            if (inside) { take(0.0f, glm::vec3(0, 1, 0), b.climbable); continue; }   // solid, like a convex shape
            if (t0 > t1 || axis < 0) continue;
            glm::vec3 n(0.0f); n[axis] = sign;
            take(t0, n, b.climbable);
        }
        return best;
    }
    bool overlaps(const glm::vec3& f, float height) const {
        const float e = 1e-4f;
        for (const Box& b : boxes)
            if (f.x + r - e > b.lo.x && f.x - r + e < b.hi.x && f.z + r - e > b.lo.z && f.z - r + e < b.hi.z &&
                f.y + height - e > b.lo.y && f.y + e < b.hi.y)
                return true;
        return false;
    }
    void step(const MoveInput& in, float dt = 1.0f / 120.0f) {
        MoveBody b;
        b.feet = feet; b.radius = r; b.height = h; b.standing_height = standing;
        b.grounded = grounded; b.ground_normal = ground_normal; b.in_water = in_water;
        const MoveResult res = step_movement(mv, in, b, [this](const glm::vec3& o, const glm::vec3& d, float m) { return ray(o, d, m); }, dt);
        events |= mv.state.events;
        if (modes.empty() || modes.back() != mv.state.mode) modes.push_back(mv.state.mode);
        last_mode = mv.state.mode;
        if (std::fabs(res.height - h) > 1e-4f && (res.height < h || !overlaps(feet, res.height))) h = res.height;
        const glm::vec3 d = res.velocity * dt;
        for (int a : {0, 2, 1}) {
            feet[a] += d[a];
            for (const Box& bx : boxes) {
                if (!overlaps(feet, h)) break;
                const float e = 1e-4f;
                const bool hit = feet.x + r - e > bx.lo.x && feet.x - r + e < bx.hi.x && feet.z + r - e > bx.lo.z &&
                                 feet.z - r + e < bx.hi.z && feet.y + h - e > bx.lo.y && feet.y + e < bx.hi.y;
                if (!hit) continue;
                if (a == 1) feet.y = d[a] < 0.0f ? bx.hi.y : bx.lo.y - h;
                else        feet[a] = d[a] > 0.0f ? bx.lo[a] - r : bx.hi[a] + r;
            }
        }
        if (ground && feet.y < 0.0f) feet.y = 0.0f;
        grounded = (ground && feet.y <= 1e-3f) || overlaps(feet - glm::vec3(0.0f, 0.02f, 0.0f), h);
        time += dt;
        max_y = std::max(max_y, feet.y);
    }
    void run(const MoveInput& in, float seconds) { for (float t = 0.0f; t < seconds; t += 1.0f / 120.0f) step(in); }
    float hspeed() const { return glm::length(glm::vec3(mv.state.velocity.x, 0.0f, mv.state.velocity.z)); }
    bool saw(MoveMode m) const { for (MoveMode x : modes) if (x == m) return true; return false; }
};

static MoveInput fwd_in(float y = 1.0f) { MoveInput i; i.move = glm::vec2(0.0f, y); return i; }

int main() {
    // ---- walking ------------------------------------------------------------------
    {
        Sim s;
        s.run(fwd_in(), 0.4f);
        check("walking reaches walking speed", std::fabs(s.hspeed() - 5.0f) < 0.05f && s.feet.z < -1.0f);
        MoveInput sp = fwd_in(); sp.sprint = true;
        s.run(sp, 0.4f);
        check("sprinting is faster", std::fabs(s.hspeed() - 8.5f) < 0.05f && s.mv.state.sprinting);
        s.run(MoveInput{}, 0.4f);
        check("letting go stops", s.hspeed() < 0.01f && s.last_mode == MoveMode::Walk);
        MoveInput back = fwd_in(-1.0f); back.sprint = true;
        s.run(back, 0.4f);
        check("no sprinting backwards", std::fabs(s.hspeed() - 5.0f) < 0.05f && !s.mv.state.sprinting);
    }
    // ---- crouching ----------------------------------------------------------------
    {
        Sim s;
        MoveInput c = fwd_in(); c.crouch = true;
        s.run(c, 0.5f);
        check("crouching lowers the capsule and walks slowly", s.h < 1.1f && std::fabs(s.hspeed() - 2.5f) < 0.05f && s.mv.state.crouched);
        // Under a low ceiling the capsule stays crouched.
        s.boxes.push_back({glm::vec3(-5, 1.3f, s.feet.z - 5), glm::vec3(5, 2.0f, s.feet.z + 5)});
        s.run(fwd_in(), 0.3f);
        check("no standing up under a low ceiling", s.h < 1.1f && std::fabs(s.hspeed() - 2.5f) < 0.05f);
        s.boxes.clear();
        s.run(fwd_in(), 0.3f);
        check("standing up again with room", std::fabs(s.h - 1.8f) < 1e-3f && std::fabs(s.hspeed() - 5.0f) < 0.05f);
    }
    // ---- jumping ------------------------------------------------------------------
    {
        Sim s;
        MoveInput j; j.jump_pressed = j.jump_held = true;
        s.step(j);
        s.events = 0;
        s.run(MoveInput{}, 1.5f);
        check("a jump reaches the jump height", std::fabs(s.max_y - 1.25f) < 0.05f);
        check("and lands", s.grounded && s.last_mode == MoveMode::Walk && (s.events & kMoveLanded));
        // Held jump does not jump again (pressed is the edge).
        MoveInput held; held.jump_held = true;
        s.max_y = 0.0f;
        s.run(held, 0.5f);
        check("holding jump does not bounce", s.max_y < 0.01f);
    }
    // ---- coyote time and the jump buffer ------------------------------------------
    for (int late = 0; late < 2; ++late) {
        Sim s;
        s.ground = false;
        s.boxes.push_back({glm::vec3(-5, -1, -1), glm::vec3(5, 0, 5)});   // a platform ending at z = -1
        s.run(fwd_in(), 0.2f);
        while (s.grounded && s.time < 3.0f) s.step(fwd_in());
        s.run(fwd_in(), late ? 0.3f : 0.06f);
        const float y0 = s.feet.y;
        MoveInput j = fwd_in(); j.jump_pressed = j.jump_held = true;
        s.step(j);
        s.run(fwd_in(), 0.15f);
        if (late) check("no jump long after leaving the edge", s.feet.y < y0);
        else      check("coyote time: a jump just after leaving the edge works", s.feet.y > y0 + 0.4f);
    }
    {
        Sim s;
        MoveInput j; j.jump_pressed = j.jump_held = true;
        s.step(j);
        s.run(MoveInput{}, 0.1f);
        while (s.mv.state.velocity.y > -6.0f && s.time < 3.0f) s.step(MoveInput{});   // falling, not yet down
        while (s.feet.y > 0.3f && s.time < 3.0f) s.step(MoveInput{});
        s.step(j);                                       // pressed before landing
        s.max_y = 0.0f;
        s.run(MoveInput{}, 0.3f);
        check("jump buffer: pressed just before landing jumps on landing", s.max_y > 0.6f);
    }
    // ---- air jumps ----------------------------------------------------------------
    {
        Sim s;
        s.mv.settings.air_jumps = 1;
        MoveInput j; j.jump_pressed = j.jump_held = true;
        s.step(j); s.run(MoveInput{}, 0.4f);
        s.step(j); s.events = 0; s.run(MoveInput{}, 0.4f);
        const float after_two = s.max_y;
        s.step(j); s.run(MoveInput{}, 0.3f);
        check("a double jump goes higher", after_two > 1.9f);
        check("but not a third", std::fabs(s.max_y - after_two) < 1e-3f);
    }
    // ---- slide --------------------------------------------------------------------
    {
        Sim s;
        MoveInput sp = fwd_in(); sp.sprint = true;
        s.run(sp, 0.5f);
        MoveInput sl = sp; sl.crouch = true;
        s.step(sl);
        check("crouch while sprinting slides, faster at first", s.last_mode == MoveMode::Slide && s.hspeed() > 10.0f && (s.events & kMoveSlid));
        s.run(sl, 0.1f);
        check("a slide is low", s.h < 1.1f);
        float t = 0.0f;
        while (s.last_mode == MoveMode::Slide && t < 3.0f) { s.step(sl); t += 1.0f / 120.0f; }
        check("a slide on flat ground ends by itself", s.last_mode == MoveMode::Walk && t > 0.6f && t < 1.4f);
        check("crouch still held after it: crouch walking", s.mv.state.crouched || s.h < 1.1f);
        // Downhill a slide gains speed.
        Sim d;
        d.ground_normal = glm::normalize(glm::vec3(0.0f, 1.0f, -1.0f));   // ground falls away forwards (-z)
        d.run(sp, 0.5f);
        d.step(sl);
        const float v0 = d.hspeed();
        d.run(sl, 0.5f);
        check("downhill a slide speeds up", d.last_mode == MoveMode::Slide && d.hspeed() > v0 + 1.0f);
        // Too slow: crouch is just crouch.
        Sim w;
        w.run(fwd_in(), 0.4f);
        MoveInput c = fwd_in(); c.crouch = true;
        w.step(c);
        check("crouch while walking does not slide", w.last_mode == MoveMode::Walk);
    }
    // ---- dash ---------------------------------------------------------------------
    {
        Sim s;
        MoveInput d = fwd_in(); d.dash_pressed = true;
        s.step(d);
        check("a dash goes at dash speed", s.last_mode == MoveMode::Dash && std::fabs(s.hspeed() - 18.0f) < 0.01f);
        const float z0 = s.feet.z;
        s.run(fwd_in(), 0.3f);
        check("for its time, then walks", s.last_mode == MoveMode::Walk && (z0 - s.feet.z) > 2.5f);
        s.step(d);
        check("not again during the cooldown", s.last_mode == MoveMode::Walk);
        s.run(fwd_in(), 0.8f);
        s.step(d);
        check("again after it", s.last_mode == MoveMode::Dash);
        // Once in the air.
        Sim a;
        MoveInput j; j.jump_pressed = j.jump_held = true;
        a.step(j); a.run(MoveInput{}, 0.1f);
        a.step(d); const bool first = a.last_mode == MoveMode::Dash;
        a.run(MoveInput{}, 1.0f);   // still in the air? no: landed. Jump again for the second test.
        a.step(j); a.run(MoveInput{}, 0.1f);
        a.step(d); a.run(MoveInput{}, 0.25f);
        a.mv.state.dash_cooldown = 0.0f;
        a.step(d);
        check("an air dash, once per jump", first && a.last_mode != MoveMode::Dash);
        Sim off; off.mv.settings.dash = false;
        off.step(d);
        check("dash can be turned off", off.last_mode == MoveMode::Walk);
    }
    // ---- mantle -------------------------------------------------------------------
    auto ledge_sim = [](float height, bool ceiling) {
        Sim s;
        s.boxes.push_back({glm::vec3(-2, 0, -6), glm::vec3(2, height, -2)});    // a block, face at z = -2
        if (ceiling) s.boxes.push_back({glm::vec3(-2, height + 0.6f, -6), glm::vec3(2, height + 4.0f, -2)});
        s.run(fwd_in(), 0.6f);                                                 // walk up to it
        MoveInput j = fwd_in(); j.jump_pressed = j.jump_held = true;
        s.step(j);
        s.run(fwd_in(), 1.0f);
        return s;
    };
    {
        Sim s = ledge_sim(1.5f, false);
        check("jump at a 1.5 m ledge: mantle onto it", s.saw(MoveMode::Mantle) && std::fabs(s.feet.y - 1.5f) < 0.02f && s.feet.z < -2.1f && s.grounded);
        Sim t3 = ledge_sim(3.0f, false);
        check("a 3 m ledge: jump, then mantle from the top of the jump", t3.saw(MoveMode::Mantle) && std::fabs(t3.feet.y - 3.0f) < 0.02f);
        Sim t = ledge_sim(4.0f, false);
        check("a 4 m wall: no mantle, just a jump", !t.saw(MoveMode::Mantle) && t.feet.y < 0.01f);
        Sim c = ledge_sim(1.5f, true);
        check("no mantle onto a ledge with no room above it", !c.saw(MoveMode::Mantle));
        Sim lo = ledge_sim(0.3f, false);
        check("a step too low to mantle", !lo.saw(MoveMode::Mantle));
        // In the air, holding forward at a ledge above the jump.
        Sim a;
        a.boxes.push_back({glm::vec3(-2, 0, -6), glm::vec3(2, 1.8f, -1.5f)});
        a.feet.z = -0.9f;
        MoveInput j = fwd_in(0.0f); j.jump_pressed = j.jump_held = true;
        a.step(j);
        a.run(fwd_in(), 1.2f);
        check("in the air, holding forward at a 1.8 m ledge: mantle", a.saw(MoveMode::Mantle) && std::fabs(a.feet.y - 1.8f) < 0.02f);
        Sim off = [] { Sim s; s.mv.settings.mantle = false; return s; }();
        off.boxes.push_back({glm::vec3(-2, 0, -6), glm::vec3(2, 1.5f, -2)});
        off.run(fwd_in(), 0.6f);
        MoveInput jj = fwd_in(); jj.jump_pressed = jj.jump_held = true;
        off.step(jj); off.run(fwd_in(), 1.0f);
        check("mantle can be turned off", !off.saw(MoveMode::Mantle) && off.feet.y < 0.01f);
    }
    // ---- glide --------------------------------------------------------------------
    {
        Sim s;
        s.mv.settings.glide = true;
        s.feet.y = 30.0f; s.grounded = false; s.mv.state.mode = MoveMode::Air;
        s.run(MoveInput{}, 0.3f);
        MoveInput g = fwd_in(); g.jump_pressed = g.jump_held = true;
        s.step(g);
        MoveInput hold = fwd_in(); hold.jump_held = true;
        s.run(hold, 1.0f);
        check("glide: falls slowly while jump is held", s.last_mode == MoveMode::Glide && s.mv.state.velocity.y >= -2.5f - 1e-3f);
        check("and flies forward", s.hspeed() > 8.0f);
        s.run(fwd_in(), 0.5f);
        check("letting go falls", s.last_mode == MoveMode::Air && s.mv.state.velocity.y < -6.0f);
        Sim n;
        n.feet.y = 30.0f; n.grounded = false; n.mv.state.mode = MoveMode::Air;
        n.step(g); n.run(hold, 0.5f);
        check("no glide unless turned on", !n.saw(MoveMode::Glide));
    }
    // ---- wall run -----------------------------------------------------------------
    {
        auto wall = [](bool on) {
            Sim s;
            s.mv.settings.wall_run = on;
            s.boxes.push_back({glm::vec3(0.55f, 0, -40), glm::vec3(1.5f, 6, 2)});   // a wall on the right
            MoveInput sp = fwd_in(); sp.sprint = true;
            s.run(sp, 0.5f);
            MoveInput j = sp; j.jump_pressed = j.jump_held = true;
            s.step(j);
            float air = 0.0f;
            s.step(sp);
            while (!s.grounded && air < 4.0f) { s.step(sp); air += 1.0f / 120.0f; }
            return std::pair<Sim, float>(s, air);
        };
        auto [s, air] = wall(true);
        auto [n, air_n] = wall(false);
        check("wall run: sprint-jumping along a wall runs on it", s.saw(MoveMode::WallRun) && (s.events & kMoveWallRun));
        check("and stays up longer than a jump", air > air_n + 0.4f);
        check("no wall run unless turned on", !n.saw(MoveMode::WallRun));
        // Jump off the wall: away from it.
        Sim w;
        w.mv.settings.wall_run = true;
        w.boxes.push_back({glm::vec3(0.55f, 0, -40), glm::vec3(1.5f, 6, 2)});
        MoveInput sp = fwd_in(); sp.sprint = true;
        w.run(sp, 0.5f);
        MoveInput j = sp; j.jump_pressed = j.jump_held = true;
        w.step(j);
        while (w.last_mode != MoveMode::WallRun && w.time < 3.0f) w.step(sp);
        w.run(sp, 0.2f);
        w.step(j);
        w.run(sp, 0.15f);
        check("a wall jump pushes away from the wall", (w.events & kMoveWallJump) && w.mv.state.velocity.x < -2.0f);
    }
    // ---- climb --------------------------------------------------------------------
    {
        Sim s;
        s.boxes.push_back({glm::vec3(-0.5f, 0, -2.2f), glm::vec3(0.5f, 4, -2.0f), true});   // a ladder face at z = -2
        s.boxes.push_back({glm::vec3(-3, 3.9f, -6), glm::vec3(3, 4, -2.2f)});              // the floor it leads to
        s.run(fwd_in(), 0.6f);
        check("walking into a climbable surface climbs it", s.last_mode == MoveMode::Climb && (s.events & kMoveClimb));
        s.run(fwd_in(), 0.5f);
        check("forward climbs up", s.feet.y > 1.0f);
        while (!(s.saw(MoveMode::Mantle) && s.last_mode == MoveMode::Walk) && s.time < 6.0f) s.step(fwd_in());
        check("and over the top onto the floor", s.feet.y > 3.95f && s.feet.z < -2.2f && s.last_mode == MoveMode::Walk);
        Sim p;
        p.boxes.push_back({glm::vec3(-0.5f, 0, -2.2f), glm::vec3(0.5f, 4, -2.0f), false});
        p.run(fwd_in(), 1.0f);
        check("a plain wall is not climbed", !p.saw(MoveMode::Climb) && p.feet.y < 0.01f);
        Sim q;
        q.boxes.push_back({glm::vec3(-0.5f, 0, -2.2f), glm::vec3(0.5f, 6, -2.0f), true});
        q.run(fwd_in(), 1.5f);
        MoveInput j = fwd_in(0.0f); j.jump_pressed = j.jump_held = true;
        q.step(j);
        q.run(MoveInput{}, 0.1f);
        check("jump pushes off a climb", q.last_mode == MoveMode::Air && q.mv.state.velocity.z > 2.0f);
    }
    // ---- swim ---------------------------------------------------------------------
    {
        Sim s;
        s.ground = false; s.feet.y = -3.0f; s.grounded = false; s.in_water = true;
        s.run(fwd_in(), 1.0f);
        check("in water: swimming, slowly", s.last_mode == MoveMode::Swim && std::fabs(s.hspeed() - 3.0f) < 0.1f);
        check("sinking slowly", s.mv.state.velocity.y < 0.0f && s.mv.state.velocity.y > -0.6f);
        MoveInput up; up.jump_held = true;
        s.run(up, 1.0f);
        check("jump held swims up", s.mv.state.velocity.y > 2.5f);
        s.in_water = false;
        s.run(MoveInput{}, 0.05f);
        check("out of the water: falling again", s.last_mode == MoveMode::Air);
    }
    // ---- stamina ------------------------------------------------------------------
    {
        Sim s;
        s.mv.settings.stamina = 30.0f;
        MoveInput sp = fwd_in(); sp.sprint = true;
        s.run(sp, 1.0f);
        check("sprinting drains stamina", s.mv.state.stamina < 30.0f - 10.0f && s.mv.state.sprinting);
        s.run(sp, 2.0f);
        check("and stops when it runs out", !s.mv.state.sprinting && std::fabs(s.hspeed() - 5.0f) < 0.05f);
        MoveInput d = fwd_in(); d.dash_pressed = true;
        s.step(d);
        check("no dash without stamina", s.last_mode == MoveMode::Walk);
        s.run(MoveInput{}, 3.0f);
        check("stamina comes back after a rest", s.mv.state.stamina > 29.0f);
    }
    // ---- falling ------------------------------------------------------------------
    {
        Sim s;
        s.feet.y = 10.0f; s.grounded = false; s.mv.state.mode = MoveMode::Air;
        s.mv.state.fall_start_y = 10.0f;
        s.events = 0;
        s.run(MoveInput{}, 2.0f);
        check("a fall lands and says how far", (s.events & kMoveLanded) && std::fabs(s.mv.state.last_fall - 10.0f) < 0.05f);
    }
    // ---- saving -------------------------------------------------------------------
    {
        Movement a;
        a.settings.walk_speed = 4.0f; a.settings.air_jumps = 2; a.settings.glide = true; a.settings.slide = false;
        a.settings.wall_run = true; a.settings.climb = false; a.settings.stamina = 80.0f; a.settings.stamina_delay = 1.5f;
        std::vector<uint8_t> buf;
        serialize_movement(&a, buf);
        Movement b;
        b.state.mode = MoveMode::Glide;
        deserialize_movement(&b, buf.data(), buf.size());
        check("settings save and load", b.settings.walk_speed == 4.0f && b.settings.air_jumps == 2 && b.settings.glide &&
                                        !b.settings.slide && b.settings.wall_run && !b.settings.climb &&
                                        b.settings.stamina == 80.0f && b.settings.stamina_delay == 1.5f &&
                                        b.state.mode == MoveMode::Walk);
        // An older, shorter save keeps the defaults for what it lacks.
        std::vector<uint8_t> old;
        detail::put_i32(old, 1); detail::put_i32(old, 2);
        detail::put_f32(old, 3.0f); detail::put_f32(old, 6.0f);
        Movement c;
        deserialize_movement(&c, old.data(), old.size());
        check("an older save keeps the defaults it lacks", c.settings.walk_speed == 3.0f && c.settings.sprint_speed == 6.0f &&
                                                          c.settings.mantle && c.settings.dash_speed == 18.0f);
        register_movement_components();
        check("Movement is an authorable component", find_authorable("Movement") != nullptr);
    }

    std::printf("\nmovement_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("ALL OK\n");
    return g_fail ? 1 : 0;
}
