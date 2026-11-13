#pragma once

// ============================================================================
// G · Movement -- how a character moves, as data and one step function.
// ============================================================================
//
// Walking, sprinting, crouching, jumping (with coyote time and a jump buffer,
// optional air jumps), sliding, dashing, mantling onto ledges, gliding, wall
// running and climbing surfaces tagged "surface.climbable", and swimming.
//
// `step_movement` is pure: it sees the world only through a ray callback and
// the body the physics reports (feet, capsule size, grounded, ground normal),
// and it returns the velocity the physics should move the capsule with (the
// caller's collide-and-slide does the rest) and the capsule height it wants.
// That keeps every move testable without a physics engine:
// tools/movement_check runs each move against boxes and planes.
//
// The `Movement` component holds the settings (saved with the scene, edited
// in the Inspector) and the state (runtime only).

#include "world.h"
#include "authorable_components.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace schizo::ecs {

struct MovementSettings {
    // Ground
    float walk_speed      = 5.0f;    // m/s
    float sprint_speed    = 8.5f;
    float crouch_speed    = 2.5f;
    float accel           = 50.0f;   // m/s^2 towards the wanted speed on the ground
    float decel           = 40.0f;   // m/s^2 without input
    float air_accel       = 14.0f;   // m/s^2 steering in the air
    // Jumping and falling
    float gravity         = 20.0f;   // m/s^2 (a game fall: snappier than 9.81)
    float jump_height     = 1.25f;   // m
    int   air_jumps       = 0;       // 1 = double jump
    float coyote_time     = 0.12f;   // s after leaving an edge a jump still works
    float jump_buffer     = 0.12f;   // s a jump pressed before landing is kept
    float max_fall_speed  = 40.0f;
    // Crouch and slide
    float crouch_height   = 0.55f;   // of the standing capsule height
    bool  slide           = true;    // crouch while running fast slides
    float slide_min_speed = 6.0f;
    float slide_boost     = 2.0f;    // m/s added when the slide starts
    float slide_friction  = 7.0f;    // m/s^2 (slopes downhill add gravity)
    float slide_max_time  = 1.25f;   // s on flat ground
    // Dash
    bool  dash            = true;
    float dash_speed      = 18.0f;
    float dash_time       = 0.18f;
    float dash_cooldown   = 0.8f;
    bool  dash_in_air     = true;    // once per jump
    // Mantle: climb onto a ledge in front, from the ground or in the air
    bool  mantle          = true;
    float mantle_min      = 0.5f;    // ledge height above the feet, m
    float mantle_max      = 1.9f;
    float mantle_reach    = 0.6f;    // how far in front (past the capsule)
    float mantle_time     = 0.38f;   // s
    // Glide: hold jump while falling
    bool  glide           = false;
    float glide_fall_speed = 2.5f;
    float glide_speed     = 10.0f;
    // Wall run: sprint-jump along a wall
    bool  wall_run        = false;
    float wall_run_min_speed = 5.5f;
    float wall_run_time   = 1.4f;
    float wall_run_gravity = 0.15f;  // of normal gravity
    float wall_jump_speed = 7.0f;    // pushed away from the wall
    // Climb surfaces tagged surface.climbable (ladders, vines, climbing walls)
    bool  climb           = true;
    float climb_speed     = 3.0f;
    // Swim (in physical water)
    float swim_speed      = 3.0f;
    // Stamina: 0 = unlimited. Sprinting drains it, a dash costs it.
    float stamina         = 0.0f;
    float sprint_cost     = 15.0f;   // per second
    float dash_cost       = 25.0f;
    float stamina_regen   = 20.0f;   // per second, after
    float stamina_delay   = 0.8f;    // s without spending

    float jump_speed() const { return std::sqrt(2.0f * std::max(0.01f, gravity) * std::max(0.0f, jump_height)); }
};

enum class MoveMode : uint8_t { Walk, Air, Slide, Dash, Mantle, Glide, WallRun, Climb, Swim };

inline const char* move_mode_name(MoveMode m) {
    switch (m) {
        case MoveMode::Walk:    return "Walk";
        case MoveMode::Air:     return "Air";
        case MoveMode::Slide:   return "Slide";
        case MoveMode::Dash:    return "Dash";
        case MoveMode::Mantle:  return "Mantle";
        case MoveMode::Glide:   return "Glide";
        case MoveMode::WallRun: return "WallRun";
        case MoveMode::Climb:   return "Climb";
        case MoveMode::Swim:    return "Swim";
    }
    return "?";
}

