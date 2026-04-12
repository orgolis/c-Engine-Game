# Testing Strategy — Phase 6 Quality Assurance

**Purpose:** Comprehensive testing approach for flawless implementation

---

## TEST PYRAMID

```
                    ▲
                   ╱ ╲        End-to-End (Manual)
                  ╱   ╲       3-5 tests, focus on workflows
                 ╱─────╲
                ╱       ╲      Integration Tests
               ╱         ╲     15-20 tests, system pairs
              ╱───────────╲
             ╱             ╲    Unit Tests
            ╱               ╲   100+ tests, coverage > 80%
           ╱─────────────────╲

Time to run:
Unit:        < 5 seconds
Integration: 5-15 seconds  
E2E:         1-5 minutes
```

---

## UNIT TESTS (Character Module)

### Test Files

```
tests/character/
├── test_input_buffer.cpp        (6 tests)
├── test_character_controller.cpp (15 tests)
├── test_character_states.cpp     (12 tests)
├── test_ground_detection.cpp     (5 tests)
└── test_character_physics.cpp    (8 tests)
```

### Input Buffer Tests

```cpp
// tests/character/test_input_buffer.cpp

#include <catch2/catch.hpp>
#include "input_buffer.h"

TEST_CASE("InputBuffer stores 6 frames correctly") {
    InputBuffer buffer(6);
    InputAction action = {1.0f, 0.0f, false};
    
    buffer.BufferInput(action);
    REQUIRE(buffer.GetInputForTick(0) == action);
}

TEST_CASE("InputBuffer wraps circularly") {
    InputBuffer buffer(3);
    
    buffer.BufferInput({1, 0, false});  // Tick 0
    buffer.BufferInput({2, 0, false});  // Tick 1
    buffer.BufferInput({3, 0, false});  // Tick 2
    buffer.BufferInput({4, 0, false});  // Tick 3 (wraps, tick 0 overwritten)
    
    auto input_at_0 = buffer.GetInputForTick(0);
    REQUIRE(input_at_0.axis_right == 4);  // Newest value
}

TEST_CASE("InputBuffer handles zero velocity") {
    InputBuffer buffer(6);
    InputAction zero_input = {0, 0, false};
    
    buffer.BufferInput(zero_input);
    auto retrieved = buffer.GetInputForTick(0);
    
    REQUIRE(retrieved.axis_forward == 0);
    REQUIRE(retrieved.axis_right == 0);
}

TEST_CASE("InputBuffer out-of-bounds access is safe") {
    InputBuffer buffer(3);
    buffer.BufferInput({1, 0, false});
    
    // Accessing tick 100 on 3-frame buffer should wrap or return empty
    auto input = buffer.GetInputForTick(100);
    REQUIRE_NOTHROW([&]() { (void)input; }());  // No crash
}

TEST_CASE("InputBuffer jump pressed recorded") {
    InputBuffer buffer(6);
    InputAction action;
    action.jump_pressed = true;
    
    buffer.BufferInput(action);
    auto retrieved = buffer.GetInputForTick(0);
    
    REQUIRE(retrieved.jump_pressed == true);
}

TEST_CASE("InputBuffer deadzone applied") {
    InputBuffer buffer(6);
    InputAction small_input = {0.01f, 0.01f, false};  // Below deadzone
    
    buffer.BufferInput(small_input);
    auto retrieved = buffer.GetInputForTick(0);
    
    // Should be zeroed out
    REQUIRE(retrieved.axis_forward < 0.001f);
    REQUIRE(retrieved.axis_right < 0.001f);
}
```

### Character Controller Tests

