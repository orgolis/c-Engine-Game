#pragma once

#include "../input/input_action.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>

namespace schizo::gameplay {

/**
 * @brief Deterministic game frame state
 * 
 * Contains all inputs and derived state for a single frame.
 * Used for:
 * - Networking (send frame input instead of object state)
 * - Replay system (record frames, replay deterministically)
 * - Rollback netcode foundation
 * 
 * ⚠ CRITICAL: Everything must be deterministic
 * - No floating point RNG (seeded RNG only)
 * - No platform-dependent operations
 * - Fixed-timestep simulation
 * - All state changes trace back to input
 */
struct GameFrame {
    uint32_t frame_number = 0;
    double world_time = 0.0;
    float delta_time = 0.016f;  // 1/60th
    
    // Input snapshots for each entity
    struct EntityInput {
        uint32_t entity_id;
        schizo::input::InputSnapshot input;
    };
    std::vector<EntityInput> entity_inputs;
    
    // Deterministic RNG seed for this frame
    uint32_t rng_seed = 0;
    
    /**
     * @brief Get input for specific entity
     */
    const schizo::input::InputSnapshot* GetEntityInput(uint32_t entity_id) const {
        for (const auto& ei : entity_inputs) {
            if (ei.entity_id == entity_id) {
                return &ei.input;
            }
        }
        return nullptr;
    }
};

/**
 * @brief Game state snapshot for deterministic replay
 * 
 * Complete world state at given frame number.
 * Used for validation, replays, state correction.
 */
struct GameStateSnapshot {
    uint32_t frame_number = 0;
    double timestamp = 0.0;
    
    // Entity states
    struct EntityState {
        uint32_t entity_id = 0;
        glm::vec3 position;
        glm::vec3 velocity;
        float health = 0.0f;
        float mana = 0.0f;
        uint32_t flags = 0;  // Poise state, stunned, etc
    };
    std::vector<EntityState> entity_states;
    
    /**
     * @brief Get state for entity
     */
    const EntityState* GetEntityState(uint32_t entity_id) const {
        for (const auto& es : entity_states) {
            if (es.entity_id == entity_id) {
                return &es;
            }
        }
        return nullptr;
    }
};

/**
 * @brief Game state tracker for deterministic simulation
 * 
 * Maintains frame history and state snapshots for:
 * - Rollback detection (if simulation diverges from snapshot)
 * - Replay validation (simulate and compare to recorded state)
 * - Netcode debugging (compare client/server snapshots)
 */
class DeterministicStateTracker {
public:
    DeterministicStateTracker() : current_frame_(0) {}
    
    virtual ~DeterministicStateTracker() = default;
    
    // ======================
    // Frame management
    // ======================
    
    /**
     * @brief Advance to next frame
     */
    void NextFrame(float delta_time) {
        current_frame_++;
    }
    
    uint32_t GetCurrentFrame() const { return current_frame_; }
    
    // ======================
    // State snapshots
    // ======================
    
    /**
     * @brief Record state snapshot
     * 
     * Called at fixed intervals or on demand for verification.
     */
    void RecordSnapshot(const GameStateSnapshot& snapshot) {
        snapshots_[snapshot.frame_number] = snapshot;
        
        // Keep only last N frames in memory (configurable)
        if (snapshots_.size() > SNAPSHOT_HISTORY) {
            auto oldest = snapshots_.begin();
            snapshots_.erase(oldest);
        }
    }
    
    /**
     * @brief Get recorded snapshot
     */
    const GameStateSnapshot* GetSnapshot(uint32_t frame_number) const {
        auto it = snapshots_.find(frame_number);
        if (it != snapshots_.end()) {
            return &it->second;
        }
        return nullptr;
    }
    
    // ======================
    // Input recording
    // ======================
    
    /**
     * @brief Record input frame
     * 
     * Used for replay system.
     */
    void RecordFrame(const GameFrame& frame) {
        frames_[frame.frame_number] = frame;
        
        // Keep only last N frames
        if (frames_.size() > FRAME_HISTORY) {
            auto oldest = frames_.begin();
            frames_.erase(oldest);
        }
    }
    
    /**
     * @brief Get recorded frame inputs
     */
    const GameFrame* GetFrame(uint32_t frame_number) const {
        auto it = frames_.find(frame_number);
        if (it != frames_.end()) {
            return &it->second;
        }
        return nullptr;
    }
    
    // ======================
    // Divergence detection
    // ======================
    
    /**
     * @brief Check if simulated state matches recorded snapshot
     * 
     * Used to detect when server diverges from expected state.
     * Triggers state correction if divergence found.
     */
    bool CheckStateConsistency(
        uint32_t frame_number,
        const GameStateSnapshot& simulated_state,
        float position_tolerance = 0.1f,
        float velocity_tolerance = 0.01f
    ) const {
        const auto* recorded = GetSnapshot(frame_number);
        if (!recorded) return true;  // No recorded state to compare
        
        for (const auto& recorded_entity : recorded->entity_states) {
            const auto* simulated_entity = simulated_state.GetEntityState(recorded_entity.entity_id);
            if (!simulated_entity) continue;
            
            // Check position
            float pos_diff = glm::distance(
                recorded_entity.position,
                simulated_entity->position
            );
            if (pos_diff > position_tolerance) {
                return false;
            }
            
            // Check velocity
            float vel_diff = glm::distance(
                recorded_entity.velocity,
                simulated_entity->velocity
            );
            if (vel_diff > velocity_tolerance) {
                return false;
            }
            
            // Check health
            if (recorded_entity.health != simulated_entity->health) {
                return false;
            }
        }
        
        return true;
    }
    
    // ======================
    // Factory
    // ======================
    
    static std::shared_ptr<DeterministicStateTracker> Create() {
        return std::make_shared<DeterministicStateTracker>();
    }
    
private:
    static constexpr uint32_t SNAPSHOT_HISTORY = 120;  // 2 seconds at 60 FPS
    static constexpr uint32_t FRAME_HISTORY = 300;     // 5 seconds at 60 FPS
    
    uint32_t current_frame_;
    
    std::unordered_map<uint32_t, GameStateSnapshot> snapshots_;
    std::unordered_map<uint32_t, GameFrame> frames_;
};

} // namespace schizo::gameplay
