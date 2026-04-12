# Integration Checkpoints & Validation — Phase 6 Milestones

**Purpose:** Specify exactly when systems must integrate, what to validate, and expected test outputs

---

## CHECKPOINT 1: Character Controller Base (Week 1-2)

### When: End of Week 2
**What's ready:** InputBuffer, CharacterState base class, 8 states, ground detection

### Integration Points
```
CharacterController ← InputBuffer (input capture)
CharacterController → Physics (velocity, impulse)
CharacterController → Animator (state, blending)
```

### Pre-Integration Checklist
- [ ] InputBuffer class compiles standalone
- [ ] CharacterState interface defined, all 8 states implement it
- [ ] Ground detection raycast code works
- [ ] State machine compiles (no circular deps)
- [ ] PhysicsBody interface exposed (velocity getter/setter)
- [ ] Animator interface exposed (Play, SetBlendParam methods)

### Integration Steps
1. Create CharacterController.cpp
2. Bind PhysicsBody to character (get component from entity)
3. Bind Animator to character (get component from entity)
4. Bind InputBuffer to character
5. In Editor: Create test entity with CharCtrl + Physics + Animator

### Validation Tests (Auto or Manual)

#### Unit Tests: InputBuffer
```cpp
TEST_CASE("InputBuffer stores 6 frames") {
    InputBuffer buffer(6);
    InputAction input;
    input.axis_forward = 1.0f;
    
    for (int i = 0; i < 6; i++) {
        buffer.BufferInput(input);
    }
    
    REQUIRE(buffer.GetInputForTick(0) == input);
    REQUIRE(buffer.GetInputForTick(5) == input);
}

TEST_CASE("InputBuffer circular: oldest overwrites") {
    InputBuffer buffer(3);
    buffer.BufferInput({1, 0});  // Tick 0
    buffer.BufferInput({2, 0});  // Tick 1
    buffer.BufferInput({3, 0});  // Tick 2
    buffer.BufferInput({1, 0});  // Tick 3 (overwrites Tick 0)
    
    REQUIRE(buffer.GetInputForTick(0).axis_forward == 1); // Just overwritten
}
```

#### Integration Tests: CharacterController + Physics
```cpp
TEST_CASE("Character accelerates in movement direction") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto physics = entity->GetComponent<RigidBodyComponent>();
    
    InputAction input;
    input.axis_forward = 1.0f;
    input.axis_right = 0.0f;
    
    char_ctrl->ProcessInput(input);
    char_ctrl->Update(FIXED_TIMESTEP);
    
    glm::vec3 velocity = physics->GetVelocity();
    REQUIRE(velocity.z > 0);  // Moving forward
    REQUIRE(velocity.y == 0); // Not flying
}

TEST_CASE("Character stops on zero input") {
    auto entity = CreateTestEntity();
    auto char_ctrl = entity->GetComponent<CharacterController>();
    auto physics = entity->GetComponent<RigidBodyComponent>();
    
    // Give some velocity
    physics->SetVelocity(glm::vec3(5, 0, 0));
    
    // Zero input should stop horizontal movement (with friction)
    InputAction input = {};
    char_ctrl->ProcessInput(input);
    char_ctrl->Update(FIXED_TIMESTEP);
    
    glm::vec3 velocity = physics->GetVelocity();
    REQUIRE(glm::length(glm::vec2(velocity.x, velocity.z)) < 4); // Decelerated
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
    REQUIRE(velocity.y > 10); // Jump force applied
}
```

#### Manual Validation: Editor
1. Create entity with CharController + Physics + Animator
2. Press WASD → Character moves in viewport
3. Press Space → Character jumps (visual arc)
4. Release keys → Character stops
5. Check animation blending smoothly
6. **Expected:** Movement is responsive, no stuttering

### Success Criteria
- ✅ All unit tests pass
- ✅ All integration tests pass
- ✅ Manual validation passes
- ✅ Zero crashes on state transitions
- ✅ Frame time < 2ms for character update

### Common Failures & Fixes