```cpp
// tests/character/test_character_controller.cpp

TEST_CASE("Character accelerates in movement direction") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto physics = entity->GetComponent<RigidBodyComponent>();
    
    InputAction input;
    input.axis_forward = 1.0f;
    
    char_ctrl->ProcessInput(input);
    char_ctrl->Update(FIXED_TIMESTEP);
    
    glm::vec3 velocity = physics->GetVelocity();
    REQUIRE(velocity.z > 0);  // Moving forward
}

TEST_CASE("Character decelerates with zero input") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto physics = entity->GetComponent<RigidBodyComponent>();
    
    physics->SetVelocity(glm::vec3(5, 0, 0));
    
    InputAction zero_input = {};
    char_ctrl->ProcessInput(zero_input);
    char_ctrl->Update(FIXED_TIMESTEP);
    
    glm::vec3 velocity = physics->GetVelocity();
    REQUIRE(glm::length(glm::vec2(velocity.x, velocity.z)) < 5);
}

TEST_CASE("Character max speed capped") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto physics = entity->GetComponent<RigidBodyComponent>();
    
    InputAction input;
    input.axis_forward = 1.0f;
    
    // Update many times
    for (int i = 0; i < 100; i++) {
        char_ctrl->ProcessInput(input);
        char_ctrl->Update(FIXED_TIMESTEP);
    }
    
    glm::vec3 velocity = physics->GetVelocity();
    float speed = glm::length(glm::vec2(velocity.x, velocity.z));
    
    REQUIRE(speed <= WALK_MAX_SPEED + 0.1f);  // Small tolerance
}

TEST_CASE("Jump applies upward impulse") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto physics = entity->GetComponent<RigidBodyComponent>();
    
    InputAction input;
    input.jump_pressed = true;
    
    char_ctrl->ProcessInput(input);
    char_ctrl->Update(FIXED_TIMESTEP);
    
    glm::vec3 velocity = physics->GetVelocity();
    REQUIRE(velocity.y > JUMP_FORCE - 1.0f);
}

TEST_CASE("Cannot jump in air (double jump blocked)") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto physics = entity->GetComponent<RigidBodyComponent>();
    
    // First jump
    InputAction input = {0, 0, true};
    char_ctrl->ProcessInput(input);
    char_ctrl->Update(FIXED_TIMESTEP);
    
    glm::vec3 vel_after_first = physics->GetVelocity();
    
    // Try to jump again immediately (still in air)
    char_ctrl->ProcessInput(input);
    char_ctrl->Update(FIXED_TIMESTEP);
    
    glm::vec3 vel_after_second = physics->GetVelocity();
    
    // Should not accumulate jump force
    REQUIRE(vel_after_second.y < vel_after_first.y * 1.5f);
}

TEST_CASE("Sprint costs stamina") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto stats = entity->GetComponent<CharacterStats>();
    
    float stamina_before = stats->GetStamina();
    
    InputAction input;
    input.axis_forward = 1.0f;
    input.sprint_pressed = true;
    
    char_ctrl->ProcessInput(input);
    char_ctrl->Update(FIXED_TIMESTEP);
    
    float stamina_after = stats->GetStamina();
    REQUIRE(stamina_after < stamina_before);
}

TEST_CASE("Cannot sprint without stamina") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto stats = entity->GetComponent<CharacterStats>();
    
    stats->SetStamina(0);  // No stamina
    
    InputAction input;
    input.sprint_pressed = true;
    
    char_ctrl->ProcessInput(input);
    char_ctrl->Update(FIXED_TIMESTEP);
    
    // Character should not be in sprint state
    REQUIRE(char_ctrl->GetCurrentState() != CharacterStateType::Sprint);
}

TEST_CASE("Character takes damage and transitions to hit") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto stats = entity->GetComponent<CharacterStats>();
    
    float hp_before = stats->GetHealth();
    
    char_ctrl->TakeDamage(25, DamageType::Physical);
    
    float hp_after = stats->GetHealth();
    REQUIRE(hp_after == hp_before - 25);
}

TEST_CASE("Zero health triggers death state") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto stats = entity->GetComponent<CharacterStats>();
    
    stats->SetHealth(1);
    char_ctrl->TakeDamage(10, DamageType::Physical);
    
    REQUIRE(char_ctrl->GetCurrentState() == CharacterStateType::Dead);
}
```

