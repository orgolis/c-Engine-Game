# System Interactions & Integration — Detailed Specification

**Date:** April 12, 2026  
**Purpose:** Show exactly how each system calls and communicates with other systems  
**Scope:** All Phase 6 systems + existing rendering/editor systems

---

## INTERACTION MATRIX

Shows which system calls methods on which other systems:

```
                    Char  Ability Network Physics Animation Editor  Stats  Input
Character Ctrl:     -     ★★★★   ★★★★    ★★★★   ★★★★     ★★     ★★★★   ★★★★
Ability System:     ★★    -      ★★★     -      ★★★      ★       ★★★    -
Network Manager:    ★★★★  ★★     -       ★★     ★★       ★★      -      -
AI System:          ★★★   ★★★    ★★      ★★     ★★       -       ★      -
Physics World:      ★★★   -      -       -      -        -       -      -
Animation System:   ★★    ★      -       -      -        ★       -      -
Editor:             ★★★   ★★     -       ★★     ★★       -       ★★     -

★ = calls method(s), ★★★★ = heavy interaction
```

---

## CHARACTER CONTROLLER → OTHER SYSTEMS

### 1. Character Controller → Physics World

```cpp
// IN: CharacterController::Update()
void CharacterController::Update(float delta_time) {
    // 1. Get current velocity from physics
    glm::vec3 current_velocity = physics_body_->GetVelocity();
    
    // 2. Process input (WASD, Jump, Dash)
    glm::vec3 desired_velocity = CalculateDesiredVelocity();
    
    // 3. Apply to physics body
    physics_body_->SetVelocity(desired_velocity);
    
    // 4. Raycast for ground detection
    RaycastHit hit;
    bool is_on_ground = physics_world_->Raycast(
        position - vec3(0, radius, 0),
        vec3(0, -1, 0),
        0.15f,
        hit
    );
    
    // 5. Apply impulse for jump
    if (state_ == CharacterStateType::Jump) {
        physics_body_->ApplyImpulse(vec3(0, jump_force_, 0));
    }
}

// DATA FLOW:
// CharController ← ReadVelocity() ← PhysicsBody
// CharController → SetVelocity() → PhysicsBody
// CharController → Raycast() → PhysicsWorld
// CharController ← RaycastResult ← PhysicsWorld
// CharController → ApplyImpulse() → PhysicsBody
```

**Calling Pattern:**
- **Every frame:** GetVelocity(), SetVelocity(), Raycast()
- **On jump:** ApplyImpulse()
- **On land:** ResetAngularVelocity()

**Timing:** Physics.Step(FIXED_TIMESTEP) happens BEFORE CharacterController.Update()

---

### 2. Character Controller → Animation System

```cpp
// IN: CharacterController::UpdateAnimation()
void CharacterController::UpdateAnimation(float delta_time) {
    // 1. Get animation component
    auto animator = entity_->GetComponent<AnimationComponent>();
    if (!animator) return;
    
    // 2. Determine animation state from movement
    float move_speed = glm::length(input_direction_);
    AnimationState target_anim = locomotion_tree_->GetAnimation(move_speed);
    
    // 3. Set blend parameter
    animator->SetBlendParameter("speed", move_speed);
    
    // 4. Play animation with blending
    animator->Play(target_anim.animation_name, true);  // Looping
    animator->SetBlendWeight(target_anim.blend_weight);
    
    // 5. Handle special animations (jump, land, etc)
    if (current_state_ == CharacterStateType::Jump) {
        animator->PlayOnce("JumpStart");  // Non-looping, has events
    }
    
    // 6. Apply root motion (animation drives position)
    glm::vec3 root_motion = animator->GetRootMotion();
    transform_->Translate(root_motion);  // Character moves with animation
    
    // 7. Update skeleton (for physics colliders on bones)
    const auto& skeleton = animator->GetSkeleton();
    for (uint32_t i = 0; i < skeleton->GetBoneCount(); i++) {
        // Bone transforms available for physics queries
        glm::mat4 bone_world_matrix = skeleton->GetBoneWorldMatrix(i);
    }
}

// EVENT CALLBACKS:
animator->OnAnimationComplete.Subscribe([this](const std::string& anim_name) {
    if (anim_name == "JumpStart") {
        // Jump animation finished ascending, blend to descent
        animator_->Play("JumpDescent");
    } else if (anim_name == "Land") {
        transitioning_to_idle_ = true;
    }
});

// DATA FLOW:
// CharController → SetBlendParameter() → Animator
// CharController → Play() → Animator
// CharController → SetBlendWeight() → Animator
// CharController ← GetRootMotion() ← Animator
// CharController ← OnAnimationComplete event ← Animator
// CharController ← GetBoneWorldMatrix() ← Skeleton
```

