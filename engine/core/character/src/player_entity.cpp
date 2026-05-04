#include "player_entity.h"
#include "scene.h"
#include "entity.h"
#include "transform_component.h"
#include "mesh_renderer_component.h"
#include "character_controller.h"
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>

namespace schizo::character {

using namespace schizo::scene;

// Component to mark entity as player
class PlayerMarkerComponent : public Component {
public:
    bool is_player = true;
};

std::shared_ptr<Entity> PlayerEntity::Create(
    Scene* scene,
    const std::string& name,
    const glm::vec3& position) 
{
    if (!scene) {
        spdlog::error("PlayerEntity::Create - Scene is null");
        return nullptr;
    }
    
    // Create base entity
    auto player = scene->CreateEntity(name);
    if (!player) {
        spdlog::error("PlayerEntity::Create - Failed to create entity");
        return nullptr;
    }
    
    // Add transform at spawn position
    auto transform = player->GetComponent<TransformComponent>();
    if (!transform) {
        transform = player->AddComponent<TransformComponent>();
    }
    if (transform) {
        transform->SetPosition(position);
        transform->SetScale(glm::vec3(0.5f, 1.8f, 0.5f));  // Capsule-like shape
    }
    
    // Add mesh renderer (white capsule by default)
    auto mesh_renderer = player->AddComponent<MeshRendererComponent>(
        MeshType::Capsule,
        glm::vec4(0.8f, 0.8f, 0.8f, 1.0f)  // Light gray
    );
    
    // Add character controller for movement
    auto controller = player->AddComponent<CharacterController>();
    if (controller) {
        controller->SetMaxSpeed(7.0f);           // m/s
        controller->SetMaxSprintSpeed(12.0f);    // m/s
        controller->SetJumpForce(15.0f);         // m/s
        controller->SetAcceleration(25.0f);      // m/s^2
    }
    
    // Add character stats
    auto stats = player->AddComponent<CharacterStats>();
    if (stats) {
        stats->SetMaxHealth(100.0f);
        stats->SetHealth(100.0f);
        stats->SetLevel(1);
        stats->SetExperience(0);
    }
    
    // Add physics component to make it responsive to forces
    // (assumes PhysicsComponent exists)
    // auto physics = player->AddComponent<PhysicsComponent>();
    
    // Mark as player
    player->AddComponent<PlayerMarkerComponent>();

    // ------------------------------------------------------------------
    // Camera children. Created in hierarchy order:
    //   1. FirstPersonCamera  (default — used when play mode starts)
    //   2. ThirdPersonCamera
    // The hierarchy panel orders children by scene-creation order, so
    // creating FP first puts it above TP in the hierarchy.
    //
    // Player visual scale is (0.5, 1.8, 0.5); local offsets are picked so
    // the world-space placement makes sense without play-time overrides.
    // ------------------------------------------------------------------
    auto first_person = scene->CreateEntity("FirstPersonCamera");
    if (first_person) {
        first_person->SetParent(player);
        if (auto t = first_person->GetTransform()) {
            t->SetLocalPosition(glm::vec3(0.0f, 0.45f, 0.0f));
            t->SetLocalScale(glm::vec3(1.0f));
        }
    }

    auto third_person = scene->CreateEntity("ThirdPersonCamera");
    if (third_person) {
        third_person->SetParent(player);
        if (auto t = third_person->GetTransform()) {
            t->SetLocalPosition(glm::vec3(0.0f, 0.7f, 6.0f));
            t->SetLocalScale(glm::vec3(1.0f));
        }
    }

    // Emit signal that player was created
    spdlog::info("Player entity '{}' created at position ({}, {}, {}) with FirstPerson + ThirdPerson cameras",
                 name, position.x, position.y, position.z);

    return player;
}

std::shared_ptr<Entity> PlayerEntity::FindPlayerInScene(Scene* scene) {
    if (!scene) return nullptr;
    
    // Get all entities and find one with PlayerMarkerComponent
    auto entities = scene->GetAllEntities();
    for (const auto& entity : entities) {
        if (entity && entity->HasComponent<PlayerMarkerComponent>()) {
            return entity;
        }
    }
    
    return nullptr;
}

bool PlayerEntity::IsPlayerEntity(Entity* entity) {
    if (!entity) return false;
    return entity->HasComponent<PlayerMarkerComponent>();
}

} // namespace schizo::character