// What happened this step (bits), for sounds, animation and scripts.
enum MoveEvent : uint32_t {
    kMoveJumped    = 1u << 0,
    kMoveAirJumped = 1u << 1,
    kMoveLanded    = 1u << 2,
    kMoveDashed    = 1u << 3,
    kMoveMantled   = 1u << 4,
    kMoveSlid      = 1u << 5,
    kMoveWallRun   = 1u << 6,
    kMoveWallJump  = 1u << 7,
    kMoveGlide     = 1u << 8,
    kMoveClimb     = 1u << 9,
};

struct MovementState {
    MoveMode  mode = MoveMode::Walk;
    glm::vec3 velocity{0.0f};
    bool      grounded = false;
    bool      sprinting = false;
    bool      crouched = false;        // the capsule is crouch height now
    float     since_ground = 0.0f;
    float     jump_buffer = 0.0f;
    bool      jump_spent = false;      // jumped since last on the ground
    int       air_jumps_left = 0;
    bool      air_dash_used = false;
    float     dash_cooldown = 0.0f;
    float     timer = 0.0f;            // time in the current timed mode
    glm::vec3 dir{0.0f};               // dash direction, or the way onto a ledge
    glm::vec3 wall_normal{0.0f};       // wall run / climb surface
    float     wall_cooldown = 0.0f;    // after leaving a wall, before another wall run
    glm::vec3 mantle_top{0.0f};        // where the feet end on a ledge
    float     mantle_rise = 0.0f;      // feet height to reach before moving onto it
    float     stamina = -1.0f;         // < 0: not started (filled on the first step)
    float     stamina_idle = 0.0f;
    uint32_t  events = 0;              // MoveEvent bits from the last step
    float     fall_start_y = 0.0f;     // feet height where the fall began
    float     last_fall = 0.0f;        // how far the last landing fell, m
};

struct Movement {
    MovementSettings settings;
    MovementState    state;            // runtime only
};

// A ray into the world. `climbable` is the host's answer for the surface hit.
struct MoveHit {
    bool      hit = false;
    float     distance = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool      climbable = false;
};
using MoveRayFn = std::function<MoveHit(const glm::vec3& origin, const glm::vec3& dir, float max_dist)>;

// The capsule as the physics has it now.
struct MoveBody {
    glm::vec3 feet{0.0f};
    float     radius = 0.3f;
    float     height = 1.8f;           // total, now (crouched or not)
    float     standing_height = 1.8f;  // total, standing
    bool      grounded = false;
    glm::vec3 ground_normal{0.0f, 1.0f, 0.0f};
    bool      in_water = false;        // the torso is under a water surface
};

struct MoveInput {
    glm::vec2 move{0.0f};              // x right, y forward, each -1..1
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    bool jump_pressed = false;
    bool jump_held = false;
    bool sprint = false;
    bool crouch = false;
    bool dash_pressed = false;
};

struct MoveResult {
    glm::vec3 velocity{0.0f};
    float     height = 1.8f;           // capsule total height wanted
};