**Calling Pattern:**
- **Every frame:** SetBlendParameter(), GetRootMotion()
- **On state change:** Play()
- **On animation finish:** Event callback triggers state transitions

---

### 3. Character Controller → Ability System

```cpp
// IN: CharacterController::ProcessInput()
void CharacterController::ProcessInput(const InputAction& input) {
    // Process movement input first
    input_direction_ = vec3(input.axis_right, 0, input.axis_forward);
    
    // Then process ability input
    if (input.ability_pressed && input.ability_slot < 3) {
        std::string ability_name = ability_system_->GetSlottedAbility(input.ability_slot);
        
        // Attempt activation
        ActivationContext ctx;
        ctx.caster = this;
        ctx.caster_entity = entity_;
        ctx.target_position = GetTargetPosition();
        ctx.animator = animator_;
        
        bool success = ability_system_->ActivateAbility(ability_name, ctx);
        
        if (!success) {
            SPDLOG_DEBUG("Cannot activate {}: {}", ability_name, 
                ability_system_->GetLastError());
        }
    }
    
    // Dash is a special ability
    if (input.dash_pressed && ability_system_->CanActivateAbility("Dash")) {
        ability_system_->ActivateAbility("Dash", ctx);
    }
}

// ABILITY CALLBACKS BACK TO CHARACTER:
ability_system_->OnAbilityCast.Subscribe([this](const std::string& ability_name) {
    // Interrupt current movement during cast time
    if (ability_system_->GetAbility(ability_name)->GetCastTime() > 0) {
        current_state_->OnInterrupt();
        transitioning_to_ability_cast_ = true;
    }
});

ability_system_->OnAbilityComplete.Subscribe([this](const std::string& ability_name) {
    // Transition back to idle/walk after ability
    transitioning_to_ability_cast_ = false;
    TransitionTo(CharacterStateType::Idle);
});

// DATA FLOW:
// CharController → ActivateAbility() → AbilitySystem
// CharController ← CanActivateAbility() ← AbilitySystem
// CharController ← GetSlottedAbility() ← AbilitySystem
// CharController ← OnAbilityCast event ← AbilitySystem
// CharController ← OnAbilityComplete event ← AbilitySystem
```

**Calling Pattern:**
- **On input:** ActivateAbility(), GetSlottedAbility()
- **Each frame:** CanActivateAbility() (for UI)
- **Events:** OnAbilityCast, OnAbilityComplete

---

### 4. Character Controller → Character Stats (Health/Stamina)

```cpp
// IN: CharacterController::Update()
void CharacterController::Update(float delta_time) {
    auto stats = GetComponent<CharacterStats>();
    
    // 1. SPRINT costs stamina
    if (current_state_ == CharacterStateType::Sprint) {
        stats->ReduceStamina(20.0f * delta_time);  // 20 per second
    }
    
    // 2. REGENERATE stamina when resting
    if (current_state_ == CharacterStateType::Idle) {
        stats->RegenerateStamina(5.0f * delta_time);  // 5 per second
    }
}

// TAKE DAMAGE (from outside - networking, enemies, etc):
void CharacterController::TakeDamage(
    float damage, 
    DamageType damage_type,
    const glm::vec3& knockback_direction
) {
    auto stats = GetComponent<CharacterStats>();
    
    // 1. Apply damage with resistances
    float effective_damage = stats->CalculateDamageReduction(damage, damage_type);
    stats->TakeDamage(effective_damage);
    
    // 2. Apply knockback
    if (glm::length(knockback_direction) > 0.1f) {
        physics_body_->ApplyImpulse(
            glm::normalize(knockback_direction) * effective_damage * 0.5f
        );
        TransitionTo(CharacterStateType::Fall);
    }
    
    // 3. Play hit reaction
    animator_->PlayOnce("HitReaction");
    
    // 4. Fire events
    on_damage_taken_.Invoke(effective_damage, damage_type);
    
    // 5. Check if dead
    if (stats->GetHealth() <= 0) {
        TransitionTo(CharacterStateType::Dead);
        on_death_.Invoke();
    }
}

// DATA FLOW:
// CharController → ReduceStamina() → Stats
// CharController → RegenerateStamina() → Stats
// CharController → TakeDamage() → Stats
// CharController ← CalculateDamageReduction() ← Stats
// CharController → TakeDamage() event → UI/Network
```

