#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace schizo::scene {
    class Entity;
}

namespace schizo::physics {
    class RigidBody;
    class PhysicsWorld;
}

namespace schizo::demo {

/**
 * @class PhysicsDemoScene
 * @brief Demonstrates physics system capabilities
 * 
 * Features demonstrated:
 * - Rigid body dynamics (falling bodies, gravity)
 * - Collision detection and response
 * - Multiple collision shapes (spheres, boxes, planes)
 * - Friction and restitution
 * - Constraints and joints (if available)
 */
class PhysicsDemoScene {
public:
    PhysicsDemoScene();
    ~PhysicsDemoScene();
    
    // Scene lifecycle
    void Initialize(schizo::physics::PhysicsWorld* world);
    void Update(float delta_time);
    void Cleanup();
    
    // Scene components
    void SetupGround();
    void SetupFallingBodies();
    void SetupCollidingBodies();
    void SetupComplexStructure();
    
    // Query scene state
    int GetBodyCount() const { return body_count_; }
    float GetSimulationTime() const { return simulation_time_; }
    
private:
    schizo::physics::PhysicsWorld* physics_world_ = nullptr;
    std::vector<schizo::physics::RigidBody*> bodies_;
    
    int body_count_ = 0;
    float simulation_time_ = 0.0f;
    
    // Helper methods
    schizo::physics::RigidBody* CreateSphere(const glm::vec3& position, float radius, float mass);
    schizo::physics::RigidBody* CreateBox(const glm::vec3& position, const glm::vec3& size, float mass);
    schizo::physics::RigidBody* CreateGround();
};

} // namespace schizo::demo
