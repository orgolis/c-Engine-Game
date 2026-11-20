// The lobby server -- see lobby_server.h and lobby_protocol.h.

#ifdef _WIN32
// One relay socket per joining player, all in one select(): Windows' fd_set
// holds 64 sockets unless told otherwise (before winsock2.h, which enet.h pulls in).
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif
#endif
#include <cstdint>
#include <enet/enet.h>

#include "lobby_server.h"
#include "lobby_protocol.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <random>
#include <vector>

namespace engine::network::lobby {

namespace {

double now_seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool same(const ENetAddress& a, const ENetAddress& b) { return a.host == b.host && a.port == b.port; }

std::string text(const ENetAddress& a) {
    char ip[64] = "?";
    enet_address_get_host_ip(&a, ip, sizeof ip);
    return std::string(ip) + ":" + std::to_string(a.port);
}

ENetSocket open_udp(uint16_t port) {
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL) return s;
    ENetAddress a{};
    a.host = ENET_HOST_ANY;
    a.port = port;
    if (enet_socket_bind(s, &a) < 0) {
        enet_socket_destroy(s);
        return ENET_SOCKET_NULL;
    }
    enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(s, ENET_SOCKOPT_RCVBUF, 1 << 20);
    enet_socket_set_option(s, ENET_SOCKOPT_SNDBUF, 1 << 20);
    return s;
}

}  // namespace

struct Server::Impl {
    ServerConfig cfg;
    bool enet_up = false;
    ENetSocket control = ENET_SOCKET_NULL;
    std::mt19937_64 rng{std::random_device{}()};

    struct Relay {
        ENetSocket  socket = ENET_SOCKET_NULL;
        std::string code;
        ENetAddress host{};      // the host's game socket, as this port sees it (from its PUNCH)
        ENetAddress client{};    // the joiner's game socket (its first packet)
        ENetAddress joiner_control{};   // where its JOIN came from (a resent JOIN gets this port again)
        bool   host_known = false, client_known = false;
        double created = 0, last_activity = 0, last_peer_notice = -1;
        int    peer_notices = 0;
    };
    struct Lobby {
        std::string game_id, title;
        int         max_players = 2;
        ENetAddress host_control{};
        double      last_heard = 0;
    };
    std::map<std::string, Lobby> lobbies;   // by code
    std::map<uint16_t, Relay>   relays;     // by port
    ServerStats counters;

    void log(const std::string& line) const { if (cfg.log) cfg.log(line); }

    void send(ENetSocket s, const ENetAddress& to, const std::string& datagram) {
        ENetBuffer b;
        b.data = const_cast<char*>(datagram.data());
        b.dataLength = datagram.size();
        enet_socket_send(s, &to, &b, 1);
    }

    int relays_of(const std::string& code) const {
        int n = 0;
        for (const auto& [port, r] : relays) n += r.code == code ? 1 : 0;
        return n;
    }

    void close_relay(std::map<uint16_t, Relay>::iterator it, const char* why) {
        log("relay " + std::to_string(it->first) + " of " + it->second.code + " closed (" + why + ")");
        if (it->second.socket != ENET_SOCKET_NULL) enet_socket_destroy(it->second.socket);
        relays.erase(it);
    }

    void close_lobby(const std::string& code, const char* why) {
        for (auto it = relays.begin(); it != relays.end();)
            if (it->second.code == code) { auto next = std::next(it); close_relay(it, why); it = next; }
            else ++it;
        if (lobbies.erase(code)) log("lobby " + code + " closed (" + why + ")");
    }

