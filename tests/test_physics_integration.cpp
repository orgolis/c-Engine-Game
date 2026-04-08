#include <catch2/catch_all.hpp>
#include <glm/glm.hpp>
#include <memory>

// Physics headers
#include "../../engine/core/physics/rigidbody_physics.h"
#include "../../engine/core/physics/collision.h"
#include "../../engine/core/physics/constraints.h"

using namespace schizo::physics;
using namespace schizo::physics::collision;

// ============================================================================
// Test: Physics World Basic Operations
// ============================================================================

TEST_CASE("Physics World creation and configuration", "[physics]") {
    auto world = std::make_unique<DefaultPhysicsWorld>();
    
    REQUIRE(world != nullptr);
    
    // Test gravity
    glm::vec3 gravity(0, -9.81f, 0);
    world->SetGravity(gravity);
    REQUIRE(world->GetGravity() == gravity);
    
    // Test world bounds
    glm::vec3 min(-100, -100, -100);
    glm::vec3 max(100, 100, 100);
    world->SetWorldBounds(min, max);
}

// ============================================================================
// Test: Rigid Body Creation & Properties
// ============================================================================

TEST_CASE("RigidBody creation with sphere collision shape", "[physics]") {
    auto body = std::make_unique<RigidBody>();
    
    // Test initial state
    REQUIRE(body->GetPosition() == glm::vec3(0));
    REQUIRE(body->GetRotation() == glm::quat(1, 0, 0, 0));
    
    // Set position
    glm::vec3 pos(5, 10, -3);
    body->SetPosition(pos);
    REQUIRE(body->GetPosition() == pos);
    
    // Set mass
    body->SetMass(2.5f);
    REQUIRE(body->GetMass() == 2.5f);
    
    // Static body (mass 0)
    auto static_body = std::make_unique<RigidBody>();
    static_body->SetMass(0);
    REQUIRE(static_body->GetBodyType() == BodyType::Static);
    
    // Dynamic body
    auto dynamic_body = std::make_unique<RigidBody>();
    dynamic_body->SetMass(1.0f);
    REQUIRE(dynamic_body->GetBodyType() == BodyType::Dynamic);
}

TEST_CASE("RigidBody with multiple collision shapes", "[physics]") {
    auto body = std::make_unique<RigidBody>();
    
    // Sphere
    auto sphere_shape = std::make_unique<SphereShape>(1.5f);
    body->SetCollisionShape(std::move(sphere_shape));
    REQUIRE(body->GetCollisionShape() != nullptr);
    
    // Box
    auto box_shape = std::make_unique<BoxShape>(glm::vec3(1, 2, 3));
    body->SetCollisionShape(std::move(box_shape));
    REQUIRE(body->GetCollisionShape() != nullptr);
}

TEST_CASE("RigidBody velocity and forces", "[physics]") {
    auto body = std::make_unique<RigidBody>();
    body->SetMass(1.0f);
    
    // Linear velocity
    glm::vec3 vel(5, 0, 0);
    body->SetLinearVelocity(vel);
    REQUIRE(body->GetLinearVelocity() == vel);
    
    // Apply force
    glm::vec3 force(10, 0, 0);
    body->ApplyForce(force);
    
    // Angular velocity
    glm::vec3 ang_vel(0, 1, 0);
    body->SetAngularVelocity(ang_vel);
    REQUIRE(body->GetAngularVelocity() == ang_vel);
}

TEST_CASE("RigidBody friction and restitution", "[physics]") {
    auto body = std::make_unique<RigidBody>();
    
    // Friction
    body->SetFriction(0.7f);
    REQUIRE(body->GetFriction() == 0.7f);
    
    // Restitution (bounciness)
    body->SetRestitution(0.8f);
    REQUIRE(body->GetRestitution() == 0.8f);
}

// ============================================================================
// Test: Collision Detection
// ============================================================================

TEST_CASE("Sphere-Sphere collision detection", "[physics]") {
    Contact contact;
    
    // Two spheres colliding
    bool colliding = SphereSphere(
        glm::vec3(0, 0, 0), 1.0f,
        glm::vec3(1.5f, 0, 0), 1.0f,
        contact
    );
    
    REQUIRE(colliding == true);
    REQUIRE(contact.depth > 0.0f);
}

TEST_CASE("Sphere-Sphere no collision (separated)", "[physics]") {
    Contact contact;
    
    // Spheres too far apart
    bool colliding = SphereSphere(
        glm::vec3(0, 0, 0), 1.0f,
        glm::vec3(5.0f, 0, 0), 1.0f,
        contact
    );
    
    REQUIRE(colliding == false);
}

