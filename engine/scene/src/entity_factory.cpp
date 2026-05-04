#include "entity_factory.h"
#include "scene.h"
#include "mesh_renderer_component.h"
#include "light_component.h"
#include <spdlog/spdlog.h>

namespace schizo::scene {

std::shared_ptr<Entity> EntityFactory::CreateCube(
    std::shared_ptr<Scene> scene,
    const std::string& name,
    float size,
    const glm::vec4& color) {
    
    if (!scene) return nullptr;
    
    auto entity = scene->CreateEntity(name);
    
    // Set transform
    entity->GetTransform()->SetLocalScale(glm::vec3(size));
    
    // Add mesh renderer component
    entity->AddComponent<MeshRendererComponent>(MeshType::Cube, color);
    
    return entity;
}

std::shared_ptr<Entity> EntityFactory::CreateSphere(
    std::shared_ptr<Scene> scene,
    const std::string& name,
    float radius,
    const glm::vec4& color) {
    
    if (!scene) return nullptr;
    
    auto entity = scene->CreateEntity(name);
    
    // Set transform
    entity->GetTransform()->SetLocalScale(glm::vec3(radius * 2.0f));
    
    // Add mesh renderer component
    entity->AddComponent<MeshRendererComponent>(MeshType::Sphere, color);
    
    return entity;
}

std::shared_ptr<Entity> EntityFactory::CreatePlane(
    std::shared_ptr<Scene> scene,
    const std::string& name,
    float width,
    float height,
    const glm::vec4& color) {
    
    if (!scene) return nullptr;
    
    auto entity = scene->CreateEntity(name);
    
    // Set transform
    entity->GetTransform()->SetLocalScale(glm::vec3(width, 1.0f, height));
    
    // Add mesh renderer component
    entity->AddComponent<MeshRendererComponent>(MeshType::Plane, color);
    
    return entity;
}

std::shared_ptr<Entity> EntityFactory::CreateCapsule(
    std::shared_ptr<Scene> scene,
    const std::string& name,
    float radius,
    float height,
    const glm::vec4& color) {
    
    if (!scene) return nullptr;
    
    auto entity = scene->CreateEntity(name);
    
    // Set transform (scale is applied differently for capsule)
    entity->GetTransform()->SetLocalScale(glm::vec3(radius * 2.0f, height, radius * 2.0f));
    
    // Add mesh renderer component
    entity->AddComponent<MeshRendererComponent>(MeshType::Capsule, color);
    
    return entity;
}

std::shared_ptr<Entity> EntityFactory::CreateCylinder(
    std::shared_ptr<Scene> scene,
    const std::string& name,
    float radius,
    float height,
    const glm::vec4& color) {
    
    if (!scene) return nullptr;
    
    auto entity = scene->CreateEntity(name);
    
    // Set transform
    entity->GetTransform()->SetLocalScale(glm::vec3(radius * 2.0f, height, radius * 2.0f));
    
    // Add mesh renderer component
    entity->AddComponent<MeshRendererComponent>(MeshType::Cylinder, color);
    
    return entity;
}

std::shared_ptr<Entity> EntityFactory::CreatePlayer(
    std::shared_ptr<Scene> scene,
    const std::string& name,
    const glm::vec3& position) {

    if (!scene) return nullptr;

    // Create player as a capsule-shaped entity
    auto entity = CreateCapsule(scene, name, 0.4f, 1.8f, glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (!entity) return entity;

    entity->GetTransform()->SetLocalPosition(position);
    entity->SetTag("Player");

    // ------------------------------------------------------------------
    // Camera children created up-front (NOT lazily on play). Order matters:
    // the hierarchy panel orders children by scene-creation order, so
    // FirstPersonCamera is created first and therefore listed above
    // ThirdPersonCamera. ScenePlaybackManager picks FirstPersonCamera as
    // the default playback view.
    // ------------------------------------------------------------------
    int created = 0;
    if (auto first_person = scene->CreateEntity("FirstPersonCamera")) {
        first_person->SetParent(entity);
        if (auto t = first_person->GetTransform()) {
            t->SetLocalPosition(glm::vec3(0.0f, 0.45f, 0.0f));
            t->SetLocalScale(glm::vec3(1.0f));
        }
        first_person->SetTag("Camera");
        ++created;
    }

    if (auto third_person = scene->CreateEntity("ThirdPersonCamera")) {
        third_person->SetParent(entity);
        if (auto t = third_person->GetTransform()) {
            t->SetLocalPosition(glm::vec3(0.0f, 0.7f, 6.0f));
            t->SetLocalScale(glm::vec3(1.0f));
        }
        third_person->SetTag("Camera");
        ++created;
    }

    spdlog::info("CreatePlayer: spawned {} camera children (FirstPersonCamera, ThirdPersonCamera)", created);
    return entity;
}

std::shared_ptr<Entity> EntityFactory::CreateCamera(
    std::shared_ptr<Scene> scene,
    const std::string& name,
    const glm::vec3& position,
    const glm::vec3& look_at) {
    
    if (!scene) return nullptr;
    
    // Create camera as a small cube for visualization
    auto entity = CreateCube(scene, name, 0.3f, glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
    
    if (entity) {
        entity->GetTransform()->SetLocalPosition(position);
        
        // Use LookAt to orient the camera
        entity->GetTransform()->LookAt(look_at);
        
        entity->SetTag("Camera");
    }
    
    return entity;
}

std::shared_ptr<Entity> EntityFactory::CreateDirectionalLight(
    std::shared_ptr<Scene> scene,
    const std::string& name,
    const glm::vec3& direction,
    const glm::vec3& color,
    float intensity) {
    
    if (!scene) return nullptr;
    
    // Create empty entity for the light
    auto entity = scene->CreateEntity(name);
    
    if (entity) {
        // Orient the entity toward the light direction
        entity->GetTransform()->LookAt(entity->GetTransform()->GetWorldPosition() + direction);
        
        // Add directional light component
        entity->AddComponent<DirectionalLightComponent>(color, intensity, true);
        
        // Set tag for identification
        entity->SetTag("DirectionalLight");
    }
    
    return entity;
}

std::shared_ptr<Entity> EntityFactory::CreateGlobalLight(
    std::shared_ptr<Scene> scene,
    const std::string& name,
    const glm::vec3& color,
    float intensity) {
    
    if (!scene) return nullptr;
    
    // Create empty entity for the light
    auto entity = scene->CreateEntity(name);
    
    if (entity) {
        // Add global light component
        entity->AddComponent<GlobalLightComponent>(color, intensity);
        
        // Set tag for identification
        entity->SetTag("GlobalLight");
    }
    
    return entity;
}

} // namespace schizo::scene
