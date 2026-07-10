// ====================
// net_check — Stage 7 transport verification CLI
// ====================
//
// Stands up an ENet server + client in one process and verifies a bidirectional
// reliable exchange over loopback UDP: client connects, sends PING, server
// replies PONG. Proves the Transport layer end-to-end.

#include "transport.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace engine::network;

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("net_check: ENet Transport (Stage 7)\n");

    auto server = create_enet_transport();
    auto client = create_enet_transport();
    if (!server->start_server(17777, 8)) { std::printf("  FAIL: start_server\n"); return 1; }
    if (!client->start_client())         { std::printf("  FAIL: start_client\n"); return 1; }

    const PeerId server_peer = client->connect("127.0.0.1", 17777);
    if (server_peer == kInvalidPeer) { std::printf("  FAIL: connect\n"); return 1; }

    // Pump both hosts; track the handshake + the message exchange.
    bool   client_connected = false;
    PeerId client_peer       = kInvalidPeer;   // server's handle for the client
    bool   server_got_ping   = false;
    bool   client_got_pong   = false;
    bool   ping_sent         = false;
    bool   pong_sent         = false;

    for (int i = 0; i < 400 && !client_got_pong; ++i) {
        NetEvent ev;
        // Server side
        while (server->poll(ev, 2)) {
            if (ev.type == NetEvent::Type::Connect) {
                client_peer = ev.peer;
                std::printf("  server: client connected (peer %u)\n", ev.peer);
            } else if (ev.type == NetEvent::Type::Receive) {
                const std::string s(ev.data.begin(), ev.data.end());
                server_got_ping = (s == "PING");
                std::printf("  server: received '%s' on ch%u -> %s\n", s.c_str(), ev.channel,
                            server_got_ping ? "PING OK" : "unexpected");
                const char* pong = "PONG";
                server->send(ev.peer, pong, std::strlen(pong), Channel::Reliable);
                server->flush();
                pong_sent = true;
            }
        }
        // Client side
        while (client->poll(ev, 2)) {
            if (ev.type == NetEvent::Type::Connect) {
                client_connected = true;
                std::printf("  client: connected to server\n");
            } else if (ev.type == NetEvent::Type::Receive) {
                const std::string s(ev.data.begin(), ev.data.end());
                client_got_pong = (s == "PONG");
                std::printf("  client: received '%s' -> %s\n", s.c_str(),
                            client_got_pong ? "PONG OK" : "unexpected");
            }
        }
        // Once connected, fire the PING.
        if (client_connected && !ping_sent) {
            const char* ping = "PING";
            client->send(server_peer, ping, std::strlen(ping), Channel::Reliable);
            client->flush();
            ping_sent = true;
        }
        (void)pong_sent;
    }

    const bool ok = client_connected && (client_peer != kInvalidPeer) &&
                    server_got_ping && client_got_pong;
    std::printf("net_check: connect %d, ping->server %d, pong->client %d -> %s\n",
                client_connected, server_got_ping, client_got_pong, ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
