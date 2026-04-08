#include "entity_factory.h"
#include "scene.h"
#include "mesh_renderer_component.h"

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
    
    // Set position
    if (entity) {
        entity->GetTransform()->SetLocalPosition(position);
        entity->SetTag("Player");
    }
    
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

} // namespace schizo::scene
