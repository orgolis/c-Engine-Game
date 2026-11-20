// Lobby codes, the game's side -- see lobby_client.h.

#include <cstdint>
#include <enet/enet.h>

#include "lobby_client.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>

namespace engine::network::lobby {

namespace {

double now_seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// The server's address as dotted IPv4 (what the intercept reports), so its
// datagrams can be told from anyone else's.
std::string resolve_ip(const std::string& host) {
    ENetAddress a{};
    if (enet_address_set_host(&a, host.c_str()) != 0) return {};
    char ip[64] = "";
    enet_address_get_host_ip(&a, ip, sizeof ip);
    return ip;
}

// One request/reply exchange from a socket of its own (a joiner has no game
// socket yet). Resent every 400 ms until a reply or the timeout.
bool request(const Endpoint& server, const std::string& datagram, std::vector<std::string>& reply,
             uint32_t timeout_ms, std::string& error) {
    if (enet_initialize() != 0) { error = "the network library did not start"; return false; }
    struct Deinit { ~Deinit() { enet_deinitialize(); } } deinit;
    ENetAddress to{};
    if (enet_address_set_host(&to, server.host.c_str()) != 0) {
        error = "cannot find the lobby server " + server.host;
        return false;
    }
    to.port = server.port;
    ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL) { error = "cannot open a socket"; return false; }
    struct Close { ENetSocket s; ~Close() { enet_socket_destroy(s); } } close{s};
    const double end = now_seconds() + timeout_ms / 1000.0;
    while (now_seconds() < end) {
        ENetBuffer b;
        b.data = const_cast<char*>(datagram.data());
        b.dataLength = datagram.size();
        enet_socket_send(s, &to, &b, 1);
        const double resend = std::min(end, now_seconds() + 0.4);
        while (now_seconds() < resend) {
            enet_uint32 wait = ENET_SOCKET_WAIT_RECEIVE;
            const auto ms = static_cast<enet_uint32>(std::max(1.0, (resend - now_seconds()) * 1000.0));
            if (enet_socket_wait(s, &wait, ms) != 0 || !(wait & ENET_SOCKET_WAIT_RECEIVE)) continue;
            uint8_t buf[2048];
            ENetAddress from{};
            ENetBuffer in;
            in.data = buf;
            in.dataLength = sizeof buf;
            const int n = enet_socket_receive(s, &from, &in, 1);
            if (n <= 0 || from.host != to.host || from.port != to.port) continue;
            reply = parse(buf, static_cast<size_t>(n));
            if (!reply.empty()) return true;
        }
    }
    error = "The lobby server " + server.host + ":" + std::to_string(server.port) + " did not answer.";
    return false;
}

}  // namespace

bool parse_endpoint(const std::string& text, Endpoint& out) {
    std::string t;
    for (char c : text) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') t += c;
    if (t.empty()) return false;
    const size_t colon = t.rfind(':');
    out = Endpoint{};
    if (colon == std::string::npos) { out.host = t; return true; }
    out.host = t.substr(0, colon);
    const std::string digits = t.substr(colon + 1);
    if (out.host.empty() || digits.empty() || digits.size() > 5 ||
        digits.find_first_not_of("0123456789") != std::string::npos) return false;
    const long p = std::atol(digits.c_str());
    if (p < 1 || p > 65535) return false;
    out.port = static_cast<uint16_t>(p);
    return true;
}

// ---- HostLink ------------------------------------------------------------------

struct HostLink::Impl {
    Transport*  transport = nullptr;
    Endpoint    server;
    std::string server_ip, game_id, title, code, error;
    int         max_players = 2, players = 1;
    State       state = State::Off;
    double      started = 0, last_sent = 0, last_heard = 0;
    struct Punch { int left = 0; double next = 0; bool done = false; };
    std::map<uint16_t, Punch> punches;   // relay port -> opening the router for it

    void send(const std::vector<std::string>& f, uint16_t port) {
        if (!transport) return;
        const std::string d = make(f);
        transport->send_raw(server_ip, port, d.data(), d.size());
    }

