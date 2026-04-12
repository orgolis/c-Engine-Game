#include "scene_playback_manager.h"
#include "../core/character/include/character_controller.h"
#include <spdlog/spdlog.h>

namespace schizo::editor {

ScenePlaybackManager::ScenePlaybackManager() = default;

ScenePlaybackManager::~ScenePlaybackManager() {
    StopPlayback();
}

bool ScenePlaybackManager::StartPlayback(std::shared_ptr<schizo::scene::Scene> scene) {
    if (is_playing_) {
        auto logger = spdlog::get("editor");
        if (logger) logger->warn("Scene already playing");
        return false;
    }
    
    scene_ = scene;
    
    // Find and setup player entity
    if (!FindAndSetupPlayer()) {
        auto logger = spdlog::get("editor");
        if (logger) logger->error("Failed to find player entity in scene");
        return false;
    }
    
    // Attach character controller
    if (!AttachCharacterController()) {
        auto logger = spdlog::get("editor");
        if (logger) logger->error("Failed to attach character controller");
        return false;
    }
    
    is_playing_ = true;
    is_paused_ = false;
    playback_time_ = 0.0f;
    
    auto logger = spdlog::get("editor");
    if (logger) logger->info("Scene playback started");
    
    return true;
}

void ScenePlaybackManager::StopPlayback() {
    is_playing_ = false;
    is_paused_ = false;
    playback_time_ = 0.0f;
    player_entity_ = nullptr;
    player_controller_ = nullptr;
    scene_ = nullptr;
    
    auto logger = spdlog::get("editor");
    if (logger) logger->info("Scene playback stopped");
}

void ScenePlaybackManager::Update(float delta_time) {
    if (!is_playing_ || is_paused_ || !player_controller_) {
        return;
    }
    
    playback_time_ += delta_time;
    
    // Update character controller
    player_controller_->Update(delta_time);
    
    // Update camera
    UpdateCamera();
}

bool ScenePlaybackManager::FindAndSetupPlayer() {
    if (!scene_) return false;
    
    // Try to find entity named "Player"
    player_entity_ = scene_->GetEntityByName("Player");
    
    if (!player_entity_) {
        auto logger = spdlog::get("editor");
        if (logger) logger->warn("No entity named 'Player' found in scene");
        return false;
    }
    
    auto logger = spdlog::get("editor");
    if (logger) logger->info("Player entity found: {}", player_entity_->GetName());
    
    return true;
}

bool ScenePlaybackManager::AttachCharacterController() {
    if (!player_entity_ || !scene_) return false;
    
    try {
        // Create and attach CharacterController component to player entity
        // The CharacterController constructor will handle initialization with default stats
        player_controller_ = player_entity_->AddComponent<engine::character::CharacterController>();
        
        if (!player_controller_) {
            auto logger = spdlog::get("editor");
            if (logger) logger->error("Failed to create CharacterController component");
            return false;
        }
        
        auto logger = spdlog::get("editor");
        if (logger) logger->info("CharacterController attached to player entity");
        
        return true;
    } catch (const std::exception& e) {
        auto logger = spdlog::get("editor");
        if (logger) logger->error("Exception when attaching CharacterController: {}", e.what());
        return false;
    }
}

void ScenePlaybackManager::UpdateCamera() {
    if (!player_entity_) return;
    
    try {
        // Try to find Camera component on player entity's children
        auto children = player_entity_->GetChildren();
        for (const auto& child : children) {
            if (child && child->GetName() == "Camera") {
                auto child_transform = child->GetTransform();
                if (child_transform) {
                    // Camera is child of player - it will follow automatically via hierarchy
                    // Just ensure camera is positioned correctly relative to player
                    auto player_pos = player_entity_->GetTransform()->GetWorldPosition();
                    
                    // Update camera to offset from player (optional, depending on desired behavior)
                    // For now, just let hierarchy handle it
                    
                    auto logger = spdlog::get("editor");
                    if (logger) logger->debug("Camera following player at position: ({:.2f}, {:.2f}, {:.2f})",
                                           player_pos.x, player_pos.y, player_pos.z);
                    return;
                }
            }
        }
        
        // If no camera found on player, look for main camera in scene
        auto all_entities = scene_->GetEntities();
        for (const auto& entity : all_entities) {
            if (entity && entity->GetName() == "MainCamera") {
                auto camera_transform = entity->GetTransform();
                if (camera_transform) {
                    auto player_pos = player_entity_->GetTransform()->GetWorldPosition();
                    
                    // Position camera to follow player (e.g., offset from player)
                    glm::vec3 camera_offset(0.0f, 2.0f, 5.0f);  // Default offset
                    camera_transform->SetWorldPosition(player_pos + camera_offset);
                    
                    // Look at player
                    camera_transform->LookAt(player_pos);
                    
                    auto logger = spdlog::get("editor");
                    if (logger) logger->debug("MainCamera updated to follow player");
                    return;
                }
            }
        }
        
    } catch (const std::exception& e) {
        auto logger = spdlog::get("editor");
        if (logger) logger->warn("Exception in UpdateCamera: {}", e.what());
    }
}

void ScenePlaybackManager::UpdateCharacterMovement(float delta_time) {
    if (!player_controller_) return;
    
    // Character controller is updated in Update()
    // This is called automatically
}

} // namespace schizo::editor