**Calling Pattern:**
- **While sprinting:** ReduceStamina() every frame
- **While idle:** RegenerateStamina() every frame
- **On damage event:** TakeDamage()
- **Callbacks:** on_damage_taken_, on_death_

---

### 5. Character Controller → Network Manager

```cpp
// IN: NetworkedCharacterController::Update()
class NetworkedCharacterController : public CharacterController {
    bool is_local_player_;
    uint32_t network_player_id_;
    
    void Update(float delta_time) override {
        if (is_local_player_) {
            // LOCAL PLAYER: Update normally, then send to server
            CharacterController::Update(delta_time);
            
            // Send movement state to server
            MovementStatePacket packet;
            packet.player_id = network_player_id_;
            packet.tick = network_manager_->GetCurrentTick();
            packet.position = GetPosition();
            packet.rotation = GetRotation();
            packet.velocity = physics_body_->GetVelocity();
            packet.state = current_state_;  // Animation state name
            packet.input = last_input_;
            
            network_manager_->SendMovementState(packet);
            
        } else {
            // REMOTE PLAYER: Receive state from network
            MovementStatePacket latest = network_manager_->GetLatestMovement(network_player_id_);
            
            // Interpolate to new position
            target_position_ = latest.position;
            InterpolateToTarget(delta_time, 0.1f);  // Smooth over 100ms
            
            // Update animation from remote state
            if (latest.state != current_state_) {
                TransitionTo(latest.state);
            }
            
            // Update velocity (for physics objects they interact with)
            physics_body_->SetVelocity(latest.velocity);
        }
    }
};

// DATA FLOW:
// LocalCharController → SendMovementState() → NetworkManager
// NetworkManager → ReceiveMovement() → RemoteCharController
// LocalCharController → GetCurrentTick() ← NetworkManager
```

**Calling Pattern:**
- **Local player, every frame:** SendMovementState()
- **Remote player, every 66ms tick:** ReceiveMovementState()

---

## ABILITY SYSTEM → OTHER SYSTEMS

### 1. Ability System → Character Controller

```cpp
// IN: AbilitySystem::ActivateAbility()
void AbilitySystem::ActivateAbility(const std::string& name, ActivationContext& ctx) {
    auto ability = abilities_[name];
    
    // 1. Query character stats (can afford cost?)
    auto stats = ctx.caster->GetStats();
    if (stats->GetMana() < ability->GetCost().mana) {
        SPDLOG_WARN("Not enough mana");
        return;
    }
    
    // 2. Get animator from character
    auto animator = ctx.animator;
    if (ability->GetCastTime() > 0) {
        animator->PlayOnce(ability->GetAnimationName());
    }
    
    // 3. Get ability target position (character facing)
    glm::vec3 target = ctx.caster->GetFacingDirection() * ability->GetRange();
}

// ABILITY CALLBACKS TO CHARACTER:
// OnAbilityCast → Character stops movement
// OnAbilityComplete → Character resumes movement
// OnAbilityInterrupt → Character plays interrupt reaction
```

**Calling Pattern:**
- **On activation:** GetStats(), GetAnimator(), GetCastTime()
- **Events:** OnAbilityCast, OnAbilityComplete

---

### 2. Ability System → Modifier Stack

```cpp
// IN: AbilitySystem::ApplyEffects()
void AbilitySystem::ApplyEffects(
    const std::string& ability_name,
    Entity* target
) {
    auto ability = abilities_[ability_name];
    
    for (const auto& effect : ability->GetEffects()) {
        // Damage effect
        if (effect.type == EffectType::Damage) {
            float base_damage = effect.base_damage;
            
            // 1. Query modifiers from ability itself
            float modified_damage = base_damage;
            for (const auto& mod : ability->GetModifiers()) {
                modified_damage = ApplyModifier(modified_damage, mod);
            }
            
            // 2. Query modifiers from target's modifier stack
            auto target_mods = target->GetComponent<ModifierStack>();
            modified_damage = target_mods->CalculateModifiedValue(
                "damage_received", modified_damage
            );
            
            // 3. Apply damage
            auto target_stats = target->GetComponent<CharacterStats>();
            target_stats->TakeDamage(modified_damage, effect.damage_type);
        }
        
        // Status effect
        else if (effect.type == EffectType::Status) {
            // Create modifier from effect
            Modifier status_mod;
            status_mod.name = effect.name;
            status_mod.duration = effect.duration;
            status_mod.value = effect.intensity;
            
            // Apply to target's modifier stack
            auto target_mods = target->GetComponent<ModifierStack>();
            target_mods->AddModifier(status_mod);
        }
    }
}

// DATA FLOW:
// AbilitySystem → ApplyModifier() → Modifier calc
// AbilitySystem → CalculateModifiedValue() ← ModifierStack
// AbilitySystem → AddModifier() → ModifierStack
```

