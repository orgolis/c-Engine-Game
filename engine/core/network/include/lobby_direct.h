#pragma once

// ============================================================================
// Lobby codes without a lobby server: the host's game and a joining game find
// each other and then talk directly. (lobby_client.h is the other way: through
// a lobby server that relays; it gets through every network but needs one.)
//
//   Address  Each game asks a public STUN server how the internet sees its game
//            socket: the router's public address and the port it gave that
//            socket.
//   Finding  The host puts that address in the BitTorrent DHT -- the public,
//            server-less address book torrent programs share; only an address
//            under a key, never files -- the key made from the game and the
//            code. A joiner looks the key up and puts its own address under
//            the code's "join" key, which the host keeps asking for.
//            On the same network or the same PC a broadcast finds the host at
//            once, without any of that.
//   Joining  Both games send to each other's public address (UDP hole
//            punching): each router sees the other game's packets as answers
//            to its own game's and lets them in. The joiner's game asks to
//            join (DJOIN), the host says yes (DSLOT) or why not (DERR), and the
//            joiner's ENet connects from that same socket.
//
// It works through most home routers (they keep a socket's public port the
// same whatever it sends to). Two networks that change the port for every
// destination ("symmetric" NAT: some mobile and carrier networks) cannot meet
// this way -- a lobby server gets through those.
//
// Messages on the game socket are lobby control datagrams (lobby_protocol.h):
//   joiner -> host   DJOIN  game code nonce
//   host -> joiner   DSLOT  code nonce          | DERR code reason
//   host -> joiner   DPUNCH code                 (opens the host's router)
// and on the local network (kLanPort):
//   joiner -> all    FIND   game code
//   host -> joiner   HERE   code game-port
// ============================================================================

#include "transport.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::network::lobby {

inline constexpr uint16_t kLanPort = 7778;   // the local network's "who has this code?"

struct DirectConfig {
    // Public STUN servers (free, no account): two are asked, to tell a router
    // that changes ports per destination from one that keeps them.
    std::vector<std::string> stun_servers{"stun.l.google.com:19302", "stun1.l.google.com:19302",
                                          "stun.cloudflare.com:3478"};
    // Well-known DHT entry points; any one answering is enough.
    std::vector<std::string> dht_bootstrap{"router.bittorrent.com:6881", "dht.transmissionbt.com:6881",
                                           "router.utorrent.com:6881", "dht.libtorrent.org:25401"};
    bool     internet = true;      // STUN and the DHT; off: the local network only
    bool     lan = true;           // the broadcast on the local network (and this PC)
    uint16_t lan_port = kLanPort;
    float    join_timeout = 30.0f; // seconds a joiner looks before it gives up
};

/// A hosting game's code. Runs on the game's own socket (Transport::send_raw
/// and set_raw_handler) plus a socket of its own for the DHT and one for the
/// local network.
class DirectHost {
public:
    enum class State { Off, Opening, Open, Failed };

    DirectHost();
    ~DirectHost();
    DirectHost(const DirectHost&) = delete;
    DirectHost& operator=(const DirectHost&) = delete;

    /// `transport` is hosting already (start_server). `max_players` counts the host.
    void start(Transport& transport, const std::string& game_id, int max_players,
               const std::string& title, const DirectConfig& config = {});
    /// Every frame, after the transport was polled.
    void update(float dt);
    void stop();

    State              state() const;
    const std::string& code() const;    // from start()
    const std::string& error() const;   // why it Failed
    /// Once Open: "internet" (its address went out, friends anywhere can look
    /// it up), or "local" (no public address: this network only).
    const std::string& reach() const;
    /// A line for the host when something limits who can join ("" when nothing does).
    const std::string& note() const;
    /// Joiners it has heard of (in the DHT or asking to join): diagnostics.
    int joiners_seen() const;
    std::string debug() const;   // its public address and what the DHT said (diagnostics)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// A joining game looking for a code. The transport is a client that has not
/// connected yet (start_client): once Found, connect it to host():port().
class DirectJoin {
public:
    enum class State { Off, Searching, Found, Failed };

    DirectJoin();
    ~DirectJoin();
    DirectJoin(const DirectJoin&) = delete;
    DirectJoin& operator=(const DirectJoin&) = delete;

    void start(Transport& transport, const std::string& game_id, const std::string& code,
               const DirectConfig& config = {});
    void update(float dt);
    void stop();

    State              state() const;
    const std::string& host() const;    // Found: connect here ...
    uint16_t           port() const;    // ... on this port
    const std::string& via() const;     // Found: "this network" or "the internet"
    const std::string& error() const;   // Failed: why, for the player
    const std::string& progress() const;   // while Searching: what it is doing, for the player
    std::string debug() const;   // its public address and what the DHT said (diagnostics)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---- parts, for the tests ------------------------------------------------------
using Key = std::array<uint8_t, 20>;
Key sha1(const void* data, size_t size);
/// The DHT key of a code: "host" (where the host's address is) or "join".
Key dht_key(const std::string& game_id, const std::string& code, const char* role);
/// A STUN Binding request with this transaction id (20 bytes).
std::string stun_request(const std::array<uint8_t, 12>& tid);
/// The public address in a STUN Binding response to `tid` ("a.b.c.d", port).
bool stun_mapped(const void* data, size_t size, const std::array<uint8_t, 12>& tid,
                 std::string& ip, uint16_t& port);

}  // namespace engine::network::lobby
