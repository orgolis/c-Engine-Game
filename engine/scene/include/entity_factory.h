#pragma once

#include "entity.h"
#include "mesh_renderer_component.h"
#include <memory>
#include <glm/glm.hpp>

namespace schizo::scene {

/**
 * @class EntityFactory
 * @brief Factory for creating prebuild entity types
 */
class EntityFactory {
public:
    EntityFactory() = delete;
    
    // ========== Primitive Mesh Entities ==========
    
    /**
     * @brief Create a cube entity
     * @param scene Target scene
     * @param name Entity name
     * @param size Scale of the cube
     * @param color Color of the cube
     * @return Shared pointer to created entity
     */
    static std::shared_ptr<Entity> CreateCube(
        std::shared_ptr<Scene> scene,
        const std::string& name = "Cube",
        float size = 1.0f,
        const glm::vec4& color = glm::vec4(0.5f, 0.8f, 1.0f, 1.0f)
    );
    
    /**
     * @brief Create a sphere entity
     * @param scene Target scene
     * @param name Entity name
     * @param radius Radius of the sphere
     * @param color Color of the sphere
     * @return Shared pointer to created entity
     */
    static std::shared_ptr<Entity> CreateSphere(
        std::shared_ptr<Scene> scene,
        const std::string& name = "Sphere",
        float radius = 0.5f,
        const glm::vec4& color = glm::vec4(1.0f, 0.5f, 0.5f, 1.0f)
    );
    
    /**
     * @brief Create a plane entity
     * @param scene Target scene
     * @param name Entity name
     * @param width Width of the plane
     * @param height Height of the plane
     * @param color Color of the plane
     * @return Shared pointer to created entity
     */
    static std::shared_ptr<Entity> CreatePlane(
        std::shared_ptr<Scene> scene,
        const std::string& name = "Plane",
        float width = 10.0f,
        float height = 10.0f,
        const glm::vec4& color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f)
    );
    
    /**
     * @brief Create a capsule entity
     * @param scene Target scene
     * @param name Entity name
     * @param radius Radius of the capsule
     * @param height Height of the capsule
     * @param color Color of the capsule
     * @return Shared pointer to created entity
     */
    static std::shared_ptr<Entity> CreateCapsule(
        std::shared_ptr<Scene> scene,
        const std::string& name = "Capsule",
        float radius = 0.5f,
        float height = 2.0f,
        const glm::vec4& color = glm::vec4(1.0f, 1.0f, 0.5f, 1.0f)
    );
    
    /**
     * @brief Create a cylinder entity
     * @param scene Target scene
     * @param name Entity name
     * @param radius Radius of the cylinder
     * @param height Height of the cylinder
     * @param color Color of the cylinder
     * @return Shared pointer to created entity
     */
    static std::shared_ptr<Entity> CreateCylinder(
        std::shared_ptr<Scene> scene,
        const std::string& name = "Cylinder",
        float radius = 0.5f,
        float height = 2.0f,
        const glm::vec4& color = glm::vec4(0.5f, 1.0f, 0.5f, 1.0f)
    );
    
    // ========== Special Entities ==========
    
    /**
     * @brief Create a player entity with default settings
     * @param scene Target scene
     * @param name Entity name
     * @param position Starting position
     * @return Shared pointer to created entity
     */
    static std::shared_ptr<Entity> CreatePlayer(
        std::shared_ptr<Scene> scene,
        const std::string& name = "Player",
        const glm::vec3& position = glm::vec3(0.0f, 1.0f, 0.0f)
    );
    
    /**
     * @brief Create a camera entity
     * @param scene Target scene
     * @param name Entity name
     * @param position Starting position
     * @param look_at Point to look at
     * @return Shared pointer to created entity
     */
    static std::shared_ptr<Entity> CreateCamera(
        std::shared_ptr<Scene> scene,
        const std::string& name = "Camera",
        const glm::vec3& position = glm::vec3(0.0f, 2.0f, 5.0f),
        const glm::vec3& look_at = glm::vec3(0.0f, 0.0f, 0.0f)
    );
    
    // ========== Light Entities ==========
    
    /**
     * @brief Create a directional light entity
     * @param scene Target scene
     * @param name Entity name
     * @param direction Light direction
     * @param color Light color
     * @param intensity Light intensity
     * @return Shared pointer to created entity
     */
    static std::shared_ptr<Entity> CreateDirectionalLight(
        std::shared_ptr<Scene> scene,
        const std::string& name = "DirectionalLight",
        const glm::vec3& direction = glm::vec3(-0.3f, -1.0f, -0.3f),
        const glm::vec3& color = glm::vec3(1.0f, 0.95f, 0.8f),
        float intensity = 1.0f
    );
    
    /**
     * @brief Create a global/ambient light entity
     * @param scene Target scene
     * @param name Entity name
     * @param color Ambient light color
     * @param intensity Light intensity
     * @return Shared pointer to created entity
     */
    static std::shared_ptr<Entity> CreateGlobalLight(
        std::shared_ptr<Scene> scene,
        const std::string& name = "GlobalLight",
        const glm::vec3& color = glm::vec3(0.5f, 0.5f, 0.6f),
        float intensity = 0.5f
    );
};

} // namespace schizo::scene
