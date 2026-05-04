#include "scene_playback_manager.h"
#include "../core/character/include/character_controller.h"
#include "../../engine/scene/include/scene.h"
#include "../../engine/scene/include/entity.h"
#include "../core/character/include/player_entity.h"
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

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
    
    // Setup playback camera
    SetupPlaybackCamera();
    
    is_playing_ = true;
    is_paused_ = false;
    is_cursor_captured_ = true;  // Host will hide+lock the OS cursor
    mouse_delta_x_ = 0.0f;
    mouse_delta_y_ = 0.0f;
    playback_time_ = 0.0f;
    player_velocity_ = glm::vec3(0.0f);
    is_on_ground_ = false;
    
    // Initialize camera position from player
    if (player_entity_) {
        auto player_pos = player_entity_->GetTransform()->GetWorldPosition();
        camera_position_ = player_pos + glm::vec3(0.0f, camera_height_, camera_distance_);
        camera_target_position_ = camera_position_;
    }
    
    auto logger = spdlog::get("editor");
    if (logger) logger->info("Scene playback started with camera: {}", 
                             playback_camera_ ? playback_camera_->GetName() : "none");
    
    return true;
}

void ScenePlaybackManager::StopPlayback() {
    is_playing_ = false;
    is_paused_ = false;
    is_cursor_captured_ = false;  // Host will restore the OS cursor
    mouse_delta_x_ = 0.0f;
    mouse_delta_y_ = 0.0f;
    playback_time_ = 0.0f;
    player_velocity_ = glm::vec3(0.0f);
    is_on_ground_ = false;
    player_entity_ = nullptr;
    playback_camera_ = nullptr;
    player_controller_ = nullptr;
    scene_ = nullptr;
    
    auto logger = spdlog::get("editor");
    if (logger) logger->info("Scene playback stopped");
}

void ScenePlaybackManager::Update(float delta_time) {
    if (!is_playing_ || is_paused_ || !player_entity_) {
        return;
    }
    
    playback_time_ += delta_time;
    
    // Update gravity and movement, then apply all velocity at once
    ApplyGravity(delta_time);  // Updates vertical velocity
    UpdateCharacterMovement(delta_time);  // Updates horizontal velocity
    ApplyVelocityToPosition(delta_time);  // Apply combined velocity to position
    
    // Update camera to follow player
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
        // Create a CharacterController for controlling the player
        // Note: This is a local instance managed by the playback manager
        // The CharacterController constructor takes an entt::entity, but we don't expose that
        // For now, just note that we found the player - actual control happens in UpdateCharacterMovement
        
        auto logger = spdlog::get("editor");
        if (logger) logger->info("Player entity is ready for control");
        
        return true;
    } catch (const std::exception& e) {
        auto logger = spdlog::get("editor");
        if (logger) logger->error("Exception when preparing player control: {}", e.what());
        return false;
    }
}

void ScenePlaybackManager::UpdateCamera() {
    if (!player_entity_ || !playback_camera_) return;

    auto player_transform = player_entity_->GetTransform();
    auto camera_transform = playback_camera_->GetTransform();
    if (!player_transform || !camera_transform) return;

    // First-person camera is parented to the player and rides at a fixed local
    // offset (set by PlayerEntity::Create). The transform hierarchy already
    // places it correctly — no per-frame override needed.
    if (playback_camera_->GetName() == "FirstPersonCamera") {
        camera_position_ = camera_transform->GetWorldPosition();
        return;
    }

    // Third-person and legacy cameras: orbit behind the player using the
    // configured distance/height, with smoothing.
    glm::vec3 player_pos = player_transform->GetWorldPosition();
    glm::vec3 player_forward = player_transform->GetForward();
    glm::vec3 backward = -player_forward;
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 camera_target = player_pos + (backward * camera_distance_) + (up * camera_height_);

    camera_position_ = glm::mix(camera_position_, camera_target, camera_smoothing_);
    camera_transform->SetWorldPosition(camera_position_);

    // Aim the camera at the player so the viewport's lookAt math (which uses
    // the camera's world rotation) renders the player in front of the lens.
    glm::vec3 to_player = player_pos + glm::vec3(0.0f, 1.0f, 0.0f) - camera_position_;
    if (glm::length(to_player) > 0.0001f) {
        glm::vec3 fwd = glm::normalize(to_player);
        // Build a rotation whose -Z (camera forward) points toward the player.
        glm::quat look = glm::quatLookAt(fwd, up);
        camera_transform->SetWorldRotation(look);
    }
}