namespace move_detail {

inline glm::vec3 flat(const glm::vec3& v) { return glm::vec3(v.x, 0.0f, v.z); }
inline glm::vec3 flat_dir(const glm::vec3& v) {
    const glm::vec3 f = flat(v);
    const float l = glm::length(f);
    return l > 1e-5f ? f / l : glm::vec3(0.0f);
}
// Move the horizontal part of v towards target by at most rate*dt.
inline glm::vec3 approach_flat(const glm::vec3& v, const glm::vec3& target, float rate, float dt) {
    glm::vec3 h = flat(v);
    const glm::vec3 d = flat(target) - h;
    const float l = glm::length(d), step = rate * dt;
    h = (l <= step || l < 1e-6f) ? flat(target) : h + d * (step / l);
    return glm::vec3(h.x, v.y, h.z);
}

// A ledge in front: a wall the feet face, with a walkable top between
// mantle_min and mantle_max above the feet and room above it.
inline bool find_ledge(const MovementSettings& s, const MoveBody& b, const glm::vec3& fwd,
                       const MoveRayFn& ray, glm::vec3& top) {
    if (glm::length(fwd) < 0.5f) return false;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float reach = b.radius + s.mantle_reach;
    const MoveHit wall = ray(b.feet + up * std::max(0.25f, s.mantle_min * 0.7f), fwd, reach);
    if (!wall.hit || std::fabs(wall.normal.y) > 0.5f || glm::dot(flat_dir(-wall.normal), fwd) < 0.5f) return false;
    // Down onto the top, just past the wall's face.
    const glm::vec3 in = flat_dir(-wall.normal);
    const glm::vec3 from = glm::vec3(wall.point.x, b.feet.y + s.mantle_max + 0.05f, wall.point.z) + in * (b.radius * 0.8f);
    const MoveHit down = ray(from, -up, s.mantle_max - s.mantle_min + 0.1f);
    if (!down.hit || down.distance < 0.01f || down.normal.y < 0.7f) return false;   // none, or the wall is taller
    const float h = down.point.y - b.feet.y;
    if (h < s.mantle_min || h > s.mantle_max) return false;
    // Room to stand (crouch height at least) above the top, and nothing just beyond it.
    const float need = b.standing_height * std::min(1.0f, s.crouch_height);
    if (ray(down.point + up * 0.05f, up, need).hit) return false;
    if (ray(glm::vec3(b.feet.x, down.point.y + 0.1f + b.radius, b.feet.z), in, reach + b.radius).hit) return false;
    top = down.point + in * (b.radius + 0.05f);
    top.y = down.point.y;
    return true;
}

// A climbable surface in front, facing the character.
inline bool find_climb(const MoveBody& b, const glm::vec3& fwd, const MoveRayFn& ray, MoveHit& out) {
    if (glm::length(fwd) < 0.5f) return false;
    const MoveHit h = ray(b.feet + glm::vec3(0.0f, b.height * 0.5f, 0.0f), fwd, b.radius + 0.35f);
    if (!h.hit || !h.climbable || std::fabs(h.normal.y) > 0.6f) return false;
    out = h;
    return true;
}

// A wall beside the character for a wall run (not climbable, near vertical).
inline bool find_side_wall(const MoveBody& b, const glm::vec3& fwd, const MoveRayFn& ray, MoveHit& out) {
    if (glm::length(fwd) < 0.5f) return false;
    const glm::vec3 side = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));   // right
    const glm::vec3 o = b.feet + glm::vec3(0.0f, b.height * 0.5f, 0.0f);
    for (float sgn : {1.0f, -1.0f}) {
        const MoveHit h = ray(o, side * sgn, b.radius + 0.45f);
        if (h.hit && std::fabs(h.normal.y) < 0.3f) { out = h; return true; }
    }
    return false;
}

}  // namespace move_detail

