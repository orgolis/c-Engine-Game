// ====================
// repl_check — Stage 7 replication verification CLI
// ====================
//
// A server ECS world with replicated entities (NetId + Transform) broadcasts
// snapshots over the Transport to an initially-EMPTY client ECS world. Verifies
// the client materializes matching entities at matching positions, and that
// moving a server entity is mirrored to the client.

#include "replication.h"

#include "ecs/world.h"
#include "ecs/components.h"

#include <cmath>
#include <cstdio>

namespace ecs = schizo::ecs;
using namespace engine::network;

static schizo::ecs::Entity make_repl_entity(ecs::World& w, uint64_t net_id, glm::vec3 pos) {
    auto e = w.create();
    ecs::NetId nid; nid.value = net_id; nid.flags = kNetIdReplicated;
    w.add<ecs::NetId>(e, nid);
    ecs::Transform t; t.position = pos;
    w.add<ecs::Transform>(e, t);
    return e;
}

// Find the client entity carrying `net_id` and return its position (NaN if absent).
static glm::vec3 client_pos(ecs::World& w, uint64_t net_id) {
    glm::vec3 found(std::nanf(""));
    w.each<ecs::NetId, ecs::Transform>([&](ecs::Entity, ecs::NetId& nid, ecs::Transform& t) {
        if (nid.value == net_id) found = t.position;
    });
    return found;
}

static bool approx(const glm::vec3& a, const glm::vec3& b) {
    return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f && std::fabs(a.z - b.z) < 1e-3f;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("repl_check: ECS replication over Transport (Stage 7)\n");
    ecs::register_core_components();   // so Transform/NetId reflect for serialization

    ecs::World server_world, client_world;
    make_repl_entity(server_world, 1, glm::vec3(1, 2, 3));
    make_repl_entity(server_world, 2, glm::vec3(4, 5, 6));
    auto e3 = make_repl_entity(server_world, 3, glm::vec3(7, 8, 9));

    ReplicationServer server;
    ReplicationClient client;
    if (!server.start(17778, 8))                 { std::printf("  FAIL: server.start\n"); return 1; }
    if (!client.connect("127.0.0.1", 17778))     { std::printf("  FAIL: client.connect\n"); return 1; }

    // Tick the loop: server broadcasts a snapshot, client applies it, until the
    // client has materialized all 3 entities.
    uint64_t tick = 0;
    for (int i = 0; i < 400 && client.entity_count() < 3; ++i) {
        server.broadcast(server_world, tick++);
        client.update(client_world);
    }

    const bool got_all = (client.entity_count() == 3);
    const bool p1 = approx(client_pos(client_world, 1), glm::vec3(1, 2, 3));
    const bool p2 = approx(client_pos(client_world, 2), glm::vec3(4, 5, 6));
    const bool p3 = approx(client_pos(client_world, 3), glm::vec3(7, 8, 9));
    std::printf("  initial sync: %zu/3 entities, positions %d/%d/%d, tick=%llu\n",
                client.entity_count(), p1, p2, p3, (unsigned long long)client.last_tick());

    // Move server entity 3, re-broadcast, confirm the client mirrors it.
    server_world.get<ecs::Transform>(e3).position = glm::vec3(70, 80, 90);
    for (int i = 0; i < 200; ++i) { server.broadcast(server_world, tick++); client.update(client_world); }
    const bool moved = approx(client_pos(client_world, 3), glm::vec3(70, 80, 90));
    std::printf("  update sync: server moved netid 3 -> client now at (%.1f,%.1f,%.1f) -> %s\n",
                client_pos(client_world, 3).x, client_pos(client_world, 3).y,
                client_pos(client_world, 3).z, moved ? "OK" : "FAIL");

    const bool ok = got_all && p1 && p2 && p3 && moved;
    std::printf("repl_check: %s\n", ok ? "ALL OK" : "FAIL");
    return ok ? 0 : 1;
}