void ScenePlaybackManager::UpdateCharacterMovement(float delta_time) {
    if (!player_entity_ || !scene_) return;
    
    // Get ImGui IO for input
    ImGuiIO& io = ImGui::GetIO();
    
    // Get player transform
    auto player_transform = player_entity_->GetTransform();
    if (!player_transform) return;
    
    // ========== MOUSE LOOK / CAMERA ROTATION ==========
    // While the cursor is captured the host (main.cpp) feeds raw GLFW deltas
    // through OnMouseDelta() — ImGui's NoMouse flag zeros io.MousePos so we
    // cannot read it here. Yaw is applied unconditionally; pitch could be
    // hooked onto an attached camera child later.
    if (is_cursor_captured_ && std::abs(mouse_delta_x_) > 0.0f) {
        const float MOUSE_SENSITIVITY = 0.0025f;  // radians per pixel
        float yaw_angle = -mouse_delta_x_ * MOUSE_SENSITIVITY;
        player_transform->Rotate(glm::vec3(0.0f, 1.0f, 0.0f), yaw_angle);
    }
    mouse_delta_x_ = 0.0f;
    mouse_delta_y_ = 0.0f;
    
    // Create input action from keyboard
    struct InputAction {
        float forward = 0.0f;   // W/S: +1 forward, -1 backward
        float lateral = 0.0f;   // A/D: +1 right, -1 left
        bool jump = false;      // Space
        bool sprint = false;    // Shift
    } input;
    
    // WASD movement mapping (standard FPS controls)
    if (ImGui::IsKeyDown(ImGuiKey_W)) input.forward += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_S)) input.forward -= 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_D)) input.lateral += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_A)) input.lateral -= 1.0f;
    
    // Sprint with Shift
    if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
        input.sprint = true;
    }
    
    // Jump with Space
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        input.jump = true;
    }
    
    // Normalize movement direction
    glm::vec2 movement(input.lateral, input.forward);
    float movement_length = glm::length(movement);
    if (movement_length > 0.0f) {
        movement = glm::normalize(movement);
    }
    
    // Get player's current orientation for movement
    glm::vec3 player_forward = player_transform->GetForward();
    glm::vec3 player_right = player_transform->GetRight();
    
    // Calculate movement direction in world space based on player orientation
    // forward input uses player's forward direction
    // lateral input uses player's right direction
    glm::vec3 move_direction = (player_right * movement.x) + (player_forward * movement.y);
    move_direction.y = 0.0f;  // Keep movement horizontal
    if (glm::length(move_direction) > 0.001f) {
        move_direction = glm::normalize(move_direction);
    }
    
    // Apply character movement (horizontal only)
    float speed = input.sprint ? 12.0f : 7.0f;  // m/s
    glm::vec3 horizontal_velocity = move_direction * speed;
    
    // Update horizontal movement only (gravity handles vertical)
    player_velocity_.x = horizontal_velocity.x;
    player_velocity_.z = horizontal_velocity.z;
    
    // Apply jump impulse (only if on ground)
    if (input.jump && is_on_ground_) {
        player_velocity_.y = 15.0f;  // m/s - jump impulse
        is_on_ground_ = false;
        
        auto logger = spdlog::get("editor");
        if (logger) logger->debug("Player jumped!");
    }
    
    auto logger = spdlog::get("editor");
    if (logger && movement_length > 0.0f) {
        logger->info("🔄 Player move: forward={:.2f}, lateral={:.2f}, sprint={}, on_ground={}", 
                     input.forward, input.lateral, input.sprint, is_on_ground_);
    }
}

