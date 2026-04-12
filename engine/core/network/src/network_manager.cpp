#include "network_manager.h"
#include <spdlog/spdlog.h>

namespace engine::network {

// Stub ServerConnection for now
class ServerConnection {
public:
    bool Connect(const std::string& address) { return true; }
    void Disconnect() {}
    void SendPacket(const void* data, size_t size) {}
    bool ReceivePacket(PacketHeader& header, void* data, size_t max_size) { return false; }
};

NetworkManager::NetworkManager()
    : role_(NetworkRole::Offline),
      is_connected_(false),
      player_id_(0) {
    auto logger = spdlog::get("engine");
    if (logger) logger->debug("NetworkManager created");
}

NetworkManager::~NetworkManager() {
    Disconnect();
}

bool NetworkManager::Connect(const std::string& server_address, const std::string& player_name) {
    if (is_connected_) {
        auto logger = spdlog::get("engine");
        if (logger) logger->warn("Already connected to a server");
        return false;
    }

    server_address_ = server_address;
    server_ = std::make_unique<ServerConnection>();

    if (!server_->Connect(server_address)) {
        auto logger = spdlog::get("engine");
        if (logger) logger->error("Failed to connect to server at {}", server_address);
        server_.reset();
        return false;
    }

    role_ = NetworkRole::Client;
    is_connected_ = true;

    auto logger = spdlog::get("engine");
    if (logger) logger->info("Connected to server: {}", server_address);

    return true;
}

void NetworkManager::Disconnect() {
    if (server_) {
        server_->Disconnect();
        server_.reset();
    }
    role_ = NetworkRole::Offline;
    is_connected_ = false;

    auto logger = spdlog::get("engine");
    if (logger) logger->info("Disconnected from server");
}

void NetworkManager::SendInput(uint64_t tick, const engine::character::InputAction& input) {
    // Buffer input for sending
    input_sync_.BufferLocalInput(input, tick);
    last_tick_sent_ = tick;
}

void NetworkManager::ReceiveGameState(const ServerGameStatePacket& packet) {
    last_server_tick_ = packet.tick;
    last_state_received_time_ = 0;  // In real impl, would track time

    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("Received game state for tick {}, {} entities", packet.tick, packet.entity_count);
    }
}

void NetworkManager::SendPing() {
    if (!is_connected_) return;

    ping_sequence_++;
    last_ping_time_ = 0;  // Should be actual timestamp

    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Ping sent (seq: {})", ping_sequence_);
}

void NetworkManager::ReceivePong(uint64_t send_time, uint64_t server_time) {
    uint64_t now = 0;  // Should be actual timestamp
    latency_ms_ = static_cast<float>(now - send_time) / 2.0f;  // Divide by 2 for round trip
    
    // Update average latency (exponential moving average)
    if (average_latency_ms_ == 0.0f) {
        average_latency_ms_ = latency_ms_;
    } else {
        average_latency_ms_ = average_latency_ms_ * 0.8f + latency_ms_ * 0.2f;
    }

    // Simple time sync (should be more sophisticated in production)
    server_time_offset_ = static_cast<int64_t>(server_time) - static_cast<int64_t>(now);

    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("Latency: {:.2f}ms, Average: {:.2f}ms, Offset: {}ms",
                     latency_ms_, average_latency_ms_, server_time_offset_);
    }
}

void NetworkManager::Update(float dt, entt::registry& registry) {
    if (!is_connected_) return;

    ProcessIncomingPackets();
    SendQueuedInputs(dt);
}

void NetworkManager::ProcessIncomingPackets() {
    if (!server_) return;

    // TODO: Implement packet receiving
    // For now, this is a stub
}

void NetworkManager::SendQueuedInputs(float dt) {
    if (!server_) return;

    // TODO: Implement input sending
    // For now, this is a stub
}

} // namespace engine::network
