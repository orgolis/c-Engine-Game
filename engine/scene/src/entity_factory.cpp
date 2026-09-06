#include "entity_factory.h"
#include "scene.h"
#include "mesh_renderer_component.h"
#include "light_component.h"
#include "collider_component.h"
#include "camera_component.h"
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

    // Default Box collider sized to the unit cube — BuildPhysicsWorld
    // multiplies the half-extents by the entity's world scale, so this
    // matches the visual regardless of `size`. Static by default; flip
    // `Dynamic` in the inspector to have it fall under gravity.
    entity->AddComponent<ColliderComponent>(ColliderShape::Box);

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

    // Default Sphere collider — unit radius 0.5 scaled by transform.
    entity->AddComponent<ColliderComponent>(ColliderShape::Sphere);

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

    // Default thin Box collider rather than an infinite Plane — a Plane
    // mesh in the scene is typically meant as bounded floor geometry, not
    // an infinite halfspace. Box he.y=0.05 gives a 10 cm thick slab after
    // the transform's y=1 scale. Users can switch to a true Plane Collider
    // in the inspector if they want infinite ground.
    if (auto col = entity->AddComponent<ColliderComponent>(ColliderShape::Box)) {
        col->SetHalfExtents(glm::vec3(0.5f, 0.05f, 0.5f));
    }

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

    // Default Capsule collider. With the transform scale above, world
    // capsule radius = 0.5*radius*2 = radius and half-height = 0.5*height,
    // so total span = height + 2*radius. That's slightly taller than the
    // visual because the visual mesh's hemispheres are baked into the
    // height parameter; close enough for a default, tweak in inspector.
    entity->AddComponent<ColliderComponent>(ColliderShape::Capsule);

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

    // Native Cylinder collider — flat caps. Sphere/Plane tests are exact;
    // Box / Capsule / Cylinder pairs reduce to a bounding capsule (slight
    // imprecision at cap-vs-face contact only).
    entity->AddComponent<ColliderComponent>(ColliderShape::Cylinder);

    return entity;
}

std::shared_ptr<Entity> EntityFactory::CreatePlayer(
    std::shared_ptr<Scene> scene,
    const std::string& name,
    const glm::vec3& position) {

    if (!scene) return nullptr;

    // Player root keeps a uniform (1,1,1) scale. A non-uniform scale on
    // the parent (e.g. 0.8 x 1.8 x 0.8 from the visual capsule) sits
    // between the player's yaw and the camera child's pitch in the world
    // matrix, which makes Transform::GetWorldRotation extract a skewed
    // (non-orthogonal) basis from the camera child and breaks vertical
    // mouse look in first-person play mode.
    auto entity = scene->CreateEntity(name);
    if (!entity) return nullptr;

    entity->GetTransform()->SetLocalPosition(position);
    entity->SetTag("Player");

    // Visual capsule on a child so its non-uniform scale never reaches
    // the camera children. The numbers match the old CreateCapsule call
    // (radius 0.4, height 1.8 -> scale 0.8 x 1.8 x 0.8).
    if (auto visual = scene->CreateEntity("PlayerMesh")) {
        visual->SetParent(entity);
        if (auto t = visual->GetTransform()) {
            t->SetLocalScale(glm::vec3(0.8f, 1.8f, 0.8f));
        }
        visual->AddComponent<MeshRendererComponent>(
            MeshType::Capsule, glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));
    }

    // Capsule collider matching the visual mesh. ScenePlaybackManager forces
    // this body to Kinematic regardless of the is_dynamic flag, so the
    // player is pushed out of Static obstacles but doesn't yield to Dynamic
    // ones. Numbers: radius 0.4 matches PlayerMesh.x scale * 0.5, cylinder
    // span 1.0 + 2*0.4 = 1.8 total height matches PlayerMesh.y scale.
    if (auto col = entity->AddComponent<ColliderComponent>(ColliderShape::Capsule)) {
        col->SetRadius(0.4f);
        col->SetHeight(1.0f);
        col->SetOffset(glm::vec3(0.0f));
        col->SetDynamic(false);
    }

    // ------------------------------------------------------------------
    // Camera children created up-front (NOT lazily on play). Order matters:
    // the hierarchy panel orders children by scene-creation order, so
    // FirstPersonCamera is created first and therefore listed above
    // ThirdPersonCamera. ScenePlaybackManager picks FirstPersonCamera as
    // the default playback view.
    //
    // Local positions are in world units now that the player root scale
    // is identity: 0.81 = previous (0.45 * 1.8), 1.26 = (0.7 * 1.8),
    // 4.8 = (6.0 * 0.8). These keep the camera in the same world spot
    // as before the visual-mesh refactor.
    // ------------------------------------------------------------------
    int created = 0;
    if (auto first_person = scene->CreateEntity("FirstPersonCamera")) {
        first_person->SetParent(entity);
        if (auto t = first_person->GetTransform()) {
            t->SetLocalPosition(glm::vec3(0.0f, 0.81f, 0.0f));
            t->SetLocalScale(glm::vec3(1.0f));
        }
        first_person->AddComponent<CameraComponent>();
        first_person->SetTag("Camera");
        ++created;
    }

    if (auto third_person = scene->CreateEntity("ThirdPersonCamera")) {
        third_person->SetParent(entity);
        if (auto t = third_person->GetTransform()) {
            t->SetLocalPosition(glm::vec3(0.0f, 1.26f, 4.8f));
            t->SetLocalScale(glm::vec3(1.0f));
        }
        third_person->AddComponent<CameraComponent>();
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

        // A camera factory should create an actual camera component, not just
        // a yellow helper mesh with a "Camera" tag.
        entity->AddComponent<CameraComponent>();
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
