#include "scene_playback_manager.h"
#include "character_controller.h"
#include "input_buffer.h"
#include "scene.h"
#include "entity.h"
#include "collider_component.h"
#include "mesh_component.h"
#include "physics/jolt_physics.h"   // Stage 4 — Jolt-backed PhysicsWorld
#include "tinygltf.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <entt/entt.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace schizo::editor {

namespace {

// ----------------------------------------------------------------------------
// Triangle loader for Mesh colliders.
// Output: flat 3N-vertex list, every three consecutive vertices form one
// triangle. Coordinates are in the mesh asset's own local space (no node
// hierarchy transforms applied — assumes the asset's mesh is already at
// origin, which is the common case for prop / level chunks).
// ----------------------------------------------------------------------------

bool LoadMeshTriangles_OBJ(const std::string& path, std::vector<glm::vec3>& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::vector<glm::vec3> positions;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 2) continue;
        if (line[0] == 'v' && line[1] == ' ') {
            glm::vec3 p;
            std::istringstream ls(line.substr(2));
            ls >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (line[0] == 'f' && line[1] == ' ') {
            // Face line — collect all "vertex/uv/normal" specs, take just
            // the position index, fan-triangulate.
            std::istringstream ls(line.substr(2));
            std::vector<int> verts;
            std::string spec;
            while (ls >> spec) {
                int vi = std::atoi(spec.c_str());
                if (vi > 0) vi -= 1;
                else if (vi < 0) vi += static_cast<int>(positions.size());
                else continue;
                if (vi < 0 || vi >= static_cast<int>(positions.size())) continue;
                verts.push_back(vi);
            }
            for (size_t i = 1; i + 1 < verts.size(); ++i) {
                out.push_back(positions[verts[0]]);
                out.push_back(positions[verts[i]]);
                out.push_back(positions[verts[i + 1]]);
            }
        }
    }
    return !out.empty();
}

bool LoadMeshTriangles_GLTF(const std::string& path, std::vector<glm::vec3>& out) {
    tinygltf::Model model;
    std::string err, warn;
    tinygltf::TinyGLTF loader;
    bool ok = false;

    // Sniff the extension (case-insensitive) to decide which loader to use.
    std::string ext;
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == ".glb") {
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    } else {
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }
    if (!ok) {
        auto logger = spdlog::get("editor");
        if (logger) logger->warn("MeshCollider load failed for '{}': {}", path, err);
        return false;
    }

    auto fetch_positions = [&](int accessor_idx) -> std::vector<glm::vec3> {
        std::vector<glm::vec3> result;
        if (accessor_idx < 0 ||
            accessor_idx >= static_cast<int>(model.accessors.size())) return result;
        const auto& acc = model.accessors[accessor_idx];
        if (acc.type != "VEC3") return result;
        auto bytes = model.GetAccessorData(accessor_idx);
        if (bytes.size() < acc.count * sizeof(float) * 3) return result;
        const float* fp = reinterpret_cast<const float*>(bytes.data());
        result.reserve(acc.count);
        for (size_t i = 0; i < acc.count; ++i) {
            result.emplace_back(fp[i * 3 + 0], fp[i * 3 + 1], fp[i * 3 + 2]);
        }
        return result;
    };

    auto fetch_indices = [&](int accessor_idx) -> std::vector<uint32_t> {
        std::vector<uint32_t> result;
        if (accessor_idx < 0 ||
            accessor_idx >= static_cast<int>(model.accessors.size())) return result;
        const auto& acc = model.accessors[accessor_idx];
        auto bytes = model.GetAccessorData(accessor_idx);
        result.reserve(acc.count);
        if (acc.componentType == 5121 || acc.componentType == 5120) {
            // UNSIGNED_BYTE / BYTE
            for (size_t i = 0; i < acc.count; ++i) result.push_back(bytes[i]);
        } else if (acc.componentType == 5123 || acc.componentType == 5122) {
            // UNSIGNED_SHORT / SHORT
            const uint16_t* sp = reinterpret_cast<const uint16_t*>(bytes.data());
            for (size_t i = 0; i < acc.count; ++i) result.push_back(sp[i]);
        } else if (acc.componentType == 5125) {
            // UNSIGNED_INT
            const uint32_t* up = reinterpret_cast<const uint32_t*>(bytes.data());
            for (size_t i = 0; i < acc.count; ++i) result.push_back(up[i]);
        }
        return result;
    };

    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            auto it = prim.attributes.find("POSITION");
            if (it == prim.attributes.end()) continue;
            auto positions = fetch_positions(it->second);
            if (positions.empty()) continue;

            std::vector<uint32_t> indices = fetch_indices(prim.indices);
            if (indices.empty()) {
                // Non-indexed primitive: vertices are already in triangle order.
                for (size_t i = 0; i + 2 < positions.size(); i += 3) {
                    out.push_back(positions[i + 0]);
                    out.push_back(positions[i + 1]);
                    out.push_back(positions[i + 2]);
                }
            } else {
                for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                    if (indices[i] >= positions.size() ||
                        indices[i+1] >= positions.size() ||
                        indices[i+2] >= positions.size()) continue;
                    out.push_back(positions[indices[i + 0]]);
                    out.push_back(positions[indices[i + 1]]);
                    out.push_back(positions[indices[i + 2]]);
                }
            }
        }
    }
    return !out.empty();
}