---

### 3. Ability System → Cooldown Manager

```cpp
// IN: AbilitySystem::Update()
void AbilitySystem::Update(float delta_time) {
    // Update cooldowns
    cooldown_manager_.Update(delta_time);
    
    // Check if abilities can be activated (for UI)
    for (const auto& [name, ability] : abilities_) {
        bool ready = cooldown_manager_.IsReady(name);
        on_ability_state_changed_.Invoke(name, ready);  // UI updates
    }
}

// IN: AbilitySystem::ActivateAbility()
void AbilitySystem::ActivateAbility(const std::string& name, ...) {
    // ... validation ...
    
    ability->Activate(ctx);
    
    // Start cooldown
    cooldown_manager_.StartCooldown(name, ability->GetCooldown());
}

// DATA FLOW:
// AbilitySystem → Update() → CooldownManager
// AbilitySystem → IsReady() ← CooldownManager
// AbilitySystem → StartCooldown() → CooldownManager
// AbilitySystem ← on_ability_state_changed event ← CooldownManager
```

---

## NETWORK MANAGER → OTHER SYSTEMS

### 1. Network Manager → Character Controller (Both directions)

```cpp
// NETWORK DETERMINES CHARACTER STATE:
// Server validates ability use, applies damage to all clients
void NetworkManager::ReceiveGameStatePacket(const GameStatePacket& packet) {
    // For each entity in packet
    for (const auto& entity_state : packet.entities) {
        auto entity = scene_->GetEntityById(entity_state.entity_id);
        auto char_controller = entity->GetComponent<CharacterController>();
        
        // 1. Update position (with interpolation for smoothness)
        char_controller->SetTargetPosition(entity_state.position);
        
        // 2. Update animation state
        if (entity_state.animation_state != char_controller->GetCurrentStateName()) {
            char_controller->TransitionTo(entity_state.animation_state);
        }
        
        // 3. Apply damage if in packet (server-authoritative)
        if (entity_state.health < char_controller->GetHealth()) {
            float damage = char_controller->GetHealth() - entity_state.health;
            char_controller->TakeDamage(damage, DamageType::ServerAuthoritative);
        }
    }
}

// CHARACTER SENDS STATE TO NETWORK:
// Network Manager keeps track of local character
void NetworkManager::SetLocalCharacter(CharacterController* char_controller) {
    local_character_ = char_controller;
    
    // Every frame, extract state
    OnFrameUpdate += [this]() {
        if (!local_character_) return;
        
        MovementStatePacket packet;
        packet.position = local_character_->GetPosition();
        packet.velocity = local_character_->GetPhysicsBody()->GetVelocity();
        packet.state = local_character_->GetCurrentStateName();
        packet.tick = network_tick_;
        
        SendToServer(packet);
    };
}

// DATA FLOW:
// Local CharController → Position/State → NetworkManager
// NetworkManager → GameStatePacket → Server
// Server → GameStatePacket → NetworkManager
// NetworkManager → Update CharController ← Remote entities
```

---

### 2. Network Manager → Physics World

