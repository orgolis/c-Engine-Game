// lobby_server -- lobby codes and the relay for online games
// (engine/core/network/include/lobby_server.h; docs/systems/ONLINE_PLAY.md).
//
//   lobby_server [--port 7780] [--relay-ports 7781-7899] [--quiet]
//
// Run it on a machine the internet can reach (a cloud VM) with those UDP
// ports open. Games name it in config/multiplayer.ini: lobby_server=<address>:7780.
// One relay port is used per joined player while they play.

#include "lobby_server.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace {

std::atomic<bool> g_running{true};
void on_signal(int) { g_running = false; }

std::string stamp() {
    const std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

int usage() {
    std::fprintf(stderr,
                 "lobby_server -- lobby codes and the relay for GameWorldshaper online games\n"
                 "  --port <n>             control UDP port (default 7780)\n"
                 "  --relay-ports <a>-<b>  relay UDP ports, one per playing guest (default 7781-7899)\n"
                 "  --quiet                no line per event, only the minute summary\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    engine::network::lobby::ServerConfig cfg;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            cfg.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (a == "--relay-ports" && i + 1 < argc) {
            const std::string r = argv[++i];
            const size_t dash = r.find('-');
            if (dash == std::string::npos) return usage();
            cfg.relay_first = static_cast<uint16_t>(std::atoi(r.substr(0, dash).c_str()));
            cfg.relay_last = static_cast<uint16_t>(std::atoi(r.substr(dash + 1).c_str()));
        } else if (a == "--quiet") {
            quiet = true;
        } else {
            return usage();
        }
    }
    if (cfg.port == 0 || cfg.relay_first == 0 || cfg.relay_last < cfg.relay_first ||
        (cfg.port >= cfg.relay_first && cfg.port <= cfg.relay_last))
        return usage();
    cfg.log = [quiet](const std::string& line) {
        if (quiet) return;
        std::printf("%s  %s\n", stamp().c_str(), line.c_str());
        std::fflush(stdout);
    };

    engine::network::lobby::Server server(cfg);
    std::string error;
    if (!server.start(error)) {
        std::fprintf(stderr, "lobby_server: %s\n", error.c_str());
        return 1;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    auto last = std::chrono::steady_clock::now();
    while (g_running) {
        server.step(200);
        if (std::chrono::steady_clock::now() - last > std::chrono::seconds(60)) {
            last = std::chrono::steady_clock::now();
            const auto s = server.stats();
            std::printf("%s  %zu lobbies, %zu relay ports in use, %llu packets / %.1f MB relayed so far\n",
                        stamp().c_str(), s.lobbies, s.relays,
                        static_cast<unsigned long long>(s.relayed_packets), s.relayed_bytes / (1024.0 * 1024.0));
            std::fflush(stdout);
        }
    }
    std::printf("%s  stopped\n", stamp().c_str());
    return 0;
}
