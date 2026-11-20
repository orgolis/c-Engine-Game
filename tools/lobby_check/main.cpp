// lobby_check -- lobby codes and the relay (engine/core/network: lobby_protocol.h,
// lobby_server.h, lobby_client.h), end to end on this machine.
//
// A lobby server runs in this process; a host's game transport registers a
// lobby through it and gets a code; joiners turn the code into a relay port
// and connect real ENet transports to it; reliable messages -- one of them
// large enough to be split into many datagrams -- cross the relay both ways.
// Then the errors a player can meet: a wrong code, another game, a full
// lobby, a closed one, and a server that is not there.

#include "lobby_client.h"
#include "lobby_protocol.h"
#include "lobby_server.h"
#include "transport.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace lobby = engine::network::lobby;
namespace net = engine::network;

static int g_pass = 0, g_fail = 0;
static void check(const char* what, bool ok) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    (ok ? g_pass : g_fail)++;
}

// Poll transports (and the host link) until `done` or `ms` passes.
static bool pump(std::vector<net::Transport*> ts, lobby::HostLink* link, int ms,
                 const std::function<bool(net::Transport*, const net::NetEvent&)>& on_event,
                 const std::function<bool()>& done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        for (auto* t : ts) {
            net::NetEvent ev;
            while (t->poll(ev, 0))
                if (on_event) on_event(t, ev);
            t->flush();
        }
        if (link) link->update(0.01f);
        if (done && done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done ? done() : true;
}

int main() {
    std::printf("lobby_check\n");

    // ---- the protocol on its own -------------------------------------------
    {
        const std::string d = lobby::make({"CREATE", "my.game", "4", "Raid\twith a tab"});
        const auto f = lobby::parse(d.data(), d.size());
        check("protocol: a datagram reads back field for field (a tab inside a field becomes a space)",
              f.size() == 4 && f[0] == "CREATE" && f[1] == "my.game" && f[2] == "4" && f[3] == "Raid with a tab");
        const uint8_t enet_like[] = {0x8F, 0xFF, 0x00, 0x01, 0x82, 0x40, 0x00, 0x00, 0x01, 0x02, 0x03};
        check("protocol: a game (ENet) packet is not taken for a control message",
              !lobby::is_control(enet_like, sizeof enet_like) && lobby::parse(enet_like, sizeof enet_like).empty());
        check("protocol: a code typed in lower case, with dashes or spaces, is the same code",
              lobby::normalize_code("k7q-2x9") == "K7Q2X9" && lobby::normalize_code(" K7Q 2X9 ") == "K7Q2X9");
        check("protocol: what cannot be a code is refused (wrong length, 0 / O / 1 / I)",
              lobby::normalize_code("K7Q2X") .empty() && lobby::normalize_code("K7Q2X90").empty() &&
              lobby::normalize_code("O7Q2X9").empty() && lobby::normalize_code("17Q2X9").empty());
        std::mt19937_64 rng(7);
        bool all_valid = true;
        for (int i = 0; i < 200; ++i) all_valid &= lobby::normalize_code(lobby::random_code(rng)).size() == 6;
        check("protocol: every generated code is a valid code", all_valid);
        lobby::Endpoint e;
        check("endpoint: host:port, host alone (default port), bad ports refused",
              lobby::parse_endpoint("lobby.example.com:7790", e) && e.host == "lobby.example.com" && e.port == 7790 &&
              lobby::parse_endpoint("10.0.0.5", e) && e.port == lobby::kDefaultPort &&
              !lobby::parse_endpoint("host:0", e) && !lobby::parse_endpoint("host:99999", e) && !lobby::parse_endpoint("", e));
    }

    // ---- a server here ------------------------------------------------------
    const uint16_t base = static_cast<uint16_t>(
        30000 + std::chrono::steady_clock::now().time_since_epoch().count() % 20000);
    lobby::ServerConfig cfg;
    cfg.port = base;
    cfg.relay_first = static_cast<uint16_t>(base + 1);
    cfg.relay_last = static_cast<uint16_t>(base + 8);
    lobby::Server server(cfg);
    std::string error;
    const bool started = server.start(error);
    check("server: opens its control port", started);
    if (!started) { std::printf("    %s\n", error.c_str()); return 1; }
    std::atomic<bool> running{true};
    std::thread server_thread([&] { while (running) server.step(10); });
    const lobby::Endpoint at{"127.0.0.1", base};

    check("ping: the server answers", lobby::ping(at, 2000));

    // The host: a game transport, and its lobby registered from the same socket.
    auto host = net::create_enet_transport();
    check("host: its game transport starts on any free port", host->start_server(0, 4));
    lobby::HostLink link;
    link.start(*host, at, "check.game", 3, "Check raid");
    pump({host.get()}, &link, 3000, nullptr, [&] { return link.state() != lobby::HostLink::State::Registering; });
    check("host: the lobby is open with a 6-character code",
          link.state() == lobby::HostLink::State::Open && lobby::normalize_code(link.code()) == link.code());
    std::printf("    code %s\n", link.code().c_str());

    // Mistakes a player can make.
    check("join: a code nobody has -> \"no game with that code\"",
          [&] { auto r = lobby::resolve(at, "check.game", "ZZZZZZ", 2000); return !r.ok && r.error.find("No game") != std::string::npos; }());
    check("join: another game's code -> refused as another game",
          [&] { auto r = lobby::resolve(at, "other.game", link.code(), 2000); return !r.ok && r.error.find("another game") != std::string::npos; }());
    check("join: not a code at all -> refused before asking",
          [&] { auto r = lobby::resolve(at, "check.game", "hello", 2000); return !r.ok && r.error.find("not a lobby code") != std::string::npos; }());

    // A player joins with the code (typed in lower case with a dash).
    std::string typed = link.code();
    for (char& c : typed) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    typed.insert(3, "-");
    const auto slot = lobby::resolve(at, "check.game", typed, 3000);
    check("join: the code gives a relay port on the server", slot.ok && slot.relay_port >= cfg.relay_first && slot.relay_port <= cfg.relay_last);

    auto guest = net::create_enet_transport();
    guest->start_client();
    const net::PeerId to_host = guest->connect(slot.relay_host, slot.relay_port);
    net::PeerId host_side = net::kInvalidPeer;
    bool guest_connected = false;
    std::vector<uint8_t> host_got, guest_got;
    auto on_event = [&](net::Transport* t, const net::NetEvent& ev) {
        if (ev.type == net::NetEvent::Type::Connect) {
            if (t == host.get()) host_side = ev.peer;
            else guest_connected = true;
        } else if (ev.type == net::NetEvent::Type::Receive) {
            (t == host.get() ? host_got : guest_got) = ev.data;
        }
        return true;
    };
    pump({host.get(), guest.get()}, &link, 5000, on_event,
         [&] { return host_side != net::kInvalidPeer && guest_connected; });
    check("relay: the player's game connects to the host through the server", host_side != net::kInvalidPeer && guest_connected);

    const std::string hello = "hello from the guest";
    guest->send(to_host, hello.data(), hello.size());
    std::vector<uint8_t> big(100 * 1024);
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 31 + 7);
    pump({host.get(), guest.get()}, &link, 3000, on_event, [&] { return !host_got.empty(); });
    host->send(host_side, big.data(), big.size());
    pump({host.get(), guest.get()}, &link, 5000, on_event, [&] { return guest_got.size() == big.size(); });
    check("relay: a message from the player reaches the host intact",
          std::string(host_got.begin(), host_got.end()) == hello);
    check("relay: 100 KB from the host (many datagrams) reaches the player intact", guest_got == big);

    // A second player fills the lobby (3 players: the host and two); a third is refused.
    const auto slot2 = lobby::resolve(at, "check.game", link.code(), 3000);
    check("join: a second player gets a relay port of their own", slot2.ok && slot2.relay_port != slot.relay_port);
    const auto slot3 = lobby::resolve(at, "check.game", link.code(), 3000);
    check("join: past the lobby's players -> \"full\"", !slot3.ok && slot3.error.find("full") != std::string::npos);

    // The host keeps the lobby open past the server's timeout... (shortened: one keep)
    pump({host.get(), guest.get()}, &link, 4500, on_event, nullptr);
    check("host: still open after a keep-alive, and the server counts its players", link.state() == lobby::HostLink::State::Open && link.players() >= 2);

    // ...and closes it on leaving.
    const std::string code = link.code();
    link.stop();
    host->flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check("host: after it left, its code finds nothing",
          [&] { auto r = lobby::resolve(at, "check.game", code, 2000); return !r.ok && r.error.find("No game") != std::string::npos; }());

    running = false;
    server_thread.join();
    const auto st = server.stats();
    check("server: relayed the game's packets and freed the closed lobby", st.relayed_packets > 10 && st.lobbies == 0 && st.relays == 0);

    // No server there: the player is told so, within the timeout.
    const auto t0 = std::chrono::steady_clock::now();
    const auto none = lobby::resolve({"127.0.0.1", static_cast<uint16_t>(base + 20)}, "check.game", "K7Q2X9", 1200);
    const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    check("join: no server -> \"did not answer\", after the timeout and not much later",
          !none.ok && none.error.find("did not answer") != std::string::npos && waited < 2.5);

    std::printf("\nlobby_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("lobby_check: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