### State Machine Tests

```cpp
// tests/character/test_character_states.cpp

TEST_CASE("Idle state does not move character") {
    auto state = std::make_unique<IdleState>();
    auto entity = CreateTestEntity();
    
    state->OnEnter(entity);
    state->OnUpdate(entity, FIXED_TIMESTEP);
    
    auto pos = entity->GetComponent<TransformComponent>()->GetPosition();
    // Idle should not change position
    REQUIRE(pos == glm::vec3(0, 0, 0));
}

TEST_CASE("Walk state applies forward velocity") {
    auto entity = CreateTestEntity();
    auto physics = entity->GetComponent<RigidBodyComponent>();
    
    auto state = std::make_unique<WalkState>();
    state->OnEnter(entity);
    state->OnUpdate(entity, FIXED_TIMESTEP);
    
    glm::vec3 velocity = physics->GetVelocity();
    REQUIRE(glm::length(velocity) > 0);
}

TEST_CASE("Jump state OnEnter applies impulse") {
    auto entity = CreateTestEntity();
    auto physics = entity->GetComponent<RigidBodyComponent>();
    
    auto state = std::make_unique<JumpState>();
    state->OnEnter(entity);  // Should apply impulse
    
    glm::vec3 velocity = physics->GetVelocity();
    REQUIRE(velocity.y > 5);  // Upward velocity
}

TEST_CASE("Land state plays landing animation") {
    auto entity = CreateTestEntity();
    auto animator = entity->GetComponent<AnimationComponent>();
    
    auto state = std::make_unique<LandState>();
    state->OnEnter(entity);
    
    REQUIRE(animator->Is Playing("Land"));
}

TEST_CASE("State transition guarded") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    
    // Try illegal transition: Idle → Land (should fail)
    bool can_transition = char_ctrl->CanTransitionTo(CharacterStateType::Land);
    REQUIRE(can_transition == false);
}

TEST_CASE("Valid transitions allowed") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    
    // Idle → Walk should be allowed
    bool can_transition = char_ctrl->CanTransitionTo(CharacterStateType::Walk);
    REQUIRE(can_transition == true);
}
```

---

## INTEGRATION TESTS (Ability Module)

### Test Files

```
tests/ability/
├── test_ability_activation.cpp    (8 tests)
├── test_modifier_stack.cpp        (12 tests)
├── test_cooldown_manager.cpp      (6 tests)
├── test_ability_effects.cpp       (8 tests)
└── test_ability_network.cpp       (4 tests)
```

### Example Tests

