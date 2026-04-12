# Networking & Replication System — Detailed Specification

**Date:** April 12, 2026
**Priority:** CRITICAL (Cannot be retrofitted)
**Scope:** Deterministic client-prediction, rollback-capable networking

> **CRITICAL:** This system MUST be designed from the start. Retrofitting networking is 10x harder than building it correctly initially.

---

## PHILOSOPHY

The networking system enables **Wuthering Waves-style multiplayer** with:
- Smooth, responsive local character movement (client-predicted)
- Deterministic simulation for rollback (fixed timestep, deterministic physics)
- Minimal latency impact through prediction & reconciliation
- Frame-data driven combat (attacks must be frame-perfect even with latency)
- Ability uses synchronized across players

### Inspirations
- **Guilty Gear Xrd:** Frame-perfect rollback netcode
- **Cyberpunk 2077:** Seamless open-world multiplayer
- **Destiny 2:** Authority model with client prediction

---

## ARCHITECTURE OVERVIEW

```
┌─────────────────────────────────────────────────────────────────┐
│ Fixed Tick Loop (every 15ms = 66 ticks/sec)                    │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────────┐ Input Buffering ┌────────────────────┐  │
│  │ Local Input      │──────────────────→│ SendToServer     │  │
│  │ (6 frames/60ms)  │                  │ Every frame      │  │
│  └──────────────────┘                  └────────────────────┘  │
│         ↓                                      ↓                │
│  ┌──────────────────┐  Prediction    ┌────────────────────┐  │
│  │ Simulate Locally │◄─────────────────│ ReceiveState     │  │
│  │ with local input │   Reconcile      │ Every 5 frames   │  │
│  │ (frame 0-5)      │                  │ (100ms)          │  │
│  └──────────────────┘                  └────────────────────┘  │
│         ↓                                                       │
│  ┌──────────────────┐                                          │
│  │ Apply Rollback   │                                          │
│  │ if server differs│                                          │
│  └──────────────────┘                                          │
│         ↓                                                       │
│  ┌──────────────────┐                                          │
│  │ Render           │                                          │
│  │ (60 FPS)         │                                          │
│  └──────────────────┘                                          │
└─────────────────────────────────────────────────────────────────┘
```

---

## TERMINOLOGY

| Term | Definition |
|------|-----------|
| **Tick** | Fixed simulation step (15ms = 66 Hz) |
| **Frame** | Render frame (1/60s = 60 Hz, varies) |
| **Rollback** | Rewind simulation to tick N, replay with new data |
| **Prediction** | Simulate locally with local input before server confirms |
| **Reconciliation** | Correct local state when server disagrees |
| **RTT** | Round-trip time (ping) in milliseconds |
| **Frame Delay** | How many ticks to delay input (buffer for latency) |

---

## DETERMINISTIC SIMULATION

### Fixed Timestep

```cpp
const float TICK_RATE = 66.0f;        // 66 ticks per second
const float FIXED_TIMESTEP = 1.0f / TICK_RATE;  // ~15ms per tick

class DeterministicSimulation {
    uint64_t current_tick_;
    float accumulated_time_;
    
    std::vector<Ability*> abilities_;  // Must be deterministic
    PhysicsWorld* physics_world_;
    CharacterController* controller_;
    
public:
    void Update(float real_delta_time) {
        accumulated_time_ += real_delta_time;
        
        // Process all fixed ticks that have elapsed
        while (accumulated_time_ >= FIXED_TIMESTEP) {
            // 1. Get input for this tick from buffer
            InputAction input = input_buffer_.GetInputForTick(current_tick_);
            
            // 2. Update character (deterministic)
            controller_->ProcessInput(input);
            controller_->Update(FIXED_TIMESTEP);
            
            // 3. Simulate physics (deterministic)
            physics_world_->Step(FIXED_TIMESTEP);
            
            // 4. Update abilities
            ability_system_->Update(FIXED_TIMESTEP);
            
            current_tick_++;
            accumulated_time_ -= FIXED_TIMESTEP;
        }
    }
    
    bool IsDeterministic() const {
        // Check for non-deterministic operations
        // - Use of rand() (bad)
        // - Use of wall clock time (bad)
        // - Multi-threaded physics (bad)
        // All inputs must be frame-synced
        return true;  // Validated at compile time
    }
};
```

