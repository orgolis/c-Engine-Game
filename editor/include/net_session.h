#pragma once

// ============================================================
// NetSession — live multiplayer session for the editor (Stage 7 integration)
// ============================================================
//
// Wires the verified Stage-7 net stack (engine/core/network: Transport +
// replication.h's write_snapshot/apply_snapshot) into the editor as a live
// host/join session.
//
//  - Host:  broadcasts a snapshot of every replicated scene entity's world
//           Transform each frame (authoritative).
//  - Client: applies received snapshots onto its own copy of the scene (matched
//           by a stable NetId derived from the entity), so the host's moving
//           objects move on the client.
//
// Phase 1 replicates the SHARED scene (both sides load the same file) — no
// per-client spawning yet. Phase 2 adds server-spawned player avatars + input.
//
// Pimpl so this header pulls in neither EnTT, the ECS headers, nor the net
// headers — main.cpp stays light (mirrors the EcsSceneBridge pattern).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace schizo::scene { class Scene; }

namespace schizo::editor {

enum class NetRole { Offline, Host, Client };

/// Lightweight live status for the Network panel (filled by NetSession).
struct NetSessionStatus {
    NetRole  role          = NetRole::Offline;
    bool     connected     = false;   // client: connected to host / host: >=1 peer
    size_t   peer_count    = 0;
    uint64_t tick          = 0;       // host: last broadcast tick / client: last applied
    uint64_t bytes_sent    = 0;
    uint64_t bytes_recv    = 0;
    size_t   replicated    = 0;       // entities being sent (host) / tracked (client)
    size_t   avatars       = 0;       // networked player avatars visible this frame
    uint64_t my_avatar     = 0;       // this instance's own avatar NetId (0 = none)
    std::string endpoint;             // "host :port" or "ip:port"
};

class NetSession {
public:
    NetSession();
    ~NetSession();

    NetSession(const NetSession&) = delete;
    NetSession& operator=(const NetSession&) = delete;

    // Start hosting on `port` (authoritative). Returns false on failure.
    bool host(uint16_t port);
    // Connect to a host at ip:port as a client. Returns false on failure.
    bool join(const std::string& ip, uint16_t port);
    // Tear the session down (safe to call when already offline).
    void shutdown();

    NetRole role() const { return status_.role; }
    bool    active() const { return status_.role != NetRole::Offline; }
    const NetSessionStatus& status() const { return status_; }

    // Feed this instance's local player for the current frame: the play
    // character's scene entity id (0 if none) + its world position.
    // Client-authoritative: each instance owns its own player; the host relays
    // everyone's positions so all instances render each other as avatars. The
    // entity id lets replication skip the local player (it must never be
    // overwritten by the host's copy of the same scene entity). Call once per
    // frame before tick_frame.
    /**
     * This client's accumulated floating-origin shift.
     *
     * Replicated positions are absolute, and every peer rebases on its OWN
     * camera -- so two clients streaming different parts of the map do not
     * share a coordinate frame at all. Without a conversion, remote players
     * appear displaced by the difference between the two origins, which grows
     * as the session goes on and is worst exactly when players are far apart
     * and least able to tell what is wrong.
     *
     * The wire frame is the un-rebased one, so outgoing positions have this
     * subtracted and incoming positions have it added. Pass
     * EditorWorldStreaming::total_shift() every frame; zero when streaming is
     * off, which is what makes this free for anyone not using it.
     */
    void set_origin_offset(const glm::vec3& shift);
    glm::vec3 origin_offset() const;

    void set_local_player(uint32_t scene_entity_id, const glm::vec3& pos);

    // World positions of every OTHER player's avatar (not our own). The host
    // feeds these to ScenePlaybackManager::SyncRemotePlayerBodies so remote
    // players get ghost colliders in the authoritative simulation.
    void remote_player_positions(std::vector<glm::vec3>& out);

    // Pump the session for one frame.
    //  - Host: place its own + each client's avatar, mirror the scene's moving
    //    props (dynamic-collider entities), broadcast one snapshot.
    //  - Client: send its player position; apply the snapshot — props onto the
    //    matching scene entities (host-authoritative), avatars onto proxies.
    //  - Both: spawn/sync avatar proxy entities for OTHER players (never the
    //    local one — you see the world in first person). Proxies clone the
    //    scene's "Player" entity visuals so remote players use the player model.
    // No-op when offline.
    void tick_frame(const std::shared_ptr<schizo::scene::Scene>& scene, float dt);

private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
    NetSessionStatus status_;
};

}  // namespace schizo::editor