/// One step of movement. Returns the velocity to move the capsule with (the
/// physics collides and slides it) and the capsule height wanted.
inline MoveResult step_movement(Movement& mv, const MoveInput& in, const MoveBody& body,
                                const MoveRayFn& ray, float dt) {
    using namespace move_detail;
    const MovementSettings& s = mv.settings;
    MovementState& st = mv.state;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    st.events = 0;
    dt = std::clamp(dt, 0.0f, 0.1f);

    // Timers and stamina
    st.dash_cooldown = std::max(0.0f, st.dash_cooldown - dt);
    st.wall_cooldown = std::max(0.0f, st.wall_cooldown - dt);
    st.jump_buffer = in.jump_pressed ? s.jump_buffer : std::max(0.0f, st.jump_buffer - dt);
    const bool limited = s.stamina > 0.0f;
    if (st.stamina < 0.0f) st.stamina = s.stamina;
    st.stamina_idle += dt;
    if (limited && st.stamina_idle >= s.stamina_delay)
        st.stamina = std::min(s.stamina, st.stamina + s.stamina_regen * dt);
    auto spend = [&](float amount) {
        if (!limited) return true;
        if (st.stamina < amount) return false;
        st.stamina -= amount;
        st.stamina_idle = 0.0f;
        return true;
    };

    // Ground contact. Rising fast (a jump's first frames) is not landing.
    const bool was_grounded = st.grounded;
    st.grounded = body.grounded && st.velocity.y <= 0.5f;
    if (st.grounded) {
        if (!was_grounded) {
            st.last_fall = std::max(0.0f, st.fall_start_y - body.feet.y);
            if (st.mode == MoveMode::Air || st.mode == MoveMode::Glide || st.mode == MoveMode::WallRun)
                st.events |= kMoveLanded;
        }
        st.since_ground = 0.0f;
        st.jump_spent = false;
        st.air_jumps_left = s.air_jumps;
        st.air_dash_used = false;
    } else {
        if (was_grounded) st.fall_start_y = body.feet.y;
        st.fall_start_y = std::max(st.fall_start_y, body.feet.y);
        st.since_ground += dt;
    }

    const glm::vec3 fwd = flat_dir(in.forward);
    const glm::vec3 right = flat_dir(in.right);
    glm::vec3 wish = right * in.move.x + fwd * in.move.y;
    const float wish_len = glm::length(wish);
    if (wish_len > 1.0f) wish /= wish_len;
    const glm::vec3 wish_dir = wish_len > 1e-3f ? wish / std::max(1e-3f, glm::length(wish)) : glm::vec3(0.0f);
    const float jump_v = s.jump_speed();
    const float hspeed = glm::length(flat(st.velocity));

    auto enter = [&](MoveMode m) { st.mode = m; st.timer = 0.0f; };
    auto try_mantle = [&](const glm::vec3& towards) {
        if (!s.mantle) return false;
        glm::vec3 top;
        if (!find_ledge(s, body, towards, ray, top)) return false;
        enter(MoveMode::Mantle);
        st.mantle_top = top;
        st.mantle_rise = top.y + 0.05f;
        st.dir = towards;
        st.velocity = glm::vec3(0.0f);
        st.jump_buffer = 0.0f;
        st.events |= kMoveMantled;
        return true;
    };
    auto try_dash = [&]() {
        if (!s.dash || !in.dash_pressed || st.dash_cooldown > 0.0f) return false;
        if (!st.grounded && (!s.dash_in_air || st.air_dash_used)) return false;
        if (!spend(s.dash_cost)) return false;
        enter(MoveMode::Dash);
        st.dir = wish_len > 1e-3f ? wish_dir : (glm::length(fwd) > 0.5f ? fwd : glm::vec3(0.0f, 0.0f, -1.0f));
        st.dash_cooldown = s.dash_cooldown;
        st.velocity = st.dir * s.dash_speed;     // moving from this step on
        if (!st.grounded) st.air_dash_used = true;
        st.events |= kMoveDashed;
        return true;
    };
    auto try_climb = [&]() {
        if (!s.climb || in.move.y < 0.5f) return false;
        MoveHit h;
        if (!find_climb(body, fwd, ray, h)) return false;
        if (glm::dot(fwd, flat_dir(-h.normal)) < 0.5f) return false;
        enter(MoveMode::Climb);
        st.wall_normal = h.normal;
        st.velocity = glm::vec3(0.0f);
        st.events |= kMoveClimb;
        return true;
    };
    auto jump_from_ground = [&]() {
        st.velocity.y = jump_v;
        st.jump_spent = true;
        st.jump_buffer = 0.0f;
        st.grounded = false;
        st.fall_start_y = body.feet.y;
        st.events |= kMoveJumped;
        enter(MoveMode::Air);
    };

    // Water takes over from anything but a mantle out of it.
    if (body.in_water && st.mode != MoveMode::Mantle) {
        if (st.mode != MoveMode::Swim) enter(MoveMode::Swim);
    } else if (!body.in_water && st.mode == MoveMode::Swim) {
        enter(st.grounded ? MoveMode::Walk : MoveMode::Air);
    }

    // Crouch: held, or sliding. The capsule only stands when the physics says
    // there is room, so `crouched` comes from the body's real height.
    st.crouched = body.height < body.standing_height * 0.95f;
    bool want_crouch = (in.crouch && (st.mode == MoveMode::Walk || st.mode == MoveMode::Slide)) ||
                       st.mode == MoveMode::Slide;

    switch (st.mode) {
    case MoveMode::Walk: {
        if (!st.grounded && st.since_ground > 0.04f) {
            // Walked off an edge (coyote time counts from losing the ground). A
            // frame or two without ground -- after a mantle, over a seam -- is
            // not a fall.
            enter(MoveMode::Air);
            break;
        }
        if (try_dash()) break;
        if (try_climb()) break;
        if (st.jump_buffer > 0.0f) {
            if (try_mantle(wish_len > 0.1f ? wish_dir : fwd)) break;
            if (!st.crouched || !in.crouch) { jump_from_ground(); break; }
        }
        st.sprinting = in.sprint && in.move.y > 0.1f && !st.crouched && spend(s.sprint_cost * dt);
        // Slide: crouch pressed while running fast.
        if (s.slide && in.crouch && !st.crouched && hspeed >= s.slide_min_speed) {
            enter(MoveMode::Slide);
            const glm::vec3 d = flat_dir(st.velocity);
            st.velocity += d * s.slide_boost;
            st.dir = d;
            st.sprinting = false;
            st.events |= kMoveSlid;
            want_crouch = true;
            break;
        }
        const float speed = st.crouched ? s.crouch_speed : (st.sprinting ? s.sprint_speed : s.walk_speed);
        st.velocity = approach_flat(st.velocity, wish * speed, wish_len > 1e-3f ? s.accel : s.decel, dt);
        st.velocity.y = 0.0f;
        break;
    }
    case MoveMode::Air: {
        st.sprinting = false;
        if (st.grounded) { enter(MoveMode::Walk); st.velocity.y = 0.0f; break; }
        if (try_dash()) break;
        if (try_climb()) break;
        if (st.jump_buffer > 0.0f) {
            if (try_mantle(wish_len > 0.1f ? wish_dir : fwd)) break;
            if (!st.jump_spent && st.since_ground <= s.coyote_time) { jump_from_ground(); break; }
            if (st.air_jumps_left > 0) {
                --st.air_jumps_left;
                st.velocity.y = jump_v;
                st.jump_buffer = 0.0f;
                st.fall_start_y = body.feet.y;
                if (wish_len > 1e-3f)   // an air jump may turn
                    st.velocity = glm::vec3(0.0f, st.velocity.y, 0.0f) + wish_dir * std::max(hspeed, s.walk_speed);
                st.events |= kMoveAirJumped;
                break;
            }
            if (s.glide && st.velocity.y < 2.0f) {
                enter(MoveMode::Glide);
                st.jump_buffer = 0.0f;
                st.events |= kMoveGlide;
                break;
            }
        }
        // Holding forward into a ledge while falling or near the top of a jump.
        if (in.move.y > 0.5f && st.velocity.y < 2.0f && try_mantle(fwd)) break;
        // A wall run: moving fast along a wall, off the ground for a moment.
        if (s.wall_run && st.wall_cooldown <= 0.0f && in.move.y > 0.5f && hspeed >= s.wall_run_min_speed &&
            st.since_ground > 0.1f) {
            MoveHit w;
            if (find_side_wall(body, fwd, ray, w)) {
                enter(MoveMode::WallRun);
                st.wall_normal = glm::normalize(flat(w.normal));
                st.velocity.y = std::max(st.velocity.y, 1.5f);
                st.events |= kMoveWallRun;
                break;
            }
        }
        // Keep momentum; steer towards the wish at least at walking speed.
        const float cap = std::max(s.walk_speed, hspeed);
        st.velocity = approach_flat(st.velocity, wish * cap, wish_len > 1e-3f ? s.air_accel : 0.0f, dt);
        st.velocity.y = std::max(-s.max_fall_speed, st.velocity.y - s.gravity * dt);
        break;
    }
    case MoveMode::Slide: {
        st.sprinting = false;
        st.timer += dt;
        if (!st.grounded) { enter(MoveMode::Air); break; }
        // A slide jump keeps the speed. Under something low the jump waits for room.
        if (st.jump_buffer > 0.0f && body.height >= body.standing_height * 0.5f) { jump_from_ground(); break; }
        glm::vec3 h = flat(st.velocity);
        float sp = glm::length(h);
        const glm::vec3 d = sp > 1e-4f ? h / sp : st.dir;
        // Downhill along the ground adds speed; friction takes it away.
        const glm::vec3 n = body.ground_normal;
        const glm::vec3 downhill = flat(glm::vec3(0.0f, -1.0f, 0.0f) - n * glm::dot(glm::vec3(0.0f, -1.0f, 0.0f), n));
        const float slope_push = glm::dot(downhill, d) * s.gravity;
        sp = std::max(0.0f, sp + (slope_push - s.slide_friction) * dt);
        // A little steering.
        glm::vec3 nd = d;
        if (wish_len > 1e-3f) nd = flat_dir(d + wish_dir * (2.0f * dt));
        st.velocity = nd * sp;
        const bool downhill_now = slope_push > s.slide_friction;
        if (sp < s.crouch_speed || (!in.crouch && st.timer > 0.25f) ||
            (st.timer > s.slide_max_time && !downhill_now)) {
            enter(MoveMode::Walk);
            want_crouch = in.crouch;
        }
        break;
    }
    case MoveMode::Dash: {
        st.timer += dt;
        st.velocity = st.dir * s.dash_speed;
        if (st.timer >= s.dash_time) {
            st.velocity = st.dir * std::max(s.walk_speed, std::min(s.dash_speed, s.sprint_speed));
            enter(st.grounded ? MoveMode::Walk : MoveMode::Air);
        }
        break;
    }
    case MoveMode::Mantle: {
        st.timer += dt;
        const float t_rise = s.mantle_time * 0.6f, t_over = s.mantle_time * 0.4f;
        if (body.feet.y < st.mantle_rise - 0.02f && st.timer < t_rise * 2.0f) {
            // Up the wall face.
            const float left = std::max(dt, t_rise - st.timer);
            st.velocity = up * std::max(2.0f, (st.mantle_rise - body.feet.y) / left);
        } else {
            // Onto the top.
            glm::vec3 to = st.mantle_top - body.feet;
            to.y = std::max(0.0f, to.y + 0.02f);
            const float dist = glm::length(flat(to));
            if (dist < 0.05f || st.timer > s.mantle_time * 2.5f) {
                enter(MoveMode::Walk);
                st.velocity = st.dir * (s.walk_speed * 0.5f);
                st.grounded = true;
                st.since_ground = 0.0f;   // standing on it (the physics may need a frame to agree)
                st.jump_spent = false;
                break;
            }
            const float left = std::max(dt, s.mantle_time - st.timer);
            st.velocity = flat(to) / std::max(left, t_over * 0.25f) + up * (to.y / std::max(dt, 0.05f));
        }
        break;
    }
    case MoveMode::Glide: {
        if (st.grounded) { enter(MoveMode::Walk); st.events |= kMoveLanded; break; }
        if (!in.jump_held) { enter(MoveMode::Air); break; }
        if (try_mantle(fwd)) break;
        const glm::vec3 target = (wish_len > 1e-3f ? wish_dir : flat_dir(st.velocity)) * s.glide_speed;
        st.velocity = approach_flat(st.velocity, target, s.air_accel * 1.5f, dt);
        st.velocity.y = std::max(-s.glide_fall_speed, st.velocity.y - s.gravity * dt);
        break;
    }
    case MoveMode::WallRun: {
        st.timer += dt;
        MoveHit w;
        const bool wall = find_side_wall(body, fwd, ray, w) && glm::dot(glm::normalize(flat(w.normal)), st.wall_normal) > 0.7f;
        if (st.jump_buffer > 0.0f) {
            // Jump off the wall: away from it, up, and on along it.
            const glm::vec3 along = flat(st.velocity);
            st.velocity = st.wall_normal * s.wall_jump_speed + along * 0.85f;
            st.velocity.y = jump_v;
            st.jump_buffer = 0.0f;
            st.wall_cooldown = 0.35f;
            st.fall_start_y = body.feet.y;
            st.events |= kMoveWallJump;
            enter(MoveMode::Air);
            break;
        }
        if (st.grounded) { enter(MoveMode::Walk); break; }
        if (!wall || in.move.y < 0.3f || st.timer > s.wall_run_time) {
            st.wall_cooldown = 0.35f;
            enter(MoveMode::Air);
            break;
        }
        st.wall_normal = glm::normalize(flat(w.normal));
        // Along the wall at least at the wall-run speed; pressed lightly to it.
        glm::vec3 along = flat(st.velocity) - st.wall_normal * glm::dot(flat(st.velocity), st.wall_normal);
        if (glm::length(along) < 1e-3f) along = fwd - st.wall_normal * glm::dot(fwd, st.wall_normal);
        along = flat_dir(along) * std::max(s.wall_run_min_speed, glm::length(along));
        const float vy = st.velocity.y - s.gravity * s.wall_run_gravity * dt;
        st.velocity = along - st.wall_normal * 1.0f;
        st.velocity.y = vy;
        break;
    }
    case MoveMode::Climb: {
        st.sprinting = false;
        MoveHit h;
        const bool on = find_climb(body, flat_dir(-st.wall_normal), ray, h);
        if (st.jump_buffer > 0.0f) {
            // Push off.
            st.velocity = st.wall_normal * 4.0f + up * (jump_v * 0.7f);
            st.jump_buffer = 0.0f;
            st.jump_spent = true;
            st.fall_start_y = body.feet.y;
            st.events |= kMoveJumped;
            enter(MoveMode::Air);
            break;
        }
        if (!on) {
            // Off the top: onto the ledge if there is one, else a small hop.
            if (in.move.y > 0.1f && try_mantle(flat_dir(-st.wall_normal))) break;
            st.velocity = in.move.y > 0.1f ? up * 2.5f + flat_dir(-st.wall_normal) * 1.5f : glm::vec3(0.0f);
            enter(MoveMode::Air);
            break;
        }
        if (st.grounded && in.move.y < -0.1f) { enter(MoveMode::Walk); break; }
        st.wall_normal = h.normal;
        const glm::vec3 side = flat_dir(glm::cross(up, st.wall_normal));   // along the surface, sideways
        st.velocity = up * (in.move.y * s.climb_speed) + side * (in.move.x * s.climb_speed * 0.6f) -
                      st.wall_normal * 0.5f;
        break;
    }
    case MoveMode::Swim: {
        st.sprinting = false;
        if (in.move.y > 0.5f && try_mantle(fwd)) break;
        st.velocity = approach_flat(st.velocity, wish * s.swim_speed, s.accel * 0.3f, dt);
        const float target_vy = in.jump_held ? 3.0f : (in.crouch ? -2.5f : -0.4f);
        st.velocity.y += (target_vy - st.velocity.y) * std::min(1.0f, 4.0f * dt);
        st.jump_buffer = 0.0f;
        break;
    }
    }

    MoveResult r;
    r.velocity = st.velocity;
    // On the ground a little down keeps the capsule on slopes going downhill.
    // Walking presses down even in the moment the ground is not reported, to settle onto it.
    if (st.mode == MoveMode::Walk) r.velocity.y = -1.0f;
    else if (st.mode == MoveMode::Slide && st.grounded) r.velocity.y = -1.0f;
    r.height = body.standing_height * (want_crouch ? std::clamp(s.crouch_height, 0.3f, 1.0f) : 1.0f);
    return r;
}