### Determinism Checklist

```cpp
// ✗ NON-DETERMINISTIC: Random number generation per frame
float random_damage = base_damage + rand() % 10;

// ✓ DETERMINISTIC: Use seeded RNG from input tick
uint64_t seed = current_tick_;
std::mt19937 rng(seed);
float random_damage = base_damage + rng() % 10;

// ✗ NON-DETERMINISTIC: Real time
float time_multiplier = glfwGetTime();

// ✓ DETERMINISTIC: Tick-based time
float time_multiplier = current_tick_ * FIXED_TIMESTEP;

// ✗ NON-DETERMINISTIC: Physics with floating-point errors
physics_world_->Step(real_delta_time);  // Different per frame!

// ✓ DETERMINISTIC: Fixed timestep physics
physics_world_->Step(FIXED_TIMESTEP);  // Always 15ms

// ✗ NON-DETERMINISTIC: Physics on different threads
// (Race conditions, ordering differences)

// ✓ DETERMINISTIC: Single-threaded physics
physics_world_->Step(FIXED_TIMESTEP);  // Main thread only
```

---

## INPUT BUFFERING & NETWORK MODEL

### Client-Side Prediction with Server Authority

```
Client Tick 0-5: Local simulation with local input
   ↓
   Send to server: "Tick 0-5 inputs"
   (15ms network latency)
   ↓
Server receives at Tick 10 (in server time)
   ↓
Server simulates Ticks 0-5 with received input
   ↓
Server broadcasts: "Tick 5 state: Player at (100,20,50), velocity (2,0,0)"
   (15ms network latency)
   ↓
Client receives at Tick 20
   ↓
Compare: "My predicted position at Tick 5 was (102,20,51)"
         "Server says position at Tick 5 was (100,20,50)"
   ↓
ROLLBACK: Restore to Tick 5 with server state
          Replay Ticks 6-20 with local input
   ↓
Smooth lerp back to predicted position (~100ms correction)
```

### Implementation

```cpp
class InputSynchronizer {
    static const uint32_t INPUT_BUFFER_SIZE = 120;  // 2 seconds @ 60Hz
    
    struct InputFrame {
        uint64_t tick;
        InputAction input;
        bool confirmed;  // Received server ack?
    };
    
    std::vector<InputFrame> input_buffer_;
    uint32_t buffer_write_index_;  // Where we write next input
    uint64_t current_tick_;
    
public:
    void BufferLocalInput(const InputAction& input) {
        InputFrame frame;
        frame.tick = current_tick_;
        frame.input = input;
        frame.confirmed = false;
        
        input_buffer_[buffer_write_index_] = frame;
        buffer_write_index_ = (buffer_write_index_ + 1) % INPUT_BUFFER_SIZE;
    }
    
    InputAction GetInputForTick(uint64_t tick) const {
        // Find input frame for this tick
        for (const auto& frame : input_buffer_) {
            if (frame.tick == tick) {
                return frame.input;
            }
        }
        
        return InputAction();  // No input, return neutral
    }
    
    void ConfirmInputTick(uint64_t confirmed_tick) {
        // Server confirmed it processed this tick
        for (auto& frame : input_buffer_) {
            if (frame.tick <= confirmed_tick) {
                frame.confirmed = true;
            }
        }
    }
};
```

---

## PACKET PROTOCOL

### Network Packet Structure

```cpp
enum class PacketType {
    Input,          // Client → Server
    GameState,      // Server → Client
    Ability,        // Client → Server (ability usage)
    Damage,         // Server → Client (authority)
    Disconnect,     // Either direction
};

struct NetworkPacket {
    PacketType type;
    uint32_t sender_id;
    uint32_t sequence_number;
    uint64_t server_tick;
    uint64_t client_tick;
    std::vector<uint8_t> payload;
    
    void Serialize(std::vector<uint8_t>& buffer) const;
    void Deserialize(const std::vector<uint8_t>& buffer);
};

// Specific packet types

struct InputPacket {
    uint64_t tick;
    InputAction input;
    
    // Serialize to ~12 bytes
    // tick (8) + input.forward (1) + input.right (1) + buttons (2)
};

struct GameStatePacket {
    uint64_t server_tick;
    std::vector<EntityState> entities;  // Only replicate changed entities
    float physics_timestamp;
    
    // Compressed: only send changed position/rotation
    // Position delta encoding: 16-bit fixed point per frame
    // Sent every 5 frames (~75ms)
};

struct AbilityPacket {
    uint64_t tick;
    uint32_t player_id;
    std::string ability_name;
    glm::vec3 target_position;
    uint32_t target_entity_id;
    
    // Server must validate:
    // - Is ability on cooldown?
    // - Does player have resources?
    // - Is target in range?
};

struct DamagePacket {
    uint64_t tick;
    uint32_t attacker_id;
    uint32_t victim_id;
    float damage;
    std::string damage_type;
    
    // Only server can send damage packets
    // Clients simulate damage but wait for server confirmation
};
```