```cpp
// DETERMINISTIC PHYSICS SIMULATION:
void NetworkManager::Update(float real_delta_time) {
    accumulated_time_ += real_delta_time;
    
    // Run physics at fixed tick rate (not variable frame rate!)
    while (accumulated_time_ >= FIXED_TIMESTEP) {
        uint64_t current_tick = network_tick_++;
        
        // 1. Get input for this tick from buffer
        InputAction input = input_buffer_.GetInputForTick(current_tick);
        
        // 2. Update character with deterministic input
        local_character_->ProcessInput(input);
        local_character_->Update(FIXED_TIMESTEP);
        
        // 3. Step physics at FIXED timestep (deterministic!)
        physics_world_->Step(FIXED_TIMESTEP);  // Always 15ms, never varies
        
        // 4. Save state snapshot
        rollback_manager_.SaveSnapshot(current_tick);
        
        accumulated_time_ -= FIXED_TIMESTEP;
    }
}

// ROLLBACK ON SERVER CORRECTION:
void NetworkManager::OnReceiveServerState(const GameStatePacket& packet) {
    uint64_t server_tick = packet.server_tick;
    
    // Check if we need to rollback
    uint64_t diff = current_tick_ - server_tick;
    if (diff > 2) {
        SPDLOG_DEBUG("Rollback {} ticks", diff);
        
        // 1. Restore to server state
        rollback_manager_.RollbackToTick(server_tick);
        
        // 2. Replay from server tick to current with local input
        rollback_manager_.FastForwardToTick(current_tick_);
        
        // 3. Smooth correction
        reconciliation_.Reconcile(packet.entities);
    }
}

// DATA FLOW:
// NetworkManager → Step(FIXED_TIME) → PhysicsWorld
// NetworkManager → SaveSnapshot() → RollbackManager
// NetworkManager ← GetState() ← PhysicsWorld
// NetworkManager → RollbackToTick() → PhysicsWorld state restore
```

---

## EDITOR → GAME SYSTEMS

### 1. Editor Creates Components

```cpp
// IN: Editor Inspector Panel
if (ImGui::Button("Add Component")) {
    if (ImGui::MenuItem("Character Controller")) {
        // 1. Create entity with components
        auto char_ctrl = selected_entity_->AddComponent<CharacterController>();
        
        // 2. Create physics body
        auto physics = selected_entity_->AddComponent<RigidBodyComponent>();
        physics->SetCapsuleShape(0.5f, 2.0f);
        
        // 3. Create animator
        auto animator = selected_entity_->AddComponent<AnimationComponent>();
        
        // 4. Create stats
        auto stats = selected_entity_->AddComponent<CharacterStats>();
        
        // 5. Create ability system
        auto ability_sys = selected_entity_->AddComponent<AbilitySystem>();
        
        // 6. Initialize character (binds all systems together)
        char_ctrl->Initialize();
    }
}

// DATA FLOW:
// Editor → AddComponent<T>() → Entity
// Entity → Attach component → Scene
// Editor → Initialize() → CharacterController
// CharacterController → Bind to other components
```

---

### 2. Editor Modifies Properties in Inspector

```cpp
// IN: Editor Inspector Panel - Update from UI sliders
if (ImGui::DragFloat("Movement Speed", &move_speed_param)) {
    auto char_ctrl = selected_entity_->GetComponent<CharacterController>();
    char_ctrl->SetMovementSpeed(move_speed_param);
}

if (ImGui::DragFloat("Jump Force", &jump_force_param)) {
    auto char_ctrl = selected_entity_->GetComponent<CharacterController>();
    char_ctrl->SetJumpForce(jump_force_param);
}

// Updates reflected in viewport immediately
if (ImGui::TreeNode("Abilities")) {
    auto ability_sys = selected_entity_->GetComponent<AbilitySystem>();
    for (const auto& [name, ability] : ability_sys->GetAbilities()) {
        if (ImGui::TreeNode(name.c_str())) {
            float cooldown = ability->GetCooldown();
            if (ImGui::DragFloat("Cooldown", &cooldown)) {
                ability->SetCooldown(cooldown);
            }
            ImGui::TreePop();
        }
    }
}

// DATA FLOW:
// Editor UI → SetMovementSpeed() → CharacterController
// CharacterController → Update position ← Physics
// Physics → Render → Viewport shows change immediately
```

---

## INTERACTION SEQUENCE DIAGRAMS

### Sequence 1: Player Jumps (Jump Animation)

