// lobby_check -- lobby codes and the relay (engine/core/network: lobby_protocol.h,
// lobby_server.h, lobby_client.h), end to end on this machine.
//
// A lobby server runs in this process; a host's game transport registers a
// lobby through it and gets a code; joiners turn the code into a relay port
// and connect real ENet transports to it; reliable messages -- one of them
// large enough to be split into many datagrams -- cross the relay both ways.
// Then the errors a player can meet: a wrong code, another game, a full
// lobby, a closed one, and a server that is not there.
//
// Codes without a server (lobby_direct.h): SHA-1 and STUN on their own, then
// a host and players on this PC found through the local network's broadcast,
// connected directly. `lobby_check --internet` also tries the real thing --
// public STUN servers and the BitTorrent DHT -- which needs the internet and
// is not part of the suite.

#include "lobby_client.h"
#include "lobby_direct.h"
#include "lobby_protocol.h"
#include "lobby_server.h"
#include "transport.h"

#include <array>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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


// Poll transports and tick lobby parts until `done` or `ms` passes.
static bool run(std::vector<net::Transport*> ts, const std::function<void(float)>& tick, int ms,
                const std::function<bool()>& done,
                const std::function<void(net::Transport*, const net::NetEvent&)>& on_event = {}) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    auto last = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < end) {
        for (auto* t : ts) {
            net::NetEvent ev;
            while (t->poll(ev, 0))
                if (on_event) on_event(t, ev);
            t->flush();
        }
        const auto now = std::chrono::steady_clock::now();
        tick(std::chrono::duration<float>(now - last).count());
        last = now;
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done();
}

static std::string hex_of(const lobby::Key& k) {
    std::string s;
    char b[3];
    for (auto c : k) { std::snprintf(b, sizeof b, "%02x", c); s += b; }
    return s;
}