```cpp
// tests/ability/test_ability_activation.cpp

TEST_CASE("Character cannot cast without mana") {
    auto entity = CreateCharacterWithStats();
    auto stats = entity->GetComponent<CharacterStats>();
    auto ability_sys = entity->GetComponent<AbilitySystem>();
    
    stats->SetMana(10);  // Not enough for Fireball (50 cost)
    
    ActivationContext ctx{entity, nullptr, glm::vec3(0,0,0)};
    bool success = ability_sys->ActivateAbility("Fireball", ctx);
    
    REQUIRE(success == false);
}

TEST_CASE("Successful ability cast reduces mana") {
    auto entity = CreateCharacterWithStats();
    auto ability_sys = entity->GetComponent<AbilitySystem>();
    
    float mana_before = entity->GetComponent<CharacterStats>()->GetMana();
    
    ActivationContext ctx{entity, nullptr, glm::vec3(0,0,0)};
    bool success = ability_sys->ActivateAbility("Fireball", ctx);
    
    REQUIRE(success == true);
    
    float mana_after = entity->GetComponent<CharacterStats>()->GetMana();
    REQUIRE(mana_after < mana_before);
}

TEST_CASE("Ability on cooldown cannot be cast") {
    auto entity = CreateCharacterWithStats();
    auto ability_sys = entity->GetComponent<AbilitySystem>();
    
    ActivationContext ctx{entity, nullptr, glm::vec3(0,0,0)};
    
    // First cast
    ability_sys->ActivateAbility("Fireball", ctx);
    
    // Immediate second cast should fail
    bool success = ability_sys->ActivateAbility("Fireball", ctx);
    REQUIRE(success == false);
}

// tests/ability/test_modifier_stack.cpp

TEST_CASE("Modifier pipeline: Add then Multiply") {
    auto entity = CreateTestEntity();
    auto mod_stack = entity->GetComponent<ModifierStack>();
    
    // Base 50 → Add 10 → Mul 1.5 → 90
    mod_stack->AddModifier({"Add10", 10, ModifierOp::Add});
    mod_stack->AddModifier({"Mul1.5", 1.5f, ModifierOp::Multiply});
    
    float result = mod_stack->CalculateModifiedValue("damage", 50);
    REQUIRE(result == Approx(90).margin(0.01f));
}

TEST_CASE("Unique modifiers replace (don't stack)") {
    auto mod_stack = CreateTestModifierStack();
    
    Modifier unique_mod;
    unique_mod.name = "Slow";
    unique_mod.value = -0.3f;
    unique_mod.stacking = StackingType::Unique;
    
    mod_stack->AddModifier(unique_mod);
    REQUIRE(mod_stack->GetModifierCount() == 1);
    
    unique_mod.value = -0.5f;  // Stronger
    mod_stack->AddModifier(unique_mod);  // Replace
    REQUIRE(mod_stack->GetModifierCount() == 1);
    REQUIRE(mod_stack->GetModifiers()[0].value == Approx(-0.5f));
}

TEST_CASE("Expired modifiers removed automatically") {
    auto mod_stack = CreateTestModifierStack();
    
    Modifier temporary;
    temporary.name = "Buff";
    temporary.duration = 1.0f;
    temporary.value = 10;
    
    mod_stack->AddModifier(temporary);
    REQUIRE(mod_stack->GetModifierCount() == 1);
    
    mod_stack->Update(1.1f);  // Expire
    REQUIRE(mod_stack->GetModifierCount() == 0);
}

// tests/ability/test_cooldown_manager.cpp

TEST_CASE("Cooldown prevents ability reuse") {
    auto cooldown_mgr = CreateTestCooldownManager();
    
    cooldown_mgr->StartCooldown("Fireball", 5.0f);
    REQUIRE(cooldown_mgr->IsReady("Fireball") == false);
    
    cooldown_mgr->Update(5.1f);  // Expire
    REQUIRE(cooldown_mgr->IsReady("Fireball") == true);
}
```

---

## INTEGRATION TESTS (Network Module - Week 9)

### Critical Tests

```cpp
// tests/network/test_determinism.cpp

TEST_CASE("Determinism: same input produces exact same result") {
    // Run 1
    auto scene1 = CreateTestScene();
    auto sim1 = DeterministicSimulation(scene1);
    auto inputs = GenerateInputSequence(100);
    
    for (const auto& input : inputs) {
        sim1.Step(input);
    }
    glm::vec3 final_pos_1 = GetCharacterPosition(scene1);
    
    // Run 2 (identical)
    auto scene2 = CreateTestScene();
    auto sim2 = DeterministicSimulation(scene2);
    
    for (const auto& input : inputs) {
        sim2.Step(input);
    }
    glm::vec3 final_pos_2 = GetCharacterPosition(scene2);
    
    // MUST BE PIXEL PERFECT
    REQUIRE(glm::distance(final_pos_1, final_pos_2) < 0.0001f);
}

TEST_CASE("Rollback restores state exactly") {
    auto scene = CreateTestScene();
    auto sim = DeterministicSimulation(scene);
    auto rollback = RollbackManager(scene);
    
    auto inputs = GenerateInputSequence(100);
    
    // Simulate and save
    for (int i = 0; i < 100; i++) {
        sim.Step(inputs[i]);
        rollback.SaveSnapshot(i);
    }
    
    glm::vec3 pos_at_100 = GetCharacterPosition(scene);
    
    // Rollback to 50
    rollback.RollbackToTick(50);
    
    // Replay to 100
    for (int i = 50; i < 100; i++) {
        sim.Step(inputs[i]);
    }
    
    glm::vec3 pos_after_replay = GetCharacterPosition(scene);
    
    // Should match
    REQUIRE(glm::distance(pos_at_100, pos_after_replay) < 0.0001f);
}

TEST_CASE("No RNG without seed") {
    // Scan code for rand() or random() calls not seeded by tick
    std::vector<std::string> violations = ScanForUnseededRNG();
    REQUIRE(violations.empty());  // Fail if found any
}
```