---

## REPLICATION & RECONCILIATION

### Entity State

```cpp
struct EntityState {
    uint32_t entity_id;
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 velocity;
    
    std::string animation_state;  // "Walk", "Sprint", "Jump"
    float animation_blend;
    
    float health;
    std::vector<std::string> active_effects;
    
    // Compressed transmission (~32-40 bytes)
    bool PositionChanged() const;
    bool RotationChanged() const;
    bool VelocityChanged() const;
    
    // Serialize with delta compression
    void SerializeDelta(const EntityState& last_state, std::vector<uint8_t>& out);
    void DeserializeDelta(const std::vector<uint8_t>& in, const EntityState& last_state);
};

class ReconciliationSystem {
    std::vector<EntityState> client_predicted_state_;  // What we predicted
    std::vector<EntityState> server_confirmed_state_;  // What server says
    
    static const float CORRECTION_SPEED = 5.0f;  // Lerp over 200ms
    std::map<uint32_t, float> correction_progress_;  // Per entity
    
public:
    void Reconcile(const std::vector<EntityState>& server_states) {
        for (const auto& server_state : server_states) {
            auto& client_state = client_predicted_state_[server_state.entity_id];
            
            // Calculate error
            float position_error = glm::distance(client_state.position, server_state.position);
            
            if (position_error > THRESHOLD) {
                // Start correction
                correction_progress_[server_state.entity_id] = 0.0f;
            }
        }
    }
    
    void Update(float delta_time) {
        for (auto& pair : correction_progress_) {
            uint32_t entity_id = pair.first;
            float& progress = pair.second;
            
            progress += delta_time * CORRECTION_SPEED;
            
            if (progress >= 1.0f) {
                // Correction complete
                pair.second = 2.0f;  // Mark for removal
            } else {
                // Lerp between predicted and server state
                float t = std::min(progress, 1.0f);
                client_predicted_state_[entity_id].position = 
                    glm::mix(
                        client_predicted_state_[entity_id].position,
                        server_confirmed_state_[entity_id].position,
                        t
                    );
            }
        }
    }
};
```

---

## ROLLBACK MECHANISM

### State Saving

```cpp
class RollbackManager {
    struct SavedState {
        uint64_t tick;
        std::vector<EntityState> entities;
        PhysicsWorldState physics;
        float timestamp;
    };
    
    std::vector<SavedState> state_history_;  // Circular buffer
    static const uint32_t MAX_ROLLBACK_TICKS = 12;  // ~180ms
    
public:
    void SaveSnapshot() {
        SavedState snapshot;
        snapshot.tick = simulation_->GetCurrentTick();
        snapshot.entities = GatherEntityStates();
        snapshot.physics = physics_world_->GetState();
        snapshot.timestamp = glfwGetTime();
        
        // Keep only last MAX_ROLLBACK_TICKS snapshots
        state_history_.push_back(snapshot);
        if (state_history_.size() > MAX_ROLLBACK_TICKS) {
            state_history_.erase(state_history_.begin());
        }
    }
    
    void RollbackToTick(uint64_t target_tick) {
        // Find snapshot at target tick
        auto it = std::find_if(
            state_history_.begin(),
            state_history_.end(),
            [target_tick](const SavedState& s) { return s.tick == target_tick; }
        );
        
        if (it == state_history_.end()) {
            SPDLOG_WARN("Cannot rollback to tick {}, too old", target_tick);
            return;
        }
        
        // Restore state
        RestoreEntityStates(it->entities);
        physics_world_->SetState(it->physics);
        
        SPDLOG_DEBUG("Rolled back {} ticks", 
            simulation_->GetCurrentTick() - target_tick);
    }
    
    void FastForwardToTick(uint64_t target_tick) {
        // Replay simulation from current tick to target tick
        // Using saved input from input_buffer
        
        uint64_t current = simulation_->GetCurrentTick();
        while (current <= target_tick) {
            InputAction input = input_synchronizer_->GetInputForTick(current);
            controller_->ProcessInput(input);
            controller_->Update(FIXED_TIMESTEP);
            physics_world_->Step(FIXED_TIMESTEP);
            current++;
        }
    }
};
```

