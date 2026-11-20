#pragma once

// ============================================================================
// Lobby codes, the game's side (lobby_protocol.h has the whole exchange).
//
//   HostLink   a hosting game's lobby: registered from the game's own socket
//              (Transport::send_raw), kept open, and told when a player is
//              coming so it can open its router for that relay port.
//   resolve()  a code to the relay port a joining game connects to.
// ============================================================================

#include "lobby_protocol.h"
#include "transport.h"

#include <cstdint>
#include <memory>
#include <string>

namespace engine::network::lobby {

struct Endpoint {
    std::string host;               // name or dotted IPv4
    uint16_t    port = kDefaultPort;
};
/// "host", "host:port". false when it is neither.
bool parse_endpoint(const std::string& text, Endpoint& out);

class HostLink {
public:
    enum class State { Off, Registering, Open, Failed };

    HostLink();
    ~HostLink();
    HostLink(const HostLink&) = delete;
    HostLink& operator=(const HostLink&) = delete;

    /// Register a lobby for the game hosted on `transport` (started already).
    /// `max_players` counts the host.
    void start(Transport& transport, const Endpoint& server, const std::string& game_id,
               int max_players, const std::string& title);
    /// Call every frame (after the transport was polled): resends, keeps the
    /// lobby open, opens the router for joining players.
    void update(float dt);
    /// Close the lobby (the server forgets the code). Safe when off.
    void stop();

    State              state() const;
    const std::string& code() const;     // set once Open
    const std::string& error() const;    // why it Failed
    int                players() const;  // as the server counts them (the host and its relays)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct JoinResult {
    bool        ok = false;
    std::string relay_host;    // connect the game here ...
    uint16_t    relay_port = 0;   // ... on this port
    std::string error;         // for the player (explain_error), when not ok
};

/// Ask the lobby server for a code's relay port. Blocks up to `timeout_ms`
/// (run it off the frame); resends while waiting (UDP).
JoinResult resolve(const Endpoint& server, const std::string& game_id, const std::string& code,
                   uint32_t timeout_ms = 4000);

/// Is a lobby server answering there? (diagnostics, tests)
bool ping(const Endpoint& server, uint32_t timeout_ms, std::string* error = nullptr);

}  // namespace engine::network::lobby
