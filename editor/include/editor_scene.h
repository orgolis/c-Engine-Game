#pragma once

#include <memory>
#include <string>

// Forward declarations
namespace schizo::scene {
    class Scene;
}

namespace schizo::editor {

class EcsSceneBridge;

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

    /// Force the flag, used to RESTORE it rather than to set it.
    ///
    /// The inspector calls MarkModified() from something over a hundred places.
    /// When it is editing a PREFAB rather than the scene, every one of those
    /// would raise a false "unsaved scene" prompt for a file the scene does not
    /// contain. Snapshotting the flag and putting it back afterwards neutralises
    /// all of them at one point, which beats auditing a hundred call sites for a
    /// condition none of them can see.
    void SetUnsavedChanges(bool v) { has_unsaved_changes_ = v; }

    // Wire the ECS bridge so save/load also persist authoritative gameplay
    // components to a `.gameplay` sidecar (F3). Optional — null = OOP-only save.
    void SetEcsBridge(EcsSceneBridge* b) { ecs_bridge_ = b; }

private:
    std::shared_ptr<schizo::scene::Scene> scene_;
    std::string scene_filepath_;
    bool has_unsaved_changes_ = false;
    EcsSceneBridge* ecs_bridge_ = nullptr;
};

} // namespace schizo::editor
