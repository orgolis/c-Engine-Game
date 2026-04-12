#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include "deterministic_simulation.h"
#include "input_synchronizer.h"
#include "network_packet.h"

namespace engine::network {

// Forward declarations
class ServerConnection;

/**
 * @enum NetworkRole
 * @brief Role this client plays in the network
 */
enum class NetworkRole {
    Offline,            // No network connection
    Client,             // Connected to server
    Server,             // Hosting game (future: P2P support)
    LocalMultiplayer    // Multiple local players, same machine
};

/**
 * @class NetworkManager
 * @brief Central network system for multiplayer
 *
 * Manages:
 * - Connection to server
 * - Input sending and synchronization
 * - Game state replication
 * - Latency measurement
 * - Client prediction and server reconciliation
 *
 * Design is client-side prediction focused:
 * - Client processes input immediately (no wait)
 * - Client sends input to server
 * - Server validates and broadcasts results
 * - Client corrects any mismatches
 *
 * This provides responsive feel while maintaining multiplayer integrity
 */
class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    // ============ CONNECTION ============

    /**
     * Connect to a game server
     * @param server_address IP:Port (e.g., "192.168.1.100:7777")
     * @param player_name Local player name
     * @return true if connection initiated (may complete asynchronously)
     */
    bool Connect(const std::string& server_address, const std::string& player_name);

    /**
     * Disconnect from server
     */
    void Disconnect();

    /**
     * Get current connection status
     */
    NetworkRole GetNetworkRole() const { return role_; }

    /**
     * Check if connected and in game
     */
    bool IsConnected() const { return role_ == NetworkRole::Client && is_connected_; }

    /**
     * Get player ID assigned by server
     */
    uint32_t GetPlayerId() const { return player_id_; }

    // ============ INPUT SENDING ============

    /**
     * Send player input for a tick
     * Called once per simulation tick
     *
     * @param tick Tick number
     * @param input Input for this tick
     */
    void SendInput(uint64_t tick, const engine::character::InputAction& input);

    /**
     * Get input synchronizer for access to buffered inputs
     */
    InputSynchronizer& GetInputSynchronizer() { return input_sync_; }
    const InputSynchronizer& GetInputSynchronizer() const { return input_sync_; }

    // ============ GAME STATE ============

    /**
     * Receive and process a game state update from server
     * @param packet Server game state packet
     */
    void ReceiveGameState(const ServerGameStatePacket& packet);

    /**
     * Get last received server tick
     */
    uint64_t GetLastServerTick() const { return last_server_tick_; }

    // ============ LATENCY & STATS ============

    /**
     * Get current estimated latency to server (milliseconds)
     */
    float GetLatencyMs() const { return latency_ms_; }

    /**
     * Get average latency over recent period
     */
    float GetAverageLatencyMs() const { return average_latency_ms_; }

    /**
     * Get packet loss percentage (0.0 - 1.0)
     */
    float GetPacketLoss() const { return packet_loss_; }

    // ============ TIME SYNC ============

    /**
     * Send ping to measure latency and sync clock
     */
    void SendPing();

    /**
     * Process pong response
     */
    void ReceivePong(uint64_t send_time, uint64_t server_time);

    /**
     * Get server time offset (local time - server time)
     */
    int64_t GetServerTimeOffset() const { return server_time_offset_; }

    // ============ UPDATE ============

    /**
     * Update network manager
     * Processes incoming packets, sends queued messages, etc.
     *
     * @param dt Delta time in seconds
     * @param registry ECS registry for entity updates
     */
    void Update(float dt, entt::registry& registry);

private:
    // Connection
    NetworkRole role_ = NetworkRole::Offline;
    bool is_connected_ = false;
    uint32_t player_id_ = 0;
    std::string server_address_;
    std::unique_ptr<ServerConnection> server_;

    // Input and synchronization
    InputSynchronizer input_sync_;
    uint64_t last_tick_sent_ = 0;
    float tick_send_rate_ = 60.0f;  // Send input every tick

    // Game state
    uint64_t last_server_tick_ = 0;
    uint64_t last_state_received_time_ = 0;

    // Latency tracking
    float latency_ms_ = 0.0f;
    float average_latency_ms_ = 0.0f;
    float packet_loss_ = 0.0f;
    uint32_t ping_sequence_ = 0;
    uint64_t last_ping_time_ = 0;

    // Time sync
    int64_t server_time_offset_ = 0;

    // Performance metrics
    struct NetworkStats {
        uint32_t packets_sent = 0;
        uint32_t packets_received = 0;
        uint32_t packets_lost = 0;
        float total_bytes_sent = 0.0f;
        float total_bytes_received = 0.0f;
    } stats_;

    /**
     * Process received packets from server
     */
    void ProcessIncomingPackets();

    /**
     * Send queued input packets
     */
    void SendQueuedInputs(float dt);
};

} // namespace engine::network
