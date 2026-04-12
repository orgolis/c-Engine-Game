# Code Review Checklist — Phase 6 Quality Gates

**Purpose:** Standardized review criteria to catch issues before integration

**Usage:** Every PR/commit should pass ALL applicable checks before merge

---

## CHARACTER CONTROLLER REVIEW

### Architecture & Design

- [ ] **Inheritance correct**: CharacterStateBase is abstract, all 8 states derive from it
- [ ] **No circular dependencies**: Character doesn't include Physics, Animation headers (only forward decl)
- [ ] **State machine valid**: Transition table has no unreachable states
- [ ] **Physics interface**: Only calls GetVelocity/SetVelocity/Raycast/ApplyImpulse (no direct Physics internals)
- [ ] **Animator interface**: Only calls Play/SetBlendParam/GetRootMotion (abstracted properly)
- [ ] **Events fired correctly**: on_state_changed, on_damage_taken, on_death exist and are invoked

### Code Quality

- [ ] **All functions have docstrings**: What it does, parameters, return value
- [ ] **No magic numbers**: Use named constants (e.g., `const float GRAVITY = 9.81f;`)
- [ ] **Handle null pointers**: Check `if (physics_body_ == nullptr)` before use
- [ ] **Error messages informative**: Not just "Error", but "Character::Update() physics_body is null"
- [ ] **No TODO/FIXME without ticket**: Every TODO has a GitHub issue link
- [ ] **Memory safe**: No raw `new`/`delete`, use smart pointers or stack
- [ ] **Thread safety**: Static methods or singletons are thread-safe (if used in networking)

### Input & Movement

- [ ] **InputBuffer is circular**: Oldest input overwrites, not a crash
- [ ] **Input deadzone applied**: Small stick inputs ignored (e.g., < 0.1)
- [ ] **Movement clamped**: Max speed bounded (no infinite acceleration)
- [ ] **Ground detection robust**: Raycast from slightly below character, not exact center
- [ ] **Slope handling**: Character can walk up/down slopes correctly
- [ ] **Zero input decelerates**: Character slows down when keys released (friction working)

### State Machine

- [ ] **All 8 states have OnEnter/OnUpdate/OnExit**: No missing implementations
- [ ] **State transitions guarded**: Check `CanTransitionTo(state)` before changing
- [ ] **Interrupt handling**: Damage/knockback can interrupt certain states (Idle, Walk, Run, Sprint)
- [ ] **Animation playing on state change**: Every OnEnter calls `animator_->Play()`
- [ ] **Physics applied on state**: Jump adds impulse, Sprint affects max speed
- [ ] **Duration checks**: States like Jump/Dash have timeout fallback transitions

### Jump Mechanics

- [ ] **Jump force reasonable**: ~10-15 units (matches game feel), tested
- [ ] **Apex animation**: Transition from JumpStart → JumpApogee when velocity.y ≈ 0
- [ ] **Airborne collision**: Can't dash or sprint while in air
- [ ] **Multiple jumps prevented**: Can't jump while already jumping (use `is_on_ground_` flag)
- [ ] **Landing detection**: Raycast hits ground → transition to Land state
- [ ] **Fall damage**: Implement fallback (optional, no crash if missing)

### Animation Blending

- [ ] **1D blend tree working**: Speed parameter (0-1) blends Idle → Walk → Run
- [ ] **Smooth blending**: Blend weight eases in/out (not instant snaps)
- [ ] **Root motion applied**: Character position follows animation
- [ ] **Event callbacks**: Animation completion fires OnAnimationComplete event
- [ ] **Interrupt cancels animation**: Damage during animation plays hit reaction

### Networking Ready (If networked character)

- [ ] **State machine deterministic**: Same input = same state (no RNG in state logic)
- [ ] **FastForward doesn't predict**: Can replay 100 ticks in < 1ms
- [ ] **Input buffer is 6 frames**: Handles ~100ms latency
- [ ] **State snapshots save/restore**: Position, velocity, current_state
- [ ] **Network serialization**: CharacterController state fits in < 50 bytes

### Testing

- [ ] **Unit tests include**:
  - [ ] Movement in all directions (WASD)
  - [ ] Jump (apply impulse, transition, land)
  - [ ] State transitions
- [ ] **Integration tests include**:
  - [ ] Physics + Character interaction
  - [ ] Animator + Character blending
  - [ ] Multiple rapid state changes (no crashes)
- [ ] **Manual testing**:
  - [ ] 5 min play session without crashes
  - [ ] No physics clipping
  - [ ] Animations smooth

### Performance

- [ ] **CharacterController::Update() < 0.5ms**: No heavy loops
- [ ] **Raycasts < 0.1ms**: Reuse one raycast per frame
- [ ] **No allocations per frame**: Use object pools or pre-allocated buffers
- [ ] **Memory footprint < 1KB per character**: Check sizeof(CharacterController)

### Documentation

