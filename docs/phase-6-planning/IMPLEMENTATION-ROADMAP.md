# GameWorldshaper Engine — Implementation Roadmap

**Date:** April 12, 2026  
**Status:** Complete architectural planning with detailed specifications  
**Next Phase:** Phase 6 Implementation (Game Systems)

---

## COMPLETE ARCHITECTURE SUMMARY

The engine architecture is now fully documented across 4 comprehensive specification files:

### 1. **ARCHITECTURE-COMPLETE-SYSTEMS.md** (Main Reference)
- Rendering system: RenderDevice → Deferred/Forward → Post-Processing → Display
- Editor system: Complete ImGui-based editor with all panels
- 8 missing game systems with full class diagrams
- Integration points between all systems

### 2. **CHARACTER-CONTROLLER-SPECIFICATION.md** (Critical - Phase 6.1)
- 8-state movement system (Idle→Walk→Sprint→Jump→Fall→etc)
- Input buffering for animation cancels
- Ground detection with raycasting
- Network-ready architecture

### 3. **ABILITY-SYSTEM-SPECIFICATION.md** (Critical - Phase 6.2)
- Destiny-style modifier pipeline
- 3 ability types (Active, Passive, Channeled)
- Skill tree progression
- Cooldown/charge management

### 4. **NETWORKING-SPECIFICATION.md** (Critical - Phase 6.3, Must implement early!)
- Deterministic fixed-timestep simulation
- Client-side prediction with server reconciliation
- Rollback mechanism for latency
- Packet protocol & replication

---

## PHASE 6: GAME SYSTEMS IMPLEMENTATION

### Timeline: 8-12 weeks
### Priority: Implement in order (each depends on previous)

---

## IMPLEMENTATION ORDER (Week by Week)

### WEEK 1-2: Character Controller Foundation

**Goal:** Basic movement system working in editor

#### Step 1: Input Handling
```cpp
// engine/core/character/include/input_buffer.h
class InputBuffer {
    std::queue<InputFrame> buffer_;
    static const int BUFFER_SIZE = 6;  // ~100ms @ 60Hz
    
public:
    void BufferInput(const InputAction& input);
    InputAction GetNextInput();
    bool HasInput();
};

// In editor: Map keyboard to InputAction
// W/A/S/D → forward/lateral axis
// Space → jump
// Shift → sprint
// Ctrl → crouch
```

**Files to Create:**
- `engine/core/character/CMakeLists.txt`
- `engine/core/character/include/input_buffer.h`
- `engine/core/character/include/character_controller.h`
- `engine/core/character/include/character_state.h`
- `engine/core/character/src/input_buffer.cpp`
- `engine/core/character/src/character_controller.cpp`

**Tests:**
- [ ] InputBuffer stores 6 frames
- [ ] W key pressed → forward input
- [ ] Multiple inputs queued properly

#### Step 2: State Machine
```cpp
// 8 states: Idle, Walk, Sprint, Jump, Fall, WallRun, Climb, Slide
class CharacterState {
    virtual void OnEnter();
    virtual void OnUpdate(float dt);
    virtual void OnExit();
};

class CharacterStateMachine {
    void TransitionTo(CharacterStateType next);
    bool CanTransitionTo(CharacterStateType next);
};
```

**Tests:**
- [ ] Idle→Walk on input
- [ ] Walk→Sprint on speed >0.5
- [ ] Walk→Jump on space
- [ ] Jump→Fall when velocity < 0
- [ ] Fall→Idle on landing

#### Step 3: Physics Integration
```cpp
// Character is RigidBody with capsule shape
// Movement applies velocity to RigidBody
// Ground detection via raycasting
class GroundDetector {
    bool IsOnGround();
    vec3 GetGroundNormal();
};
```

---

### WEEK 3-4: Advanced Movement

**Goal:** Fluent multi-directional movement with animation blending

#### Step 1: Locomotion Blend Tree
```cpp
class LocomotionSystem {
    // 1D blend space: Idle(0) → Walk(0.5) → Sprint(1.0)
    AnimationState BlendAnimations(float speed_param);
};
```