static void direct_checks(bool internet) {
    std::printf("\n  -- codes without a lobby server --\n");
    check("sha1: \"abc\" and \"\" give their published digests",
          hex_of(lobby::sha1("abc", 3)) == "a9993e364706816aba3e25717850c26c9cd0d89d" &&
              hex_of(lobby::sha1("", 0)) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    check("dht: a code's host and join keys differ, and differ between games",
          lobby::dht_key("g", "K7Q2X9", "host") != lobby::dht_key("g", "K7Q2X9", "join") &&
              lobby::dht_key("g", "K7Q2X9", "host") != lobby::dht_key("h", "K7Q2X9", "host"));
    // A STUN answer made by hand: XOR-MAPPED-ADDRESS 203.0.113.7:54321.
    const std::array<uint8_t, 12> tid{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    std::string resp{0x01, 0x01, 0x00, 0x0C, 0x21, 0x12, static_cast<char>(0xA4), 0x42};
    resp.append(reinterpret_cast<const char*>(tid.data()), tid.size());
    const uint16_t xport = 54321 ^ 0x2112;
    resp += std::string{0x00, 0x20, 0x00, 0x08, 0x00, 0x01, static_cast<char>(xport >> 8), static_cast<char>(xport & 0xFF)};
    const uint8_t ip4[4] = {203, 0, 113, 7}, cookie[4] = {0x21, 0x12, 0xA4, 0x42};
    for (int i = 0; i < 4; ++i) resp += static_cast<char>(ip4[i] ^ cookie[i]);
    std::string mip, ip2;
    uint16_t mport = 0, p2 = 0;
    const bool parsed = lobby::stun_mapped(resp.data(), resp.size(), tid, mip, mport);
    auto other = tid;
    other[0] ^= 1;
    check("stun: the public address is read from a Binding response, only for our own request",
          parsed && mip == "203.0.113.7" && mport == 54321 && !lobby::stun_mapped(resp.data(), resp.size(), other, ip2, p2));
    check("stun: an answer is told apart from the game's packets and lobby messages",
          lobby::is_stun_response(resp.data(), resp.size()) && !lobby::is_control(resp.data(), resp.size()));
    const std::string probe = lobby::stun_request(tid);
    check("stun: a Binding request is 20 bytes with the magic cookie", probe.size() == 20 && probe[4] == 0x21);

    // ---- this PC, no internet: found by the local network's broadcast ----------
    lobby::DirectConfig local;
    local.internet = false;
    local.lan_port = 27778;
    local.join_timeout = 3.0f;
    auto host = net::create_enet_transport();
    const bool hosting = host->start_server(0, 3);
    lobby::DirectHost dh;
    dh.start(*host, "check.game", 2, "Check", local);   // the host and one player
    run({host.get()}, [&](float dt) { dh.update(dt); }, 2000, [&] { return dh.state() != lobby::DirectHost::State::Opening; });
    check("direct host: open at once with a code, reachable on this network",
          hosting && dh.state() == lobby::DirectHost::State::Open && lobby::normalize_code(dh.code()) == dh.code() &&
              dh.reach() == "local" && host->local_port() != 0);

    auto guest = net::create_enet_transport();
    guest->start_client();
    lobby::DirectJoin dj;
    dj.start(*guest, "check.game", dh.code(), local);
    const auto t0 = std::chrono::steady_clock::now();
    run({host.get(), guest.get()}, [&](float dt) { dh.update(dt); dj.update(dt); }, 4000,
        [&] { return dj.state() != lobby::DirectJoin::State::Searching; });
    const double found_in = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("         (found in %.2f s at %s:%u, via %s)\n", found_in, dj.host().c_str(), dj.port(), dj.via().c_str());
    check("direct join: the code finds the host on this network within a second, at its game port",
          dj.state() == lobby::DirectJoin::State::Found && dj.port() == host->local_port() && dj.via() == "this network" &&
              found_in < 1.5);

    bool host_saw = false, guest_in = false;
    std::vector<uint8_t> got;
    const net::PeerId to_host = guest->connect(dj.host(), dj.port());
    run({host.get(), guest.get()}, [&](float dt) { dh.update(dt); }, 3000, [&] { return host_saw && guest_in; },
        [&](net::Transport* t, const net::NetEvent& ev) {
            if (ev.type == net::NetEvent::Type::Connect) (t == host.get() ? host_saw : guest_in) = true;
        });
    const std::string hello = "hello, directly";
    guest->send(to_host, hello.data(), hello.size());
    run({host.get(), guest.get()}, [&](float dt) { dh.update(dt); }, 2000, [&] { return !got.empty(); },
        [&](net::Transport* t, const net::NetEvent& ev) {
            if (t == host.get() && ev.type == net::NetEvent::Type::Receive) got = ev.data;
        });
    check("direct join: the game connects to the host from the same socket, and a message arrives",
          to_host != net::kInvalidPeer && host_saw && guest_in && std::string(got.begin(), got.end()) == hello);

    auto second = net::create_enet_transport();
    second->start_client();
    lobby::DirectJoin full;
    full.start(*second, "check.game", dh.code(), local);
    run({host.get(), guest.get(), second.get()}, [&](float dt) { dh.update(dt); full.update(dt); }, 4000,
        [&] { return full.state() != lobby::DirectJoin::State::Searching; });
    check("direct join: past the game's players -> \"full\"",
          full.state() == lobby::DirectJoin::State::Failed && full.error().find("full") != std::string::npos);

    // (Each transport outlives the lobby part riding on it: declared first.)
    auto third = net::create_enet_transport();
    third->start_client();
    auto fourth = net::create_enet_transport();
    fourth->start_client();
    lobby::DirectJoin wrong, other_game;
    wrong.start(*third, "check.game", "ZZZZZZ", local);
    run({host.get(), third.get()}, [&](float dt) { dh.update(dt); wrong.update(dt); }, 5000,
        [&] { return wrong.state() != lobby::DirectJoin::State::Searching; });
    check("direct join: a code nobody has -> \"no game with that code\" after the timeout",
          wrong.state() == lobby::DirectJoin::State::Failed && wrong.error().find("No game") != std::string::npos);
    other_game.start(*fourth, "another.game", dh.code(), local);
    run({host.get(), fourth.get()}, [&](float dt) { dh.update(dt); other_game.update(dt); }, 5000,
        [&] { return other_game.state() != lobby::DirectJoin::State::Searching; });
    check("direct join: the right code for another game finds nothing",
          other_game.state() == lobby::DirectJoin::State::Failed);
    dh.stop();
    check("direct host: stopped", dh.state() == lobby::DirectHost::State::Off);

    if (!internet) return;
    // ---- the internet: public STUN servers and the BitTorrent DHT ----------------
    std::printf("\n  -- the internet (STUN + the BitTorrent DHT; this PC plays both sides) --\n");
    lobby::DirectConfig inet;
    inet.lan = false;
    inet.join_timeout = 60.0f;
    auto h2 = net::create_enet_transport();
    h2->start_server(0, 3);
    lobby::DirectHost ih;
    ih.start(*h2, "check.game", 4, "Check", inet);
    run({h2.get()}, [&](float dt) { ih.update(dt); }, 10000, [&] { return ih.state() != lobby::DirectHost::State::Opening; });
    std::printf("         host: code %s, reach '%s', note '%s'\n", ih.code().c_str(), ih.reach().c_str(), ih.note().c_str());
    check("internet: the host learned its public address from STUN", ih.reach() == "internet");
    // Give the host's announcement a few seconds to reach the DHT.
    run({h2.get()}, [&](float dt) { ih.update(dt); }, 8000, [] { return false; });
    auto g2 = net::create_enet_transport();
    g2->start_client();
    lobby::DirectJoin ij;
    ij.start(*g2, "check.game", ih.code(), inet);
    std::string last;
    bool saw_found = false;
    const auto t1 = std::chrono::steady_clock::now();
    run({h2.get(), g2.get()},
        [&](float dt) {
            ih.update(dt);
            ij.update(dt);
            if (ij.progress() != last) {
                last = ij.progress();
                saw_found |= last.find("Found game") != std::string::npos;
                std::printf("         %.1f s: %s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count(), last.c_str());
            }
        },
        62000, [&] { return ij.state() != lobby::DirectJoin::State::Searching; });
    const std::string outcome = ij.state() == lobby::DirectJoin::State::Found ? std::string("found") : "failed: " + ij.error();
    std::printf("         joiner: %s %s:%u via %s; the host heard of %d joiner(s)\n", outcome.c_str(),
                ij.host().c_str(), ij.port(), ij.via().c_str(), ih.joiners_seen());
    if (std::getenv("GWS_LOBBY_DEBUG")) {
        std::printf("--- host ---\n%s--- joiner ---\n%s", ih.debug().c_str(), ij.debug().c_str());
    }
    check("internet: the joiner found the host's address in the DHT", saw_found || ij.state() == lobby::DirectJoin::State::Found);
    check("internet: the host found the joiner's address in the DHT", ih.joiners_seen() > 0);
    std::printf("         (an answer between two games behind the SAME router needs that router to loop back to\n"
                "          its own public address; between two homes it is not needed)\n");
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool internet = argc > 1 && std::strcmp(argv[1], "--internet") == 0;
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

    direct_checks(internet);

    std::printf("\nlobby_check: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) std::printf("lobby_check: ALL OK\n");
    return g_fail == 0 ? 0 : 1;
}