    void on_control(const ENetAddress& from, const std::vector<std::string>& f) {
        const std::string& verb = f[0];
        if (verb == "PING") {
            send(control, from, make({"PONG", "1"}));
        } else if (verb == "CREATE" && f.size() >= 4) {
            const std::string& game = f[1];
            const int max_players = std::clamp(std::atoi(f[2].c_str()), 2, 16);
            if (game.empty() || game.size() > 64) { send(control, from, make({"ERR", "game"})); return; }
            // A resent CREATE (the reply was lost): the same lobby again.
            for (const auto& [code, l] : lobbies)
                if (same(l.host_control, from) && l.game_id == game) { send(control, from, make({"CREATED", code})); return; }
            if (lobbies.size() >= 4096) { send(control, from, make({"ERR", "busy"})); return; }
            std::string code;
            do code = random_code(rng); while (lobbies.count(code));
            Lobby l;
            l.game_id = game;
            l.title = f[3];
            l.max_players = max_players;
            l.host_control = from;
            l.last_heard = now_seconds();
            lobbies[code] = l;
            log("lobby " + code + " opened by " + text(from) + ": " + game + ", " + f[3] + ", up to " + std::to_string(max_players));
            send(control, from, make({"CREATED", code}));
        } else if (verb == "KEEP" && f.size() >= 2) {
            auto it = lobbies.find(f[1]);
            if (it == lobbies.end()) { send(control, from, make({"ERR", "unknown"})); return; }
            it->second.host_control = from;   // a router may move the host to another port
            it->second.last_heard = now_seconds();
            send(control, from, make({"KEPT", f[1], std::to_string(1 + relays_of(f[1]))}));
        } else if (verb == "CLOSE" && f.size() >= 2) {
            auto it = lobbies.find(f[1]);
            if (it != lobbies.end() && same(it->second.host_control, from)) close_lobby(f[1], "the host left");
        } else if (verb == "JOIN" && f.size() >= 3) {
            const std::string code = normalize_code(f[2]);
            if (code.empty()) { send(control, from, make({"ERR", "code"})); return; }
            auto it = lobbies.find(code);
            if (it == lobbies.end()) { send(control, from, make({"ERR", "unknown"})); return; }
            if (it->second.game_id != f[1]) { send(control, from, make({"ERR", "game"})); return; }
            // A resent JOIN: the port it was given.
            for (const auto& [port, r] : relays)
                if (r.code == code && same(r.joiner_control, from)) { send(control, from, make({"SLOT", std::to_string(port)})); return; }
            if (1 + relays_of(code) >= it->second.max_players) { send(control, from, make({"ERR", "full"})); return; }
            for (uint32_t p = cfg.relay_first; p <= cfg.relay_last; ++p) {
                const uint16_t port = static_cast<uint16_t>(p);
                if (relays.count(port)) continue;
                ENetSocket s = open_udp(port);
                if (s == ENET_SOCKET_NULL) continue;   // taken by something else
                Relay r;
                r.socket = s;
                r.code = code;
                r.joiner_control = from;
                r.created = r.last_activity = now_seconds();
                relays[port] = r;
                log("relay " + std::to_string(port) + " for " + code + " to " + text(from));
                send(control, from, make({"SLOT", std::to_string(port)}));
                notify_host(port);
                return;
            }
            send(control, from, make({"ERR", "ports"}));
        }
    }

    // Tell the host a player is coming to `port`; repeated (UDP) until its PUNCH arrives.
    void notify_host(uint16_t port) {
        auto r = relays.find(port);
        if (r == relays.end() || r->second.host_known) return;
        auto l = lobbies.find(r->second.code);
        if (l == lobbies.end()) return;
        send(control, l->second.host_control, make({"PEER", r->second.code, std::to_string(port)}));
        r->second.last_peer_notice = now_seconds();
        ++r->second.peer_notices;
    }

    void on_relay(uint16_t port, Relay& r, const ENetAddress& from, const uint8_t* data, size_t size) {
        if (is_control(data, size)) {
            const auto f = parse(data, size);
            if (f.size() >= 2 && f[0] == "PUNCH" && f[1] == r.code) {
                if (!r.host_known) log("relay " + std::to_string(port) + ": host at " + text(from));
                r.host = from;
                r.host_known = true;
                r.last_activity = now_seconds();
                send(r.socket, from, make({"PUNCHED", r.code, std::to_string(port)}));
            }
            return;
        }
        ENetBuffer b;
        b.data = const_cast<uint8_t*>(data);
        b.dataLength = size;
        if (r.host_known && same(from, r.host)) {
            if (!r.client_known) return;
            enet_socket_send(r.socket, &r.client, &b, 1);
        } else {
            // The first other sender is the joiner; anyone else is not let through.
            if (!r.client_known) {
                r.client = from;
                r.client_known = true;
                log("relay " + std::to_string(port) + ": player at " + text(from));
            } else if (!same(from, r.client)) {
                return;
            }
            if (!r.host_known) return;   // ENet resends its connect until the host has punched
            enet_socket_send(r.socket, &r.host, &b, 1);
        }
        r.last_activity = now_seconds();
        ++counters.relayed_packets;
        counters.relayed_bytes += size;
    }

