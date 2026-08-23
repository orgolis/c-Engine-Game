#include "scene_playback_manager.h"
#include "character_controller.h"
#include "input_buffer.h"
#include "scene.h"
#include "entity.h"
#include "collider_component.h"
#include "mesh_component.h"
#include "assets/mesh_asset.h"       // MeshAsset primitives — collider triangles without a disk read
#include "asset_path_util.h"       // resolve_asset_path / utf8_path (shared w/ render loader)
#include "terrain_component.h"
#include "water_component.h"
#include "physics/jolt_physics.h"   // Stage 4 — Jolt-backed PhysicsWorld
#include "tinygltf.hpp"
#include <algorithm>
#include <cctype>
#include <cfloat>
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
#include <cstdint>

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
    // utf8_path: non-ASCII filenames (Cyrillic etc.) fail with a narrow ifstream.
    std::ifstream f(utf8_path(path));
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
    // Resolve CWD-independently (the editor runs from build-editor/bin) — MUST
    // match the render mesh loader (scene_render_bridge.h) or a model renders
    // while its collider silently fails to load.
    const std::string disk = resolve_asset_path(path);
    std::string ext;
    size_t dot = disk.find_last_of('.');
    if (dot != std::string::npos) ext = disk.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".obj")  return LoadMeshTriangles_OBJ(disk, out);
    if (ext == ".gltf" || ext == ".glb") return LoadMeshTriangles_GLTF(disk, out);
    auto logger = spdlog::get("editor");
    if (logger) logger->warn("MeshCollider: unsupported extension '{}' for '{}'", ext, path);
    return false;
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// Collider triangles: two sources, and why the cheap one has to be tried first.
//
// BuildPhysicsWorld used to call LoadMeshTriangles() per mesh collider, which
// re-parses the OBJ off disk with getline + istringstream per line. Measured on
// a real project: Porsche 341 ms, building_04 238 ms, and a tree mesh 5 ms that
// three colliders each paid separately -- 609 ms of synchronous disk parsing in
// the frame that enters play mode.
//
// It was invisible because BeginPlayMode is called from inside ShowViewport, so
// the whole cost was billed to a profile zone named "ui_viewport" and read as a
// UI problem for six releases. The published play_mode_entry metric says 7 ms
// because the headless probe scene has no mesh colliders.
//
// The renderer has already parsed these meshes; MeshAsset holds the vertices
// and indices. Using them costs a walk instead of a file read.
//
// The RISK is that the two sources disagree on geometry -- a collider silently
// in the wrong place is far worse than a slow one -- so physmesh_check asserts
// they produce identical triangles, and the disk path stays as the fallback.
// ----------------------------------------------------------------------------

bool collider_triangles_from_disk(const std::string& path, std::vector<glm::vec3>& out) {
    return LoadMeshTriangles(path, out);
}

bool collider_triangles_from_asset(const schizo::scene::MeshComponent& mc,
                                   std::vector<glm::vec3>& out) {
    out.clear();
    auto asset = mc.GetMeshAsset();
    if (!asset) return false;
    for (const auto& prim : asset->GetPrimitives()) {
        // Indexed triangles only. A primitive with no indices would need its
        // own draw-mode handling, and guessing would fabricate geometry.
        if (prim.indices.size() < 3) continue;
        out.reserve(out.size() + prim.indices.size());
        for (size_t i = 0; i + 2 < prim.indices.size(); i += 3) {
            const uint32_t a = prim.indices[i], b = prim.indices[i + 1], c = prim.indices[i + 2];
            if (a >= prim.vertices.size() || b >= prim.vertices.size() ||
                c >= prim.vertices.size())
                continue;                       // corrupt index: drop the tri, not the mesh
            out.push_back(prim.vertices[a].position);
            out.push_back(prim.vertices[b].position);
            out.push_back(prim.vertices[c].position);
        }
    }
    return !out.empty();
}

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

    // 5. Swimming: when the player's torso is under a PHYSICAL water surface,
    //    replace the controller's gravity-driven vertical velocity with a swim
    //    model — damped movement, gentle sink, Space to swim up. Above the
    //    surface the normal gravity/jump behavior resumes (so you bob).
    {
        const float wl = WaterLevelAt(player_pos);
        if (wl > -1e9f && player_pos.y + 0.4f < wl) {
            world_velocity.x *= 0.6f;
            world_velocity.z *= 0.6f;
            float vy = glm::clamp(world_velocity.y, -8.0f, 8.0f) * 0.35f - 0.4f;
            if (ImGui::IsKeyDown(ImGuiKey_Space)) vy = 3.0f;          // swim up
            world_velocity.y = vy;
        }
    }

    // 6. Drive the Jolt character with the controller's full velocity (the
    //    controller already owns gravity/jump in world_velocity.y). Jolt does
    //    the collide-and-slide; StepPhysics writes the corrected position back
    //    to the entity transform.
    if (player_char_id_ != 0xFFFFFFFFu && physics_world_) {
        physics_world_->update_character(player_char_id_, world_velocity, delta_time);
    }
}

