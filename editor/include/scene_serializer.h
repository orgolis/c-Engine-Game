#pragma once

#include "scene.h"
#include "entity.h"
#include <string>
#include <memory>

namespace schizo::editor {

// ============================================================================
// Scene Serialization
// ============================================================================

/**
 * @class SceneSerializer
 * @brief Handles loading and saving scenes to/from JSON format
 */
class SceneSerializer {
public:
    /**
     * @brief Save scene to JSON file
     * @param filepath Path to save the file
     * @param scene Scene to save
     * @return True if successful, false otherwise
     */
    static bool SaveScene(const std::string& filepath, const std::shared_ptr<schizo::scene::Scene>& scene);
    
    /**
     * @brief Load scene from JSON file
     * @param filepath Path to load from
     * @return Loaded scene, or nullptr if failed
     */
    static std::shared_ptr<schizo::scene::Scene> LoadScene(const std::string& filepath);

    // ------------------------------------------------------------------
    // Prefabs
    //
    // A prefab is a SCENE FILE holding one subtree. It reuses the scene
    // format, the same entity writer and the same entity reader, so a
    // component gaining a field reaches prefabs with no extra work -- and,
    // more to the point, cannot reach one and not the other.
    //
    // The only differences from a scene file are the header key and one rule
    // in the writer: PARENT_ID is emitted only for parents inside the subtree,
    // which is what detaches the prefab's root from whatever it was parented
    // to in the scene it was dragged out of.
    // ------------------------------------------------------------------

    /**
     * @brief Write an entity and ALL its descendants to a .prefab file.
     * @param filepath Destination path
     * @param root Entity to capture; its children are captured with it
     * @return True if written
     */
    static bool SavePrefab(const std::string& filepath,
                           const std::shared_ptr<schizo::scene::Entity>& root);

    /**
     * @brief Instantiate a .prefab into an existing scene.
     * @param filepath Prefab to read
     * @param scene Scene to add the entities to
     * @return The new root entity, or nullptr on failure
     *
     * Entities get FRESH ids: the ids in the file are used only to rebuild the
     * parent links between them, so instantiating the same prefab twice into
     * one scene does not produce two entities claiming the same id.
     */
    static std::shared_ptr<schizo::scene::Entity> LoadPrefab(
        const std::string& filepath,
        const std::shared_ptr<schizo::scene::Scene>& scene);
    
private:
    SceneSerializer() = default;
};

} // namespace schizo::editor