| Issue | Cause | Fix |
|-------|-------|-----|
| State transitions hang | Missing state.OnExit() call | Add transition logging |
| Physics doesn't apply velocity | Physics component not bound | Check GetComponent<>() returns valid |
| Raycast fails | Ray origin wrong | Debug draw raycast lines |
| Animation doesn't play | Animator component null | Verify animation loaded in asset browser |
| Character slides infinitely | No friction applied | Check physics material friction > 0 |

---

## CHECKPOINT 2: Animation Integration (Week 2-3)

### When: During Week 3
**What's ready:** Blend trees, root motion, state callbacks

### Integration Points
```
CharacterController → Animator.SetBlendParameter()
Animator → Root motion applied to transform
Animator → Bone matrices for ragdoll physics
Animator → Event callbacks for state exit
```

### Pre-Integration Checklist
- [ ] All animations imported (Jump, Land, Idle, Walk, Run, Sprint, Dash)
- [ ] Blend tree 1D created (Speed parameter drives idle→walk→run)
- [ ] Root motion extracted from animations
- [ ] Animation events set up (JumpEnd event on landing)

### Integration Tests

```cpp
TEST_CASE("Blend tree interpolates between animations") {
    auto animator = CreateTestAnimator();
    
    animator->SetBlendParameter("speed", 0.0f);
    auto idle_time = animator->GetCurrentAnimationTime();
    
    animator->SetBlendParameter("speed", 1.0f);
    auto run_time = animator->GetCurrentAnimationTime();
    
    // Weight should blend smoothly
    REQUIRE(idle_time != run_time);
}

TEST_CASE("Root motion moves character") {
    auto entity = CreateTestEntity();
    auto animator = entity->GetComponent<AnimationComponent>();
    auto transform = entity->GetComponent<TransformComponent>();
    
    glm::vec3 start_pos = transform->GetPosition();
    
    animator->Play("WalkForward");
    for (int i = 0; i < 60; i++) {  // 1 second
        animator->Update(FIXED_TIMESTEP);
        glm::vec3 root_motion = animator->GetRootMotion();
        transform->Translate(root_motion);
    }
    
    glm::vec3 end_pos = transform->GetPosition();
    REQUIRE(glm::distance(start_pos, end_pos) > 1.0f);
}

TEST_CASE("Animation event fires callback") {
    auto animator = CreateTestAnimator();
    
    bool event_fired = false;
    animator->OnAnimationEvent.Subscribe([&](const std::string& name) {
        if (name == "JumpEnd") {
            event_fired = true;
        }
    });
    
    animator->Play("Jump");
    for (int i = 0; i < animator->GetAnimationLength("Jump") * 60; i++) {
        animator->Update(FIXED_TIMESTEP);
    }
    
    REQUIRE(event_fired);
}
```

### Manual Validation
1. Play each animation individually
2. Verify blend tree transitions smoothly
3. Check root motion moves character correctly
4. Verify animation events trigger at right times
5. **Expected:** Silky smooth animation blending, no frame hitches

---

## CHECKPOINT 3: Ability System Base (Week 4-5)

### When: End of Week 5
**What's ready:** Ability activation, cooldown, basic effects

### Integration Points
```
CharacterController → AbilitySystem.ActivateAbility()
AbilitySystem → Animator.Play()
AbilitySystem → Stats.ReduceMana()
AbilitySystem → CooldownManager
```

### Pre-Integration Checklist
- [ ] Ability class with properties (cost, cooldown, cast time)
- [ ] Effect system (Damage, Healing, Status)
- [ ] CooldownManager compiles and manages timers
- [ ] CharacterStats stores mana
- [ ] Ability database loaded from config

### Integration Tests