bool LoadMeshTriangles(const std::string& path, std::vector<glm::vec3>& out) {
    out.clear();
    if (path.empty()) return false;
    std::string ext;
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".obj")  return LoadMeshTriangles_OBJ(path, out);
    if (ext == ".gltf" || ext == ".glb") return LoadMeshTriangles_GLTF(path, out);
    auto logger = spdlog::get("editor");
    if (logger) logger->warn("MeshCollider: unsupported extension '{}' for '{}'", ext, path);
    return false;
}

} // anonymous namespace

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

    // Snapshot BEFORE finding the player. If the user has the player
    // selected and play setup fails, we still want a clean restore path.
    CaptureSceneSnapshot();

    // Find and setup player entity
    if (!FindAndSetupPlayer()) {
        auto logger = spdlog::get("editor");
        if (logger) logger->error("Failed to find player entity in scene");
        entity_snapshots_.clear();
        scene_ = nullptr;
        return false;
    }

    // Attach character controller
    if (!AttachCharacterController()) {
        auto logger = spdlog::get("editor");
        if (logger) logger->error("Failed to attach character controller");
        entity_snapshots_.clear();
        scene_ = nullptr;
        player_entity_ = nullptr;
        return false;
    }

    // Setup playback camera
    SetupPlaybackCamera();

    // Phase 2: build a fresh PhysicsWorld from any ColliderComponents in the
    // scene. The player entity is deliberately excluded — Phase 3 will give
    // it a Kinematic body of its own.
    BuildPhysicsWorld();

    is_playing_ = true;
    is_paused_ = false;
    is_cursor_captured_ = true;  // Host will hide+lock the OS cursor
    mouse_delta_x_ = 0.0f;
    mouse_delta_y_ = 0.0f;
    playback_time_ = 0.0f;
    is_on_ground_ = false;

    // Initialize camera position from player
    if (player_entity_) {
        auto player_pos = player_entity_->GetTransform()->GetWorldPosition();
        camera_position_ = player_pos + glm::vec3(0.0f, camera_height_, camera_distance_);
        camera_target_position_ = camera_position_;
        if (player_controller_) {
            player_controller_->SetPosition(player_pos);
            player_controller_->SetGrounded(player_pos.y <= GROUND_LEVEL);
        }
    }

    auto logger = spdlog::get("editor");
    if (logger) logger->info("Scene playback started with camera: {}",
                             playback_camera_ ? playback_camera_->GetName() : "none");

    return true;
}

