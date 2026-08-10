#pragma once

// ============================================================================
// G13 · Vehicles & racing — arcade drive model + checkpoints / laps.
// ============================================================================
//
// A VehicleDef is authored data (top speed, accel/brake, turn rate, drag, boost).
// drive_vehicle() integrates an arcade model on the entity's Transform each frame
// from throttle/steer/brake/boost input (steering scales with speed, reverses in
// reverse; a boost/nitro resource is consumed + regenerates). RaceTrack is a lap
// of checkpoints; RaceProgress tracks lap/checkpoint/timing and race_rank orders
// the field. Deterministic + testable; the visual car + terrain collision are the
// integration layer (Jolt), this is the gameplay/feel model.

#include "world.h"
#include "components.h"           // Transform
#include "gameplay_events.h"
#include "authorable_components.h"
#include "component_serialize.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace schizo::ecs {

struct VehicleDef {
    std::string id, name;
    float max_speed    = 40.0f;   // m/s
    float accel        = 15.0f;   // m/s^2
    float brake_decel  = 25.0f;
    float reverse_speed = 10.0f;
    float turn_rate    = 90.0f;   // deg/s (at speed)
    float drag         = 5.0f;    // engine braking toward 0 when off-throttle
    float boost_mult   = 1.5f;    // top-speed multiplier while boosting
    float boost_accel  = 25.0f;   // extra accel while boosting
};
inline std::vector<VehicleDef>& vehicle_registry() { static std::vector<VehicleDef> r; return r; }
inline void register_vehicle(const VehicleDef& d) {
    for (auto& e : vehicle_registry()) if (e.id == d.id) { e = d; return; }
    vehicle_registry().push_back(d);
}
inline const VehicleDef* find_vehicle(const std::string& id) {
    for (const auto& e : vehicle_registry()) if (e.id == id) return &e;
    return nullptr;
}

struct Vehicle {
    std::string vehicle_id;
    float speed = 0.0f;       // signed, along heading (runtime)
    float heading = 0.0f;     // radians yaw (runtime)
    float boost = 100.0f;     // nitro 0..max (runtime)
    float max_boost = 100.0f;
};
struct VehicleInput { float throttle = 0.0f; float steer = 0.0f; bool brake = false; bool boost = false; };

inline glm::vec3 heading_forward(float h) { return glm::vec3(std::sin(h), 0.0f, std::cos(h)); }

// Integrate the arcade drive model one step on the entity's Transform.
//
// Driving CLAIMS AUTHORITY over the transform. This system integrates position
// and heading every frame, so it is the owner of both; without the claim the
// editor's scene mirror overwrites the result the same frame and the vehicle
// does not move -- silently, with nothing reporting an error. The rule is
// general: a system that integrates a transform declares it owns it, so any
// caller gets the right behaviour without remembering to say so.
inline void drive_vehicle(World& w, Entity e, const VehicleInput& in, float dt) {
    Vehicle*   v = w.try_get<Vehicle>(e);
    Transform* t = w.try_get<Transform>(e);
    if (!v || !t) return;
    const VehicleDef* d = find_vehicle(v->vehicle_id);
    if (!d) return;

    if (!w.has<EcsAuthoritative>(e)) w.tag<EcsAuthoritative>(e);

    const bool  boosting = in.boost && v->boost > 0.0f;
    const float accel = d->accel + (boosting ? d->boost_accel : 0.0f);
    const float top   = d->max_speed * (boosting ? d->boost_mult : 1.0f);

    if (in.throttle != 0.0f) {
        v->speed += in.throttle * accel * dt;
    } else {                                   // engine braking toward 0
        const float s = std::max(0.0f, std::abs(v->speed) - d->drag * dt);
        v->speed = std::copysign(s, v->speed);
    }
    if (in.brake) {
        const float s = std::max(0.0f, std::abs(v->speed) - d->brake_decel * dt);
        v->speed = std::copysign(s, v->speed);
    }
    v->speed = std::clamp(v->speed, -d->reverse_speed, top);

    const float speed_factor = std::min(1.0f, std::abs(v->speed) / 5.0f);   // can't turn stationary
    const float dir = v->speed >= 0.0f ? 1.0f : -1.0f;                      // reverse inverts steering
    v->heading += glm::radians(d->turn_rate) * in.steer * dt * speed_factor * dir;

    t->position += heading_forward(v->heading) * v->speed * dt;
    t->rotation = glm::angleAxis(v->heading, glm::vec3(0.0f, 1.0f, 0.0f));

    if (boosting) v->boost = std::max(0.0f, v->boost - 20.0f * dt);
    else          v->boost = std::min(v->max_boost, v->boost + 5.0f * dt);
}