```cpp
TEST_CASE("Character cannot cast without mana") {
    auto entity = CreateCharacterWithAbilities();
    auto stats = entity->GetComponent<CharacterStats>();
    auto ability_sys = entity->GetComponent<AbilitySystem>();
    
    stats->SetMana(10);  // Not enough for Fireball (cost 50)
    
    ActivationContext ctx;
    ctx.caster = entity;
    bool result = ability_sys->ActivateAbility("Fireball", ctx);
    
    REQUIRE(result == false);
}

TEST_CASE("Ability cooldown prevents reuse") {
    auto entity = CreateCharacterWithAbilities();
    auto ability_sys = entity->GetComponent<AbilitySystem>();
    
    ActivationContext ctx;
    ctx.caster = entity;
    
    ability_sys->ActivateAbility("Fireball", ctx);
    
    // Immediately try again
    bool result = ability_sys->ActivateAbility("Fireball", ctx);
    REQUIRE(result == false);  // On cooldown
    
    // Wait for cooldown
    ability_sys->Update(5.1f);  // 5.0s cooldown + 0.1s buffer
    
    result = ability_sys->ActivateAbility("Fireball", ctx);
    REQUIRE(result == true);  // Ready again
}

TEST_CASE("Ability consumes mana on cast") {
    auto entity = CreateCharacterWithAbilities();
    auto stats = entity->GetComponent<CharacterStats>();
    
    float mana_before = stats->GetMana();
    
    ActivationContext ctx;
    ctx.caster = entity;
    ability_sys->ActivateAbility("Fireball", ctx);
    
    float mana_after = stats->GetMana();
    REQUIRE(mana_after == mana_before - 50);  // Fireball costs 50
}
```

### Manual Validation
1. Cast ability from UI
2. Verify cooldown timer shows
3. Try to cast again → "On cooldown" message
4. Wait for cooldown, cast again → Success
5. Check mana decreases
6. **Expected:** Smooth ability flow, no crashes

---

## CHECKPOINT 4: Modifier Stack (Week 6-7)

### When: During Week 7
**What's ready:** Modifier stacking, pipeline (Add→Multiply→Set)

### Integration Points
```
AbilitySystem → ModifierStack.AddModifier()
ModifierStack → Stats.CalculateDamageReduction()
CooldownManager → ModifierStack (on ability haste buff)
```

### Pre-Integration Checklist
- [ ] Modifier struct defined
- [ ] Add/Multiply/Set operations implemented
- [ ] Stacking rules enforced (unique vs stackable)
- [ ] Expiration handling works

### Integration Tests

```cpp
TEST_CASE("Modifier pipeline: Add then Multiply") {
    auto entity = CreateTestEntity();
    auto mod_stack = entity->GetComponent<ModifierStack>();
    
    // Base damage 50
    // Add 10
    // Multiply by 1.5
    // Expected: (50 + 10) * 1.5 = 90
    
    mod_stack->AddModifier({"DamageBuff", 10, ModifierOp::Add});
    mod_stack->AddModifier({"DamageAmp", 1.5f, ModifierOp::Multiply});
    
    float result = mod_stack->CalculateModifiedValue("damage", 50);
    REQUIRE(result == 90);
}

TEST_CASE("Expired modifiers remove automatically") {
    auto mod_stack = CreateTestModifierStack();
    
    auto mod = Modifier{"Slow", -0.5f, ModifierOp::Multiply};
    mod.duration = 1.0f;
    
    mod_stack->AddModifier(mod);
    REQUIRE(mod_stack->GetActiveModifierCount() == 1);
    
    mod_stack->Update(1.1f);  // Expire
    REQUIRE(mod_stack->GetActiveModifierCount() == 0);
}

TEST_CASE("Unique modifiers don't stack") {
    auto mod_stack = CreateTestModifierStack();
    
    Modifier slow;
    slow.name = "Slow";
    slow.intensity = -0.3f;
    slow.stacking_type = StackingType::Unique;  // Only 1 allowed
    
    mod_stack->AddModifier(slow);
    slow.intensity = -0.5f;  // Stronger slow
    mod_stack->AddModifier(slow);  // Should replace
    
    REQUIRE(mod_stack->GetActiveModifierCount() == 1);
    auto active = mod_stack->GetModifiers()[0];
    REQUIRE(active.intensity == -0.5f);  // Updated
}
```

### Manual Validation
1. Apply buff → Damage increases (+ operator)
2. Apply damage amp → Damage increases again (* operator)
3. Check UI shows all active modifiers
4. Wait for buff to expire → Modifier removed
5. **Expected:** Damage values increase as expected

---

## CHECKPOINT 5: Network Foundation (Week 8-9)

### When: End of Week 9 (CRITICAL)
**What's ready:** DeterministicSimulation, InputBuffer synced, snapshots

