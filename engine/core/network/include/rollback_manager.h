#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace engine::network {

/**
 * @struct EntitySnapshot
 * @brief Snapshot of an entity state at a specific tick
 */
struct EntitySnapshot {
    uint32_t entity_id;
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 rotation;
    // Additional: health, stamina, animation state, etc.
};

/**
 * @struct GameStateSnapshot
 * @brief Complete game state at a single simulation tick
 */
struct GameStateSnapshot {
    uint64_t tick;                          // Tick number
    std::vector<EntitySnapshot> entities;
    // Could extend with: physics state, ability states, etc.
};

/**
 * @class RollbackManager
 * @brief Manages snapshots for client-side prediction rollback
 *
 * Enables this workflow:
 * 1. Client predicts future: Apply local input, simulate forward
 * 2. Server corrects: Receive authoritative state from different input
 * 3. Rollback & Replay: Restore old snapshot, replay with correct input
 * 4. Reconciliation: Smoothly correct to server state
 *
 * Example:
 * - Frame 0: Save snapshot
 * - Frames 1-3: Predict with wrong input from network lag
 * - Frame 4: Receive server correction for frame 2
 * - Rollback to frame 2 snapshot
 * - Replay frames 2-4 with correct input
 * - Result: Seamless correction, no jank
 */
class RollbackManager {
public:
    static constexpr size_t MAX_SNAPSHOTS = 180;  // 3 seconds @ 60Hz

    RollbackManager();
    ~RollbackManager() = default;

    /**
     * Save current game state
     * Called once per simulation tick
     *
     * @param tick Current tick number
     * @param registry ECS registry containing entity states
     */
    void SaveSnapshot(uint64_t tick, const entt::registry& registry);

    /**
     * Rollback to a specific tick
     * Restores all entities to their state at that tick
     *
     * @param target_tick Tick to rollback to
     * @param registry ECS registry to restore into
     * @return true if successful, false if snapshot not found
     */
    bool RollbackToTick(uint64_t target_tick, entt::registry& registry);

    /**
     * Get a snapshot for a tick
     */
    const GameStateSnapshot* GetSnapshot(uint64_t tick) const;

    /**
     * Get the oldest saved snapshot tick
     */
    uint64_t GetOldestSnapshotTick() const;

    /**
     * Get the newest saved snapshot tick
     */
    uint64_t GetNewestSnapshotTick() const;

    /**
     * Check if a snapshot exists for a tick
     */
    bool HasSnapshot(uint64_t tick) const;

    /**
     * Clear old snapshots (older than a certain tick)
     * Called to free up memory
     */
    void PruneOldSnapshots(uint64_t keep_from_tick);

    /**
     * Clear all snapshots
     */
    void Clear();

    /**
     * Get number of stored snapshots
     */
    size_t GetSnapshotCount() const { return snapshots_.size(); }

    /**
     * Get memory usage of all snapshots (bytes)
     */
    size_t GetMemoryUsage() const;

private:
    std::deque<GameStateSnapshot> snapshots_;

    /**
     * Create snapshot from registry
     */
    GameStateSnapshot CreateSnapshot(uint64_t tick, const entt::registry& registry) const;

    /**
     * Apply snapshot to registry
     */
    void ApplySnapshot(const GameStateSnapshot& snapshot, entt::registry& registry);

    /**
     * Find snapshot by tick (binary search)
     */
    auto FindSnapshot(uint64_t tick);
    auto FindSnapshot(uint64_t tick) const;
};

/**
 * @class ReconciliationSystem
 * @brief Handles smooth correction of client prediction errors
 *
 * When server sends corrected state, instead of jumping directly,
 * gradually move towards the correction over multiple frames.
 *
 * This reduces visual jank from prediction mismatches.
 */
class ReconciliationSystem {
public:
    static constexpr float RECONCILE_TIME = 0.1f;  // 100ms to reconcile

    ReconciliationSystem();
    ~ReconciliationSystem() = default;

    /**
     * Queue a reconciliation correction
     * @param entity Entity to reconcile
     * @param target_position Server's position
     * @param target_velocity Server's velocity
     */
    void QueueReconciliation(entt::entity entity, const glm::vec3& target_position,
                            const glm::vec3& target_velocity);

    /**
     * Update reconciliation (called per frame)
     * Gradually corrects entity positions towards server state
     *
     * @param dt Delta time
     * @param registry ECS registry
     */
    void Update(float dt, entt::registry& registry);

    /**
     * Check if any reconciliations active
     */
    bool IsReconciling() const;

private:
    struct ReconciliationData {
        entt::entity entity;
        glm::vec3 target_position;
        glm::vec3 target_velocity;
        glm::vec3 start_position;
        float remaining_time = RECONCILE_TIME;
    };

    std::vector<ReconciliationData> active_reconciliations_;

    float blend(float current, float target, float t) const;
};

} // namespace engine::network
