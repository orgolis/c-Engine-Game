#include <iostream>
#include <glm/glm.hpp>
#include <chrono>

// Physics headers
#include "../../engine/core/physics/rigidbody_physics.h"

using namespace schizo::physics;
using namespace std::chrono;

/**
 * Simple console-based physics test demonstrating:
 * - RigidBody creation and physics simulation
 * - Collision detection (sphere collisions)
 * - Physics world stepping
 * - Force application and gravity
 */
int main() {
    std::cout << "=== GameWorldshaper Physics Engine Test ===\n" << std::endl;
    
    // Create physics world
    std::cout << "[1/5] Creating physics world..." << std::endl;
    auto physics_world = std::make_unique<DefaultPhysicsWorld>();
    
    PhysicsConfig config;
    config.gravity = glm::vec3(0, -9.81f, 0);
    config.time_scale = 1.0f;
    config.solver_iterations = 4;
    config.max_bodies = 100;
    
    physics_world->SetGravity(config.gravity);
    std::cout << "  ✓ Physics world created" << std::endl;
    std::cout << "  ✓ Gravity: " << config.gravity.y << " m/s²" << std::endl;
    
    // Create test bodies
    std::cout << "\n[2/5] Creating rigid bodies..." << std::endl;
    
    // Ground plane (static)
    auto ground = std::make_unique<RigidBody>();
    ground->SetPosition(glm::vec3(0, -10, 0));
    ground->SetMass(0);  // Static
    auto ground_shape = std::make_unique<BoxShape>(glm::vec3(100, 1, 100));
    ground->SetCollisionShape(std::move(ground_shape));
    
    RigidBody* ground_ptr = ground.get();
    physics_world->AddRigidBody(ground.get());
    physics_world->AddRigidBody(std::move(ground));
    std::cout << "  ✓ Ground plane created (static, mass=0)" << std::endl;
    
    // Test sphere 1 (dynamic)
    auto sphere1 = std::make_unique<RigidBody>();
    sphere1->SetPosition(glm::vec3(0, 10, 0));
    sphere1->SetMass(1.0f);
    auto sphere1_shape = std::make_unique<SphereShape>(1.0f);
    sphere1->SetCollisionShape(std::move(sphere1_shape));
    sphere1->SetFriction(0.5f);
    sphere1->SetRestitution(0.7f);
    
    RigidBody* sphere1_ptr = sphere1.get();
    physics_world->AddRigidBody(sphere1.get());
    physics_world->AddRigidBody(std::move(sphere1));
    std::cout << "  ✓ Sphere 1 created (mass=1.0, at height=10)" << std::endl;
    
    // Test sphere 2 (dynamic)
    auto sphere2 = std::make_unique<RigidBody>();
    sphere2->SetPosition(glm::vec3(5, 15, 0));
    sphere2->SetMass(2.0f);
    auto sphere2_shape = std::make_unique<SphereShape>(0.8f);
    sphere2->SetCollisionShape(std::move(sphere2_shape));
    sphere2->SetFriction(0.3f);
    sphere2->SetRestitution(0.9f);
    sphere2->ApplyForce(glm::vec3(-20, 0, 0));  // Initial force to the left
    
    RigidBody* sphere2_ptr = sphere2.get();
    physics_world->AddRigidBody(sphere2.get());
    physics_world->AddRigidBody(std::move(sphere2));
    std::cout << "  ✓ Sphere 2 created (mass=2.0, at height=15, force applied)" << std::endl;
    
    // Test box (dynamic)
    auto box = std::make_unique<RigidBody>();
    box->SetPosition(glm::vec3(-5, 8, 0));
    box->SetMass(3.0f);
    auto box_shape = std::make_unique<BoxShape>(glm::vec3(0.5f, 0.5f, 0.5f));
    box->SetCollisionShape(std::move(box_shape));
    box->SetFriction(0.6f);
    box->SetRestitution(0.4f);
    
    RigidBody* box_ptr = box.get();
    physics_world->AddRigidBody(box.get());
    physics_world->AddRigidBody(std::move(box));
    std::cout << "  ✓ Box created (mass=3.0, at height=8)" << std::endl;
    
    // Simulate
    std::cout << "\n[3/5] Running physics simulation..." << std::endl;
    std::cout << "  Simulating 5 seconds with 60 Hz fixed timestep\n" << std::endl;
    
    float simulation_time = 5.0f;
    float timestep = 1.0f / 60.0f;  // 60 Hz
    float elapsed = 0.0f;
    int frame = 0;
    
    // Print header
    std::cout << "Frame | Time(s) | Sphere1 Y | Sphere2 Y | Box Y | Collisions" << std::endl;
    std::cout << "------|---------|-----------|-----------|-------|------------" << std::endl;
    
    while (elapsed < simulation_time) {
        physics_world->Step(timestep);
        elapsed += timestep;
        frame++;
        
        // Print every 30 frames (0.5 seconds at 60 Hz)
        if (frame % 30 == 0) {
            uint32_t collision_count = physics_world->GetCollisionCount();
            printf("%5d | %7.2f | %9.2f | %9.2f | %5.2f | %10u\n",
                   frame,
                   elapsed,
                   sphere1_ptr->GetPosition().y,
                   sphere2_ptr->GetPosition().y,
                   box_ptr->GetPosition().y,
                   collision_count);
        }
    }
    
    // Results
    std::cout << "\n[4/5] Final state:" << std::endl;
    std::cout << "  Sphere 1:" << std::endl;
    std::cout << "    Position: (" << sphere1_ptr->GetPosition().x << ", " 
              << sphere1_ptr->GetPosition().y << ", " << sphere1_ptr->GetPosition().z << ")" << std::endl;
    std::cout << "    Velocity: " << glm::length(sphere1_ptr->GetVelocity()) << " m/s" << std::endl;
    std::cout << "    Sleeping: " << (sphere1_ptr->IsSleeping() ? "YES" : "NO") << std::endl;
    
    std::cout << "  Sphere 2:" << std::endl;
    std::cout << "    Position: (" << sphere2_ptr->GetPosition().x << ", " 
              << sphere2_ptr->GetPosition().y << ", " << sphere2_ptr->GetPosition().z << ")" << std::endl;
    std::cout << "    Velocity: " << glm::length(sphere2_ptr->GetVelocity()) << " m/s" << std::endl;
    std::cout << "    Sleeping: " << (sphere2_ptr->IsSleeping() ? "YES" : "NO") << std::endl;
    
    std::cout << "  Box:" << std::endl;
    std::cout << "    Position: (" << box_ptr->GetPosition().x << ", " 
              << box_ptr->GetPosition().y << ", " << box_ptr->GetPosition().z << ")" << std::endl;
    std::cout << "    Velocity: " << glm::length(box_ptr->GetVelocity()) << " m/s" << std::endl;
    std::cout << "    Sleeping: " << (box_ptr->IsSleeping() ? "YES" : "NO") << std::endl;
    
    std::cout << "\n[5/5] Physics statistics:" << std::endl;
    std::cout << "  Total bodies: " << physics_world->GetBodyCount() << std::endl;
    std::cout << "  Active bodies: " << physics_world->GetActiveBodyCount() << std::endl;
    std::cout << "  Sleeping bodies: " << physics_world->GetSleepingBodyCount() << std::endl;
    std::cout << "  Total collisions detected: " << physics_world->GetCollisionCount() << std::endl;
    std::cout << "  Total collision tests: " << physics_world->GetCollisionTestCount() << std::endl;
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    std::cout << "\nExpected behavior:" << std::endl;
    std::cout << "  ✓ Objects fall due to gravity" << std::endl;
    std::cout << "  ✓ Objects eventually settle on ground" << std::endl;
    std::cout << "  ✓ Bouncing due to restitution" << std::endl;
    std::cout << "  ✓ Objects enter sleep mode when still" << std::endl;
    std::cout << "  ✓ Collision detection active throughout" << std::endl;
    
    return 0;
}
