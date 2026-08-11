// ============================================================================
// GameWorldshaper dedicated server (Stage 7 H3) — headless authoritative sim.
// No renderer / audio / window: just the ECS world + transport + replication
// running at a fixed tick. Deployable as a standalone binary.
//   dedicated_server [--port 7777] [--tickrate 66]
// ============================================================================
#include "dedicated_server.h"
#include "physics/physics_scene.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>
#include <cstdint>

using namespace engine::network;

static std::atomic<bool> g_running{true};
static void on_signal(int) { g_running.store(false); }

// Seed a static ground + a few dynamic cubes so the authoritative world has real
// physics to simulate (and replicate) even before a client connects.
static void seed_world(schizo::ecs::World& w) {
    // Ground: mass<=0 -> Static, a big flat box with its top at y=0.
    {
        const auto g = w.create();
        schizo::ecs::Transform t; t.position = glm::vec3(0.0f, -0.5f, 0.0f);
        w.add<schizo::ecs::Transform>(g, t);
        schizo::ecs::RigidBody rb; rb.mass = 0.0f;               // static
        w.add<schizo::ecs::RigidBody>(g, rb);
        schizo::ecs::Collider col; col.half_extents = glm::vec3(20.0f, 0.5f, 20.0f);
        w.add<schizo::ecs::Collider>(g, col);
    }
    // Dynamic cubes dropped from a height; replicated so clients see them fall.
    uint64_t nid = 9000;
    for (int i = 0; i < 3; ++i) {
        const auto e = w.create();
        w.add<schizo::ecs::NetId>(e, schizo::ecs::NetId{nid++, 0, kNetIdReplicated});
        schizo::ecs::Transform t;
        t.position = glm::vec3(static_cast<float>(i) * 1.2f, 5.0f + static_cast<float>(i), 0.0f);
        w.add<schizo::ecs::Transform>(e, t);
        w.add<schizo::ecs::RigidBody>(e, schizo::ecs::RigidBody{});   // mass 1 -> dynamic
        w.add<schizo::ecs::Collider>(e, schizo::ecs::Collider{});     // 0.5 box
    }
}

int main(int argc, char** argv) {
    uint16_t port = 7777;
    int      tickrate = 66;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if      (a == "--port"     && i + 1 < argc) port     = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (a == "--tickrate" && i + 1 < argc) tickrate = std::atoi(argv[++i]);
    }
    if (tickrate < 1) tickrate = 66;

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    DedicatedServer server;
    if (!server.start(port)) {
        spdlog::error("[server] failed to bind port {}", port);
        return 1;
    }

    // Authoritative Jolt physics stepped inside the fixed tick (network lib stays
    // Jolt-free — the dependency lives here in the binary).
    schizo::physics::PhysicsScene physics;
    physics.init();
    server.set_sim_step([&physics](schizo::ecs::World& w, float dt) { physics.simulate(w, dt); });
    seed_world(server.world());

    spdlog::info("[server] GameWorldshaper dedicated server on :{} @ {} Hz (physics on, Ctrl+C to stop)", port, tickrate);

    const float dt   = 1.0f / static_cast<float>(tickrate);
    const auto  step = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(dt));
    auto     next     = std::chrono::steady_clock::now();
    uint64_t last_log = 0;

    while (g_running.load()) {
        server.tick(dt);

        if (server.tick_count() - last_log >= static_cast<uint64_t>(tickrate)) {  // ~1/s
            last_log = server.tick_count();
            spdlog::info("[server] tick {} | clients {} | entities {}",
                         server.tick_count(), server.client_count(),
                         server.world().count<schizo::ecs::NetId>());
        }

        next += step;
        std::this_thread::sleep_until(next);
    }

    spdlog::info("[server] shutdown after {} ticks ({} clients)", server.tick_count(), server.client_count());
    return 0;
}