// ---------------- racing ----------------
struct Checkpoint { glm::vec3 position{0.0f}; float radius = 8.0f; };
struct RaceTrack  { std::string id; std::vector<Checkpoint> checkpoints; int laps = 3; };
inline std::vector<RaceTrack>& track_registry() { static std::vector<RaceTrack> r; return r; }
inline void register_track(const RaceTrack& t) {
    for (auto& e : track_registry()) if (e.id == t.id) { e = t; return; }
    track_registry().push_back(t);
}
inline const RaceTrack* find_track(const std::string& id) {
    for (const auto& e : track_registry()) if (e.id == id) return &e;
    return nullptr;
}

struct RaceProgress {
    std::string track_id;
    int   next_checkpoint = 0;
    int   lap = 0;
    float time = 0.0f;       // total elapsed
    float lap_time = 0.0f;   // current lap elapsed
    float best_lap = 0.0f;   // 0 = none yet
    bool  finished = false;
};

// Advance every racer: age the clocks, cross checkpoints, complete laps + finish.
inline void tick_race(World& w, float dt, GameplayEventBus* bus = nullptr) {
    w.each<RaceProgress, Transform>([&](Entity e, RaceProgress& rp, Transform& t) {
        if (rp.finished) return;
        const RaceTrack* trk = find_track(rp.track_id);
        if (!trk || trk->checkpoints.empty()) return;
        rp.time += dt; rp.lap_time += dt;
        const Checkpoint& cp = trk->checkpoints[rp.next_checkpoint];
        const glm::vec3 d = t.position - cp.position;
        if (glm::dot(d, d) <= cp.radius * cp.radius) {
            ++rp.next_checkpoint;
            if (rp.next_checkpoint >= static_cast<int>(trk->checkpoints.size())) {
                rp.next_checkpoint = 0;
                ++rp.lap;
                if (rp.best_lap == 0.0f || rp.lap_time < rp.best_lap) rp.best_lap = rp.lap_time;
                if (bus) { GameplayEvent ev; ev.name = "race.lap"; ev.target = e; ev.value = static_cast<float>(rp.lap); bus->publish(ev); }
                rp.lap_time = 0.0f;
                if (rp.lap >= trk->laps) { rp.finished = true; if (bus) { GameplayEvent ev; ev.name = "race.finish"; ev.target = e; ev.value = rp.time; bus->publish(ev); } }
            } else if (bus) {
                GameplayEvent ev; ev.name = "race.checkpoint"; ev.target = e; ev.value = static_cast<float>(rp.next_checkpoint); bus->publish(ev);
            }
        }
    });
}

// How far along the track a racer is (higher = further): lap, then checkpoint,
// then proximity to the next checkpoint.
inline float race_progress_score(World& w, Entity e) {
    const RaceProgress* rp = w.try_get<RaceProgress>(e);
    if (!rp) return -1.0f;
    float score = static_cast<float>(rp->lap) * 1000.0f + static_cast<float>(rp->next_checkpoint) * 10.0f;
    const RaceTrack* trk = find_track(rp->track_id);
    const Transform* t = w.try_get<Transform>(e);
    if (trk && t && rp->next_checkpoint < static_cast<int>(trk->checkpoints.size()))
        score -= glm::distance(t->position, trk->checkpoints[rp->next_checkpoint].position) * 0.01f;
    return score;
}
inline int race_rank(World& w, Entity racer, const std::vector<Entity>& racers) {
    const float mine = race_progress_score(w, racer);
    int rank = 1;
    for (Entity o : racers) if (o != racer && race_progress_score(w, o) > mine) ++rank;
    return rank;
}

// ---------------- custom serialization ----------------
inline void serialize_vehicle(const void* obj, std::vector<uint8_t>& out) {
    const auto& v = *static_cast<const Vehicle*>(obj);
    detail::put_str(out, v.vehicle_id);
    detail::put_f32(out, v.max_boost);
}
inline void deserialize_vehicle(void* obj, const uint8_t* data, size_t n) {
    auto& v = *static_cast<Vehicle*>(obj);
    const uint8_t* p = data; const uint8_t* e = data + n;
    if (!detail::get_str(p, e, v.vehicle_id)) return;
    detail::get_f32(p, e, v.max_boost);
    v.speed = 0.0f; v.heading = 0.0f; v.boost = v.max_boost;
}
inline void serialize_race(const void* obj, std::vector<uint8_t>& out) {
    detail::put_str(out, static_cast<const RaceProgress*>(obj)->track_id);
}
inline void deserialize_race(void* obj, const uint8_t* data, size_t n) {
    auto& rp = *static_cast<RaceProgress*>(obj);
    const uint8_t* p = data; const uint8_t* e = data + n;
    detail::get_str(p, e, rp.track_id);
    rp.next_checkpoint = 0; rp.lap = 0; rp.time = rp.lap_time = rp.best_lap = 0.0f; rp.finished = false;
}
inline void register_vehicle_components() {
    register_authorable(make_authorable_custom<Vehicle>("Vehicle", &serialize_vehicle, &deserialize_vehicle));
    register_authorable(make_authorable_custom<RaceProgress>("Race Progress", &serialize_race, &deserialize_race));
}

}  // namespace schizo::ecs
