// ============================================================================
// server_check — verifies the headless dedicated server end-to-end over REAL
// loopback UDP (in-process server + a raw transport client): connect -> avatar
// spawn -> client input -> authoritative apply -> replicate to client ->
// disconnect -> despawn.
// ============================================================================
#include "dedicated_server.h"
#include "transport.h"
#include "replication.h"   // apply_snapshot
#include "physics/physics_scene.h"

#include "ecs/world.h"
#include "ecs/components.h"

#include <spdlog/spdlog.h>

#include <cmath>
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

// Seed a static ground (top at y=0) + one dynamic box dropped from y=5, replicated
// under `body_nid`. A 0.5-half-extent box comes to rest with its centre at y=0.5.
static ecs::Entity seed_falling_body(ecs::World& w, uint64_t body_nid) {
    {
        const auto g = w.create();
        ecs::Transform t; t.position = glm::vec3(0.0f, -0.5f, 0.0f);
        w.add<ecs::Transform>(g, t);
        ecs::RigidBody rb; rb.mass = 0.0f;               // static ground
        w.add<ecs::RigidBody>(g, rb);
        ecs::Collider col; col.half_extents = glm::vec3(20.0f, 0.5f, 20.0f);
        w.add<ecs::Collider>(g, col);
    }
    const auto b = w.create();
    w.add<ecs::NetId>(b, ecs::NetId{body_nid, 0, kNetIdReplicated});
    ecs::Transform t; t.position = glm::vec3(0.0f, 5.0f, 0.0f);
    w.add<ecs::Transform>(b, t);
    w.add<ecs::RigidBody>(b, ecs::RigidBody{});          // mass 1 -> dynamic
    w.add<ecs::Collider>(b, ecs::Collider{});            // 0.5 box
    return b;
}

int main() {
    spdlog::set_level(spdlog::level::warn);
    ecs::register_core_components();

    const uint16_t port = 7913;
    DedicatedServer server;
    check("server starts", server.start(port));

    // Install authoritative physics inside the server tick + seed a falling body
    // (replicated) BEFORE any client connects, so the first snapshots carry it.
    schizo::physics::PhysicsScene physics;
    check("physics scene init", physics.init());
    server.set_sim_step([&physics](ecs::World& w, float dt) { physics.simulate(w, dt); });
    const uint64_t body_nid = 5000;
    const ecs::Entity body  = seed_falling_body(server.world(), body_nid);

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
    check("client received the replicated world", netmap.size() >= 1);
    check("client received the seeded physics body", netmap.count(body_nid) == 1);

    const uint64_t nid = server.avatar_netid(sp);
    check("server assigned the client an avatar netid", nid != 0);
    check("client received its avatar", netmap.count(nid) == 1);

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

    // ---- Physics stepped INSIDE the server tick (via the sim-step hook) ----
    float body_y_server = 999.0f;
    if (server.world().valid(body) && server.world().has<ecs::Transform>(body))
        body_y_server = server.world().get<ecs::Transform>(body).position.y;
    check("physics ran in server tick (body fell from y=5)", body_y_server < 4.0f);
    check("physics settled body on the ground (~y=0.5)", body_y_server > 0.3f && body_y_server < 0.7f);

    float body_y_client = 999.0f;
    if (auto it = netmap.find(body_nid); it != netmap.end() && client_world.has<ecs::Transform>(it->second))
        body_y_client = client_world.get<ecs::Transform>(it->second).position.y;
    check("settled body replicated to client", std::fabs(body_y_client - body_y_server) < 0.2f);

    // Determinism: two isolated sims of the same seed land bit-identically —
    // the property netcode rollback/reconciliation relies on.
    auto isolated_final_y = []() {
        ecs::World w;
        schizo::physics::PhysicsScene p; p.init();
        const ecs::Entity b = seed_falling_body(w, 1);
        for (int i = 0; i < 200; ++i) p.simulate(w, 1.0f / 66.0f);
        return w.get<ecs::Transform>(b).position.y;
    };
    const float y1 = isolated_final_y();
    const float y2 = isolated_final_y();
    check("physics deterministic (two isolated sims bit-identical)", y1 == y2);

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
