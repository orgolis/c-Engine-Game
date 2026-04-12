#include "deterministic_simulation.h"
#include <spdlog/spdlog.h>

namespace engine::network {

DeterministicSimulation::DeterministicSimulation() {
    auto logger = spdlog::get("engine");
    if (logger) {
        logger->debug("DeterministicSimulation created - Tick Rate: {}Hz, Timestep: {}ms",
                     TICK_RATE, FIXED_TIMESTEP * 1000.0f);
    }
}

void DeterministicSimulation::Update(float real_delta_time, entt::registry& registry) {
    // Accumulate real time
    accumulated_time_ += real_delta_time;

    // Process all complete fixed ticks
    ticks_processed_this_frame_ = 0;
    while (accumulated_time_ >= FIXED_TIMESTEP) {
        FixedTick(registry);
        accumulated_time_ -= FIXED_TIMESTEP;
        ticks_processed_this_frame_++;

        // Safety: don't process too many ticks in one frame (game is falling behind)
        if (ticks_processed_this_frame_ > 4) {
            auto logger = spdlog::get("engine");
            if (logger) {
                logger->warn("Processing more than 4 ticks per frame - game falling behind!");
                logger->warn("Accumulated time: {}ms, skipping rest", accumulated_time_ * 1000.0f);
            }
            accumulated_time_ = 0.0f;  // Skip frame to catch up
            break;
        }
    }
}

void DeterministicSimulation::FixedTick(entt::registry& registry) {
    // Call virtual method for subclass implementation
    ProcessFixedTick(registry, FIXED_TIMESTEP);
    
    // Increment tick counter
    current_tick_++;
}

void DeterministicSimulation::Reset() {
    current_tick_ = 0;
    accumulated_time_ = 0.0f;
    ticks_processed_this_frame_ = 0;

    auto logger = spdlog::get("engine");
    if (logger) logger->debug("DeterministicSimulation reset");
}

} // namespace engine::network