#### Step 2: Jump Mechanics
```cpp
class JumpState {
    void OnEnter(): apply_impulse(15 m/s)
    void OnUpdate(): blend animation Ascent→Apogee→Descent
    void OnExit(): check_land_damage()
};
```

#### Step 3: Dash/Ability Integration
```cpp
// Dash is an ability managed by AbilitySystem
// CharacterController calls ability_system->ActivateAbility("Dash")
class DashState {
    void OnEnter(): apply_velocity(20 m/s)
    void OnUpdate(): maintain_direction()
    void OnExit(): transition_to(Fall)
};
```

**Tests:**
- [ ] Walk direction matches input
- [ ] Animation blends smoothly
- [ ] Jump reaches 15 m/s velocity
- [ ] Dash has 1.5s cooldown
- [ ] Stamina depletes at 20/sec sprint

---

### WEEK 5-6: Ability System

**Goal:** Castable abilities with cooldown management

#### Step 1: Ability Base Classes
```cpp
// Create ability hierarchy
class Ability: Active, Passive, Channeled

typedef std::map<std::string, std::unique_ptr<Ability>> AbilityLibrary;
```

#### Step 2: Effects System
```cpp
// Damage, Heal, Status (Stun, Slow, Burn, etc)
class Effect {
    virtual void Apply(Entity* target);
};
```

#### Step 3: Cooldown Manager
```cpp
class CooldownManager {
    bool IsReady(const std::string& ability);
    void StartCooldown(const std::string& ability, float duration);
    float GetRemainingCooldown(const std::string& ability);
};
```

**Create Files:**
- `engine/core/ability/CMakeLists.txt`
- `engine/core/ability/include/ability.h`
- `engine/core/ability/include/ability_system.h`
- `engine/core/ability/include/effect.h`
- `engine/core/ability/include/modifier.h`
- `engine/core/ability/include/cooldown_manager.h`
- All corresponding `.cpp` files

**Tests:**
- [ ] Fireball ability deals 50 damage
- [ ] Cooldown prevents re-cast for 5s
- [ ] Multiple modifiers stack correctly
- [ ] Damage reduced by target armor

---

### WEEK 7-8: Modifier Pipeline

**Goal:** Destiny-style stat modification system

#### Step 1: Modifier Stack
```cpp
class ModifierStack : public Component {
    void AddModifier(const Modifier& mod);
    float CalculateModifiedValue(const std::string& stat, float base);
    // Modifier applies: Add → Multiply → Set
};
```

#### Step 2: Skill Tree
```cpp
class SkillNode {
    std::vector<std::string> abilities_granted;
    std::vector<Modifier> modifiers_granted;
    std::vector<uint32_t> prerequisites;
};

class SkillTree {
    bool UnlockNode(uint32_t node_id);
    std::set<uint32_t> unlocked_nodes_;
};
```

#### Step 3: Character Stats
```cpp
class CharacterStats : public Component {
    float health, max_health;
    float mana, max_mana;
    float armor;
    array<float, ElementCount> resistances;
    ModifierStack* modifiers_;  // Reference
    
    void TakeDamage(float damage);
    void Heal(float amount);
};
```

---

### WEEK 9: Networking Foundation

**Goal:** Deterministic simulation framework

> ⚠️ **CRITICAL:** Start networking now, cannot retrofit later!

#### Step 1: Deterministic Simulation Wrapper
```cpp
class DeterministicSimulation {
    const float TICK_RATE = 66.0f;
    const float FIXED_TIMESTEP = 1.0f / TICK_RATE;
    
    uint64_t current_tick;
    void Update(float real_delta_time);  // Processes all fixed ticks
};
```

#### Step 2: Input Synchronization
```cpp
class InputSynchronizer {
    void BufferLocalInput(const InputAction& input);
    InputAction GetInputForTick(uint64_t tick);
    void ConfirmInputTick(uint64_t confirmed_tick);
};
```

#### Step 3: Network Manager Stub
```cpp
class NetworkManager {
    bool is_connected_;
    std::unique_ptr<ServerConnection> server_;
    
    void Connect(const std::string& server_address);
    void SendInput(uint64_t tick, const InputAction& input);
    void ReceiveGameState(const GameStatePacket& packet);
};
```