void ScenePlaybackManager::StopPlayback() {
    // Tear down the physics world FIRST so the bodies' raw pointers in the
    // world don't outlive the unique_ptrs we own in entity_bodies_.
    TearDownPhysicsWorld();

    // Restore scene state captured at StartPlayback. Doing this before clearing
    // scene_ so RestoreSceneSnapshot can find the entities.
    if (is_playing_) {
        RestoreSceneSnapshot();
    }
    entity_snapshots_.clear();

    is_playing_ = false;
    is_paused_ = false;
    is_cursor_captured_ = false;  // Host will restore the OS cursor
    mouse_delta_x_ = 0.0f;
    mouse_delta_y_ = 0.0f;
    playback_time_ = 0.0f;
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

    UpdateMouseLook();
    DriveCharacterController(delta_time);
    // Physics runs after the player is integrated so any future Kinematic
    // player body (Phase 3) gets to update its target position before the
    // world resolves Dynamic-vs-Kinematic contacts.
    StepPhysics(delta_time);
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
        // CharacterController is constructed with an entt::entity but never
        // dereferences a registry — its subsystems (GroundDetector, etc.) just
        // hold the id. entt::null is fine because we feed position/ground state
        // via SetPosition/SetGrounded from the scene's Entity instead.
        player_controller_ = std::make_shared<engine::character::CharacterController>(
            entt::null);

        auto logger = spdlog::get("editor");
        if (logger) logger->info("CharacterController attached to player '{}'",
                                 player_entity_->GetName());
        return true;
    } catch (const std::exception& e) {
        auto logger = spdlog::get("editor");
        if (logger) logger->error("Exception when attaching CharacterController: {}",
                                  e.what());
        return false;
    }
}

