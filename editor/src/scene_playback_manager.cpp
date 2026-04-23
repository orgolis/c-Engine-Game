#include "scene_playback_manager.h"
#include "../core/character/include/character_controller.h"
#include "../../engine/scene/include/scene.h"
#include "../../engine/scene/include/entity.h"
#include "../core/character/include/player_entity.h"
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>

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
    auto logger = spdlog::get("editor");
    
    // Check player
    if (!player_entity_) {
        if (logger) logger->error("❌ UpdateCamera FAILED: player_entity_ is nullptr");
        return;
    }
    
    // Check camera
    if (!playback_camera_) {
        if (logger) logger->error("❌ UpdateCamera FAILED: playback_camera_ is nullptr");
        return;
    }
    
    // Get player transform
    auto player_transform = player_entity_->GetTransform();
    if (!player_transform) {
        if (logger) logger->error("❌ UpdateCamera FAILED: player_entity_ has no Transform");
        return;
    }
    
    // Get camera transform
    auto camera_transform = playback_camera_->GetTransform();
    if (!camera_transform) {
        if (logger) logger->error("❌ UpdateCamera FAILED: playback_camera_ has no Transform");
        return;
    }
    
    // Get positions
    glm::vec3 player_pos = player_transform->GetWorldPosition();
    glm::vec3 player_forward = player_transform->GetForward();
    
    // Calculate camera position
    glm::vec3 backward = -player_forward;
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 camera_target = player_pos + (backward * camera_distance_) + (up * camera_height_);
    
    // Smooth follow
    camera_position_ = glm::mix(camera_position_, camera_target, camera_smoothing_);
    
    // CRITICAL: Update the camera entity's world position
    if (logger) logger->info("🎥 Setting camera world pos from ({:.2f}, {:.2f}, {:.2f}) to ({:.2f}, {:.2f}, {:.2f})",
        camera_transform->GetWorldPosition().x,
        camera_transform->GetWorldPosition().y,
        camera_transform->GetWorldPosition().z,
        camera_position_.x, camera_position_.y, camera_position_.z);
    
    camera_transform->SetWorldPosition(camera_position_);
    
    // Verify the update
    glm::vec3 verified_pos = camera_transform->GetWorldPosition();
    if (logger) logger->info("✅ Verified camera pos: ({:.2f}, {:.2f}, {:.2f})",
        verified_pos.x, verified_pos.y, verified_pos.z);
    
    if (glm::distance(verified_pos, camera_position_) > 0.01f) {
        if (logger) logger->error("❌ WARNING: SetWorldPosition didn't work! Distance: {:.3f}",
            glm::distance(verified_pos, camera_position_));
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
    // Handle mouse input to rotate the player
    // Rotation is applied around Y axis (yaw) based on mouse X movement
    if (!ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
        static glm::vec2 last_mouse_pos = glm::vec2(io.MousePos.x, io.MousePos.y);
        glm::vec2 curr_mouse_pos = glm::vec2(io.MousePos.x, io.MousePos.y);
        
        if (io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2]) {
            // Right-click or any mouse button: enable mouse look
            glm::vec2 mouse_delta = curr_mouse_pos - last_mouse_pos;
            
            // Mouse sensitivity (adjust for feel)
            const float MOUSE_SENSITIVITY = 0.01f;  // radians per pixel
            
            // Yaw rotation around Y axis (horizontal mouse movement)
            if (glm::abs(mouse_delta.x) > 0.1f) {
                float yaw_angle = -mouse_delta.x * MOUSE_SENSITIVITY;
                player_transform->Rotate(glm::vec3(0.0f, 1.0f, 0.0f), yaw_angle);
                
                auto logger = spdlog::get("editor");
                if (logger) {
                    auto player_rot = player_transform->GetLocalRotation();
                    logger->info("🔄 Player rotated: yaw={:.3f}rad, quat=({:.3f}, {:.3f}, {:.3f}, {:.3f})", 
                        yaw_angle, player_rot.x, player_rot.y, player_rot.z, player_rot.w);
                }
            }
        }
        
        last_mouse_pos = curr_mouse_pos;
    }
    
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
    if (logger) logger->warn("⚙️ =================  SetupPlaybackCamera START  =================");
    
    if (!player_entity_) {
        if (logger) logger->error("❌ FATAL: player_entity_ is nullptr!");
        return;
    }
    if (logger) logger->info("✅ player_entity_ exists: {}", player_entity_->GetName());
    
    if (!scene_) {
        if (logger) logger->error("❌ FATAL: scene_ is nullptr!");
        return;
    }
    if (logger) logger->info("✅ scene_ exists");
    
    // Try to find existing camera child
    if (logger) logger->info("🔍 Checking for existing camera children...");
    auto children = player_entity_->GetChildren();
    if (logger) logger->info("   Player has {} children", children.size());
    
    for (const auto& child : children) {
        if (child) {
            std::string child_name = child->GetName();
            if (logger) logger->info("   - Found child: '{}'", child_name);
            if (child_name == "Camera" || child_name == "PlayerCamera") {
                playback_camera_ = child;
                if (logger) logger->warn("✅ Using EXISTING camera: {}", child_name);
                return;
            }
        }
    }
    
    if (logger) logger->info("   No existing camera found");
    
    // Create new camera entity
    if (logger) logger->info("➕ Creating new PlayerCamera entity...");
    auto camera_entity = scene_->CreateEntity("PlayerCamera");
    
    if (!camera_entity) {
        if (logger) logger->error("❌ FATAL: CreateEntity() returned nullptr!");
        return;
    }
    if (logger) logger->info("✅ PlayerCamera entity created successfully");
    
    // Store it immediately
    playback_camera_ = camera_entity;
    if (logger) logger->info("✅ playback_camera_ assigned to new entity");
    
    // Get transform
    auto camera_transform = camera_entity->GetTransform();
    if (!camera_transform) {
        if (logger) logger->error("❌ FATAL: Camera entity has NO Transform component!");
        return;
    }
    if (logger) logger->info("✅ Camera has Transform component");
    
    // Set local position
    glm::vec3 local_pos(0.0f, 0.8f, 0.0f);
    if (logger) logger->info("📍 Setting local position to ({:.2f}, {:.2f}, {:.2f})", 
        local_pos.x, local_pos.y, local_pos.z);
    camera_transform->SetLocalPosition(local_pos);
    if (logger) logger->info("✅ SetLocalPosition() called");
    
    if (logger) logger->warn("⚙️ =================  SetupPlaybackCamera END  =================");
    
    auto logger = spdlog::get("editor");
    if (logger) logger->info("Created new camera child: PlayerCamera");
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