**Create Files:**
- `engine/core/network/CMakeLists.txt`
- `engine/core/network/include/deterministic_simulation.h`
- `engine/core/network/include/input_synchronizer.h`
- `engine/core/network/include/network_manager.h`
- `engine/core/network/include/network_packet.h`
- All corresponding `.cpp` files

---

### WEEK 10: Rollback & Reconciliation

**Goal:** Client prediction with server correction

#### Step 1: Rollback Manager
```cpp
class RollbackManager {
    struct SavedState {
        uint64_t tick;
        std::vector<EntityState> entities;
        PhysicsWorldState physics;
    };
    
    void SaveSnapshot();
    void RollbackToTick(uint64_t target_tick);
    void FastForwardToTick(uint64_t target_tick);
};
```

#### Step 2: Reconciliation
```cpp
class ReconciliationSystem {
    void Reconcile(const std::vector<EntityState>& server_states);
    void Update(float delta_time);  // Smooth correction
};
```

---

### WEEK 11: AI System Basics

**Goal:** Simple AI for NPCs and enemies

#### Step 1: Behavior Tree
```cpp
class BehaviorTree {
    BehaviorNode* root;
    Blackboard blackboard;
    
    NodeStatus Tick();
};

// Node types: Select, Sequence, Parallel, Task
```

#### Step 2: Basic Tasks
```cpp
class MoveTask : public BehaviorTask;
class AttackTask : public BehaviorTask;
class PatrolTask : public BehaviorTask;
```

#### Step 3: Combat AI
```cpp
class CombatAI {
    Entity* target;
    float combat_range = 5.0f;
    
    void UpdateTargetTracking();
    void SelectAbility();
    void Execute();
};
```

---

### WEEK 12: Polish & Optimization

**Goal:** Performance optimization and bug fixes

- Profiling with GPUProfiler
- Frustum culling optimization
- Batch rendering validation
- Network bandwidth optimization
- Animation frame timing verification

---

## CRITICAL IMPLEMENTATION CHECKLIST

### Before Starting Implementation

- [ ] Read all 4 specification documents
- [ ] Review class diagrams
- [ ] Understand data flow for each system
- [ ] Plan file structure (headers/sources)
- [ ] Add to CMakeLists.txt

### Phase 6.1: Character Controller

- [ ] InputBuffer queues inputs correctly
- [ ] 8 states implemented
- [ ] Transitions work (Idle↔Walk, Walk↔Sprint, Jump, Fall)
- [ ] GroundDetector raycasts correctly
- [ ] Animation blending smooth
- [ ] Networked (local player works)
- [ ] Editor camera follows player

### Phase 6.2: Ability System

- [ ] 3 ability types working (Active, Passive, Channeled)
- [ ] Effects apply correctly (Damage, Heal, Status)
- [ ] Modifiers stack correctly (Add→Multiply→Set)
- [ ] Cooldowns prevent re-cast
- [ ] Skill tree unlocks abilities
- [ ] UI shows cooldown timer

### Phase 6.3: Networking (CRITICAL)

- [ ] DeterministicSimulation produces same results on replay
- [ ] Input buffering prevents input loss
- [ ] Rollback saves/restores state correctly
- [ ] Reconciliation smoothly corrects positions
- [ ] Ability packets synchronized
- [ ] Damage authority from server
- [ ] Multiple clients stay in sync

---

## EDITOR INTEGRATION POINTS

### Inspector Panel Additions

```cpp
// CharacterController component in inspector
if (ImGui::TreeNode("Character Controller")) {
    ImGui::DragFloat("Movement Speed", &move_speed, 0.1f);
    ImGui::DragFloat("Sprint Speed", &sprint_speed, 0.1f);
    ImGui::DragFloat("Jump Force", &jump_force, 0.1f);
    if (ImGui::Button("Edit Locomotion Tree")) {
        show_locomotion_editor_ = true;
    }
}

// Ability System component
if (ImGui::TreeNode("Ability System")) {
    ImGui::ListBox("Available Abilities", &selected_ability,
        ability_names.data(), ability_names.size());
    if (selected_ability >= 0) {
        ShowAbilityProperties(abilities_[selected_ability]);
    }
}

// Skill Tree viewer
if (ImGui::TreeNode("Skill Tree")) {
    for (const auto& node : skill_tree.GetAllNodes()) {
        ImGui::Checkbox(node.name.c_str(), &node.unlocked);
        ImGui::SameLine();
        ImGui::Text(" -> Grants: %s", JoinStrings(node.abilities_granted).c_str());
    }
}
```