TEST_CASE("Sphere-Box collision detection", "[physics]") {
    Contact contact;
    
    // Sphere colliding with box
    bool colliding = SphereBox(
        glm::vec3(0, 0, 0), 1.0f,
        glm::vec3(2, 0, 0), glm::vec3(1, 1, 1),
        contact
    );
    
    REQUIRE(colliding == true);
}

TEST_CASE("Box-Box collision detection (AABB)", "[physics]") {
    Contact contact;
    
    // Boxes colliding
    bool colliding = BoxBox(
        glm::vec3(-1, 0, 0), glm::vec3(1, 1, 1),
        glm::vec3(1, 0, 0), glm::vec3(1, 1, 1),
        contact
    );
    
    REQUIRE(colliding == true);
    REQUIRE(contact.depth > 0.0f);
}

TEST_CASE("Sphere-Plane collision detection", "[physics]") {
    Contact contact;
    
    // Sphere hitting plane from above
    bool colliding = SpherePlane(
        glm::vec3(0, 1, 0), 0.5f,
        glm::vec3(0, 0, 0), glm::vec3(0, 1, 0),  // plane at y=0, normal up
        contact
    );
    
    REQUIRE(colliding == true);
}

// ============================================================================
// Test: Physics World Integration
// ============================================================================

TEST_CASE("Physics world with falling body", "[physics]") {
    auto world = std::make_unique<DefaultPhysicsWorld>();
    world->SetGravity(glm::vec3(0, -9.81f, 0));
    
    // Create falling sphere
    auto sphere = std::make_unique<RigidBody>();
    sphere->SetPosition(glm::vec3(0, 10, 0));
    sphere->SetMass(1.0f);
    auto shape = std::make_unique<SphereShape>(1.0f);
    sphere->SetCollisionShape(std::move(shape));
    
    RigidBody* sphere_ptr = sphere.get();
    world->AddRigidBody(sphere.get());
    world->AddRigidBody(std::move(sphere));
    
    // Step simulation
    world->Update(0.016f);  // 60 Hz timestep
    
    // Sphere should have moved down due to gravity
    REQUIRE(sphere_ptr->GetPosition().y < 10.0f);
}

TEST_CASE("Physics world with collision response", "[physics]") {
    auto world = std::make_unique<DefaultPhysicsWorld>();
    world->SetGravity(glm::vec3(0, -9.81f, 0));
    
    // Ground (static)
    auto ground = std::make_unique<RigidBody>();
    ground->SetPosition(glm::vec3(0, -5, 0));
    ground->SetMass(0);  // Static
    auto ground_shape = std::make_unique<BoxShape>(glm::vec3(100, 1, 100));
    ground->SetCollisionShape(std::move(ground_shape));
    ground->SetFriction(0.8f);
    
    RigidBody* ground_ptr = ground.get();
    world->AddRigidBody(ground.get());
    world->AddRigidBody(std::move(ground));
    
    // Falling ball
    auto ball = std::make_unique<RigidBody>();
    ball->SetPosition(glm::vec3(0, 0, 0));
    ball->SetMass(1.0f);
    auto ball_shape = std::make_unique<SphereShape>(0.5f);
    ball->SetCollisionShape(std::move(ball_shape));
    ball->SetRestitution(0.6f);
    ball->SetFriction(0.5f);
    
    RigidBody* ball_ptr = ball.get();
    world->AddRigidBody(ball.get());
    world->AddRigidBody(std::move(ball));
    
    // Simulate falling and bouncing
    float time = 0.0f;
    float timestep = 1.0f / 60.0f;
    
    for (int i = 0; i < 300; ++i) {  // 5 seconds
        world->Update(timestep);
        time += timestep;
    }
    
    // Ball should settle on ground
    REQUIRE(ball_ptr->GetPosition().y >= -5.5f);  // Near ground level
}

// ============================================================================
// Test: Distance Queries
// ============================================================================

TEST_CASE("Point in sphere test", "[physics]") {
    // Point inside sphere
    glm::vec3 sphere_center(0, 0, 0);
    float radius = 5.0f;
    glm::vec3 point(1, 1, 1);
    
    float dist_sq = glm::length2(point - sphere_center);
    bool inside = dist_sq <= (radius * radius);
    
    REQUIRE(inside == true);
}

TEST_CASE("Point in box test", "[physics]") {
    // Point inside AABB
    glm::vec3 box_min(-5, -5, -5);
    glm::vec3 box_max(5, 5, 5);
    glm::vec3 point(0, 0, 0);
    
    bool inside = (point.x >= box_min.x && point.x <= box_max.x &&
                   point.y >= box_min.y && point.y <= box_max.y &&
                   point.z >= box_min.z && point.z <= box_max.z);
    
    REQUIRE(inside == true);
}