---

## END-TO-END TESTS (Manual in Editor)

### Character Controller E2E

```
TEST: Player Movement Flow

Steps:
1. Create entity with CharacterController
2. Press WASD → Character moves
3. Press Space → Character jumps
4. While airborne, press A/D → Character rotates
5. Land on ground → Character plays land animation
6. Press and hold Shift → Sprint animation, faster speed
7. Release keys → Character stops smoothly

Expected:
✓ Movement responsive (< 50ms latency)
✓ Jump arc smooth and natural
✓ Landing animation plays
✓ Sprint noticeably faster than walk
✓ No physics clipping
✓ No animation popping

Pass?  [✓] [✗]  Notes: _______________
```

### Network Multiplayer E2E (2 clients + server)

```
TEST: Network Two-Player Movement

1. Start server locally
2. Connect Client 1
3. Connect Client 2
4. Client 1: Move forward 10m
5. Client 2: Observe Client 1 moving (with ~100ms delay)
6. Client 2: Jump and rotate in place
7. Client 1: Observe Client 2 jumping and rotating
8. Client 1: Cast ability at Client 2
9. Server validates, applies effect
10. Client 2: Takes damage (sees damage number)

Expected:
✓ Movement visible on remote client
✓ No stuttering or teleporting
✓ Ability validated server-side
✓ Damage applied both clients
✓ No desyncs after 5 minutes
✓ Tolerance for 300ms latency

Pass?  [✓] [✗]  Notes: _______________
```

---

## TEST METRICS & COVERAGE

### Coverage Targets

```
Unit test coverage by module:
  CharacterController:  > 85%
  AbilitySystem:        > 80%
  ModifierStack:        > 90%
  NetworkManager:       > 70% (hard to 100%)
  AnimationSystem:      > 75%

Overall project:       > 80%
```

### CI/CD Pipeline

```yaml
# .github/workflows/test.yml
name: Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Build
        run: cmake --build build --config Debug
      
      - name: Unit Tests
        run: cd build && ctest -C Debug --output-on-failure
      
      - name: Code Coverage
        run: |
          # Generate coverage report
          cd build
          OpenCppCoverage --source engine --export_type cobertura coverage.xml
      
      - name: Upload Coverage
        uses: codecov/codecov-action@v2
```

### Weekly Test Report

```
WEEK 5 TEST REPORT
==================

Unit Tests:
  Character:  15/15 passed ✓
  Ability:    20/20 passed ✓
  Modifier:   12/12 passed ✓
  Total:      47/47 passed (100%)

Integration Tests:
  Character + Physics: 8/8 passed ✓
  Character + Animation: 6/6 passed ✓
  Ability + Character: 5/5 passed ✓
  Total: 19/19 passed (100%)

Coverage:
  Character Controller: 87%
  Ability System: 82%
  Modifier: 91%
  Average: 87%

Performance:
  Unit tests: 2.3s
  Integration: 8.1s
  Total: 10.4s

Failures This Week:
  - None

Blockers:
  - None
```

---