- [ ] **README.md created**: Describes character system, integration points
- [ ] **Header comments**: Every public class/method documented
- [ ] **State diagram included**: Visual of all 8 states and transitions
- [ ] **Example usage**: How to create and use CharacterController

---

## ABILITY SYSTEM REVIEW

### Architecture

- [ ] **Ability hierarchy**: Base class with Active/Passive/Channeled derivation
- [ ] **Effect system abstraction**: Damage, Healing, Status are Effects with polymorphic Apply()
- [ ] **Modifier stack decoupled**: AbilitySystem doesn't directly modify stats, uses ModifierStack
- [ ] **Cooldown manager separate**: Can exist and update independently
- [ ] **No hardcoded abilities**: All abilities loaded from config/database

### Ability Activation

- [ ] **Pre-cast validation**:
  - [ ] Player has enough mana
  - [ ] Ability not on cooldown
  - [ ] Player not dead/stunned
  - [ ] Target in range (if targeted)
- [ ] **Cast time**: If > 0, animation plays and ability can be interrupted
- [ ] **Resource cost deducted**: Mana/stamina spent immediately (not on completion)
- [ ] **Events fired**: on_ability_cast → UI shows bar, on_ability_complete → ui clears bar
- [ ] **No double-cast**: Guard against rapid clicking

### Effects

- [ ] **Damage effect**: Query target stats, apply modifiers, invoke on_damage event
- [ ] **Healing effect**: Respects overcap, clamped to max health
- [ ] **Status effects**: Applies modifier to target, duration tracked
- [ ] **Area effects**: Can query multiple targets (no just single target)
- [ ] **Knockback**: Applies impulse to physics body, moves character

### Modifier Pipeline

- [ ] **Order enforced**: Add → Multiply → Set (never varies)
- [ ] **Stacking rules**: Unique mods replaced, stackable mods added
- [ ] **Expiration handling**: Mods removed on duration = 0
- [ ] **Recalculation**: Modifiers recalculate on add/remove
- [ ] **Math precise**: 0.0001 precision, no floating point errors
- [ ] **Performance**: Calculation < 0.1ms even with 20 stacked modifiers

### Cooldown Manager

- [ ] **Timer accuracy**: Cooldown ticks correctly in Update()
- [ ] **IsReady() state**: Returns true when cooldown_remaining ≤ 0
- [ ] **StartCooldown() replaces**: Don't double-add cooldowns
- [ ] **Reset available**: Can force reset if needed (for debug/testing)
- [ ] **UI integration**: Emits events when state changes

### Database/Config

- [ ] **Ability data separate**: YAML/JSON config, not hardcoded
- [ ] **Hot reload works**: Can edit ability.yaml and reload without restart (nice-to-have)
- [ ] **Validation on load**: Check cost > 0, cooldown >= 0, effects array not empty
- [ ] **Error on missing ability**: "Fireball not found in database" not silent failure

### Networking Ready

- [ ] **Ability name used as ID**: Can serialize "Fireball" not uint32
- [ ] **Effects deterministic**: Same input = same damage (no RNG in damage calc)
- [ ] **Server validates**: Never trust client ability activation
- [ ] **Modification resolved server-side**: Damage calculated by server

### Testing

- [ ] **Unit tests**:
  - [ ] Ability activation succeeds/fails correctly
  - [ ] Cooldown prevents reuse
  - [ ] Cost deducted
  - [ ] Modifier math correct
- [ ] **Integration tests**:
  - [ ] Character activates ability
  - [ ] Animation plays during cast
  - [ ] Effects apply to targets
  - [ ] Modifiers stack correctly
- [ ] **Manual testing**:
  - [ ] Cast ability in editor
  - [ ] See VFX
  - [ ] See cooldown timer
  - [ ] Try to cast again → blocked

### Performance

- [ ] **AbilitySystem::Update() < 0.2ms**: Cooldown update cheap
- [ ] **ActivateAbility() < 1ms**: Effect application not slow
- [ ] **Modifier calculation < 0.1ms**: Even with many mods
- [ ] **Memory < 2KB per ability**: Check sizeof(Ability)

### Documentation

- [ ] **Ability template documented**: How to create new ability
- [ ] **Effect types listed**: Damage, Healing, Status with parameters
- [ ] **Modifier examples**: Add 10, Multiply 1.5, Set 100
- [ ] **Config format documented**: YAML/JSON structure

---

## NETWORK MANAGER REVIEW (Week 9+)

### ⚠️ CRITICAL - Determinism

- [ ] **Same input = same result**: Run simulation twice with identical input, PIXEL-PERFECT match
- [ ] **No random numbers**: All RNG seeded by tick number (no `rand()`)
- [ ] **No platform differences**: Windows/Linux produce same result
- [ ] **Floating point consistent**: Use fixed-point or careful ordering
- [ ] **Test harness**: Automated test runs same simulation twice

### Input Synchronization