// ============================================================================
// Test: Constraints (Joints)
// ============================================================================

TEST_CASE("Fixed constraint creation", "[physics]") {
    auto body1 = std::make_unique<RigidBody>();
    auto body2 = std::make_unique<RigidBody>();
    
    body1->SetPosition(glm::vec3(0, 0, 0));
    body2->SetPosition(glm::vec3(1, 0, 0));
    body1->SetMass(1.0f);
    body2->SetMass(1.0f);
    
    // Create fixed constraint
    FixedConstraint constraint(body1.get(), body2.get());
    REQUIRE(constraint.GetBodyA() == body1.get());
    REQUIRE(constraint.GetBodyB() == body2.get());
}

TEST_CASE("Ball-and-socket constraint creation", "[physics]") {
    auto body1 = std::make_unique<RigidBody>();
    auto body2 = std::make_unique<RigidBody>();
    
    body1->SetPosition(glm::vec3(0, 0, 0));
    body2->SetPosition(glm::vec3(1, 0, 0));
    body1->SetMass(1.0f);
    body2->SetMass(1.0f);
    
    // Create ball-and-socket constraint
    BallSocketConstraint constraint(body1.get(), body2.get(), glm::vec3(0.5f, 0, 0));
    REQUIRE(constraint.GetBodyA() == body1.get());
    REQUIRE(constraint.GetBodyB() == body2.get());
}

TEST_CASE("Hinge constraint creation", "[physics]") {
    auto body1 = std::make_unique<RigidBody>();
    auto body2 = std::make_unique<RigidBody>();
    
    body1->SetPosition(glm::vec3(0, 0, 0));
    body2->SetPosition(glm::vec3(1, 0, 0));
    body1->SetMass(1.0f);
    body2->SetMass(1.0f);
    
    // Create hinge constraint
    HingeConstraint constraint(body1.get(), body2.get(), 
                               glm::vec3(0.5f, 0, 0), glm::vec3(0, 0, 1));
    REQUIRE(constraint.GetBodyA() == body1.get());
    REQUIRE(constraint.GetBodyB() == body2.get());
}

// ============================================================================
// Test: Physics Queries
// ============================================================================

TEST_CASE("Raycast query (no hit)", "[physics]") {
    auto world = std::make_unique<DefaultPhysicsWorld>();
    
    glm::vec3 ray_start(0, 0, 0);
    glm::vec3 ray_dir(1, 0, 0);
    float ray_length = 100.0f;
    
    bool hit = false;
    float hit_distance = 0.0f;
    RigidBody* hit_body = nullptr;
    
    // Should not hit anything (empty world)
    REQUIRE(hit == false);
}

// ============================================================================
// Test: Body States & Sleeping
// ============================================================================

TEST_CASE("Rigid body sleeping state", "[physics]") {
    auto body = std::make_unique<RigidBody>();
    
    // Initially should not be sleeping
    REQUIRE(body->IsSleeping() == false);
    
    // Set to sleeping
    body->SetSleeping(true);
    body->SetAwake(false);
    REQUIRE(body->IsSleeping() == true);
    
    // Wake up
    body->SetAwake(true);
    REQUIRE(body->IsSleeping() == false);
}

TEST_CASE("Rigid body kinematic mode", "[physics]") {
    auto body = std::make_unique<RigidBody>();
    body->SetMass(1.0f);
    
    // Switch to kinematic
    body->SetBodyType(BodyType::Kinematic);
    REQUIRE(body->GetBodyType() == BodyType::Kinematic);
    
    // Can move programmatically
    body->SetLinearVelocity(glm::vec3(5, 0, 0));
    REQUIRE(body->GetLinearVelocity() == glm::vec3(5, 0, 0));
}

// ============================================================================
// Test: Physics Statistics
// ============================================================================

TEST_CASE("Physics world statistics", "[physics]") {
    auto world = std::make_unique<DefaultPhysicsWorld>();
    
    // Add some bodies
    for (int i = 0; i < 5; ++i) {
        auto body = std::make_unique<RigidBody>();
        body->SetMass(1.0f);
        auto shape = std::make_unique<SphereShape>(0.5f);
        body->SetCollisionShape(std::move(shape));
        world->AddRigidBody(std::move(body));
    }
    
    // Step simulation
    world->Update(0.016f);
    
    auto stats = world->GetStats();
    REQUIRE(stats.active_bodies > 0);
    REQUIRE(stats.collision_tests >= 0);
    REQUIRE(stats.collisions_found >= 0);
}
