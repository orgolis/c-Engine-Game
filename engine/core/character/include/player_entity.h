#pragma once

#include <memory>
#include <string>
#include <glm/glm.hpp>

namespace schizo::scene {
    class Entity;
    class Scene;
}

namespace schizo::renderer {
    class Mesh;
    class Material;
}

namespace schizo::character {

/**
 * @class PlayerEntity
 * @brief Factory for creating fully-featured player entities
 * 
 * Creates a player entity with:
 * - Transform, RigidBody (physics)
 * - Mesh renderer with material
 * - Character controller
 * - Health/stats
 * - Input handling capability
 */
class PlayerEntity {
public:
    /**
     * Create a player entity in the scene
     * @param scene Scene to spawn player in
     * @param name Entity name
     * @param position Spawn position
     * @return Shared pointer to created player entity
     */
    static std::shared_ptr<schizo::scene::Entity> Create(
        schizo::scene::Scene* scene,
        const std::string& name = "Player",
        const glm::vec3& position = glm::vec3(0.0f, 1.0f, 0.0f)
    );
    
    /**
     * Find the player entity in a scene (returns first entity tagged as player)
     */
    static std::shared_ptr<schizo::scene::Entity> FindPlayerInScene(schizo::scene::Scene* scene);
    
    /**
     * Check if an entity is a player entity
     */
    static bool IsPlayerEntity(schizo::scene::Entity* entity);
};

} // namespace schizo::character