void ScenePlaybackManager::SetupPlaybackCamera() {
    auto logger = spdlog::get("editor");
    if (!player_entity_ || !scene_) {
        if (logger) logger->error("SetupPlaybackCamera: player or scene is null");
        return;
    }

    // Preference order: FirstPersonCamera → ThirdPersonCamera → any "Camera"/
    // "PlayerCamera" child created by older saves. Fall back to creating a
    // PlayerCamera child as a last resort (legacy scenes).
    auto find_child = [this](const std::string& name) -> std::shared_ptr<schizo::scene::Entity> {
        for (const auto& child : player_entity_->GetChildren()) {
            if (child && child->GetName() == name) return child;
        }
        return nullptr;
    };

    if (auto fp = find_child("FirstPersonCamera")) {
        playback_camera_ = fp;
        if (logger) logger->info("Playback camera: FirstPersonCamera (default)");
        return;
    }
    if (auto tp = find_child("ThirdPersonCamera")) {
        playback_camera_ = tp;
        if (logger) logger->info("Playback camera: ThirdPersonCamera (no FirstPerson child found)");
        return;
    }
    if (auto legacy = find_child("Camera")) { playback_camera_ = legacy; return; }
    if (auto legacy = find_child("PlayerCamera")) { playback_camera_ = legacy; return; }

    // Legacy fallback: spawn a PlayerCamera child so older scenes still work.
    auto camera_entity = scene_->CreateEntity("PlayerCamera");
    if (!camera_entity) return;
    camera_entity->SetParent(player_entity_);
    if (auto t = camera_entity->GetTransform()) {
        t->SetLocalPosition(glm::vec3(0.0f, 0.45f, 0.0f));
    }
    playback_camera_ = camera_entity;
    if (logger) logger->info("Playback camera: created fallback PlayerCamera");
}

void ScenePlaybackManager::SwitchToFirstPerson() {
    if (!player_entity_) return;
    for (const auto& child : player_entity_->GetChildren()) {
        if (child && child->GetName() == "FirstPersonCamera") {
            playback_camera_ = child;
            return;
        }
    }
}

void ScenePlaybackManager::SwitchToThirdPerson() {
    if (!player_entity_) return;
    for (const auto& child : player_entity_->GetChildren()) {
        if (child && child->GetName() == "ThirdPersonCamera") {
            playback_camera_ = child;
            return;
        }
    }
}

void ScenePlaybackManager::ToggleCameraView() {
    if (!playback_camera_) return;
    if (playback_camera_->GetName() == "FirstPersonCamera") {
        SwitchToThirdPerson();
    } else {
        SwitchToFirstPerson();
    }
}

void ScenePlaybackManager::ApplyGravity(float delta_time) {
    if (!player_entity_) return;
    
    const float GRAVITY = 9.8f;  // m/s^2
    
    // Apply gravity to vertical velocity only
    player_velocity_.y -= GRAVITY * delta_time;
    
    // Clamp falling speed to prevent tunneling
    if (player_velocity_.y < -20.0f) {
        player_velocity_.y = -20.0f;
    }
    
    auto logger = spdlog::get("editor");
    if (logger) {
        logger->debug("Gravity applied: velocity.y = {:.2f}", player_velocity_.y);
    }
}

void ScenePlaybackManager::ApplyVelocityToPosition(float delta_time) {
    if (!player_entity_) return;
    
    const float GROUND_LEVEL = 0.0f;  // y position where ground is
    
    auto player_transform = player_entity_->GetTransform();
    if (!player_transform) return;
    
    glm::vec3 player_pos = player_transform->GetWorldPosition();
    
    // Apply combined velocity to position
    glm::vec3 new_pos = player_pos + player_velocity_ * delta_time;
    
    // Ground collision - simple check
    if (new_pos.y <= GROUND_LEVEL) {
        new_pos.y = GROUND_LEVEL;
        player_velocity_.y = 0.0f;  // Stop falling
        is_on_ground_ = true;
    } else {
        is_on_ground_ = false;
    }
    
    // Update position with combined velocity
    player_transform->SetWorldPosition(new_pos);
    
    auto logger = spdlog::get("editor");
    if (logger) {
        logger->debug("Position updated: ({:.2f}, {:.2f}, {:.2f}), on_ground={}", 
                     new_pos.x, new_pos.y, new_pos.z, is_on_ground_);
    }
}

} // namespace schizo::editor