// ---- animation ---------------------------------------------------------------
// Which clip of the player's rigged model plays for each move, by clip index.
// -1 leaves it to the model (its own idle / move clips by speed).
struct MoveAnimation {
    int sprint = -1, crouch = -1, crouch_move = -1, air = -1, slide = -1, dash = -1,
        mantle = -1, glide = -1, wall_run = -1, climb = -1, swim = -1;
};

/// The clip for the move now; `fallback` is the model's own choice.
inline int move_animation_clip(const MoveAnimation& a, const MovementState& st, bool moving, int fallback) {
    auto pick = [fallback](int c) { return c >= 0 ? c : fallback; };
    switch (st.mode) {
        case MoveMode::Walk:
            if (st.crouched) return pick(moving && a.crouch_move >= 0 ? a.crouch_move : a.crouch);
            if (st.sprinting && moving) return pick(a.sprint);
            return fallback;
        case MoveMode::Air:     return pick(a.air);
        case MoveMode::Slide:   return pick(a.slide >= 0 ? a.slide : a.crouch);
        case MoveMode::Dash:    return pick(a.dash >= 0 ? a.dash : a.sprint);
        case MoveMode::Mantle:  return pick(a.mantle >= 0 ? a.mantle : a.climb);
        case MoveMode::Glide:   return pick(a.glide >= 0 ? a.glide : a.air);
        case MoveMode::WallRun: return pick(a.wall_run >= 0 ? a.wall_run : a.sprint);
        case MoveMode::Climb:   return pick(a.climb);
        case MoveMode::Swim:    return pick(a.swim);
    }
    return fallback;
}

