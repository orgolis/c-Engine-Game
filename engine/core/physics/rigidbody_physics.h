#pragma once

// Rigid Body Physics Module - Dynamics simulation system
// Includes: collision detection, rigid body simulation, physics world, constraints & joints
// Namespace: schizo::physics (for rigid body dynamics)
// Note: Advanced physics systems (PDEs, particles, vector fields) are in gws::physics namespace

#include "collision.h"
#include "rigidbody.h"
#include "physics_world.h"
#include "constraints.h"

namespace schizo::physics {
    // Rigid body physics simulation system
    // 
    // Core Components:
    // - CollisionShape & subclasses: Define collision geometry (Sphere, Box, Capsule, Cylinder, Plane)
    // - RigidBody: Physics component for entities with mass, velocity, forces, constraints
    // - PhysicsWorld: Simulation manager handling collision detection, constraint solving, queries
    // - Constraints: Joint types (Distance, FixedJoint, HingeJoint, SpringConstraint)
    //
    // Usage Pattern:
    // 1. Create PhysicsWorld with configuration
    // 2. For each physics entity:
    //    - Add RigidBody component to entity
    //    - Set collision shape
    //    - Configure mass, damping, constraints
    // 3. Each frame, call PhysicsWorld::Step(delta_time)
    // 4. Entity transforms update from physics simulation
    //
    // Features:
    // - Multiple collision shapes (sphere, box, capsule, cylinder, plane)
    // - Dynamic, kinematic, and static bodies
    // - Force and impulse application
    // - Constraint-based joints (distance, fixed, hinge, spring)
    // - Sleeping system for dormant bodies
    // - Spatial queries (raycasts, AABB, sphere queries)
    // - Performance profiling and statistics
    // - Optional debug visualization
    //
    // Physics Settings (PhysicsConfig):
    // - Gravity: Default (0, -9.81, 0) - set via SetGravity()
    // - Time scale: Slow motion, instant replay effects
    // - Solver iterations: Higher = more stable but slower (default: 4)
    // - Sleeping: Enable to improve performance on idle bodies
    // - Broad/narrow phase: Optimization flags
}