    void receive_all(ENetSocket s, const std::function<void(const ENetAddress&, const uint8_t*, size_t)>& fn) {
        uint8_t buf[65536];
        for (int i = 0; i < 4096; ++i) {
            ENetAddress from{};
            ENetBuffer b;
            b.data = buf;
            b.dataLength = sizeof buf;
            const int n = enet_socket_receive(s, &from, &b, 1);
            if (n <= 0) return;   // nothing more (or an ICMP-caused error: try next time)
            fn(from, buf, static_cast<size_t>(n));
        }
    }

    void expire() {
        const double t = now_seconds();
        std::vector<std::string> dead;
        for (const auto& [code, l] : lobbies)
            if (t - l.last_heard > kLobbyTimeout) dead.push_back(code);
        for (const auto& c : dead) close_lobby(c, "the host went quiet");
        for (auto it = relays.begin(); it != relays.end();) {
            auto next = std::next(it);
            Relay& r = it->second;
            if (t - r.last_activity > kSlotTimeout) close_relay(it, "idle");
            else if (!r.host_known && r.peer_notices < 40 && t - r.last_peer_notice > 0.3) notify_host(it->first);
            it = next;
        }
    }
};

Server::Server(ServerConfig config) : impl_(std::make_unique<Impl>()) { impl_->cfg = std::move(config); }

Server::~Server() {
    for (auto& [port, r] : impl_->relays)
        if (r.socket != ENET_SOCKET_NULL) enet_socket_destroy(r.socket);
    if (impl_->control != ENET_SOCKET_NULL) enet_socket_destroy(impl_->control);
    if (impl_->enet_up) enet_deinitialize();
}

bool Server::start(std::string& error) {
    if (!impl_->enet_up) {
        if (enet_initialize() != 0) { error = "the network library did not start"; return false; }
        impl_->enet_up = true;
    }
    impl_->control = open_udp(impl_->cfg.port);
    if (impl_->control == ENET_SOCKET_NULL) {
        error = "cannot open UDP port " + std::to_string(impl_->cfg.port) + " (in use?)";
        return false;
    }
    impl_->log("lobby server on UDP " + std::to_string(impl_->cfg.port) + ", relay ports " +
               std::to_string(impl_->cfg.relay_first) + "-" + std::to_string(impl_->cfg.relay_last));
    return true;
}

void Server::step(uint32_t timeout_ms) {
    auto& m = *impl_;
    if (m.control == ENET_SOCKET_NULL) return;
    ENetSocketSet set;
    ENET_SOCKETSET_EMPTY(set);
    ENET_SOCKETSET_ADD(set, m.control);
    ENetSocket highest = m.control;
    for (auto& [port, r] : m.relays) {
        ENET_SOCKETSET_ADD(set, r.socket);
        highest = std::max(highest, r.socket);
    }
    if (enet_socketset_select(highest, &set, nullptr, timeout_ms) > 0) {
        if (ENET_SOCKETSET_CHECK(set, m.control))
            m.receive_all(m.control, [&](const ENetAddress& from, const uint8_t* d, size_t n) {
                const auto f = parse(d, n);
                if (!f.empty()) m.on_control(from, f);
            });
        std::vector<uint16_t> ready;
        for (auto& [port, r] : m.relays)
            if (ENET_SOCKETSET_CHECK(set, r.socket)) ready.push_back(port);
        for (uint16_t port : ready) {
            auto it = m.relays.find(port);
            if (it == m.relays.end()) continue;
            m.receive_all(it->second.socket, [&](const ENetAddress& from, const uint8_t* d, size_t n) {
                m.on_relay(port, it->second, from, d, n);
            });
        }
    }
    m.expire();
}

ServerStats Server::stats() const {
    ServerStats s = impl_->counters;
    s.lobbies = impl_->lobbies.size();
    s.relays = impl_->relays.size();
    return s;
}

}  // namespace engine::network::lobby