inline void serialize_move_animation(const void* obj, std::vector<uint8_t>& out) {
    const auto& a = *static_cast<const MoveAnimation*>(obj);
    const int v[] = {a.sprint, a.crouch, a.crouch_move, a.air, a.slide, a.dash, a.mantle, a.glide, a.wall_run, a.climb, a.swim};
    detail::put_i32(out, 1);
    detail::put_i32(out, static_cast<int32_t>(sizeof v / sizeof v[0]));
    for (int x : v) detail::put_i32(out, x);
}
inline void deserialize_move_animation(void* obj, const uint8_t* data, size_t n) {
    auto& a = *static_cast<MoveAnimation*>(obj);
    a = MoveAnimation{};
    int* f[] = {&a.sprint, &a.crouch, &a.crouch_move, &a.air, &a.slide, &a.dash, &a.mantle, &a.glide, &a.wall_run, &a.climb, &a.swim};
    const uint8_t* p = data; const uint8_t* e = data + n;
    int32_t ver = 0, count = 0;
    if (!detail::get_i32(p, e, ver) || ver < 1 || !detail::get_i32(p, e, count)) return;
    for (int i = 0; i < count; ++i) {
        int32_t v = -1;
        if (!detail::get_i32(p, e, v)) return;
        if (i < static_cast<int>(sizeof f / sizeof f[0])) *f[i] = v;
    }
}

