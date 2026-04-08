#pragma once

#include "scene.h"
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
    
private:
    SceneSerializer() = default;
};

} // namespace schizo::editor