    void on_datagram(const std::string& ip, uint16_t from_port, const uint8_t* data, size_t size) {
        if (ip != server_ip) return;   // only the lobby server speaks this
        const auto f = parse(data, size);
        if (f.empty()) return;
        last_heard = now_seconds();
        if (f[0] == "CREATED" && f.size() >= 2 && state == State::Registering) {
            code = f[1];
            state = State::Open;
        } else if (f[0] == "KEPT" && f.size() >= 3 && f[1] == code) {
            players = std::max(1, std::atoi(f[2].c_str()));
        } else if (f[0] == "ERR" && f.size() >= 2) {
            if (state == State::Registering) {
                state = State::Failed;
                error = explain_error(f[1]);
            } else if (f[1] == "unknown" && state == State::Open) {
                // The server forgot the lobby (it restarted, or we were silent
                // too long): a new one -- a new code.
                state = State::Registering;
                code.clear();
                started = now_seconds();
            }
        } else if (f[0] == "PEER" && f.size() >= 3 && f[1] == code) {
            const long p = std::atol(f[2].c_str());
            if (p > 0 && p < 65536) {
                Punch& pu = punches[static_cast<uint16_t>(p)];
                if (!pu.done && pu.left == 0) pu.left = 8;
            }
        } else if (f[0] == "PUNCHED" && f.size() >= 3 && f[1] == code) {
            punches[from_port].done = true;
        }
    }
};

HostLink::HostLink() : impl_(std::make_unique<Impl>()) {}
HostLink::~HostLink() { stop(); }

void HostLink::start(Transport& transport, const Endpoint& server, const std::string& game_id,
                     int max_players, const std::string& title) {
    stop();
    auto& m = *impl_;
    m = Impl{};
    m.transport = &transport;
    m.server = server;
    m.game_id = game_id;
    m.title = title;
    m.max_players = std::clamp(max_players, 2, 16);
    m.server_ip = resolve_ip(server.host);
    if (m.server_ip.empty()) {
        m.state = State::Failed;
        m.error = "Cannot find the lobby server " + server.host + ".";
        return;
    }
    m.state = State::Registering;
    m.started = now_seconds();
    m.last_sent = 0;
    Impl* self = &m;
    transport.set_raw_handler([self](const std::string& ip, uint16_t port, const uint8_t* d, size_t n) {
        self->on_datagram(ip, port, d, n);
    });
}

void HostLink::update(float) {
    auto& m = *impl_;
    const double t = now_seconds();
    if (m.state == State::Registering) {
        if (t - m.started > 6.0) {
            m.state = State::Failed;
            m.error = "The lobby server " + m.server.host + ":" + std::to_string(m.server.port) +
                      " did not answer. (Its address is lobby_server in config/multiplayer.ini.)";
            return;
        }
        if (t - m.last_sent > 0.4) {
            m.send({"CREATE", m.game_id, std::to_string(m.max_players), m.title}, m.server.port);
            m.last_sent = t;
        }
        return;
    }
    if (m.state != State::Open) return;
    if (t - m.last_sent > kKeepSeconds) {
        m.send({"KEEP", m.code}, m.server.port);
        m.last_sent = t;
    }
    // Open the router for each relay port a player was given: a datagram out
    // to it from the game's socket. Resent a few times until the server saw it.
    for (auto& [port, pu] : m.punches) {
        if (pu.done || pu.left <= 0 || t < pu.next) continue;
        m.send({"PUNCH", m.code}, port);
        --pu.left;
        pu.next = t + 0.25;
    }
}

void HostLink::stop() {
    auto& m = *impl_;
    if (m.transport) {
        if (m.state == State::Open) m.send({"CLOSE", m.code}, m.server.port);
        m.transport->set_raw_handler(nullptr);
    }
    m.transport = nullptr;
    m.state = State::Off;
    m.punches.clear();
}

HostLink::State HostLink::state() const { return impl_->state; }
const std::string& HostLink::code() const { return impl_->code; }
const std::string& HostLink::error() const { return impl_->error; }
int HostLink::players() const { return impl_->players; }

// ---- joining ------------------------------------------------------------------------

JoinResult resolve(const Endpoint& server, const std::string& game_id, const std::string& typed,
                   uint32_t timeout_ms) {
    JoinResult r;
    const std::string code = normalize_code(typed);
    if (code.empty()) { r.error = explain_error("code"); return r; }
    std::vector<std::string> reply;
    if (!request(server, make({"JOIN", game_id, code}), reply, timeout_ms, r.error)) return r;
    if (reply[0] == "SLOT" && reply.size() >= 2) {
        const long p = std::atol(reply[1].c_str());
        if (p > 0 && p < 65536) {
            r.ok = true;
            r.relay_host = server.host;
            r.relay_port = static_cast<uint16_t>(p);
            return r;
        }
    }
    r.error = explain_error(reply.size() >= 2 && reply[0] == "ERR" ? reply[1] : "an unexpected answer");
    return r;
}

bool ping(const Endpoint& server, uint32_t timeout_ms, std::string* error) {
    std::vector<std::string> reply;
    std::string err;
    const bool ok = request(server, make({"PING"}), reply, timeout_ms, err) && reply[0] == "PONG";
    if (!ok && error) *error = err.empty() ? "unexpected answer" : err;
    return ok;
}

}  // namespace engine::network::lobby