---

## LATENCY & FRAME DELAY

### Frame Delay Calculation

```cpp
class LatencyHandler {
    uint32_t frame_delay_;  // How many ticks to delay input
    uint32_t estimated_rtt_;  // Milliseconds
    
public:
    void UpdateLatencyEstimate(uint32_t new_rtt) {
        // Exponential moving average
        const float ALPHA = 0.7f;
        estimated_rtt_ = ALPHA * new_rtt + (1.0f - ALPHA) * estimated_rtt_;
        
        // Calculate frame delay needed
        // Delay = RTT / TICK_DURATION
        frame_delay_ = estimated_rtt_ * TICK_RATE / 1000.0f;
        frame_delay_ = std::min(frame_delay_, MAX_FRAME_DELAY);
        
        SPDLOG_DEBUG("New RTT: {} ms, Frame delay: {}", 
            estimated_rtt_, frame_delay_);
    }
    
    uint32_t GetFrameDelay() const {
        return frame_delay_;
    }
};
```

### Frame Delay Example

```
RTT = 100ms
Frame = 1/66 = 15ms per tick
Frame Delay = 100ms / 15ms ≈ 7 ticks

Timeline:
Tick 0: Player presses Jump
Tick 7: Jump input sent to server (after 100ms RTT)
Tick 7: Server processes jump, broadcasts to others
Tick 14: Other clients receive jump (another 100ms RTT)

Total latency visible: ~200ms for other players to see your action
```

---

## ABILITY SYNCHRONIZATION

### Ability Activation Network Flow

```cpp
// Client activates ability
player_controller_->ActivateAbility("Fireball", target_pos);

// 1. Local prediction (immediate)
ability_system_->ApplyEffects(target_pos);
ability_renderer_->PlayVFX(target_pos);

// 2. Send to server
struct AbilityUsePacket {
    uint64_t tick;
    std::string ability_name;
    glm::vec3 target_position;
} packet;

network_manager_->SendAbilityPacket(packet);

// 3. Server validates (delay = RTT)
server_->ValidateAbility(packet);
if (!server_->CanUseAbility(player_id, packet.ability_name)) {
    return;  // Server rejects
}

// Calculate damage with server-authoritative values
float server_calculated_damage = server_->CalculateDamage(
    packet.ability_name,
    player_id
);

// 4. Broadcast damage to all clients
server_->BroadcastDamageEvent(
    attacker_id,
    target_id,
    server_calculated_damage
);

// 5. Client receives damage confirmation (RTT later)
client_->ReceiveDamageEvent(damage_packet);
if (local_predicted_damage_ != damage_packet.damage) {
    ApplyCorrection();
}
```

---

## MULTIPLAYER SCENARIOS

### Scenario 1: Two Players Attacking Each Other

```
Tick 0: Player A attacks Player B
   ↓
   (100ms RTT)
   ↓
Tick 7: Player B receives: "Taking damage"
   ↓
Tick 7: Player B attacks Player A (reflexively)
   ↓
   (100ms RTT)
   ↓
Tick 14: Player A receives: "Taking damage from reflexive attack"
```

### Scenario 2: Rollback from Latency

```
Tick 0-5: Client predicts "Jump at position (100, 5, 50)"
   ↓
Tick 10: Server says "Jump was not valid, position was (100, 0, 50)"
   ↓
Action: ROLLBACK
       1. Restore to Tick 5 state
       2. Apply server correction
       3. Replay Ticks 6-10 with local input
       4. Smooth lerp back to predicted
```

---

## NETWORK STATISTICS & VISUALIZATION

