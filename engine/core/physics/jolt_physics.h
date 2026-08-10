#pragma once

// ====================
// PhysicsWorld — thin engine interface over Jolt  (Master Plan Stage 4)
// ====================
//
// Wraps Jolt Physics behind an engine-native API so gameplay/ECS never bind to
// Jolt types (the plan's "wrap in schizo::physics::PhysicsWorld behind a thin
// engine interface"). Jolt headers live ONLY in jolt_physics.cpp (pimpl). Step
// the world at a FIXED timestep inside the deterministic sim (Jolt is
// deterministic given identical inputs/order — built with
// CROSS_PLATFORM_DETERMINISTIC).

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace schizo::physics {

enum class ShapeType : uint32_t { Box = 0, Sphere = 1, Capsule = 2, Cylinder = 3 };
enum class MotionType : uint32_t { Static = 0, Dynamic = 1, Kinematic = 2 };

using BodyId = uint32_t;
inline constexpr BodyId kInvalidBody = 0xFFFFFFFFu;

struct BodyDesc {
    ShapeType  shape        = ShapeType::Box;
    glm::vec3  half_extents{0.5f, 0.5f, 0.5f};  // box
    float      radius       = 0.5f;             // sphere / capsule
    float      height       = 1.0f;             // capsule cylinder height
    glm::vec3  position{0.0f, 0.0f, 0.0f};
    glm::quat  rotation{1.0f, 0.0f, 0.0f, 0.0f};
    MotionType motion       = MotionType::Dynamic;
    float      mass         = 1.0f;
    float      friction     = 0.5f;
    float      restitution  = 0.2f;
};

struct BodyState {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 linear_velocity{0.0f};
};

// Closest-hit ray query result (used for audio occlusion, line-of-sight, etc.).
struct RayHit {
    bool      hit      = false;
    float     distance = 0.0f;          // along the ray, in world units
    glm::vec3 position{0.0f};           // world-space hit point
    BodyId    body     = kInvalidBody;
};

// Kinematic capsule character controller (Jolt CharacterVirtual): collide-and-
// slide against the world with slope + stair handling. Drives the Stage-10
// movement state machine.
using CharacterId = uint32_t;

struct CharacterDesc {
    float     radius        = 0.3f;   // capsule radius
    float     height        = 1.2f;   // capsule cylinder height (total = height + 2*radius)
    glm::vec3 position{0.0f, 1.0f, 0.0f};
    float     max_slope_deg = 50.0f;  // steeper than this = not walkable (slides)
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&)            = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Bring up the Jolt world (process-global Jolt state is initialised once,
    // lazily, across all PhysicsWorld instances).
    bool init(glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f), uint32_t max_bodies = 65536);

    // Create + add a body; returns its id (kInvalidBody on failure).
    BodyId add_body(const BodyDesc& desc);
    // Add a STATIC triangle-mesh body (e.g. terrain / level geometry collider).
    // `triangles` is a soup: every 3 consecutive verts form one triangle.
    BodyId add_mesh_body(const std::vector<glm::vec3>& triangles,
                         const glm::vec3& position = glm::vec3(0.0f),
                         const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    // Add a body whose shape is the CONVEX HULL of `points` — the standard way
    // to give a DYNAMIC object a custom mesh shape (Jolt triangle meshes can
    // only be static). `desc.shape` is ignored; motion/mass/friction/etc. apply.
    BodyId add_convex_body(const std::vector<glm::vec3>& points, const BodyDesc& desc);
    void   remove_body(BodyId id);

    // Advance the simulation by `dt`. Call at a fixed timestep for determinism.
    void   step(float dt, int collision_steps = 1);

    BodyState get_state(BodyId id) const;

    // Cast a ray from `origin` along `direction` (need not be unit-length) up to
    // `max_distance`, returning the closest hit. Used by audio occlusion to test
    // line-of-sight between the listener and a source.
    RayHit    raycast(const glm::vec3& origin, const glm::vec3& direction, float max_distance) const;

    void      set_transform(BodyId id, const glm::vec3& pos, const glm::quat& rot);
    // Kinematic move: sets the body's velocities so it reaches the target pose
    // over `dt`, pushing any dynamic bodies it contacts on the way (unlike
    // set_transform, which teleports through them). Intended for kinematic
    // bodies driven from external state (e.g. net-replicated players).
    void      move_kinematic(BodyId id, const glm::vec3& pos, const glm::quat& rot, float dt);
    void      set_linear_velocity(BodyId id, const glm::vec3& v);
    glm::vec3 get_linear_velocity(BodyId id) const;
    // Instantaneous impulse at the body's center of mass (dynamic bodies).
    void      add_impulse(BodyId id, const glm::vec3& impulse);

    // --- Character controller (collide-and-slide) ---
    CharacterId add_character(const CharacterDesc& desc);
    // Advance the character with the caller's FULL desired velocity (the caller
    // owns gravity + jump in velocity.y; this only does collide-and-slide +
    // stair-step). Call at the fixed timestep.
    void        update_character(CharacterId id, const glm::vec3& velocity, float dt);
    void        set_character_position(CharacterId id, const glm::vec3& pos);
    glm::vec3   character_position(CharacterId id) const;
    bool        character_on_ground(CharacterId id) const;

    /// Move every body and character by `d`, for a floating-origin rebase.
    ///
    /// The rebase moves the scene; the simulation does not follow, and the
    /// write-back then teleports every dynamic entity to its pre-rebase
    /// position -- the dynamic half of the world snaps back to the old origin
    /// while the static half moves, which looks like the level tearing in two.
    ///
    /// Velocities are deliberately untouched: a rigid translation of the whole
    /// world preserves them, and relative positions are unchanged, so no new
    /// contacts appear. Bodies are moved WITHOUT activation for the same
    /// reason -- waking every sleeping body on a rebase would cost more than
    /// the rebase saves and would perturb a simulation that has not changed.
    void translate_world(const glm::vec3& d);

    size_t body_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace schizo::physics
