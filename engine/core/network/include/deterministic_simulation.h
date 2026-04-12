#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace engine::network {

/**
 * @class DeterministicSimulation
 * @brief Wraps the game loop for deterministic, networked execution
 *
 * Critical for multiplayer:
 * - All clients run at same fixed timestep (no variable delta)
 * - Same inputs = same results (bit-perfect replay)
 * - Allows rollback and fast-forward for network correction
 * - Enables server to verify client actions
 *
 * The simulation runs at a fixed TICK_RATE (typically 60Hz) regardless of frame rate.
 * Multiple fixed ticks may be processed per visual frame.
 */
class DeterministicSimulation {
public:
    // Network tick rate - independent of graphics frame rate
    static constexpr float TICK_RATE = 60.0f;          // Hz
    static constexpr float FIXED_TIMESTEP = 1.0f / TICK_RATE;  // ~16.67ms

    DeterministicSimulation();
    ~DeterministicSimulation() = default;

    /**
     * Update the simulation
     * Processes all pending fixed ticks based on accumulated real time
     *
     * @param real_delta_time Real time elapsed since last Update (variable)
     * @param registry The ECS registry to update
     */
    void Update(float real_delta_time, entt::registry& registry);

    /**
     * Manually tick the simulation once
     * For testing and deterministic replay
     *
     * @param registry The ECS registry to update
     */
    void FixedTick(entt::registry& registry);

    /**
     * Get current simulation tick number
     * Can be used as unique identifier for network packets
     */
    uint64_t GetCurrentTick() const { return current_tick_; }

    /**
     * Get the accumulated time that hasn't been processed yet
     * Useful for interpolation between ticks
     */
    float GetAccumulatedTime() const { return accumulated_time_; }

    /**
     * Get how many fixed ticks were processed this frame
     */
    uint8_t GetTicksProcessedThisFrame() const { return ticks_processed_this_frame_; }

    /**
     * Reset simulation to tick 0
     */
    void Reset();

    /**
     * Check if running in deterministic mode
     */
    bool IsDeterministic() const { return true; }

    /**
     * Get interp factor (0.0 - 1.0) for smooth rendering between ticks
     * Used to interpolate visual position between network ticks
     */
    float GetInterpolationFactor() const {
        if (FIXED_TIMESTEP > 0.0f) {
            return accumulated_time_ / FIXED_TIMESTEP;
        }
        return 0.0f;
    }

private:
    uint64_t current_tick_ = 0;
    float accumulated_time_ = 0.0f;
    uint8_t ticks_processed_this_frame_ = 0;

    // Performance tracking
    struct PerformanceStats {
        float min_tick_time = 999.0f;
        float max_tick_time = 0.0f;
        float avg_tick_time = 0.0f;
    } perf_stats_;

    /**
     * Process a single fixed timestep
     * Override in subclass or set callback to implement actual logic
     */
    virtual void ProcessFixedTick(entt::registry& registry, float dt) {}
};

} // namespace engine::network