void ScenePlaybackManager::UpdateCamera() {
    if (!player_entity_ || !playback_camera_) return;

    auto player_transform = player_entity_->GetTransform();
    auto camera_transform = playback_camera_->GetTransform();
    if (!player_transform || !camera_transform) return;

    // First-person camera is parented to the player and rides at a fixed local
    // offset (set by EntityFactory::CreatePlayer). The transform hierarchy
    // already places it correctly — no per-frame override needed.
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

void ScenePlaybackManager::UpdateMouseLook() {
    if (!player_entity_) return;
    auto player_transform = player_entity_->GetTransform();
    if (!player_transform) return;

    // While the cursor is captured the host (main.cpp) feeds raw GLFW deltas
    // through OnMouseDelta() — ImGui's NoMouse flag zeros io.MousePos so we
    // cannot read it here. Yaw is applied to the player body; pitch is applied
    // to the camera child for up/down look.
    if (is_cursor_captured_) {
        const float MOUSE_SENSITIVITY = 0.0025f;  // radians per pixel

        if (std::abs(mouse_delta_x_) > 0.0f) {
            float yaw_angle = -mouse_delta_x_ * MOUSE_SENSITIVITY;
            player_transform->Rotate(glm::vec3(0.0f, 1.0f, 0.0f), yaw_angle);
        }

        if (std::abs(mouse_delta_y_) > 0.0f && playback_camera_) {
            auto camera_transform = playback_camera_->GetTransform();
            if (camera_transform) {
                // GLFW cursor Y grows downward, and a positive rotation around
                // local X tilts the camera up. Negate so pushing the mouse
                // forward (cursor Y decreasing) looks up — standard FPS feel.
                float pitch_angle = -mouse_delta_y_ * MOUSE_SENSITIVITY;
                glm::quat current_rot = camera_transform->GetLocalRotation();
                glm::vec3 euler = glm::eulerAngles(current_rot);
                euler.x = glm::clamp(euler.x + pitch_angle, -1.57f, 1.57f);
                camera_transform->SetLocalRotation(glm::quat(euler));
            }
        }
    }
    mouse_delta_x_ = 0.0f;
    mouse_delta_y_ = 0.0f;
}

void ScenePlaybackManager::DriveCharacterController(float delta_time) {
    if (!player_entity_ || !player_controller_) return;
    auto player_transform = player_entity_->GetTransform();
    if (!player_transform) return;

    // 1. Sync world position into the controller before it ticks, and
    //    determine grounded state via a short downward raycast from just
    //    below the player capsule's bottom. The previous code clamped
    //    against a hard-coded y=0 floor; with Phase 3 the ground is
    //    whatever Static body the cast hits within ~10cm of the feet.
    glm::vec3 player_pos = player_transform->GetWorldPosition();
    player_controller_->SetPosition(player_pos);
    player_controller_->SetGrounded(is_on_ground_);

    // 2. Build the input action from the keyboard and hand it to the
    //    controller. Calling ProcessInput directly bypasses the input buffer,
    //    which is fine for editor playback — the buffer is for network sync.
    engine::character::InputAction input;
    if (ImGui::IsKeyDown(ImGuiKey_W)) input.forward += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_S)) input.forward -= 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_D)) input.lateral += 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_A)) input.lateral -= 1.0f;
    if (ImGui::IsKeyDown(ImGuiKey_LeftShift) ||
        ImGui::IsKeyDown(ImGuiKey_RightShift)) {
        input.sprint = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) input.jump = true;
    player_controller_->ProcessInput(input);

    // 3. Tick the controller: state machine, gravity-or-clamp, stamina, dash.
    player_controller_->Update(delta_time);

    // 4. The controller produces a velocity in its own (input-aligned) axis
    //    frame: input.lateral → +x, input.forward → +z. Project onto the
    //    player's basis (yaw-rotated) so movement follows facing direction.
    glm::vec3 cv = player_controller_->GetVelocity();
    glm::vec3 right    = player_transform->GetRight();
    glm::vec3 forward  = player_transform->GetForward();
    glm::vec3 world_velocity = right * cv.x + forward * cv.z;
    world_velocity.y = cv.y;

    // 5. Drive the Jolt character with the controller's full velocity (the
    //    controller already owns gravity/jump in world_velocity.y). Jolt does
    //    the collide-and-slide; StepPhysics writes the corrected position back
    //    to the entity transform.
    if (player_char_id_ != 0xFFFFFFFFu && physics_world_) {
        physics_world_->update_character(player_char_id_, world_velocity, delta_time);
    }
}

void ScenePlaybackManager::CaptureSceneSnapshot() {
    entity_snapshots_.clear();
    if (!scene_) return;
    for (const auto& ent : scene_->GetEntities()) {
        if (!ent) continue;
        auto t = ent->GetTransform();
        if (!t) continue;
        EntitySnapshot snap;
        snap.local_position = t->GetLocalPosition();
        snap.local_rotation = t->GetLocalRotation();
        snap.local_scale    = t->GetLocalScale();
        entity_snapshots_[ent->GetId()] = snap;
    }
    auto logger = spdlog::get("editor");
    if (logger) logger->debug("Captured snapshot of {} entity transforms",
                              entity_snapshots_.size());
}

void ScenePlaybackManager::RestoreSceneSnapshot() {
    if (!scene_) return;
    size_t restored = 0;
    for (const auto& ent : scene_->GetEntities()) {
        if (!ent) continue;
        auto it = entity_snapshots_.find(ent->GetId());
        if (it == entity_snapshots_.end()) continue;
        auto t = ent->GetTransform();
        if (!t) continue;
        t->SetLocalPosition(it->second.local_position);
        t->SetLocalRotation(it->second.local_rotation);
        t->SetLocalScale(it->second.local_scale);
        ++restored;
    }
    auto logger = spdlog::get("editor");
    if (logger) logger->debug("Restored {} entity transforms from snapshot",
                              restored);
}