### Integration Points
```
NetworkManager → DeterministicSimulation
DeterministicSimulation → CharacterController.Update()
DeterministicSimulation → Physics.Step()
RollbackManager → Save/Restore entity state
```

### Pre-Integration Checklist
- [ ] DeterministicSimulation compiles with 15ms fixed timestep
- [ ] RollbackManager can save/restore state
- [ ] InputBuffer integrated with NetworkManager
- [ ] Tick counter synchronized

### Integration Tests (CRITICAL)

```cpp
TEST_CASE("Deterministic simulation: same input = same result") {
    // Run 1: Simulate with specific input sequence
    auto scene1 = CreateTestScene();
    auto sim1 = DeterministicSimulation(scene1);
    
    std::vector<InputAction> inputs = GenerateTestInputSequence();
    for (auto& input : inputs) {
        sim1.Step(input);
    }
    glm::vec3 final_pos_1 = scene1->GetEntityById(1)->GetComponent<TransformComponent>()->GetPosition();
    
    // Run 2: Repeat exact same steps
    auto scene2 = CreateTestScene();
    auto sim2 = DeterministicSimulation(scene2);
    
    for (auto& input : inputs) {
        sim2.Step(input);
    }
    glm::vec3 final_pos_2 = scene2->GetEntityById(1)->GetComponent<TransformComponent>()->GetPosition();
    
    // **MUST BE IDENTICAL**
    REQUIRE(final_pos_1 == final_pos_2);
    REQUIRE(glm::distance(final_pos_1, final_pos_2) < 0.0001f);
}

TEST_CASE("Rollback and replay reproduces state") {
    auto scene = CreateTestScene();
    auto sim = DeterministicSimulation(scene);
    auto rollback = RollbackManager(scene);
    
    std::vector<InputAction> inputs = GenerateTestInputSequence(100);
    
    // Simulate 100 ticks
    for (int i = 0; i < 100; i++) {
        sim.Step(inputs[i]);
        rollback.SaveSnapshot(i);
    }
    
    glm::vec3 pos_at_100 = scene->GetEntityById(1)->GetComponent<TransformComponent>()->GetPosition();
    
    // Rollback to tick 50
    rollback.RollbackToTick(50);
    glm::vec3 pos_at_50 = scene->GetEntityById(1)->GetComponent<TransformComponent>()->GetPosition();
    
    // Replay from tick 50 to 100
    for (int i = 50; i < 100; i++) {
        sim.Step(inputs[i]);
    }
    
    glm::vec3 pos_after_replay = scene->GetEntityById(1)->GetComponent<TransformComponent>()->GetPosition();
    
    // Should match original position at tick 100
    REQUIRE(glm::distance(pos_at_100, pos_after_replay) < 0.0001f);
}

TEST_CASE("Network tick synchronizes with simulation") {
    auto network = NetworkManager();
    network.SetTickRate(66);  // 66 Hz = 15ms per tick
    
    int tick_count = 0;
    for (int i = 0; i < 1000; i++) {
        network.Update(16.67f);  // 60 FPS frame time
        
        // Should process 0 or 1 ticks per frame (sometimes 2)
        int expected_ticks = (i + 1) * 66 / 60 - tick_count;
        REQUIRE(expected_ticks <= 1);
    }
}
```

### Manual Validation (Network Play Test)
1. Launch 2 clients locally
2. Connect to test server
3. Player 1 moves forward
4. Player 2 sees Player 1 move (with ~100ms delay)
5. Player 1 jumps
6. Player 2 sees jump with smooth interpolation
7. **Expected:** Smooth multiplayer, no jitter or rollback visible

### Success Criteria (CRITICAL)
- ✅ Deterministic test passes (MUST BE PIXEL-PERFECT)
- ✅ Rollback+replay test passes
- ✅ Network players see each other move
- ✅ No desync after 10 minutes of play
- ✅ Latency up to 300ms tolerated

---

## CHECKPOINT 6: Ability + Network Integration (Week 10)

### When: During Week 10
**What's ready:** Abilities work over network, server-auth validation

