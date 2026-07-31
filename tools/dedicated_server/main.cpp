// ============================================================================
// GameWorldshaper dedicated server (Stage 7 H3) — headless authoritative sim.
// No renderer / audio / window: just the ECS world + transport + replication
// running at a fixed tick. Deployable as a standalone binary.
//   dedicated_server [--port 7777] [--tickrate 66]
// ============================================================================
#include "dedicated_server.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string>
#include <thread>

using namespace engine::network;

static std::atomic<bool> g_running{true};
static void on_signal(int) { g_running.store(false); }

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
    spdlog::info("[server] GameWorldshaper dedicated server on :{} @ {} Hz (Ctrl+C to stop)", port, tickrate);

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
