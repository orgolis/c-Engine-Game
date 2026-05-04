#include "editor_scene.h"
#include "scene_serializer.h"
#include "scene.h"
#include <spdlog/spdlog.h>

namespace schizo::editor {

EditorScene::EditorScene() {
    NewScene("Default Scene");
}

void EditorScene::NewScene(const std::string& name) {
    schizo::scene::SceneConfig config;
    config.name = name;
    scene_ = schizo::scene::Scene::Create(config);
    scene_filepath_.clear();
    has_unsaved_changes_ = false;
    
    spdlog::info("Created new scene: {}", name);
}

bool EditorScene::SaveScene(const std::string& filepath) {
    if (!scene_) {
        spdlog::error("Cannot save: no scene loaded");
        return false;
    }
    
    if (SceneSerializer::SaveScene(filepath, scene_)) {
        scene_filepath_ = filepath;
        has_unsaved_changes_ = false;
        return true;
    }
    return false;
}

bool EditorScene::LoadScene(const std::string& filepath) {
    auto loaded_scene = SceneSerializer::LoadScene(filepath);
    if (loaded_scene) {
        scene_ = loaded_scene;
        scene_filepath_ = filepath;
        has_unsaved_changes_ = false;
        return true;
    }
    return false;
}

} // namespace schizo::editor