// ---- saving ------------------------------------------------------------------
// Version, then the settings in order. A field added later goes at the end and
// reads its default from older files.
inline void serialize_movement(const void* obj, std::vector<uint8_t>& out) {
    const MovementSettings& s = static_cast<const Movement*>(obj)->settings;
    detail::put_i32(out, 1);
    const float f[] = {s.walk_speed, s.sprint_speed, s.crouch_speed, s.accel, s.decel, s.air_accel,
                       s.gravity, s.jump_height, static_cast<float>(s.air_jumps), s.coyote_time, s.jump_buffer,
                       s.max_fall_speed, s.crouch_height, s.slide ? 1.0f : 0.0f, s.slide_min_speed, s.slide_boost,
                       s.slide_friction, s.slide_max_time, s.dash ? 1.0f : 0.0f, s.dash_speed, s.dash_time,
                       s.dash_cooldown, s.dash_in_air ? 1.0f : 0.0f, s.mantle ? 1.0f : 0.0f, s.mantle_min,
                       s.mantle_max, s.mantle_reach, s.mantle_time, s.glide ? 1.0f : 0.0f, s.glide_fall_speed,
                       s.glide_speed, s.wall_run ? 1.0f : 0.0f, s.wall_run_min_speed, s.wall_run_time,
                       s.wall_run_gravity, s.wall_jump_speed, s.climb ? 1.0f : 0.0f, s.climb_speed, s.swim_speed,
                       s.stamina, s.sprint_cost, s.dash_cost, s.stamina_regen, s.stamina_delay};
    detail::put_i32(out, static_cast<int32_t>(sizeof f / sizeof f[0]));
    for (float v : f) detail::put_f32(out, v);
}
inline void deserialize_movement(void* obj, const uint8_t* data, size_t n) {
    Movement& m = *static_cast<Movement*>(obj);
    MovementSettings& s = m.settings;
    m.state = MovementState{};
    const uint8_t* p = data; const uint8_t* e = data + n;
    int32_t ver = 0, count = 0;
    if (!detail::get_i32(p, e, ver) || ver < 1 || !detail::get_i32(p, e, count)) return;
    float* fields[] = {&s.walk_speed, &s.sprint_speed, &s.crouch_speed, &s.accel, &s.decel, &s.air_accel,
                       &s.gravity, &s.jump_height, nullptr, &s.coyote_time, &s.jump_buffer,
                       &s.max_fall_speed, &s.crouch_height, nullptr, &s.slide_min_speed, &s.slide_boost,
                       &s.slide_friction, &s.slide_max_time, nullptr, &s.dash_speed, &s.dash_time,
                       &s.dash_cooldown, nullptr, nullptr, &s.mantle_min,
                       &s.mantle_max, &s.mantle_reach, &s.mantle_time, nullptr, &s.glide_fall_speed,
                       &s.glide_speed, nullptr, &s.wall_run_min_speed, &s.wall_run_time,
                       &s.wall_run_gravity, &s.wall_jump_speed, nullptr, &s.climb_speed, &s.swim_speed,
                       &s.stamina, &s.sprint_cost, &s.dash_cost, &s.stamina_regen, &s.stamina_delay};
    const int known = static_cast<int>(sizeof fields / sizeof fields[0]);
    for (int i = 0; i < count; ++i) {
        float v = 0.0f;
        if (!detail::get_f32(p, e, v)) return;
        if (i >= known) continue;
        if (fields[i]) { *fields[i] = v; continue; }
        const bool on = v > 0.5f;
        switch (i) {
            case 8:  s.air_jumps = static_cast<int>(v + 0.5f); break;
            case 13: s.slide = on; break;
            case 18: s.dash = on; break;
            case 22: s.dash_in_air = on; break;
            case 23: s.mantle = on; break;
            case 28: s.glide = on; break;
            case 31: s.wall_run = on; break;
            case 36: s.climb = on; break;
            default: break;
        }
    }
}
inline void register_movement_components() {
    register_authorable(make_authorable_custom<Movement>("Movement", &serialize_movement, &deserialize_movement));
    register_authorable(make_authorable_custom<MoveAnimation>("Move Animation", &serialize_move_animation, &deserialize_move_animation));
}

}  // namespace schizo::ecs
