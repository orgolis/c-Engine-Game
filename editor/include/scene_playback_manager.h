#pragma once

#include <memory>
#include <cstdint>
#include <unordered_map>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::character {
    class CharacterController;
}

namespace schizo::scene {
    class Entity;
    class Scene;
    struct MeshComponent;
}

namespace schizo::physics {
    class PhysicsWorld;   // Jolt-backed (Stage 4)
}

namespace schizo::editor {

/**
 * @class ScenePlaybackManager
 * @brief Manages scene playback with character controller integration
 *
 * Features:
 * - Play/pause/stop scene execution
 * - Player entity detection and setup
 * - Character controller attachment
 * - Uses the authored player camera without imposing a camera mode
 * - Game loop time management
 */
class ScenePlaybackManager {
public:
    ScenePlaybackManager();
    ~ScenePlaybackManager();

    /**
     * Start playing the scene
     * @param scene Scene to play
     * @return true if playback started successfully
     */
    bool StartPlayback(std::shared_ptr<schizo::scene::Scene> scene);

    /**
     * Stop playing the scene
     */
    void StopPlayback();

    /**
     * Where the last BuildPhysicsWorld() got its mesh-collider triangles.
     *
     * Exposed because "play mode got faster" and "play mode stopped parsing
     * OBJ files on the main thread" are different claims, and only the second
     * is the fix. A non-zero disk count means a collider re-read a file in the
     * frame that entered play mode — the 609 ms stall, back.
     */
    size_t LastMeshCollidersFromAsset() const { return last_mesh_from_asset_; }
    size_t LastMeshCollidersFromDisk()  const { return last_mesh_from_disk_; }
    size_t LastMeshCollidersFromMemo()  const { return last_mesh_from_memo_; }

    /**
     * Pause/resume playback
     */
    void SetPaused(bool paused) { is_paused_ = paused; }

    /**
     * Net client mode. Set BEFORE StartPlayback. When enabled, dynamic-collider
     * props are created as Kinematic bodies that FOLLOW their entity transforms
     * (written by the replication layer) instead of being simulated locally.
     * The player is unaffected (still a CharacterVirtual — the client owns it).
     */
    void SetNetClientMode(bool enabled) { net_client_mode_ = enabled; }
    bool IsNetClientMode() const { return net_client_mode_; }

    /**
     * Host-side ghost colliders for REMOTE players: one kinematic capsule per
     * connected player, kinematically moved to its replicated position each
     * frame so remote players push dynamic props in the authoritative
     * simulation (which then replicates back to everyone). Call every frame
     * while playing; pass the CURRENT remote positions (count may change).
     */
    /**
     * Re-express everything play mode remembers in world space after a
     * floating-origin rebase.
     *
     * A rebase moves the scene out from under a running simulation. Four
     * separate caches go stale, and each fails differently:
     *   - the Jolt bodies, which then write their pre-rebase positions back
     *     over the entities -- the dynamic half of the level snaps to the old
     *     origin while the static half moves, and the world tears in two;
     *   - the restore snapshot, so pressing Stop teleports the scene back to
     *     where it was before the rebase;
     *   - the water volumes, so buoyancy and swimming trigger over dry ground;
     *   - the cached camera world position.
     * Only ROOT entities are shifted by a rebase, so only their snapshots are.
     */
    void ApplyOriginShift(const glm::vec3& shift);

    void SyncRemotePlayerBodies(const std::vector<glm::vec3>& positions, float dt);

    // ---- Script-system physics access (Stage 12) ----
    schizo::physics::PhysicsWorld* GetPhysicsWorld() { return physics_world_.get(); }
    /// Jolt BodyId for an entity's rigid body (0xFFFFFFFF if none).
    uint32_t BodyForEntity(uint32_t entity_id) const;
    /// Reverse lookup: which entity owns this body (0 if none).
    uint32_t EntityForBody(uint32_t body_id) const;
    /// Create a live rigid body for an entity spawned DURING play from its
    /// ColliderComponent (Box/Sphere only — the script-spawnable primitives).
    bool AddRuntimeBody(const std::shared_ptr<schizo::scene::Entity>& ent);
    /// Remove an entity's body (entity being destroyed mid-play).
    void RemoveBodyForEntity(uint32_t entity_id);

    // ---- Physical water (terrain-integrated + WaterComponent volumes) ----
    /// Rect water volume gathered at play start from PHYSICAL water only.
    struct WaterVolume {
        glm::vec2 center_xz{0.0f};
        glm::vec2 half_size{0.0f};
        float     level = 0.0f;    // world-space surface height
    };
    /// Water surface height above `pos` (XZ containment), or -FLT_MAX when
    /// not over any physical water volume.
    float WaterLevelAt(const glm::vec3& pos) const;

    /**
     * Update scene during playback (called each frame)
     * @param delta_time Time elapsed since last update
     */
    void Update(float delta_time);

    /**
     * Check if scene is currently playing
     */
    bool IsPlaying() const { return is_playing_; }

    /**
     * Check if scene is paused
     */
    bool IsPaused() const { return is_paused_; }

    /**
     * Get the player entity (if any)
     */
    schizo::scene::Entity* GetPlayerEntity() const { return player_entity_.get(); }

