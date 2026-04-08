#include "physics_demo_scene.h"
#include "../../core/physics/rigidbody_physics.h"
#include <glm/gtc/matrix_transform.hpp>

namespace schizo::demo {

using namespace schizo::physics;

PhysicsDemoScene::PhysicsDemoScene()
    : body_count_(0), simulation_time_(0.0f)
{
}

PhysicsDemoScene::~PhysicsDemoScene()
{
    Cleanup();
}

void PhysicsDemoScene::Initialize(PhysicsWorld* world)
{
    physics_world_ = world;
    if (!physics_world_) {
        return;
    }
    
    // Set up scene gravity
    physics_world_->SetGravity(glm::vec3(0, -9.81f, 0));
    
    // Build scene in stages
    SetupGround();
    SetupFallingBodies();
    SetupCollidingBodies();
    SetupComplexStructure();
}

void PhysicsDemoScene::Update(float delta_time)
{
    if (!physics_world_) {
        return;
    }
    
    simulation_time_ += delta_time;
    
    // Step physics simulation
    physics_world_->Update(delta_time);
}

void PhysicsDemoScene::Cleanup()
{
    bodies_.clear();
    physics_world_ = nullptr;
    body_count_ = 0;
}

void PhysicsDemoScene::SetupGround()
{
    if (!physics_world_) {
        return;
    }
    
    // Create large static ground plane
    auto ground = CreateGround();
    if (ground) {
        bodies_.push_back(ground);
        body_count_++;
    }
}

void PhysicsDemoScene::SetupFallingBodies()
{
    if (!physics_world_) {
        return;
    }
    
    // Create multiple spheres at different heights
    for (int i = 0; i < 5; ++i) {
        float height = 10.0f + (i * 3.0f);
        float x_offset = (i - 2) * 2.0f;
        
        auto sphere = CreateSphere(
            glm::vec3(x_offset, height, 0),
            0.5f + (i * 0.1f),
            0.5f + (i * 0.2f)
        );
        
        if (sphere) {
            sphere->SetRestitution(0.6f + (i * 0.05f));
            sphere->SetFriction(0.5f);
            bodies_.push_back(sphere);
            body_count_++;
        }
    }
}

void PhysicsDemoScene::SetupCollidingBodies()
{
    if (!physics_world_) {
        return;
    }
    
    // Create colliding balls
    auto ball1 = CreateSphere(glm::vec3(-5, 8, 0), 1.0f, 2.0f);
    if (ball1) {
        ball1->SetLinearVelocity(glm::vec3(5, 0, 0));  // Moving right
        ball1->SetRestitution(0.8f);
        bodies_.push_back(ball1);
        body_count_++;
    }
    
    auto ball2 = CreateSphere(glm::vec3(5, 8, 0), 1.2f, 1.5f);
    if (ball2) {
        ball2->SetLinearVelocity(glm::vec3(-3, 0, 0));  // Moving left
        ball2->SetRestitution(0.7f);
        bodies_.push_back(ball2);
        body_count_++;
    }
}

void PhysicsDemoScene::SetupComplexStructure()
{
    if (!physics_world_) {
        return;
    }
    
    // Create a pyramid-like structure of boxes
    const float box_size = 0.5f;
    const float spacing = 0.02f;  // Small gap between boxes
    float base_y = 0.5f;  // Just above ground
    
    // Base row (3 boxes)
    for (int i = 0; i < 3; ++i) {
        float x = (i - 1) * (box_size + spacing);
        auto box = CreateBox(glm::vec3(x, base_y, 0), glm::vec3(box_size, box_size, box_size), 1.0f);
        if (box) {
            box->SetFriction(0.4f);
            bodies_.push_back(box);
            body_count_++;
        }
    }
    
    // Middle row (2 boxes)
    for (int i = 0; i < 2; ++i) {
        float x = (i - 0.5f) * (box_size + spacing);
        auto box = CreateBox(glm::vec3(x, base_y + box_size + spacing, 0), glm::vec3(box_size, box_size, box_size), 1.0f);
        if (box) {
            box->SetFriction(0.4f);
            bodies_.push_back(box);
            body_count_++;
        }
    }
    
    // Top box
    auto top_box = CreateBox(glm::vec3(0, base_y + 2 * (box_size + spacing), 0), glm::vec3(box_size, box_size, box_size), 1.0f);
    if (top_box) {
        top_box->SetFriction(0.4f);
        bodies_.push_back(top_box);
        body_count_++;
    }
}

RigidBody* PhysicsDemoScene::CreateSphere(const glm::vec3& position, float radius, float mass)
{
    if (!physics_world_) {
        return nullptr;
    }
    
    auto body = std::make_unique<RigidBody>();
    body->SetPosition(position);
    body->SetMass(mass);
    
    auto shape = std::make_unique<SphereShape>(radius);
    body->SetCollisionShape(std::move(shape));
    
    RigidBody* body_ptr = body.get();
    physics_world_->AddRigidBody(body.get());
    physics_world_->AddRigidBody(std::move(body));
    
    return body_ptr;
}

RigidBody* PhysicsDemoScene::CreateBox(const glm::vec3& position, const glm::vec3& size, float mass)
{
    if (!physics_world_) {
        return nullptr;
    }
    
    auto body = std::make_unique<RigidBody>();
    body->SetPosition(position);
    body->SetMass(mass);
    
    // Box shape takes half extents
    auto shape = std::make_unique<BoxShape>(size * 0.5f);
    body->SetCollisionShape(std::move(shape));
    
    RigidBody* body_ptr = body.get();
    physics_world_->AddRigidBody(body.get());
    physics_world_->AddRigidBody(std::move(body));
    
    return body_ptr;
}

RigidBody* PhysicsDemoScene::CreateGround()
{
    if (!physics_world_) {
        return nullptr;
    }
    
    auto body = std::make_unique<RigidBody>();
    body->SetPosition(glm::vec3(0, -10, 0));
    body->SetMass(0);  // Static
    
    auto shape = std::make_unique<BoxShape>(glm::vec3(50, 1, 50));  // Wide ground plane
    body->SetCollisionShape(std::move(shape));
    body->SetFriction(0.8f);
    
    RigidBody* body_ptr = body.get();
    physics_world_->AddRigidBody(body.get());
    physics_world_->AddRigidBody(std::move(body));
    
    return body_ptr;
}

} // namespace schizo::demo
