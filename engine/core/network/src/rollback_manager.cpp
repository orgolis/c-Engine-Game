#include "rollback_manager.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <cstdint>

namespace engine::network {

RollbackManager::RollbackManager() {
    // Note: deque doesn't support reserve(), unlike vector
    // Pre-allocation happens at first use
}

// ============ Helper Methods (defined before use) ============

auto RollbackManager::FindSnapshot(uint64_t tick) {
    return std::find_if(snapshots_.begin(), snapshots_.end(),
        [tick](const GameStateSnapshot& snap) { return snap.tick == tick; });
}

auto RollbackManager::FindSnapshot(uint64_t tick) const {
    return std::find_if(snapshots_.begin(), snapshots_.end(),
        [tick](const GameStateSnapshot& snap) { return snap.tick == tick; });
}

// ============ Public Interface ============

void RollbackManager::SaveSnapshot(uint64_t tick, const entt::registry& registry) {
    auto snapshot = CreateSnapshot(tick, registry);
    snapshots_.push_back(snapshot);

    // Remove oldest if buffer full
    if (snapshots_.size() > MAX_SNAPSHOTS) {
        snapshots_.pop_front();
    }
}

bool RollbackManager::RollbackToTick(uint64_t target_tick, entt::registry& registry) {
    const GameStateSnapshot* snapshot = GetSnapshot(target_tick);
    if (!snapshot) {
        auto logger = spdlog::get("engine");
        if (logger) logger->warn("No snapshot for tick {}", target_tick);
        return false;
    }

    ApplySnapshot(*snapshot, registry);

    auto logger = spdlog::get("engine");
    if (logger) logger->debug("Rolled back to tick {}", target_tick);

    return true;
}

const GameStateSnapshot* RollbackManager::GetSnapshot(uint64_t tick) const {
    auto it = FindSnapshot(tick);
    if (it == snapshots_.end()) {
        return nullptr;
    }
    return &(*it);
}

uint64_t RollbackManager::GetOldestSnapshotTick() const {
    if (snapshots_.empty()) return 0;
    return snapshots_.front().tick;
}

uint64_t RollbackManager::GetNewestSnapshotTick() const {
    if (snapshots_.empty()) return 0;
    return snapshots_.back().tick;
}

bool RollbackManager::HasSnapshot(uint64_t tick) const {
    return FindSnapshot(tick) != snapshots_.end();
}

void RollbackManager::PruneOldSnapshots(uint64_t keep_from_tick) {
    auto it = std::find_if(snapshots_.begin(), snapshots_.end(),
        [keep_from_tick](const GameStateSnapshot& snap) { return snap.tick >= keep_from_tick; });
    snapshots_.erase(snapshots_.begin(), it);
}

void RollbackManager::Clear() {
    snapshots_.clear();
}

size_t RollbackManager::GetMemoryUsage() const {
    size_t total = 0;
    for (const auto& snapshot : snapshots_) {
        total += sizeof(GameStateSnapshot);
        total += snapshot.entities.size() * sizeof(EntitySnapshot);
    }
    return total;
}

GameStateSnapshot RollbackManager::CreateSnapshot(uint64_t tick, const entt::registry& registry) const {
    GameStateSnapshot snapshot;
    snapshot.tick = tick;

    // TODO: Extract entity states from registry
    // This is where we'd iterate through entities and save their state
    
    return snapshot;
}

void RollbackManager::ApplySnapshot(const GameStateSnapshot& snapshot, entt::registry& registry) {
    // TODO: Restore entity states from snapshot
    // This is where we'd restore positions, velocities, etc.
}

// ============ ReconciliationSystem ============

ReconciliationSystem::ReconciliationSystem() {
    active_reconciliations_.reserve(16);
}

void ReconciliationSystem::QueueReconciliation(entt::entity entity, const glm::vec3& target_position,
                                             const glm::vec3& target_velocity) {
    ReconciliationData data;
    data.entity = entity;
    data.target_position = target_position;
    data.target_velocity = target_velocity;
    data.start_position = glm::vec3(0.0f);  // Should get current position from entity
    data.remaining_time = RECONCILE_TIME;

    active_reconciliations_.push_back(data);
}

void ReconciliationSystem::Update(float dt, entt::registry& registry) {
    auto it = active_reconciliations_.begin();
    while (it != active_reconciliations_.end()) {
        it->remaining_time -= dt;

        if (it->remaining_time <= 0.0f) {
            // Reconciliation complete
            // TODO: Set entity to exact target position
            it = active_reconciliations_.erase(it);
        } else {
            // Blend towards target
            float t = 1.0f - (it->remaining_time / RECONCILE_TIME);
            glm::vec3 corrected_pos = glm::mix(it->start_position, it->target_position, t);
            // TODO: Update entity position

            ++it;
        }
    }
}

bool ReconciliationSystem::IsReconciling() const {
    return !active_reconciliations_.empty();
}

float ReconciliationSystem::blend(float current, float target, float t) const {
    return glm::mix(current, target, t);
}

} // namespace engine::network