    /**
     * Get the character controller for the player
     */
    engine::character::CharacterController* GetPlayerController() const
    { return player_controller_.get(); }

    /**
     * Get total playback time
     */
    float GetPlaybackTime() const { return playback_time_; }

    /**
     * Get the authored camera used by playback.
     */
    schizo::scene::Entity* GetPlaybackCamera() const { return playback_camera_.get(); }

    /**
     * Legacy editor hook. Intentionally does not change the camera.
     * Camera rigs and view-mode switching belong to the game/project code.
     */
    void ToggleCameraView();

    /**
     * Cursor capture state. While captured, main.cpp hides the OS cursor
     * (GLFW_CURSOR_DISABLED) and tells ImGui to ignore mouse input so the
     * player cannot hover/click editor panels. The game can release the
     * cursor when an in-game GUI opens by calling SetCursorCaptured(false).
     */
    bool IsCursorCaptured() const { return is_cursor_captured_; }
    void SetCursorCaptured(bool captured) { is_cursor_captured_ = captured; }
    void ToggleCursorCaptured() { is_cursor_captured_ = !is_cursor_captured_; }

    /**
     * Mouse delta from the editor host. While the cursor is captured, ImGui
     * is told to ignore the mouse, so the host (main.cpp) reads the raw
     * GLFW position delta and feeds it here for the player look code.
     */
    void OnMouseDelta(float dx, float dy) { mouse_delta_x_ += dx; mouse_delta_y_ += dy; }

private:
    // Mesh-collider triangle provenance from the last BuildPhysicsWorld().
    size_t last_mesh_from_asset_ = 0;
    size_t last_mesh_from_disk_  = 0;
    size_t last_mesh_from_memo_  = 0;

    std::shared_ptr<schizo::scene::Scene> scene_;
    std::shared_ptr<schizo::scene::Entity> player_entity_;
    std::shared_ptr<schizo::scene::Entity> playback_camera_;  // authored camera used by Play
    std::shared_ptr<engine::character::CharacterController> player_controller_;

    bool is_playing_ = false;
    bool is_paused_ = false;
    bool is_cursor_captured_ = false;  // True while the host should hide+lock the OS cursor
    bool net_client_mode_ = false;     // Props follow net-set transforms, no local sim
    float playback_time_ = 0.0f;

    // Mouse delta accumulated by the host between Update() calls (raw GLFW)
    float mouse_delta_x_ = 0.0f;
    float mouse_delta_y_ = 0.0f;

    // External ground check fed into the CharacterController, since the
    // controller's GroundDetector raycast is a stub. Hard-coded floor for now.
    static constexpr float GROUND_LEVEL = 0.0f;
    bool is_on_ground_ = false;

    // Snapshot taken on StartPlayback and replayed on StopPlayback so play
    // testing does not mutate the saved scene. Keyed by Entity::GetId().
    // Only transform state is captured for now — components added/removed
    // during play are NOT undone.
    struct EntitySnapshot {
        glm::vec3 local_position{0.0f};
        glm::quat local_rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 local_scale{1.0f};
    };
    std::unordered_map<uint32_t, EntitySnapshot> entity_snapshots_;

    // Stage-4 physics: a Jolt PhysicsWorld is rebuilt fresh on every
    // StartPlayback from the scene's ColliderComponents. `entity_bodies_` maps
    // entity id -> Jolt BodyId; `dynamic_entities_` is the subset whose
    // simulated transform is written back each step. The player is a Jolt
    // CharacterVirtual (`player_char_id_`), not a rigid body.
    std::unique_ptr<schizo::physics::PhysicsWorld> physics_world_;
    std::unordered_map<uint32_t, uint32_t> entity_bodies_;   // entity id -> BodyId
    std::vector<uint32_t>                  dynamic_entities_; // entity ids w/ dynamic body
    std::vector<uint32_t>                  remote_player_bodies_; // ghost kinematic capsules (BodyIds)
    std::vector<WaterVolume>               water_volumes_;        // physical water (play-time)
    uint32_t                               player_char_id_ = 0xFFFFFFFFu;

    // Cached world position of the playback camera (profiling/rebase use).
    glm::vec3 camera_position_ = glm::vec3(0.0f);

    // Helper methods
    bool FindAndSetupPlayer();
    bool AttachCharacterController();
    void SetupPlaybackCamera();
    void UpdateCamera();
    void UpdateMouseLook();
    void DriveCharacterController(float delta_time);
    void CaptureSceneSnapshot();
    void RestoreSceneSnapshot();
    void BuildPhysicsWorld();
    void TearDownPhysicsWorld();
    void StepPhysics(float delta_time);
};

/// Mesh-collider triangles from the already-loaded MeshAsset. False when no
/// asset is resident, which is when the disk path below is the fallback.
bool collider_triangles_from_asset(const schizo::scene::MeshComponent& mc,
                                   std::vector<glm::vec3>& out);

/// Mesh-collider triangles by re-parsing the file. Kept as the fallback, and
/// exposed so physmesh_check can assert the two sources agree -- a collider
/// silently in the wrong place is far worse than a slow one.
bool collider_triangles_from_disk(const std::string& path, std::vector<glm::vec3>& out);

} // namespace schizo::editor