void ScenePlaybackManager::ApplyOriginShift(const glm::vec3& shift) {
    if (shift == glm::vec3(0.0f) || !is_playing_) return;

    // 1) The simulation. Bodies AND the player character; the character is not
    //    a rigid body, so it is not in the body list.
    if (physics_world_) physics_world_->translate_world(shift);

    // 2) The restore snapshot. Roots only -- a child's local position is
    //    relative to its parent and the rebase never touched it. Shifting
    //    children too would move them twice on Stop.
    if (scene_) {
        for (const auto& ent : scene_->GetEntities()) {
            if (!ent || ent->GetParent()) continue;
            auto it = entity_snapshots_.find(ent->GetId());
            if (it != entity_snapshots_.end()) it->second.local_position += shift;
        }
    }

    // 3) Physical water. Gathered once at play start, in world space.
    for (WaterVolume& wv : water_volumes_) {
        wv.center_xz += glm::vec2(shift.x, shift.z);
        wv.level     += shift.y;
    }

    // 4) The follow camera. Smoothed toward its target, so leaving these
    //    behind would not snap the camera -- it would sail across the level
    //    over the next second, which reads as the world sliding away.
    camera_position_        += shift;
    camera_target_position_ += shift;
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

// Local-space triangle soup from a terrain heightmap (every 3 verts = 1 tri),
// for a static Jolt mesh collider. Mirrors build_terrain_mesh's grid.
static void BuildTerrainTriangles(const schizo::scene::TerrainComponent& tc,
                                  std::vector<glm::vec3>& out) {
    const int   res   = tc.GetResolution();
    const float half  = tc.GetSize() * 0.5f;
    const float cell  = tc.CellSize();
    const float scale = tc.GetHeightScale();
    out.clear();
    out.reserve(static_cast<size_t>(res) * res * 6);
    auto vert = [&](int ix, int iz) {
        return glm::vec3(-half + ix * cell, tc.HeightAt(ix, iz) * scale, -half + iz * cell);
    };
    for (int z = 0; z < res; ++z)
        for (int x = 0; x < res; ++x) {
            if (tc.HasHole(x, z)) continue;   // carved cell — caves pass through
            const glm::vec3 p0 = vert(x, z),     p1 = vert(x + 1, z);
            const glm::vec3 p2 = vert(x, z + 1), p3 = vert(x + 1, z + 1);
            out.push_back(p0); out.push_back(p2); out.push_back(p1);
            out.push_back(p1); out.push_back(p2); out.push_back(p3);
        }
}

void ScenePlaybackManager::BuildPhysicsWorld() {
    TearDownPhysicsWorld();
    if (!scene_) return;

    // Triangle sources for this build. Counted rather than merely used, because
    // "it got faster" and "it stopped reading the disk" are different claims and
    // only the second one is the fix — a silent fallback to the disk path would
    // restore the 609 ms stall with nothing in the log to say so.
    std::unordered_map<std::string, std::vector<glm::vec3>> tri_memo;
    size_t stats_from_asset = 0, stats_from_disk = 0, stats_from_memo = 0;

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

        // PHYSICAL water volumes (buoyancy + swimming). Standalone
        // WaterComponent: entity Y is the surface level.
        if (auto wcomp = ent->GetComponent<schizo::scene::WaterComponent>()) {
            if (wcomp->IsPhysical()) {
                auto wt = ent->GetTransform();
                if (wt) {
                    WaterVolume v;
                    const glm::vec3 wp = wt->GetWorldPosition();
                    v.center_xz = glm::vec2(wp.x, wp.z);
                    v.half_size = wcomp->GetSize() * 0.5f;
                    v.level     = wp.y;
                    water_volumes_.push_back(v);
                    if (logger) logger->info("  +water volume '{}' (level {:.2f})",
                                             ent->GetName(), v.level);
                }
            }
            // Water entities have no collider — nothing else to build.
        }

        // Terrain: a static triangle-mesh collider built from the heightmap so
        // the player + dynamic bodies stand on it. (Built at play start; re-enter
        // Play to refresh collision after sculpting.) Hole cells are carved out
        // of the collision too. Terrain-integrated PHYSICAL water becomes a
        // volume covering the terrain rect.
        if (auto terr = ent->GetComponent<schizo::scene::TerrainComponent>()) {
            auto tt = ent->GetTransform();
            if (tt) {
                std::vector<glm::vec3> tris;
                BuildTerrainTriangles(*terr, tris);
                if (!tris.empty()) {
                    BodyId tid = physics_world_->add_mesh_body(
                        tris, tt->GetWorldPosition(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                    if (tid != kInvalidBody) {
                        entity_bodies_.emplace(ent->GetId(), tid);
                        ++built;
                        if (logger) logger->info("  +terrain collider '{}' ({} tris)",
                                                 ent->GetName(), tris.size() / 3);
                    }
                }
                if (terr->IsWaterEnabled() && terr->IsWaterPhysical()) {
                    WaterVolume v;
                    const glm::vec3 wp = tt->GetWorldPosition();
                    v.center_xz = glm::vec2(wp.x, wp.z);
                    v.half_size = glm::vec2(terr->GetSize() * 0.5f);
                    v.level     = wp.y + terr->GetWaterLevel();
                    water_volumes_.push_back(v);
                    if (logger) logger->info("  +terrain water volume '{}' (level {:.2f})",
                                             ent->GetName(), v.level);
                }
            }
            continue;
        }

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
            if (!mc || mc->mesh_path.empty()) {
                if (logger) logger->warn("MeshCollider on '{}' unavailable — skipped", ent->GetName());
                continue;
            }
            // Memoised for this build: three colliders sharing one mesh used to
            // parse the same file three times.
            if (auto cached = tri_memo.find(mc->mesh_path); cached != tri_memo.end()) {
                tris = cached->second;
                ++stats_from_memo;
            } else {
                // Cached asset first, disk only as the fallback. See the note
                // above collider_triangles_from_asset.
                if (collider_triangles_from_asset(*mc, tris) && !tris.empty()) {
                    ++stats_from_asset;
                } else if (collider_triangles_from_disk(mc->mesh_path, tris) && !tris.empty()) {
                    ++stats_from_disk;
                } else {
                    if (logger) logger->warn("MeshCollider on '{}' unavailable — skipped", ent->GetName());
                    continue;
                }
                tri_memo.emplace(mc->mesh_path, tris);
            }
            for (auto& v : tris) v *= world_scale;           // bake scale into the verts
            if (col->IsDynamic()) {
                // Jolt triangle meshes can't be dynamic — use the convex hull of
                // the mesh verts (the standard approximation for dynamic meshes).
                BodyDesc d;
                d.position = wpos; d.rotation = wrot;
                // Net clients don't simulate props: dynamic colliders become
                // Kinematic bodies driven from the replicated entity transforms,
                // so the local player still collides with them where drawn.
                d.motion = net_client_mode_ ? MotionType::Kinematic : MotionType::Dynamic;
                d.mass = col->GetMass();
                id = physics_world_->add_convex_body(tris, d);
            } else {
                id = physics_world_->add_mesh_body(tris, t->GetWorldPosition() + col->GetOffset(), wrot);
            }
        } else {
            BodyDesc d;
            if (!fill_desc(*col, world_scale, d)) continue;
            d.position    = wpos;
            d.rotation    = wrot;
            // Net clients: see the kinematic note on the dynamic-mesh path above.
            d.motion      = col->IsDynamic()
                              ? (net_client_mode_ ? MotionType::Kinematic : MotionType::Dynamic)
                              : MotionType::Static;
            d.mass        = col->IsDynamic() ? col->GetMass() : 0.0f;
            id = physics_world_->add_body(d);
        }
        if (id == kInvalidBody) continue;

        entity_bodies_.emplace(ent->GetId(), id);
        if (col->IsDynamic()) dynamic_entities_.push_back(ent->GetId());  // incl. dynamic mesh (convex)
        if (logger) logger->info("  +body '{}' [{}] shape={}", ent->GetName(),
                                 col->IsDynamic() ? "Dynamic" : "Static", static_cast<int>(col->GetShape()));
        ++built;
    }
    last_mesh_from_asset_ = stats_from_asset;
    last_mesh_from_disk_  = stats_from_disk;
    last_mesh_from_memo_  = stats_from_memo;
    if (logger) logger->info("Jolt PhysicsWorld built: {} bodies, {} dynamic, player={}",
                             built, dynamic_entities_.size(),
                             player_char_id_ != 0xFFFFFFFFu ? "yes" : "no");
    if (logger && (stats_from_asset || stats_from_disk || stats_from_memo)) {
        // A non-zero disk count is the regression signal: it means a mesh
        // collider parsed a file on the frame that entered play mode.
        logger->info("  mesh colliders: {} from loaded asset, {} memoised, {} re-read from DISK",
                     stats_from_asset, stats_from_memo, stats_from_disk);
    }
}

void ScenePlaybackManager::TearDownPhysicsWorld() {
    entity_bodies_.clear();
    dynamic_entities_.clear();
    remote_player_bodies_.clear();
    water_volumes_.clear();
    player_char_id_ = 0xFFFFFFFFu;
    physics_world_.reset();   // PhysicsWorld dtor frees all Jolt bodies/characters
}

float ScenePlaybackManager::WaterLevelAt(const glm::vec3& pos) const {
    float best = -FLT_MAX;
    for (const WaterVolume& v : water_volumes_) {
        if (std::abs(pos.x - v.center_xz.x) <= v.half_size.x &&
            std::abs(pos.z - v.center_xz.y) <= v.half_size.y)
            best = std::max(best, v.level);
    }
    return best;
}

uint32_t ScenePlaybackManager::BodyForEntity(uint32_t entity_id) const {
    auto it = entity_bodies_.find(entity_id);
    return it != entity_bodies_.end() ? it->second : 0xFFFFFFFFu;
}

uint32_t ScenePlaybackManager::EntityForBody(uint32_t body_id) const {
    for (const auto& [eid, bid] : entity_bodies_)
        if (bid == body_id) return eid;
    return 0;
}

bool ScenePlaybackManager::AddRuntimeBody(const std::shared_ptr<schizo::scene::Entity>& ent) {
    using namespace schizo::physics;
    if (!physics_world_ || !ent) return false;
    auto col = ent->GetComponent<schizo::scene::ColliderComponent>();
    auto t   = ent->GetTransform();
    if (!col || !t) return false;
    if (entity_bodies_.count(ent->GetId())) return true;   // already has one

    const glm::vec3 ws = t->GetWorldScale();
    BodyDesc d;
    switch (col->GetShape()) {
        case schizo::scene::ColliderShape::Box:
            d.shape        = ShapeType::Box;
            d.half_extents = col->GetHalfExtents() * ws;
            break;
        case schizo::scene::ColliderShape::Sphere:
            d.shape  = ShapeType::Sphere;
            d.radius = col->GetRadius() * std::max({ws.x, ws.y, ws.z});
            break;
        default:
            return false;   // script-spawnable primitives only
    }
    d.position = t->GetWorldPosition() + col->GetOffset();
    d.rotation = t->GetWorldRotation();
    d.motion   = col->IsDynamic()
                   ? (net_client_mode_ ? MotionType::Kinematic : MotionType::Dynamic)
                   : MotionType::Static;
    d.mass     = col->IsDynamic() ? col->GetMass() : 0.0f;
    const BodyId id = physics_world_->add_body(d);
    if (id == kInvalidBody) return false;
    entity_bodies_.emplace(ent->GetId(), id);
    if (col->IsDynamic()) dynamic_entities_.push_back(ent->GetId());
    return true;
}

void ScenePlaybackManager::RemoveBodyForEntity(uint32_t entity_id) {
    auto it = entity_bodies_.find(entity_id);
    if (it == entity_bodies_.end()) return;
    if (physics_world_) physics_world_->remove_body(it->second);
    entity_bodies_.erase(it);
    dynamic_entities_.erase(
        std::remove(dynamic_entities_.begin(), dynamic_entities_.end(), entity_id),
        dynamic_entities_.end());
}

void ScenePlaybackManager::SyncRemotePlayerBodies(const std::vector<glm::vec3>& positions,
                                                  float dt) {
    using namespace schizo::physics;
    if (!physics_world_ || !is_playing_) return;

    // Grow: one kinematic capsule per remote player (dimensions match the
    // player factory: radius 0.4, cylinder height 1.0 -> 1.8 total).
    while (remote_player_bodies_.size() < positions.size()) {
        BodyDesc d;
        d.shape    = ShapeType::Capsule;
        d.radius   = 0.4f;
        d.height   = 1.0f;
        d.motion   = MotionType::Kinematic;
        d.position = positions[remote_player_bodies_.size()];
        const BodyId id = physics_world_->add_body(d);
        if (id == kInvalidBody) return;
        remote_player_bodies_.push_back(id);
    }
    // Shrink: a player left.
    while (remote_player_bodies_.size() > positions.size()) {
        physics_world_->remove_body(remote_player_bodies_.back());
        remote_player_bodies_.pop_back();
    }
    // Kinematic move (not teleport) so the capsules push dynamic props with
    // proper velocities as remote players walk into them.
    for (size_t i = 0; i < positions.size(); ++i)
        physics_world_->move_kinematic(remote_player_bodies_[i], positions[i],
                                       glm::quat(1.0f, 0.0f, 0.0f, 0.0f), dt);
}

void ScenePlaybackManager::StepPhysics(float delta_time) {
    if (!physics_world_) return;

    // Net client mode: the replication layer owns prop transforms. Push
    // entity -> body BEFORE stepping so contacts (and the player character)
    // resolve against the replicated positions, not stale local-sim ones.
    if (net_client_mode_) {
        for (uint32_t eid : dynamic_entities_) {
            auto it = entity_bodies_.find(eid);
            if (it == entity_bodies_.end()) continue;
            auto ent = scene_ ? scene_->GetEntityById(eid) : nullptr;
            if (!ent) continue;
            auto t = ent->GetTransform();
            if (!t) continue;
            physics_world_->set_transform(it->second,
                                          t->GetWorldPosition(), t->GetWorldRotation());
        }
    }

    // Buoyancy: dynamic bodies inside a PHYSICAL water volume get an upward
    // acceleration proportional to submersion plus velocity drag, so props
    // splash in, bob up, and settle floating. (Skipped on net clients — their
    // props are kinematic mirrors of the host's simulation.)
    if (!net_client_mode_ && !water_volumes_.empty()) {
        for (uint32_t eid : dynamic_entities_) {
            auto it = entity_bodies_.find(eid);
            if (it == entity_bodies_.end()) continue;
            const auto st = physics_world_->get_state(it->second);
            const float level = WaterLevelAt(st.position);
            if (level <= -1e9f || st.position.y >= level) continue;
            const float submerged = glm::clamp((level - st.position.y) / 1.0f, 0.0f, 1.0f);
            glm::vec3 v = st.linear_velocity;
            v.y += delta_time * 9.81f * 1.7f * submerged;              // buoyant lift (> gravity when deep)
            const float drag = glm::min(0.9f, 3.0f * submerged * delta_time);
            v *= (1.0f - drag);                                        // water resistance
            physics_world_->set_linear_velocity(it->second, v);
        }
    }

    physics_world_->step(delta_time);

    // Write simulated dynamic bodies back to their entity transforms.
    // (Skipped on net clients — the replication layer owns those transforms.)
    if (!net_client_mode_)
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
