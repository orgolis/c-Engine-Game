#pragma once

#include <memory>
#include <string>

// Forward declarations
namespace schizo::scene {
    class Scene;
}

namespace schizo::editor {

/**
 * @class EditorScene
 * @brief Manages the current scene being edited
 */
class EditorScene {
public:
    EditorScene();
    ~EditorScene() = default;
    
    // Delete copy, allow move
    EditorScene(const EditorScene&) = delete;
    EditorScene& operator=(const EditorScene&) = delete;
    EditorScene(EditorScene&&) = default;
    EditorScene& operator=(EditorScene&&) = default;
    
    /**
     * @brief Get the current scene
     */
    std::shared_ptr<schizo::scene::Scene> GetScene() const { return scene_; }
    
    /**
     * @brief Create new scene
     */
    void NewScene(const std::string& name = "Untitled");
    
    /**
     * @brief Save current scene to file
     */
    bool SaveScene(const std::string& filepath);
    
    /**
     * @brief Load scene from file
     */
    bool LoadScene(const std::string& filepath);
    
    /**
     * @brief Get current scene filename
     */
    const std::string& GetSceneFilepath() const { return scene_filepath_; }
    
    /**
     * @brief Check if scene has unsaved changes
     */
    bool HasUnsavedChanges() const { return has_unsaved_changes_; }
    
    /**
     * @brief Mark scene as modified
     */
    void MarkModified() { has_unsaved_changes_ = true; }
    
private:
    std::shared_ptr<schizo::scene::Scene> scene_;
    std::string scene_filepath_;
    bool has_unsaved_changes_ = false;
};

} // namespace schizo::editor