```
Frame 0:
  Input: Space pressed
  ↓
CharacterController.ProcessInput():
  → InputBuffer.BufferInput(jump)
  → Can transition to Jump? (check IsOnGround)
  → ✓ Yes
  ↓
CharacterController.Update():
  → TransitionTo(JumpState)
  → JumpState.OnEnter():
     → Physics.ApplyImpulse(15 m/s up)
     → Animator.Play("JumpStart")
     ↓
     (Animator initializes animation)
  ↓
Render Frame 0: Shows jump start pose

Frames 1-5:
  JumpState.OnUpdate():
    → Check velocity.y
    → Blend animation between JumpStart → JumpApogee
    → Keep physics body active
    ↓
    (Physics gravity naturally pulls down)
    ↓
  Render: Smooth jump arc animation

Frame 6-8:
  Velocity.y becomes negative (falling)
  ↓
  JumpState.OnUpdate():
    → Blend to JumpDescent animation
    → Re-enable ground detection
    ↓
  Render: Falling animation

Frame 10:
  Raycast hits ground
  ↓
  CharacterController:
    → is_on_ground = true
    → TransitionTo(LandState)
    ↓
    LandState.OnEnter():
      → Animator.PlayOnce("Land")
      → Check fall damage
      ↓
  Render: Landing animation

Frame 12:
  Landing animation complete
  ↓
  LandState.OnExit():
    → TransitionTo(IdleState)
    ↓
  CharacterController.Update():
    → Blend animation to Idle
    ↓
  Render: Idle pose (smooth blend)
```

---

### Sequence 2: Player Casts Fireball Ability

```
Frame 0:
  Input: Ability key pressed (Slot 1)
  ↓
CharacterController.ProcessInput():
  → ability_system.ActivateAbility("Fireball", ctx)
  ↓
AbilitySystem.ActivateAbility():
  1. Check can activate:
     → CooldownManager.IsReady("Fireball")  ✓
     → CharacterStats.GetMana() >= cost     ✓
     ↓
  2. Apply cost:
     → CharacterStats.ReduceMana(50)
     ↓
  3. Play animation:
     → Animator.PlayOnce("CastFireball")  (1.5s cast time)
     ↓
  4. Fire event:
     → on_ability_cast.Invoke()
     ↓
CharacterController receives event:
  → TransitionTo(AbilityCastState)
  → Stop movement input processing
  ↓
Frames 1-24:
  AbilitySystem.Update(delta_time):
    → cast_progress += delta_time / cast_time
    → render_vfx_preview()  (growing fireball effect)
    ↓
  Animator.Update():
    → Play "CastFireball" animation
    → triggers event at frame 20 (projectile launch)
    ↓
  on_cast_progress.Invoke(progress) → UI shows cast bar
  ↓
Render: Character in cast pose, animation playing, VFX growing

Frame 24:
  Cast time complete (1.5s ≈ 24 frames @ 60fps)
  ↓
  AbilitySystem.ApplyEffects():
    1. Spawn fireball projectile:
       → scene.SpawnEntity("Fireball")
       → Set position to muzzle_point
       → Set velocity toward target
       ↓
    2. Query modifiers:
       → ModifierStack.CalculateModifiedValue("damage", 50)
       → Check buffs/debuffs
       → Final damage = 55 (5% damage buff applied)
       ↓
    3. Fire event:
       → on_ability_complete.Invoke("Fireball")
       ↓
CharacterController receives event:
  → TransitionTo(PostAbilityState)
  → Allow movement next frame
  ↓
CooldownManager:
  → StartCooldown("Fireball", 5.0f)
  ↓
UI updates:
  → Show cooldown timer "5.0s"
  ↓
Render: Fireball projectile traveling toward target
```

---

### Sequence 3: Network - Other Player Takes Damage

```
LOCAL TIMELINE:
Frame 0-60 (Local):
  Local player casts Fireball
  → Able.ApplyEffects("Fireball")
  → Predict: Opponent takes 50 damage
  → LOCAL opponent HP: 100 → 50
  → Show damage number
  ↓
  SendToServer: "Fireball cast at opponent position"
  ↓
  (100ms network latency)

SERVER TIMELINE (Frame 6-10):
Server receives:
  → Validate: Is ability ready? ✓
  → Validate: Is target in range? ✓
  → Validate: Does attacker have mana? ✓
  → Calculate AUTHORITATIVE damage = 50 (with server modifiers)
  → Opponent HP: 100 → 50
  ↓
  BroadcastDamageEvent:
    {
      attacker_id: local_player_id,
      victim_id: opponent_id,
      damage: 50,
      tick: server_tick
    }
  ↓
  (100ms network latency)

LOCAL TIMELINE (Frame 12+):
Receive DamageEvent from server:
  → damage = 50
  → local_predicted_damage = 50
  → ✓ Match! No rollback needed
  → Reconciliation.Conclude()
  ↓
  Opponent.TakeDamage(50):
    → Opponent.Stats.ReduceHealth(50)
    → Opponent.Animator.PlayOnce("HitReaction")
    → Fire on_damage_taken event
    ↓
  If opponent health <= 0:
    → Opponent.TransitionTo(DeadState)
    → Play death animation
    → Disable physics
    → broadcast_death_event()
```

