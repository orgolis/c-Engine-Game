#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace engine::network {

// ============ PACKET TYPES ============

enum class PacketType : uint8_t {
    // Client -> Server
    ClientInput,            // Player input frame
    ClientSpawn,            // Client ready to spawn
    ClientDisconnect,       // Client leaving
    
    // Server -> Client
    ServerGameState,        // Full/delta game state
    ServerInputAck,         // Acknowledge received input
    ServerAbilityActivate,  // Ability execution result
    
    // Bidirectional
    Ping,                   // Latency check
    Pong
};

// ============ COMMON STRUCTURES ============

/**
 * @struct PacketHeader
 * @brief Common header for all network packets
 */
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic = 0xDEADBEEF;    // Packet validity check
    PacketType type;
    uint16_t size;                  // Total packet size including header
    uint32_t sequence;              // Packet sequence number (for loss detection)
};
#pragma pack(pop)

/**
 * @struct EntityState
 * @brief Minimal entity state for network synchronization
 */
#pragma pack(push, 1)
struct EntityState {
    uint32_t entity_id;
    glm::vec3 position;
    glm::vec3 velocity;
    float rotation;                 // Yaw rotation
    uint8_t state_type;            // Character state enum
};
#pragma pack(pop)

// ============ CLIENT -> SERVER PACKETS ============

/**
 * @struct ClientInputPacket
 * @brief Player input for a tick (~12-16 bytes per packet)
 *
 * Sent every tick or every Nth tick depending on network conditions
 * Tick rate: 60Hz = ~16.7ms per tick
 * Bandwidth: 60 packets/sec * 16 bytes = 960 bytes/sec = 7.7 Kbps per player
 */
#pragma pack(push, 1)
struct ClientInputPacket : public PacketHeader {
    uint64_t tick;                      // Simulation tick
    
    // Locomotion (2 bytes)
    float forward;                      // -1.0 to 1.0
    float lateral;                      // -1.0 to 1.0
    
    // Actions (1 byte bitmask)
    uint8_t action_flags;               // jump, sprint, crouch, etc.
    
    // Aim direction (4 bytes, compressed)
    uint16_t aim_yaw;                   // 0-65535 maps to 0-360 degrees
    uint16_t aim_pitch;
    
    // Ability activation
    uint8_t ability_slot;               // 0-9, 255 = none
    uint8_t ability_activation;         // 0=none, 1=start, 2=release
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ClientSpawnPacket : public PacketHeader {
    uint32_t player_id;
    char player_name[32];
    glm::vec3 spawn_position;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ClientDisconnectPacket : public PacketHeader {
    uint32_t player_id;
    uint8_t reason;
};
#pragma pack(pop)

// ============ SERVER -> CLIENT PACKETS ============

/**
 * @struct ServerGameStatePacket
 * @brief Game state update (~200+ bytes depending on entity count)
 *
 * Sent less frequently than input (e.g., 10-20Hz)
 * Bandwidth: 10 packets/sec * 200 bytes = 2000 bytes/sec = 16 Kbps per player
 */
struct ServerGameStatePacket : public PacketHeader {
    uint64_t tick;
    uint32_t entity_count;
    // Followed by entity_count * EntityState structs
    std::vector<EntityState> entities;
};

/**
 * @struct ServerInputAckPacket
 * @brief Server acknowledges received client input
 *
 * Used to confirm which inputs were received, enable client-side prediction cleanup
 */
#pragma pack(push, 1)
struct ServerInputAckPacket : public PacketHeader {
    uint64_t last_confirmed_tick;
    float client_latency_ms;            // Server's measurement of client latency
};
#pragma pack(pop)

/**
 * @struct ServerAbilityActivatePacket
 * @brief Authority notification that an ability was activated
 *
 * Sent by server when ability is authorized
 * Client uses this for ability effects that server controls (damage, healing, etc)
 */
#pragma pack(push, 1)
struct ServerAbilityActivatePacket : public PacketHeader {
    uint32_t ability_id;
    uint32_t caster_id;
    uint32_t target_id;                 // 0 if no target
    glm::vec3 target_position;
    float damage_dealt;
    float heal_applied;
};
#pragma pack(pop)

// ============ BIDIRECTIONAL ============

#pragma pack(push, 1)
struct PingPacket : public PacketHeader {
    uint64_t timestamp;
    uint32_t client_tick_offset;        // Used for clock sync
};
#pragma pack(pop)

#pragma pack(push, 1)
struct PongPacket : public PacketHeader {
    uint64_t client_timestamp;
    uint64_t server_timestamp;
};
#pragma pack(pop)

} // namespace engine::network
