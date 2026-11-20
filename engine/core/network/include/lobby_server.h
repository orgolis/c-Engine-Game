#pragma once

// ============================================================================
// The lobby server: lobby codes and the relay (lobby_protocol.h). One control
// UDP port; one relay UDP port per joining player, from a range. Single
// threaded: step() waits for datagrams on every socket, handles them, and
// closes what has gone quiet. tools/lobby_server is the program around it;
// tools/lobby_check runs one in-process.
// ============================================================================

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace engine::network::lobby {

struct ServerConfig {
    uint16_t port = 7780;          // control (CREATE / JOIN / KEEP ...)
    uint16_t relay_first = 7781;   // relay ports: one per joining player
    uint16_t relay_last = 7899;
    std::function<void(const std::string&)> log;   // one line per event (optional)
};

struct ServerStats {
    size_t   lobbies = 0;
    size_t   relays = 0;           // relay ports in use
    uint64_t relayed_packets = 0;
    uint64_t relayed_bytes = 0;
};

class Server {
public:
    explicit Server(ServerConfig config);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Open the control port. false + `error` when it cannot.
    bool start(std::string& error);
    /// Wait up to `timeout_ms` for datagrams, handle all that came, expire.
    void step(uint32_t timeout_ms);
    ServerStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::network::lobby