void ScenePlaybackManager::SetupPlaybackCamera() {
    auto logger = spdlog::get("editor");
    if (!player_entity_ || !scene_) {
        if (logger) logger->error("SetupPlaybackCamera: player or scene is null");
        return;
    }

    // Preference order: FirstPersonCamera → ThirdPersonCamera → any "Camera"/
    // "PlayerCamera" child created by older saves. Fall back to creating a
    // FirstPersonCamera child if none exist.
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

    // Create FirstPersonCamera child if none exist
    auto camera_entity = scene_->CreateEntity("FirstPersonCamera");
    if (!camera_entity) return;
    camera_entity->SetParent(player_entity_);
    if (auto t = camera_entity->GetTransform()) {
        t->SetLocalPosition(glm::vec3(0.0f, 0.62f, 0.0f));  // Head height on a 1.8m tall capsule
    }
    playback_camera_ = camera_entity;
    if (logger) logger->info("Playback camera: created FirstPersonCamera");
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

void ScenePlaybackManager::BuildPhysicsWorld() {
    TearDownPhysicsWorld();
    if (!scene_) return;

    using namespace schizo::physics;
    physics_world_ = std::make_unique<PhysicsWorld>();
    if (!physics_world_->init(glm::vec3(0.0f, -9.81f, 0.0f))) {
        auto logger = spdlog::get("editor");
        if (logger) logger->error("BuildPhysicsWorld: Jolt PhysicsWorld init failed");
        physics_world_.reset();
        return;
    }

    // Fill a Jolt BodyDesc from a ColliderComponent, baking the entity's world
    // scale into the authored dimensions.
    auto fill_desc = [](const schizo::scene::ColliderComponent& c,
                        const glm::vec3& world_scale, BodyDesc& d) -> bool {
        switch (c.GetShape()) {
            case schizo::scene::ColliderShape::Box:
                d.shape = ShapeType::Box;
                d.half_extents = c.GetHalfExtents() * world_scale;
                return true;
            case schizo::scene::ColliderShape::Sphere:
                d.shape = ShapeType::Sphere;
                d.radius = c.GetRadius() * std::max({world_scale.x, world_scale.y, world_scale.z});
                return true;
            case schizo::scene::ColliderShape::Capsule:
                d.shape  = ShapeType::Capsule;
                d.radius = c.GetRadius() * std::max(world_scale.x, world_scale.z);
                d.height = c.GetHeight() * world_scale.y;   // full cylinder span
                return true;
            case schizo::scene::ColliderShape::Cylinder:
                d.shape  = ShapeType::Cylinder;
                d.radius = c.GetRadius() * std::max(world_scale.x, world_scale.z);
                d.height = c.GetHeight() * world_scale.y;
                return true;
            case schizo::scene::ColliderShape::Plane:
                // Jolt has no infinite plane body — approximate with a large,
                // thin box (the authored normal is assumed +Y for ground).
                d.shape = ShapeType::Box;
                d.half_extents = glm::vec3(500.0f, 0.05f, 500.0f);
                return true;
            default:
                return false;   // Mesh handled on the dedicated path below
        }
    };

    size_t built = 0;
    auto logger = spdlog::get("editor");
    for (const auto& ent : scene_->GetEntities()) {
        if (!ent || !ent->IsActiveInHierarchy()) continue;
        auto col = ent->GetComponent<schizo::scene::ColliderComponent>();
        if (!col) continue;
        auto t = ent->GetTransform();
        if (!t) continue;

        const glm::vec3 world_scale = t->GetWorldScale();
        const glm::vec3 wpos = t->GetWorldPosition() + col->GetOffset();
        const glm::quat wrot = t->GetWorldRotation();
        const bool is_player = (player_entity_ && ent == player_entity_);

        // The player is a kinematic capsule character (collide-and-slide),
        // driven by the CharacterController — not a rigid body.
        if (is_player) {
            CharacterDesc cd;
            cd.radius   = col->GetRadius() * std::max(world_scale.x, world_scale.z);
            cd.height   = (col->GetShape() == schizo::scene::ColliderShape::Box)
                            ? col->GetHalfExtents().y * 2.0f * world_scale.y
                            : col->GetHeight() * world_scale.y;
            if (cd.height < 0.1f) cd.height = 1.2f;
            cd.position = wpos;
            player_char_id_ = physics_world_->add_character(cd);
            if (logger) logger->info("  +player character (capsule r={:.2f} h={:.2f})", cd.radius, cd.height);
            continue;
        }

        BodyId id = kInvalidBody;
        if (col->GetShape() == schizo::scene::ColliderShape::Mesh) {
            auto* mc = ent->GetMeshComponent();
            std::vector<glm::vec3> tris;
            if (!mc || mc->mesh_path.empty() || !LoadMeshTriangles(mc->mesh_path, tris) || tris.empty()) {
                if (logger) logger->warn("MeshCollider on '{}' unavailable — skipped", ent->GetName());
                continue;
            }
            for (auto& v : tris) v *= world_scale;           // bake scale into the verts
            if (col->IsDynamic()) {
                // Jolt triangle meshes can't be dynamic — use the convex hull of
                // the mesh verts (the standard approximation for dynamic meshes).
                BodyDesc d;
                d.position = wpos; d.rotation = wrot;
                d.motion = MotionType::Dynamic; d.mass = col->GetMass();
                id = physics_world_->add_convex_body(tris, d);
            } else {
                id = physics_world_->add_mesh_body(tris, t->GetWorldPosition() + col->GetOffset(), wrot);
            }
        } else {
            BodyDesc d;
            if (!fill_desc(*col, world_scale, d)) continue;
            d.position    = wpos;
            d.rotation    = wrot;
            d.motion      = col->IsDynamic() ? MotionType::Dynamic : MotionType::Static;
            d.mass        = col->IsDynamic() ? col->GetMass() : 0.0f;
            id = physics_world_->add_body(d);
        }
        if (id == kInvalidBody) continue;

        entity_bodies_.emplace(ent->GetId(), id);
        if (col->IsDynamic()) dynamic_entities_.push_back(ent->GetId());  // incl. dynamic mesh (convex)
        if (logger) logger->info("  +body '{}' [{}] shape={}", ent->GetName(),
                                 dynamic ? "Dynamic" : "Static", static_cast<int>(col->GetShape()));
        ++built;
    }
    if (logger) logger->info("Jolt PhysicsWorld built: {} bodies, {} dynamic, player={}",
                             built, dynamic_entities_.size(),
                             player_char_id_ != 0xFFFFFFFFu ? "yes" : "no");
}

void ScenePlaybackManager::TearDownPhysicsWorld() {
    entity_bodies_.clear();
    dynamic_entities_.clear();
    player_char_id_ = 0xFFFFFFFFu;
    physics_world_.reset();   // PhysicsWorld dtor frees all Jolt bodies/characters
}

void ScenePlaybackManager::StepPhysics(float delta_time) {
    if (!physics_world_) return;
    physics_world_->step(delta_time);

    // Write simulated dynamic bodies back to their entity transforms.
    for (uint32_t eid : dynamic_entities_) {
        auto it = entity_bodies_.find(eid);
        if (it == entity_bodies_.end()) continue;
        auto ent = scene_ ? scene_->GetEntityById(eid) : nullptr;
        if (!ent) continue;
        auto t = ent->GetTransform();
        if (!t) continue;
        const schizo::physics::BodyState s = physics_world_->get_state(it->second);
        t->SetWorldPosition(s.position);
        t->SetWorldRotation(s.rotation);
    }

    // Write the player character's collide-and-slide result back + refresh the
    // grounded flag the CharacterController reads next frame.
    if (player_char_id_ != 0xFFFFFFFFu && player_entity_) {
        if (auto t = player_entity_->GetTransform())
            t->SetWorldPosition(physics_world_->character_position(player_char_id_));
        is_on_ground_ = physics_world_->character_on_ground(player_char_id_);
    } else {
        is_on_ground_ = false;
    }
}

} // namespace schizo::editor