```cpp
class NetworkStats {
    uint32_t latency_ms;
    float packet_loss_percent;
    uint32_t jitter_ms;
    uint64_t bytes_sent_per_second;
    uint64_t bytes_received_per_second;
    
public:
    void UpdateStats() {
        // Measure RTT
        auto ack_time = last_ack_time_ - last_send_time_;
        latency_ms = ack_time.count();
        
        // Measure packet loss
        packet_loss_percent = (packets_lost_ / packets_sent_) * 100.0f;
        
        // Measure jitter (variance in latency)
        jitter_ms = std::abs(latency_ms - average_latency_ms_);
    }
    
    void RenderDebugUI() {
        ImGui::Text("Network Stats:");
        ImGui::Text("  Ping: %d ms", latency_ms);
        ImGui::Text("  Packet Loss: %.2f%%", packet_loss_percent);
        ImGui::Text("  Jitter: %d ms", jitter_ms);
        ImGui::Text("  Upload: %.2f Mbps", bytes_sent_per_second * 8 / 1000000.0f);
        ImGui::Text("  Download: %.2f Mbps", bytes_received_per_second * 8 / 1000000.0f);
        ImGui::Text("  Current Tick: %llu", current_tick_);
    }
};
```

---

## ARCHITECTURE DIAGRAM

```
┌─────────────────────────────────────────┐
│  Client                                 │
├─────────────────────────────────────────┤
│                                         │
│  InputBuffer → DeterministicSimulation │
│       ↓                                 │
│  LocalPrediction (Tick 0-7)             │
│       ↓                                 │
│  RollbackManager (save every tick)      │
│       ↓                                 │
│  ReconciliationSystem (check server)    │
│       ↓                                 │
│  Render (60 FPS from simulation)        │
│                                         │
│  NetworkManager ↔ Server                │
│  - Send input every frame               │
│  - Receive game state every 75ms        │
│                                         │
└─────────────────────────────────────────┘

         ↕ Network (100ms RTT)

┌─────────────────────────────────────────┐
│  Server                                 │
├─────────────────────────────────────────┤
│                                         │
│  ReceiveInput → Validate → Simulate     │
│       ↓                                 │
│  DeterministicSimulation (Tick 0-7)     │
│       ↓                                 │
│  ProcessAbilities (authority)           │
│       ↓                                 │
│  ApplyDamage (server-authoritative)     │
│       ↓                                 │
│  BroadcastGameState (every 5 ticks)     │
│       ↓                                 │
│  SendToAllClients                       │
│                                         │
└─────────────────────────────────────────┘
```

---

## FILE STRUCTURE

```
engine/
├── core/
│   └── network/
│       ├── CMakeLists.txt
│       ├── include/
│       │   ├── network_manager.h
│       │   ├── network_packet.h
│       │   ├── deterministic_simulation.h
│       │   ├── input_synchronizer.h
│       │   ├── rollback_manager.h
│       │   ├── reconciliation_system.h
│       │   ├── network_entity.h
│       │   └── network_stats.h
│       └── src/
│           ├── network_manager.cpp
│           ├── network_packet.cpp
│           ├── deterministic_simulation.cpp
│           ├── input_synchronizer.cpp
│           ├── rollback_manager.cpp
│           └── reconciliation_system.cpp
```

---

## CRITICAL IMPLEMENTATION ORDER

1. **Backbone First**
   - DeterministicSimulation with fixed timestep
   - InputSynchronizer with buffering
   - NetworkManager (basic send/receive)

2. **Prediction & Rollback**
   - RollbackManager (save/restore state)
   - ReconciliationSystem (handle discrepancies)
   - FastForward (replay from saved state)

3. **Synchronization**
   - EntityReplication (only changed entities)
   - AbilityValidation (server authority)
   - DamageReplication (server-authoritative)

4. **Polish**
   - Latency compensation
   - Packet loss handling
   - Network visualization

---

## TESTING STRATEGY

- [ ] Determinism verified (replay produces same result)
- [ ] Rollback restores correct state
- [ ] Reconciliation handles 100ms latency
- [ ] Input buffering prevents input loss
- [ ] Ability replication works with latency
- [ ] Damage is authoritative from server
- [ ] Multiple clients stay synchronized
- [ ] Network bandwidth under 1 Mbps per player
