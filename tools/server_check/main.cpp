// ============================================================================
// server_check — verifies the headless dedicated server end-to-end over REAL
// loopback UDP (in-process server + a raw transport client): connect -> avatar
// spawn -> client input -> authoritative apply -> replicate to client ->
// disconnect -> despawn.
// ============================================================================
#include "dedicated_server.h"
#include "transport.h"
#include "replication.h"   // apply_snapshot

#include "ecs/world.h"
#include "ecs/components.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <iostream>
#include <unordered_map>

using namespace engine::network;
namespace ecs = schizo::ecs;

static int g_fail = 0;
static void check(const char* what, bool ok) {
    std::cout << (ok ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!ok) ++g_fail;
}

int main() {
    spdlog::set_level(spdlog::level::warn);
    ecs::register_core_components();

    const uint16_t port = 7913;
    DedicatedServer server;
    check("server starts", server.start(port));

    auto client = create_enet_transport();
    const bool cstart = client && client->start_client();
    check("client starts", cstart);
    const PeerId sp = cstart ? client->connect("127.0.0.1", port) : kInvalidPeer;
    check("client connect() returns a peer", sp != kInvalidPeer);

    const float dt = 1.0f / 66.0f;
    ecs::World client_world;
    std::unordered_map<uint64_t, ecs::Entity> netmap;
    bool connected = false;

    // Service both hosts; apply any snapshots the client receives.
    auto pump = [&](int iters) {
        for (int i = 0; i < iters; ++i) {
            server.tick(dt);
            NetEvent ev;
            while (client->poll(ev, 2)) {
                if (ev.type == NetEvent::Type::Connect)      connected = true;
                else if (ev.type == NetEvent::Type::Receive) apply_snapshot(client_world, netmap, ev.data);
            }
        }
    };

    pump(120);
    check("client connected to server", connected);
    check("server sees exactly 1 client", server.client_count() == 1);
    check("client received a snapshot (world replicated)", netmap.size() == 1);

    const uint64_t nid = server.avatar_netid(sp);
    check("server assigned the client an avatar netid", nid != 0);

    float z_before = 0.0f;
    if (auto it = netmap.find(nid); it != netmap.end() && client_world.has<ecs::Transform>(it->second))
        z_before = client_world.get<ecs::Transform>(it->second).position.z;

    // Send "move forward" (+Z) inputs; the server applies them authoritatively.
    for (uint64_t t = 0; t < 10; ++t) {
        const InputMsg m = make_input_msg(t, 0.0f, 1.0f);
        client->send(sp, &m, sizeof(m), Channel::Reliable);
        client->flush();
        pump(3);
    }
    pump(10);

    float z_server = 0.0f; bool found = false;
    server.world().each<ecs::NetId, ecs::Transform>([&](ecs::Entity, ecs::NetId& n, ecs::Transform& tr) {
        if (n.value == nid) { z_server = tr.position.z; found = true; }
    });
    check("server applied input (authoritative avatar moved +Z)", found && z_server > 0.5f);

    float z_client = z_before;
    if (auto it = netmap.find(nid); it != netmap.end() && client_world.has<ecs::Transform>(it->second))
        z_client = client_world.get<ecs::Transform>(it->second).position.z;
    check("replicated to client (client sees the moved avatar)", z_client > z_before + 0.4f);

    check("server tick advanced", server.tick_count() > 100);
    check("server acked the client input", server.acked_input_tick(sp) >= 0);

    client->disconnect(sp);
    client->flush();
    pump(120);
    check("server despawned the avatar on disconnect", server.client_count() == 0);

    if (g_fail == 0) { std::cout << "server_check: ALL OK\n"; return 0; }
    std::cout << "server_check: " << g_fail << " FAILED\n";
    return 1;
}
