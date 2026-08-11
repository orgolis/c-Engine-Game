#pragma once

// ====================
// PhysicsScene — ECS <-> Jolt bridge  (Master Plan Stage 4, step 3)
// ====================
//
// Drives a Jolt PhysicsWorld from the ECS RigidBody / Collider / Transform
// components. Call simulate() at the fixed timestep (inside the deterministic
// sim): it lazily creates a Jolt body for each new physics entity, pushes
// kinematic transforms in, steps the world, and writes dynamic results back to
// the ECS Transform + RigidBody velocity. Header-only so it needs no link
// dependency between gws_ecs and gws_physics — the consumer links both.
//
//   PhysicsScene phys; phys.init();
//   phys.simulate(world, 1.0f/66.0f);   // each fixed step

#include "physics/jolt_physics.h"

#include "ecs/world.h"
#include "ecs/components.h"

#include <cstdint>

namespace schizo::physics {

// RigidBody::flags bits (mirrors the ECS component doc).
inline constexpr uint32_t kRbKinematic = 1u << 0;
inline constexpr uint32_t kRbGravity   = 1u << 1;

class PhysicsScene {
public:
    bool init(glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f)) { return world_.init(gravity); }

    PhysicsWorld&       world()       { return world_; }
    const PhysicsWorld& world() const { return world_; }

    // One fixed simulation step against the ECS world.
    void simulate(ecs::World& w, float dt) {
        create_bodies(w);
        push_kinematic(w);
        world_.step(dt);
        pull_dynamic(w);
    }

    // Create Jolt bodies for entities that have RigidBody + Collider + Transform
    // but no backing body yet (body_handle == 0). body_handle stores id+1 so 0
    // keeps meaning "no body".
    void create_bodies(ecs::World& w) {
        w.each<ecs::Transform, ecs::RigidBody, ecs::Collider>(
            [&](ecs::Entity, ecs::Transform& t, ecs::RigidBody& rb, ecs::Collider& col) {
                if (rb.body_handle != 0) return;
                BodyDesc d;
                d.shape        = shape_of(col);
                d.half_extents = col.half_extents;
                d.radius       = col.radius;
                d.height       = col.height;
                d.position     = t.position;
                d.rotation     = t.rotation;
                // Convention: kinematic flag -> Kinematic; mass<=0 -> Static
                // (immovable, e.g. ground/level geometry); else Dynamic.
                d.motion       = (rb.flags & kRbKinematic) ? MotionType::Kinematic
                               : (rb.mass <= 0.0f)         ? MotionType::Static
                                                          : MotionType::Dynamic;
                d.mass         = rb.mass;
                d.friction     = rb.friction;
                d.restitution  = rb.restitution;
                const BodyId id = world_.add_body(d);
                rb.body_handle = static_cast<uint64_t>(id) + 1;
                if (id != kInvalidBody) world_.set_linear_velocity(id, rb.velocity);
            });
    }

    // Static bodies don't move; kinematic bodies are driven BY the game, so push
    // their authored ECS transform into Jolt before stepping.
    void push_kinematic(ecs::World& w) {
        w.each<ecs::Transform, ecs::RigidBody>(
            [&](ecs::Entity, ecs::Transform& t, ecs::RigidBody& rb) {
                if (rb.body_handle == 0 || !(rb.flags & kRbKinematic)) return;
                world_.set_transform(static_cast<BodyId>(rb.body_handle - 1), t.position, t.rotation);
            });
    }

    // Write simulated dynamic bodies back to the ECS (Transform + velocity).
    void pull_dynamic(ecs::World& w) {
        w.each<ecs::Transform, ecs::RigidBody>(
            [&](ecs::Entity, ecs::Transform& t, ecs::RigidBody& rb) {
                if (rb.body_handle == 0 || (rb.flags & kRbKinematic)) return;
                const BodyState s = world_.get_state(static_cast<BodyId>(rb.body_handle - 1));
                t.position  = s.position;
                t.rotation  = s.rotation;
                rb.velocity = s.linear_velocity;
            });
    }

private:
    static ShapeType shape_of(const ecs::Collider& c) {
        switch (c.shape) {
            case 1:  return ShapeType::Sphere;
            case 2:  return ShapeType::Capsule;
            default: return ShapeType::Box;   // 0 box (3 convex / 4 mesh fall back to box for now)
        }
    }

    PhysicsWorld world_;
};

}  // namespace schizo::physics