- [ ] **Input buffer 6-tick**: Handles ~100ms latency
- [ ] **Server receives input**: Within 1-2 ticks of client sending
- [ ] **Order guaranteed**: Input for Tick N processed before Tick N+1
- [ ] **Loss handling**: Missing packet doesn't crash, next packet continues sequence

### Rollback Mechanism

- [ ] **State snapshot**: Saves position, velocity, animation state every tick
- [ ] **Rollback restores**: Position/velocity/state restored exactly
- [ ] **FastForward replays**: Can replay 100 ticks in < 1ms (perf budget)
- [ ] **No data corruption**: Rollback doesn't corrupt other entities

### Reconciliation

- [ ] **Position error detected**: Server position vs predicted > threshold (0.1m)
- [ ] **Smooth correction**: Lerp from current to correct position over 100ms
- [ ] **No visible jumps**: Player doesn't see character snap
- [ ] **Velocity adjusted**: Not just position, velocity corrected too

### Server Authority

- [ ] **Ability validation server-side**: Client thinks they hit, server confirms
- [ ] **Damage server-calculated**: Client doesn't decide who takes damage
- [ ] **Physics collision server-auth**: Server decides what hits what
- [ ] **Denial on failure**: If ability invalid, client notified to undo

### Performance

- [ ] **Simulation < 2ms per tick**: 15ms tick can absorb 6-7 simultaneous clients
- [ ] **Serialization < 50 bytes**: Per-player state packet compact
- [ ] **Network bandwidth < 10 Kbps**: Per player, even with updates every 100ms
- [ ] **Memory for snapshots**: 6 frames × players × entity size < 100MB

### Testing

- [ ] **Determinism test**: ✓ MUST PASS (pixel-perfect)
- [ ] **Rollback test**: Rollback to Tick 50, replay to Tick 100, same result
- [ ] **2-client network test**: Two players move, see each other smoothly
- [ ] **Latency test**: Simulate 100ms, 200ms, 300ms delays
- [ ] **Packet loss test**: Simulate 5% packet loss, recovery smooth

### Documentation

- [ ] **Determinism guarantees documented**: What NOT to do (no RNG, no platform-specific code)
- [ ] **Rollback mechanism explained**: How snapshots work
- [ ] **Packet format specified**: What data in each network message
- [ ] **Latency budget**: "Tolerates up to 300ms" stated

---

## MODIFIER STACK REVIEW

- [ ] **Add → Multiply → Set order**: Never varies, order is law
- [ ] **Stacking type enforced**: Unique mods don't stack, stackable do
- [ ] **Expiration automatic**: Update() removes expired modifiers
- [ ] **Performance**: Calculation < 0.1ms with 20 modifiers
- [ ] **Recalculation on change**: Every add/remove triggers recalc
- [ ] **Tests numeric**: Expected: 50 + 10 * 1.5 = 90, actual == 90

---

## AI SYSTEM REVIEW (Week 11+)

- [ ] **Uses CharacterController**: AI input in → char controller processes
- [ ] **Behavior tree valid**: No infinite loops, all nodes terminating
- [ ] **Pathfinding optimized**: A* heuristic admissible (never overestimates)
- [ ] **Abilities used correctly**: Casts during combat
- [ ] **Performance < 5ms per AI**: 20 enemies don't exceed frame budget
- [ ] **Memory < 10KB per AI**: Behavior tree + state compact

---

## UNIVERSAL CHECKLIST (All PRs)

- [ ] **No merge conflicts**: Branch up-to-date with main
- [ ] **Builds without warnings**: W4 clean on MSVC
- [ ] **Tests green**: All unit + integration tests pass
- [ ] **No TODO without issue**: Every TODO links `#123`
- [ ] **Memory clean**: No leaks detected (valgrind or VSAnalyzer)
- [ ] **Formatting consistent**: Clang-format or consistent style
- [ ] **No hardcoded paths**: Use CMake variables or constants
- [ ] **Commit message clear**: Describes what changed, why
- [ ] **Documentation updated**: README, design docs match code

---

## REVIEW SEVERITY LEVELS

### 🔴 MUST FIX (Blocks merge)

- Determinism test failing
- Circular dependency
- Memory leak
- Unhandled null pointer
- Performance > budget

### 🟡 SHOULD FIX (Discuss in code review)

- TODO without ticket
- Magic numbers
- Missing docstring
- Performance close to budget

### 🟢 NICE TO FIX (Can merge if other stuff urgent)

- Code style inconsistency
- Extra blank lines
- Verbose variable names
- Over-engineered solution

---

## SIGN-OFF

Code review approved when:
- ✅ All 🔴 items resolved
- ✅ 🟡 items discussed
- ✅ Tests passing
- ✅ Reviewer and author agree

```
Reviewed by: @username
Date: YYYY-MM-DD
Status: ✅ APPROVED
Notes: "Character controller solid, state machine clean. Ready for integration."
```

---