---

### Sequence 4: Network - Rollback Scenario

```
LOCAL CLIENT STATE:
Tick 0-5: Simulate locally
  Input: Move forward
  → Position predicted: (100, 0, 50)
  ↓
  (100ms network delay)

SERVER RECEIVES Tick 0-5 input at SERVER Tick 7:
  Server simulates Ticks 0-5
  → Position calculated: (99, 0, 52)  [Different! Server had obstacle]
  ↓
  Broadcasts GameStatePacket:
    position: (99, 0, 52)  [Server's calculated position]
    velocity: (0, 0, 1.5)

LOCAL CLIENT (Tick 10):
Receives GameStatePacket for Tick 5:
  local_predicted_position_at_tick_5 = (100, 0, 50)
  server_confirmed_position_at_tick_5 = (99, 0, 52)
  ↓
  Error = distance > THRESHOLD
  ↓
  ROLLBACK TRIGGERED:
  1. RollbackManager.RollbackToTick(5)
     → Restore physics state from Tick 5
     → Restore character state
     → Position = (99, 0, 52)  [Server's version]
     ↓
  2. FastForwardToTick(10)
     → Replay Ticks 6-10 with local input
     → Position progresses from (99, 0, 52)
     → Position now: (99, 0, 58)
     ↓
  3. ReconciliationSystem:
     → Target = (99, 0, 58)
     → Smooth lerp from current toward target
     → Duration: 100ms
     ↓
Render: Character smoothly corrects position (not jumpy)
```

---

## DEPENDENCY GRAPH

```
                        Editor (Top Level)
                           ↓
        ╔═══════════════════════════════════════╗
        ↓                                       ↓
    Scene (ECS)                         Network Manager
        ↓                                       ↓
    ┌───┴───┬───────┬──────────┐   ┌──────────┴─────────┐
    ↓       ↓       ↓          ↓   ↓                    ↓
  Entity Component System      Deterministic Sim   Rollback Mgr
    ↓                              ↓                    ↓
  Transform   Physics   Animation   Stats   Ability    InputBuffer
    ↓          ↓          ↓         ↓         ↓
    ├─────────┼──────────┼─────────┤         ↓
    ↓         ↓          ↓                 Modifiers
 Renderer   Physics     Animator            ↓
    ↓       World       ↓             Cooldown Mgr
    ├─────→ Events ←────┤
    ↓       ↓           ↓
   GPU  Collision   Frame Events
      Detection

CRITICAL PATHS (Do First):
1. Transform → Physics → Renderer
2. Input → CharacterController → Physics
3. CharacterController → Animator
4. AbilitySystem → Modifiers → Stats
5. Network → Input → Deterministic Sim
```

---

## TIMING & SYNCHRONIZATION

### Frame Timing (60 FPS = 16.67ms per frame)

```
RENDER LOOP (every frame):
  ├─ Input polling (1ms)
  │
  ├─ Fixed timestep loop (may run 0-2 ticks):
  │  ├─ Process input for this tick
  │  ├─ Character.Update(15ms)  (1ms)
  │  ├─ Physics.Step(15ms)      (2ms)
  │  ├─ Animation.Update(15ms)  (1ms)
  │  └─ Save state snapshot     (0.1ms)
  │
  ├─ Interpolation for rendering (0.5ms)
  │
  ├─ Ability.Update() - check cooldowns (0.2ms)
  │
  ├─ AI.Update() - pathfinding, decisions (2ms)
  │
  ├─ Network sync - send/receive (0.5ms)
  │  ├─ Send movement state
  │  ├─ Receive game state
  │  └─ Reconcile positions
  │
  ├─ Rendering:
  │  ├─ Frustum cull (1ms)
  │  ├─ Batch render (3ms)
  │  ├─ Deferred lighting (4ms)
  │  ├─ Post-processing (2ms)
  │  └─ Present (1ms)
  │
  └─ Total: ~16ms ✓ At budget
```

### Event Ordering