### Viewport Enhancements

```cpp
// Show character debug info in viewport
if (show_debug_) {
    // Display velocity vector
    DrawArrow(position, velocity * 0.1f, glm::vec3(0, 1, 0));
    
    // Display current state
    DrawText(position + glm::vec3(0, 3, 0), 
        "State: " + state_machine->GetCurrentStateName());
    
    // Display ground detection
    if (ground_detector->IsOnGround()) {
        DrawSphere(GetGroundContact(), 0.2f, glm::vec3(0, 1, 0));
    }
    
    // Display ability cooldowns
    float y_offset = 0;
    for (const auto& [name, cooldown] : cooldown_manager->GetCooldowns()) {
        float remaining = cooldown.GetRemaining();
        DrawText(position + glm::vec3(0, y_offset, 0),
            name + ": " + std::to_string(remaining) + "s");
        y_offset += 0.3f;
    }
}
```

---

## BUILD SYSTEM UPDATES

### CMakeLists.txt Integration

```cmake
# engine/core/CMakeLists.txt
add_subdirectory(character)
add_subdirectory(ability)
add_subdirectory(network)
add_subdirectory(ai)

# Link to scene
target_link_libraries(scene PUBLIC character ability network ai)
```

### Header Chain

```cpp
// engine/core/game_systems.h - Master include
#pragma once

#include "character/character_controller.h"
#include "character/character_state.h"
#include "character/input_buffer.h"
#include "character/ground_detector.h"
#include "character/character_stats.h"

#include "ability/ability.h"
#include "ability/ability_system.h"
#include "ability/effect.h"
#include "ability/modifier.h"
#include "ability/cooldown_manager.h"
#include "ability/skill_tree.h"

#include "network/network_manager.h"
#include "network/deterministic_simulation.h"
#include "network/input_synchronizer.h"
#include "network/rollback_manager.h"

#include "ai/ai_controller.h"
#include "ai/behavior_tree.h"
#include "ai/combat_ai.h"
```

---

## PERFORMANCE TARGETS

### Frame Time Budget (60 FPS = 16.67ms per frame)

```
Character Controller Update:  1-2ms
Ability System Update:        0.5-1ms
AI Update (10 NPCs):          2-3ms
Physics Step:                 2-4ms  (existing)
Network Sync:                 0.1-0.2ms (async thread)
Animation Update:             1-2ms  (existing)
Rendering:                    8-10ms (existing)
─────────────────────────────────────
Total CPU Time:              ~16-17ms ✓
```

### Network Bandwidth Budget

Per Player:
```
Input Packet:     ~12 bytes  × 60 Hz     = 720 bytes/sec  = 5.8 Kbps
Game State:       ~200 bytes × 10 Hz     = 2000 bytes/sec = 16 Kbps
Ability Packet:   ~50 bytes  × 2 Hz      = 100 bytes/sec  = 0.8Kbps
─────────────────────────────────────────────────────
Total Upstream:   ~22.6 Kbps per player
Downstream:       ~22.6 Kbps × 8 players = 180.8 Kbps
```

Total for 8-player multiplayer: **~200 Kbps per player**

---

## DEPENDENCIES & EXTERNAL LIBRARIES

### Already Integrated
- ✅ GLM (Math)
- ✅ spdlog (Logging)
- ✅ ImGui (Editor UI)
- ✅ Glad (OpenGL)
- ✅ EnTT (ECS)

### New Dependencies Needed

- [ ] asio - Networking library (header-only)
  - For NetworkManager socket operations
  - Add to `third_party/asio/`

- [ ] Optional: cereal - Serialization
  - For network packet serialization
  - Add to `third_party/cereal/`

### No External Dependencies Needed For:
- Character controller (pure C++)
- Ability system (pure C++)
- Modifier pipeline (pure C++)
- Deterministic simulation (pure C++)
- AI behavior trees (pure C++)

