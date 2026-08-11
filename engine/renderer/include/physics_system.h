#pragma once

#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <cstdint>

// Forward declarations
namespace schizo::physics {
    class PhysicsWorld;
    class RigidBody;
}

namespace schizo::renderer {
    class PhysicsComponent;
}

namespace schizo::scene {

/**
 * @class PhysicsSystem
 * @brief Manages physics simulation at scene level
 * 
 * Coordinates:
 * - Physics world creation and configuration
 * - Physics update timing and stepping
 * - Physics component lifecycle
 * - Collision events and callbacks
 */
class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    /**
     * Initialize physics system with given gravity
     */
    void Initialize(const glm::vec3& gravity = glm::vec3(0, -9.81f, 0));
    
    /**
     * Shutdown physics system
     */
    void Shutdown();
    
    /**
     * Update physics simulation (called once per frame)
     * 
     * @param delta_time Time since last frame (seconds)
     * @param fixed_timestep Fixed physics timestep (usually 1/60 or 1/120)
     */
    void Update(float delta_time, float fixed_timestep = 1.0f / 60.0f);
    
    /**
     * Step physics by fixed timestep (called internally by Update)
     */
    void FixedUpdate(float fixed_timestep);
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * Set world gravity
     */
    void SetGravity(const glm::vec3& gravity);
    glm::vec3 GetGravity() const;
    
    /**
     * Set time scale (0.0 = paused, 1.0 = normal, 2.0 = 2x speed)
     */
    void SetTimeScale(float scale);
    float GetTimeScale() const;
    
    /**
     * Set constraint solver iterations (higher = more accurate but slower)
     */
    void SetSolverIterations(uint32_t iterations);
    uint32_t GetSolverIterations() const;
    
    /**
     * Set maximum number of bodies the physics world can hold
     */
    void SetMaxBodies(uint32_t max_bodies);
    uint32_t GetMaxBodies() const;
    
    /**
     * Enable/disable physics simulation entirely
     */
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    
    // ========================================================================
    // Physics World Access
    // ========================================================================
    
    /**
     * Get underlying physics world (for advanced use)
     */
    schizo::physics::PhysicsWorld* GetPhysicsWorld() const;
    
    // ========================================================================
    // Statistics & Debugging
    // ========================================================================
    
    /**
     * Get number of active bodies in simulation
     */
    uint32_t GetActiveBodyCount() const;
    
    /**
     * Get number of sleeping bodies
     */
    uint32_t GetSleepingBodyCount() const;
    
    /**
     * Get total number of bodies
     */
    uint32_t GetTotalBodyCount() const;
    
    /**
     * Get number of collisions detected this frame
     */
    uint32_t GetCollisionCount() const;
    
    /**
     * Get number of collision tests performed this frame
     */
    uint32_t GetCollisionTestCount() const;
    
    /**
     * Enable debug rendering of physics shapes
     */
    void SetDebugVisualization(bool enabled);
    bool GetDebugVisualization() const;

private:
    std::unique_ptr<schizo::physics::PhysicsWorld> physics_world_;
    
    float accumulator_;  // For fixed timestep accumulation
    float time_scale_;
    bool enabled_;
    bool debug_visualization_;
    
    /**
     * Run a fixed physics step
     */
    void RunPhysicsStep(float fixed_timestep);
};

} // namespace schizo::scene