### Integration Points
```
LocalCharacter → ActivateAbility() → NetworkManager.SendAbility()
NetworkManager → Server validation
Server → Broadcast effect → RemoteCharacters
RemoteCharacter → Apply effect locally
```

### Integration Tests

```cpp
TEST_CASE("Server rejects out-of-range ability") {
    auto server = CreateTestServer();
    auto client = CreateTestClient();
    
    client->GetCharacter()->SetPosition(glm::vec3(100, 0, 0));
    client->GetCharacter()->ActivateAbility("Fireball", {range: 10});
    
    server->Update();
    
    // Server should reject: target at (100+15=115), out of range
    auto error = server->GetLastValidationError();
    REQUIRE(error.Contains("Out of range"));
}

TEST_CASE("Multiple clients cast same ability simultaneously") {
    auto server = CreateTestServer();
    auto client1 = CreateTestClient();
    auto client2 = CreateTestClient();
    
    // Both cast at same time (on host clock, slightly different)
    client1->ActivateAbility("Fireball");
    client2->ActivateAbility("Fireball");
    
    server->Update();
    
    // Both abilities should process
    auto broadcasts = server->GetBroadcasts();
    REQUIRE(broadcasts.size() == 2);
}
```

---

## CHECKPOINT 7: AI Integration (Week 11)

### When: During Week 11
**What's ready:** AI can use character controller, pathfinding, behavior tree

### Integration Tests
```cpp
TEST_CASE("AI character uses CharacterController") {
    auto ai_entity = CreateAICharacter();
    auto ai = ai_entity->GetComponent<AIComponent>();
    auto char_ctrl = ai_entity->GetComponent<CharacterController>();
    
    ai->SetTarget(player_position);
    ai->Update(FIXED_TIMESTEP);
    
    InputAction ai_input = ai->GetComputedInput();
    char_ctrl->ProcessInput(ai_input);
    
    // AI should have moved toward target
    glm::vec3 new_pos = ai_entity->GetComponent<TransformComponent>()->GetPosition();
    REQUIRE(glm::distance(new_pos, ai->GetTarget()) < initial_distance);
}
```

---

## WEEKLY VALIDATION CHECKLIST

Use this every Friday:

```
WEEK 1-2: Character Controller
□ InputBuffer unit tests pass
□ CharacterController compiles
□ All 8 states implemented
□ Movement works in viewport
□ Jump works visually
□ No crashes in 5 min play session

WEEK 3: Animation
□ All animations play
□ Blend tree smooth
□ Root motion works
□ Events fire correctly
□ No animation popping

WEEK 4-5: Abilities
□ Can cast ability
□ Ability on cooldown
□ Mana consumed
□ Animation plays during cast
□ Effects appear (VFX)

WEEK 6-7: Modifiers
□ Damage buff works
□ Stacking correct
□ Modifiers expire
□ UI shows modifiers

WEEK 8-9: Network
□ Deterministic test passes
□ 2-client connection works
□ Movement syncs
□ No desyncs in 30 min

WEEK 10: Network Abilities
□ Ability works on network
□ Server validates
□ Remote sees ability effect
□ Damage applies on server

WEEK 11-12: AI & Polish
□ AI moves toward target
□ AI uses abilities
□ Performance < 16ms total
□ No memory leaks (5 min)
```

---

## PASS/FAIL CRITERIA BY CHECKPOINT

| Checkpoint | Must Pass | Nice to Have |
|------------|-----------|-------------|
| 1: Character Controller | Unit tests 100%, manual play smooth | State logging works |
| 2: Animation | All anims play, blend tree smooth | Root motion pixel-perfect |
| 3: Abilities | Cooldown works, mana consumed | UI shows casting bar |
| 4: Modifiers | Pipeline math correct | Visual indicators |
| 5: Network ⚠️ **CRITICAL** | Deterministic identical positions | Network desync < 0.1m |
| 6: Network Abilities | Server validates correctly | Ability effects networked |
| 7: AI | AI uses char controller | AI casts abilities |

**Legend:**
- 🟢 Must pass or checkpoint FAILS
- 🟡 Missing nice-to-have delays next checkpoint
- ⚠️ Network checkpoint extra critical (cannot retrofit)

---

