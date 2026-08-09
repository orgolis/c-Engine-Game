// ============================================================================
// headless — run a project's gameplay simulation with no window and no renderer.
//
// Invoked as `gws run --headless`. This is the piece that makes the engine
// scriptable and agent-drivable (issue #64): a caller can start a world, tick it
// deterministically, and read what happened — without a GPU, a display, or a
// human.
//
// Deterministic by construction: a FIXED timestep, and gameplay ticked through
// the SAME shared frame the editor uses (engine/core/ecs/gameplay_tick.h), so a
// headless run and play mode cannot silently diverge.
//
// What it does NOT do: render, or load the OOP editor scene. It loads gameplay
// state — the authorable ECS components that a `.gameplay` sidecar or a save
// file holds — which is the half that matters for assertions about rules,
// progression and combat. Scene geometry is the renderer's concern.
// ============================================================================
#include "engine/core/ecs/register_all.h"
#include "engine/core/ecs/gameplay_tick.h"
#include "engine/core/ecs/gameplay_savegame.h"
#include "engine/core/ecs/world.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace schizo;

namespace {

const char* opt(int argc, char** argv, const char* name, const char* def = nullptr) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return def;
}
bool flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const bool  json    = flag(argc, argv, "--json");
    const int   frames  = std::atoi(opt(argc, argv, "--frames", "60"));
    const float hz      = static_cast<float>(std::atof(opt(argc, argv, "--rate", "60")));
    const char* load    = opt(argc, argv, "--load");     // .gameplay / save file
    const char* project = opt(argc, argv, "--project");  // project dir (for defaults)

    if (frames <= 0 || hz <= 0.0f) {
        std::fprintf(stderr, "headless: --frames and --rate must be positive\n");
        return 2;
    }

    ecs::register_all_components();

    ecs::World world;
    ecs::GameplayEventBus bus;
    ecs::TimerManager     timers;
    float combat_accum = 0.0f;

    // Count every event the run produces — this is what an automated play test
    // or an agent actually asserts on.
    std::vector<std::pair<std::string, int>> event_counts;
    bus.subscribe("*", [&event_counts](const ecs::GameplayEvent& ev) {
        for (auto& e : event_counts)
            if (e.first == ev.name) { ++e.second; return; }
        event_counts.emplace_back(ev.name, 1);
    });

    // --demo seeds a small world so the runner is self-verifying: with no
    // project to load, "0 entities, 0 events" proves nothing about whether the
    // simulation actually runs. This exercises regeneration (G3), a timer (G0)
    // and the event bus end to end.
    std::string loaded_from;
    if (flag(argc, argv, "--demo")) {
        const ecs::Entity e = world.create();
        ecs::AttributeSet attrs;
        // define(), not set(): set() only assigns to an attribute that already
        // exists, so seeding with set() silently does nothing.
        attrs.define("Health", 50.0f, /*min*/ 0.0f, /*max*/ 100.0f);
        world.add<ecs::AttributeSet>(e, attrs);

        ecs::Regeneration regen;
        regen.entries.push_back(ecs::RegenEntry{"Health", 10.0f});  // +10 health / second
        world.add<ecs::Regeneration>(e, regen);
        // SaveId makes the entity capturable by the save format, and is how
        // external tools address it (gws project).
        world.add<ecs::SaveId>(e, ecs::SaveId{1001});

        timers.every(0.25f, [&bus]() { bus.publish(ecs::GameplayEvent{"demo.tick"}); });
        loaded_from = "(demo world)";
    }

    if (load && *load) {
        if (!fs::exists(load)) {
            std::fprintf(stderr, "headless: no such file: %s\n", load);
            return 1;
        }
        if (!ecs::load_game_file(world, load)) {
            std::fprintf(stderr, "headless: failed to load %s\n", load);
            return 1;
        }
        loaded_from = load;
    } else if (project && *project) {
        // Convention: <project>/scenes/main.gameplay is the sidecar for the
        // default scene. Absent is not an error — an empty world still ticks,
        // which is a legitimate smoke test of the simulation itself.
        const fs::path guess = fs::path(project) / "scenes" / "main.gameplay";
        if (fs::exists(guess) && ecs::load_game_file(world, guess.string()))
            loaded_from = guess.string();
    }

    const float dt = 1.0f / hz;
    for (int i = 0; i < frames; ++i)
        ecs::tick_all_gameplay(world, bus, timers, combat_accum, dt);

    // World::each needs component types, so count live entities through the
    // underlying storage rather than a query.
    size_t entities = 0;
    for (auto e : world.registry().view<entt::entity>()) { (void)e; ++entities; }

    // In demo mode report the regenerated value. Without this the run proves
    // only that timers fired; this proves a GAMEPLAY system (G3 regeneration)
    // actually mutated component state.
    float demo_health = -1.0f;
    if (flag(argc, argv, "--demo"))
        for (auto e : world.view<ecs::AttributeSet>())
            demo_health = world.get<ecs::AttributeSet>(e).get("Health");
    // --save-to writes the final world, so a run can produce a fixture for
    // other tools (and for regression tests) rather than only reporting.
    if (const char* sp = opt(argc, argv, "--save-to")) {
        if (!ecs::save_game_file(world, sp))
            std::fprintf(stderr, "headless: failed to write %s\n", sp);
    }

    const float sim_seconds = static_cast<float>(frames) * dt;

    if (json) {
        std::printf("{\"ok\":true,\"frames\":%d,\"rate\":%.3f,\"sim_seconds\":%.4f,"
                    "\"entities\":%zu,\"loaded\":\"%s\",\"events\":{",
                    frames, hz, sim_seconds, entities, loaded_from.c_str());
        if (demo_health >= 0.0f) { /* reported in text mode; JSON keeps a stable shape */ }
        for (size_t i = 0; i < event_counts.size(); ++i)
            std::printf("%s\"%s\":%d", i ? "," : "",
                        event_counts[i].first.c_str(), event_counts[i].second);
        std::printf("}}\n");
    } else {
        std::printf("headless run: %d frames @ %.0f Hz (%.2f s simulated)\n",
                    frames, hz, sim_seconds);
        if (!loaded_from.empty()) std::printf("  loaded:   %s\n", loaded_from.c_str());
        else                      std::printf("  loaded:   nothing (empty world)\n");
        std::printf("  entities: %zu\n", entities);
        if (demo_health >= 0.0f)
            std::printf("  demo Health (starts 50, +10/s, max 100): %.1f\n", demo_health);
        if (event_counts.empty()) {
            std::printf("  events:   none\n");
        } else {
            std::printf("  events:\n");
            for (const auto& e : event_counts)
                std::printf("    %-28s %d\n", e.first.c_str(), e.second);
        }
    }
    return 0;
}