```
1. INPUT → CharacterController.ProcessInput()
2. CONTROLLER UPDATE → Ability.ActivateAbility()
3. ABILITY UPDATE → Effects.ApplyEffects()
4. EFFECTS → Stats.TakeDamage()
5. STATS → ModifierStack.RemoveExpiredModifiers()
6. PHYSICS → RigidBody.Update()
7. PHYSICS → Collision Detection
8. CHARACTER → Animation Update
9. ANIMATION → Bone Update
10. NETWORK → Send state to server
11. NETWORK → Receive remote state
12. RECONCILE → Smooth correction
13. RENDER → Display final frame
```

---

## ERROR HANDLING & FALLBACKS

### What if Network Packet is Lost?

```
Client Tick 0: Send movement state to server
  → No ACK received
  ↓
Client Tick 6 (100ms later):
  Prediction continued with local input
  Looks correct locally
  ↓
Server broadcasted state for Tick 0:
  → Never received (packet lost)
  ↓
Client doesn't know server disagrees
  → Keep playing
  → Position slowly drifts from server
  ↓
Server broadcasts state for Tick 5:
  → Received at Client Tick 10
  → Position error detected!
  → Rollback + reconcile
  ↓
Net result: ~200ms of prediction before correction
  (Smooth recovery, player doesn't notice packet loss)
```

---

### What if Ability Activation Fails?

```
Client: ActivateAbility("Fireball")
  → Local prediction succeeds
  → Show VFX, play animation
  ↓
Server receives ability packet
  → VALIDATION FAILS: Target out of range
  → DO NOT apply effects
  → Send rejection to client
  ↓
Client receives rejection:
  → Undo local prediction
  → Stop animation
  → Show error message: "Out of range"
  → Refund ability resources
```

---

### What if Character Takes Damage While Climbing?

```
Character state: Climbing
  ↓
TakeDamage(50) event:
  ↓
CharacterController.TakeDamage():
  1. Is climbing state interruptible?
     → Yes, take damage
     ↓
  2. Apply damage:
     → Stats.TakeDamage(50)
     ↓
  3. Interrupt climbing:
     → TransitionTo(FallState)
     → Apply knockback impulse
     → Play "HitReaction" animation
     ↓
  4. Fire events:
     → on_damage_taken.Invoke()
     → on_state_changed.Invoke()
```

---

## SUMMARY TABLE: Who Calls Who

| Source | Target | Method | Frequency | Latency |
|--------|--------|--------|-----------|---------|
| CharCtrl | Physics | SetVelocity() | Every tick | Immediate |
| CharCtrl | Physics | Raycast() | Every tick | Immediate |
| CharCtrl | Animator | Play() | On state change | Immediate |
| CharCtrl | Animator | SetBlendParam() | Every frame | Immediate |
| CharCtrl | AbilitySystem | ActivateAbility() | On input | Immediate |
| CharCtrl | Stats | ReduceStamina() | While acting | Immediate |
| AbilitySystem | ModifierStack | AddModifier() | On effect apply | Immediate |
| AbilitySystem | Stats | TakeDamage() | On effect apply | Immediate |
| Network | CharCtrl | UpdatePosition() | Every ~75ms | 100ms+ |
| Network | Physics | Step() | Every 15ms | Immediate |
| Editor | Scene | AddComponent() | On menu click | Immediate |
| Editor | Property | SetValue() | On UI change | Immediate |

---

## CRITICAL INTERACTION INVARIANTS

### Must Always Be True

1. **Physics runs at fixed timestep**
   - Never vary: always 15ms (66 Hz)
   - Character.Update() before Physics.Step()

2. **Input is deterministic**
   - Same input + same game state = same result
   - No random numbers without deterministic seed

3. **Network packets are ordered**
   - Tick N always processed before Tick N+1
   - Use sequence numbers to detect loss

4. **Animations blend smoothly**
   - Never snap between animations
   - Use blend weights for transitions

5. **Modifiers are ordered**
   - Add → Multiply → Set (never change order)
   - Recalculate whenever modifier list changes

6. **Remote players interpolate**
   - Never jump position
   - Smooth lerp between network updates

7. **Physics collisions are synchronized**
   - All clients see the same collision results
   - Server is authority on collision resolution

8. **Abilities are server-authoritative**
   - Client predicts, server validates
   - Server result wins if different

---

This document shows exactly how every system talks to every other system through method calls, data flows, and event callbacks.
