#pragma once

#include "rigidbody.h"
#include "collision.h"
#include "constraints.h"
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace schizo::physics {

// Forward declarations
class RigidBody;

// ============================================================================
// Physics World Configuration
// ============================================================================

struct PhysicsConfig {
    glm::vec3 gravity = glm::vec3(0, -9.81f, 0);
    float time_scale = 1.0f;
    uint32_t solver_iterations = 4;
    uint32_t max_bodies = 10000;
    
    // Collision detection
    bool enable_broad_phase = true;
    bool enable_narrow_phase = true;
    
    // Optimization
    bool enable_sleeping = true;
    float sleep_time_threshold = 0.5f;
};

// ============================================================================
// Physics World / Simulation
// ============================================================================

/**
 * @class PhysicsWorld
 * @brief Main physics simulation world
 * 
 * Manages all rigid bodies, performs collision detection, and integrates forces.
 * Should be updated once per fixed timestep for stability.
 * 
 * Usage:
 * auto physics = PhysicsWorld::Create(config);
 * physics->AddRigidBody(rigidbody_component);
 * physics->Step(fixed_delta_time);
 */
class PhysicsWorld {
public:
    /**
     * Create a physics world
     * @param config Physics configuration
     * @return Unique pointer to world
     */
    static std::unique_ptr<PhysicsWorld> Create(const PhysicsConfig& config = PhysicsConfig());
    
    virtual ~PhysicsWorld() = default;
    
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) = default;
    PhysicsWorld& operator=(PhysicsWorld&&) = default;
    
    // ========== World Management ==========
    
    /**
     * Add a rigid body to the world
     */
    virtual void AddRigidBody(RigidBody* body) = 0;
    
    /**
     * Remove a rigid body from the world
     */
    virtual void RemoveRigidBody(RigidBody* body) = 0;
    
    /**
     * Clear all bodies
     */
    virtual void Clear() = 0;
    
    /**
     * Get number of bodies in world
     */
    virtual uint32_t GetBodyCount() const = 0;
    
    // ========== Simulation ==========
    
    /**
     * Step the simulation
     * @param delta_time Timestep (use fixed timestep for stability)
     */
    virtual void Step(float delta_time) = 0;
    
    /**
     * Set gravity
     */
    virtual void SetGravity(const glm::vec3& gravity) = 0;
    glm::vec3 GetGravity() const { return gravity_; }
    
    /**
     * Set time scale (slow-mo: < 1.0, fast-forward: > 1.0)
     */
    virtual void SetTimeScale(float scale) = 0;
    float GetTimeScale() const { return time_scale_; }
    
    /**
     * Enable/disable world simulation
     */
    virtual void SetEnabled(bool enabled) = 0;
    bool IsEnabled() const { return enabled_; }
    
    // ========== Collision Queries ==========
    
    /**
     * Raycast into the world
     * @param origin Ray start position
     * @param direction Ray direction (normalized)
     * @param max_distance Maximum raycast distance
     * @param out_hit Body hit by ray (nullptr if none)
     * @param out_point Intersection point
     * @param out_normal Surface normal at intersection
     * @return True if hit something
     */
    virtual bool Raycast(const glm::vec3& origin, const glm::vec3& direction,
                        float max_distance, RigidBody*& out_hit,
                        glm::vec3& out_point, glm::vec3& out_normal) const = 0;
    
    /**
     * Get all bodies overlapping an AABB
     * @param min Bounding box minimum
     * @param max Bounding box maximum
     * @return Vector of bodies in the region
     */
    virtual std::vector<RigidBody*> GetBodiesInAABB(const glm::vec3& min,
                                                     const glm::vec3& max) const = 0;
    
    /**
     * Get all bodies overlapping a sphere
     * @param center Sphere center
     * @param radius Sphere radius
     * @return Vector of bodies in the region
     */
    virtual std::vector<RigidBody*> GetBodiesInSphere(const glm::vec3& center,
                                                       float radius) const = 0;

    /**
     * Get the list of contacts a body participated in during the most recent
     * Step(). Each Contact's `normal` is oriented "from the queried body
     * outward toward the other body" (so for a body resting on the floor,
     * the contact normal points downward).
     *
     * @return Pointer to the contact list (owned by the world), or nullptr
     *         if this body had no contacts last frame.
     */
    virtual const std::vector<Contact>* GetBodyContacts(RigidBody* body) const = 0;
    
    // ========== Statistics ==========
    
    struct Stats {
        uint32_t body_count = 0;
        uint32_t active_bodies = 0;
        uint32_t sleeping_bodies = 0;
        uint32_t collision_tests = 0;
        uint32_t collisions_found = 0;
        double simulation_time_ms = 0.0;
    };
    
    /**
     * Get simulation statistics
     */
    virtual Stats GetStats() const = 0;
    
    /**
     * Reset statistics
     */
    virtual void ResetStats() = 0;
    
    // ========== Configuration ==========
    
    /**
     * Set number of solver iterations (higher = more stable, slower)
     */
    virtual void SetSolverIterations(uint32_t iterations) = 0;
    
    /**
     * Enable/disable broad phase (spatial partitioning)
     */
    virtual void SetBroadPhaseEnabled(bool enabled) = 0;
    
    /**
     * Enable/disable sleeping optimization
     */
    virtual void SetSleepingEnabled(bool enabled) = 0;

protected:
    PhysicsWorld() = default;
    
    glm::vec3 gravity_ = glm::vec3(0, -9.81f, 0);
    float time_scale_ = 1.0f;
    bool enabled_ = true;
};

// ============================================================================
// Physics Debug Visualization
// ============================================================================

/**
 * Callback for physics debug rendering
 * Implement to visualize physics objects (shapes, velocities, forces)
 */
class PhysicsDebugDraw {
public:
    virtual ~PhysicsDebugDraw() = default;
    
    /**
     * Draw a line
     */
    virtual void DrawLine(const glm::vec3& from, const glm::vec3& to,
                         const glm::vec3& color) = 0;
    
    /**
     * Draw a wireframe sphere
     */
    virtual void DrawSphere(const glm::vec3& center, float radius,
                           const glm::vec3& color) = 0;
    
    /**
     * Draw a wireframe box
     */
    virtual void DrawBox(const glm::vec3& center, const glm::vec3& half_extents,
                        const glm::vec3& color) = 0;
    
    /**
     * Draw contact points
     */
    virtual void DrawContact(const glm::vec3& point, const glm::vec3& normal,
                            float distance, const glm::vec3& color) = 0;
};

} // namespace schizo::physics