---

## TESTING STRATEGY

### Unit Tests (Catch2)

```cpp
// tests/test_character_controller.cpp
TEST_CASE("Character transitions Idle->Walk on input") {
    CharacterController controller(entity);
    InputAction input{.forward = 1.0f};
    controller.ProcessInput(input);
    REQUIRE(controller.GetCurrentState() == CharacterStateType::Walk);
}

TEST_CASE("Ability cooldown prevents re-cast") {
    AbilitySystem system;
    system.ActivateAbility("Fireball");
    REQUIRE_FALSE(system.CanActivateAbility("Fireball"));  // On cooldown
}

TEST_CASE("Modifiers stack correctly") {
    ModifierStack stack;
    stack.AddModifier({.operation = Add, .value = 10});    // 50 + 10 = 60
    stack.AddModifier({.operation = Multiply, .value = 0.5}); // 60 * 1.5 = 90
    REQUIRE(stack.CalculateModifiedValue("damage", 50) == 90);
}
```

### Integration Tests

```cpp
// tests/test_character_controller_integration.cpp
TEST_CASE("Player can move and jump in scene") {
    Scene scene;
    auto player = CreatePlayerEntity(scene);
    
    // Move forward
    player->GetComponent<CharacterController>()->ProcessInput({.forward = 1.0f});
    scene.Update(0.016f);  // 1 frame
    vec3 moved_pos = player->GetComponent<Transform>()->GetPosition();
    REQUIRE(moved_pos.z > 0);  // Moved forward
    
    // Jump
    player->GetComponent<CharacterController>()->ProcessInput({.jump = true});
    scene.Update(0.016f);
    vec3 jumped_pos = player->GetComponent<Transform>()->GetPosition();
    REQUIRE(jumped_pos.y > moved_pos.y);  // Moved up
}
```

### Network Tests

```cpp
// tests/test_network_determinism.cpp
TEST_CASE("Replay produces identical results") {
    DeterministicSimulation sim1, sim2;
    std::vector<InputAction> inputs = GenerateTestInputs(100);  // 100 frames
    
    // Simulate once
    for (auto& input : inputs) {
        sim1.Update(0.016f, input);
    }
    vec3 result1 = sim1.GetCharacterPosition();
    
    // Rewind and simulate again with same inputs
    sim2.Reset();
    for (auto& input : inputs) {
        sim2.Update(0.016f, input);
    }
    vec3 result2 = sim2.GetCharacterPosition();
    
    REQUIRE(result1 == result2);  // Exact match
}
```

---

## SUCCESS CRITERIA FOR PHASE 6

✅ **Character Controller**
- 8 states working correctly
- Smooth animation blending
- Responsive input (no lag)
- Network-ready

✅ **Ability System**
- 10+ abilities castable
- Modifier pipeline working
- Skill tree unlocks abilities
- UI shows cooldown timers

✅ **Networking**
- Deterministic simulation verified
- Input buffering working
- Rollback/reconciliation functioning
- 2+ players stay synced

✅ **Game Feel**
- Movement feels responsive
- Abilities feel impactful
- No animation jank
- <16ms frame time maintained

---

## PHASE 7 PREPARATION

After Phase 6 is complete:

### Phase 7.1: Advanced AI (2-3 weeks)
- Pathfinding with Navmesh
- Combat decision trees
- Multi-phase boss encounters

### Phase 7.2: Audio Integration (1-2 weeks)
- Connect AudioManager to action events
- Spatial audio for moves/abilities
- Music system

### Phase 7.3: World Streaming (3-4 weeks)
- Chunk-based loading
- LOD management
- Seamless world updates

---

## CONCLUSION

The engine now has **complete architectural specifications** for all remaining systems. Each specification includes:

- **Class diagrams** with all methods
- **Data flow diagrams** showing system interactions
- **Code examples** for critical implementations
- **File structure** recommendations
- **Testing checklists** for verification
- **Performance budgets** for optimization targets

**Next step:** Begin Phase 6 implementation following the weekly roadmap.

**Critical path:** Character Controller → Ability System → Networking (do networking early!)

**Estimated completion:** 12 weeks to functional game systems
