# Character Controller System — Detailed Specification

**Date:** April 12, 2026  
**Priority:** Critical (Phase 6 immediate)  
**Complexity:** High (Wuthering Waves-style movement)

---

## OVERVIEW

The Character Controller is the heart of player experience. It manages:
- Movement state machine (8+ states)
- Input buffering for responsive controls
- Ground detection and physics integration
- Animation blending and root motion
- Stat tracking and damage application
- Camera management

### Core Philosophy
- **Responsive:** Input buffer enables 1-frame animation cancels
- **Fluid Movement:** Smooth locomotion blending (idle→walk→sprint)
- **Physics-Driven:** Uses RigidBody for all movement
- **Frame-Data Aware:** Ready for frame-perfect combat timing
- **Networked:** Deterministic for multiplayer rollback

---

## INITIALIZATION

```cpp
// In Scene or GameManager
auto player_entity = scene->CreateEntity("Player");
player_entity->AddComponent<TransformComponent>();
player_entity->AddComponent<MeshComponent>();
player_entity->AddComponent<AnimationComponent>();
player_entity->AddComponent<RigidBodyComponent>();

// Create physics body
auto rigidbody = player_entity->GetComponent<RigidBodyComponent>();
rigidbody->SetBodyType(BodyType::Dynamic);
rigidbody->SetCapsuleShape(0.5f, 2.0f); // radius, height

// Create character controller
auto controller = std::make_unique<CharacterController>(player_entity);
controller->Initialize();
game_manager->SetCharacterController(controller);
```

---

## STATE MACHINE DEFINITION

### States (Priority Order)

| State | Conditions | Exit | Animations |
|-------|-----------|------|-----------|
| **Idle** | No input, on ground | Input detected, jump | Idle (multiple variations) |
| **Walk** | Move input <50% speed, on ground | Input stops, sprint input, jump | Walk forward/back/strafe |
| **Sprint** | Move input >50% speed, stamina >0, on ground | No stamina, jump, ability cast | Sprint, tired recovery |
| **Jump** | Jump input + on ground | Landing detected | Jump ascent, apogee, descent |
| **Fall** | Velocity.Y < -threshold, not on ground | Landing detected | Falling, hard land |
| **WallRun** | Jump away from wall + against surface | Jump off wall, fall off | Wall run loop |
| **Climb** | Jump into climbable surface  | No input, jump off | Climb up/down |
| **Slide** | Crouch+Forward at speed >40% | Speed <20%, jump | Slide animation |
| **Dash** | Dash ability input + cooldown ready | End duration, land | Dash start, loop, end |

### State Transition Logic

```cpp
// Priority-based transitions
if (can_jump && is_on_ground && input.jump_pressed) {
    transition_to(JumpState);
    return;
}

if (is_falling && velocity.y < -fall_threshold) {
    transition_to(FallState);
    return;
}

if (wall_detected && velocity.y < 0) {
    transition_to(WallRunState);
    return;
}

if (input.forward > 0 && input.crouch) {
    transition_to(SlideState);
    return;
}

// Locomotion blend tree (idle→walk→sprint)
float move_magnitude = glm::length(input_direction);
if (move_magnitude > 0.5f) {
    transition_to(SprintState);
} else if (move_magnitude > 0.1f) {
    transition_to(WalkState);
} else {
    transition_to(IdleState);
}
```

---

## MOVEMENT MECHANICS

### Input Processing

```cpp
void CharacterController::ProcessInput(const InputFrame& input) {
    // Buffer input for animation cancels (1 frame after move ends)
    input_buffer_.BufferInput(input);
    
    // Process WASD movement
    input_direction = glm::vec3(
        input.axis_right,  // D/A (strafe right)
        0,                  // Y unused for movement
        input.axis_forward  // W/S (forward/back)
    );
    
    // Normalize diagonal movement
    if (glm::length(input_direction) > 0.1f) {
        input_direction = glm::normalize(input_direction);
    }
    
    // Full player rotation toward camera
    if (glm::length(input_direction) > 0.1f) {
        float target_angle = atan2(input_direction.x, input_direction.z);
        current_angle = smooth_damp(current_angle, target_angle, 0.1f);
        entity_->GetTransform()->SetRotation(
            glm::angleAxis(current_angle, glm::vec3(0, 1, 0))
        );
    }
}
```

### Velocity Application

```cpp
void CharacterController::ApplyMovement(float delta_time) {
    if (!is_on_ground_) return;
    
    // Get target velocity from blend tree
    float blend_param = glm::length(input_direction);
    vec3 anim_velocity = locomotion_system_->BlendVelocity(blend_param);
    
    // Apply direction
    vec3 desired_velocity = input_direction * anim_velocity * move_speed_;
    
    // Smooth acceleration
    current_velocity_ = glm::mix(
        current_velocity_,
        desired_velocity,
        acceleration_ * delta_time
    );
    
    // Maintain current Y velocity (gravity handles it)
    current_velocity_.y = rigidbody_->GetVelocity().y;
    
    // Apply to physics body
    rigidbody_->SetVelocity(current_velocity_);
}
```

### Sprint System with Stamina

```cpp
void SprintState::OnUpdate(float delta_time) {
    CharacterStats* stats = owner_->GetStats();
    
    // Cost: 20 stamina/second
    if (stats->GetStamina() > 0) {
        stats->ReduceStamina(20.0f * delta_time);
        owner_->MoveSpeed = 7.0f;  // 7 m/s sprint speed
    } else {
        // Out of stamina, enter tired recovery
        owner_->TransitionTo(IdleState);
    }
    
    // Animation: SprintForward (1.5s loop)
    owner_->Animator()->Play("SprintForward", true);
}
```

---

## JUMP MECHANICS

### Basic Jump

```cpp
void JumpState::OnEnter() {
    // Apply upward impulse
    float jump_force = 15.0f;  // m/s^2
    rigidbody_->ApplyImpulse(glm::vec3(0, jump_force, 0));
    
    // Play jump animation
    animator_->Play("JumpStart");
    jump_start_time_ = current_time_;
    
    // Groundedness check: will fail while in air
    owner_->GroundDetector()->SetGroundCheckEnabled(false);
}

void JumpState::OnUpdate(float delta_time) {
    float time_since_jump = current_time_ - jump_start_time_;
    float jump_duration = 0.8f;  // apogee ~0.4s, descent ~0.4s
    
    // Blend animations: Start → Apogee → Descent
    if (time_since_jump < 0.4f) {
        animator_->Play("JumpApogee");  // Traveling up
    } else {
        animator_->Play("JumpDescent");  // Traveling down
    }
    
    // Reacquire ground check after peak
    if (time_since_jump > 0.5f) {
        owner_->GroundDetector()->SetGroundCheckEnabled(true);
    }
}

void JumpState::OnExit() {
    owner_->GroundDetector()->SetGroundCheckEnabled(true);
    
    // Check if we landed hard (velocity.y < -10)
    if (landed_velocity_.y < -10.0f) {
        owner_->TransitionTo(FallState);  // Hard land animation
    } else {
        owner_->TransitionTo(IdleState);
    }
}
```

### Jump Chaining (Wuthering Waves-style)

```cpp
// In IdleState/WalkState
void CharacterState::CanTransitionTo(CharacterStateType target) {
    if (target == CharacterStateType::Jump) {
        // Store pre-jump velocity for direction continuation
        owner_->stored_movement_direction_ = owner_->GetMovementDirection();
        
        // Can jump while moving (doesn't reset direction)
        jump_direction_ = owner_->stored_movement_direction_;
        
        // Jump cancels into walk/sprint if input is held
        owner_->jump_will_blend_to_walk_ = true;
        return true;
    }
}
```

---

## DASH MECHANICS

```cpp
class DashState : public CharacterState {
    glm::vec3 dash_direction_;
    float dash_speed_ = 20.0f;
    float dash_duration_ = 0.3f;
    float current_dash_time_ = 0;
    float dash_cooldown_ = 1.5f;
    
public:
    void OnEnter() override {
        // Dash direction = current facing + input direction
        dash_direction_ = glm::normalize(
            owner_->facing_direction_ + owner_->GetMovementDirection()
        );
        
        if (glm::length(dash_direction_) < 0.1f) {
            dash_direction_ = owner_->facing_direction_;  // Forward if no input
        }
        
        // Apply dash velocity
        rigidbody_->SetVelocity(dash_direction_ * dash_speed_);
        
        // Stop animation (dash is instant visual)
        animator_->Stop();
        animator_->Play("DashStart");
        
        // Use ability
        owner_->AbilitySystem()->UseAbility("Dash");
    }
    
    void OnUpdate(float delta_time) override {
        current_dash_time_ += delta_time;
        
        // Maintain dash direction
        rigidbody_->SetVelocity(
            dash_direction_ * dash_speed_ * (1.0f - current_dash_time_ / dash_duration_)
        );
        
        if (current_dash_time_ >= dash_duration_) {
            owner_->TransitionTo(FallState);  // Keeps momentum, gravity takes over
        }
    }
};
```

---

## GROUND DETECTION

```cpp
class GroundDetector {
    static const int RAYCAST_COUNT = 3;  // Front, center, back of capsule
    static const float RAYCAST_DISTANCE = 0.15f;  // Slightly beyond capsule
    
public:
    bool IsOnGround() {
        // Perform 3 raycasts from capsule bottom
        vec3 base = rigidbody_->GetPosition() - vec3(0, rigidbody_->GetRadius(), 0);
        
        for (int i = 0; i < RAYCAST_COUNT; i++) {
            float angle = (i / RAYCAST_COUNT) * 2 * 3.14159f;
            
            vec3 raycast_start = base + vec3(
                cos(angle) * rigidbody_->GetRadius(),
                0,
                sin(angle) * rigidbody_->GetRadius()
            );
            
            // Ray down
            RaycastHit hit;
            if (physics_world_->Raycast(raycast_start, vec3(0, -1, 0), 
                                       RAYCAST_DISTANCE, hit)) {
                return true;
            }
        }
        
        return false;
    }
    
    vec3 GetGroundNormal() {
        // Return average normal from ground raycasts
        // Used for slope sliding calculations
    }
};
```

---

## ANIMATION BLENDING

### Locomotion Blend Tree

```cpp
class LocomotionTransitionTree {
    // 1D blend space: Idle (0) → Walk (0.5) → Sprint (1.0)
    
public:
    AnimationState BlendAnimations(float move_speed) {
        if (move_speed < 0.1f) {
            return {
                .primary = "Idle",
                .blend_target = nullptr,
                .blend_weight = 0
            };
        }
        
        if (move_speed < 0.5f) {
            return {
                .primary = "Walk",
                .blend_target = "Idle",
                .blend_weight = 1.0f - (move_speed / 0.5f)
            };
        }
        
        if (move_speed < 1.0f) {
            return {
                .primary = "Sprint",
                .blend_target = "Walk",
                .blend_weight = (move_speed - 0.5f) / 0.5f
            };
        }
        
        return {
            .primary = "Sprint",
            .blend_target = nullptr,
            .blend_weight = 0
        };
    }
};
```

---

## NETWORKED CHARACTER CONTROLLER

```cpp
class NetworkedCharacterController : public CharacterController {
    bool is_local_;
    uint32_t network_id_;
    
public:
    void Update(float delta_time) override {
        if (is_local_) {
            // Local player: normal update
            CharacterController::Update(delta_time);
            
            // Send state to server
            NetworkInputPacket packet;
            packet.input = input_buffer_.GetLastInput();
            packet.position = GetPosition();
            packet.rotation = GetRotation();
            network_manager_->SendMovementState(packet);
        } else {
            // Remote player: receive state from network
            NetworkInputPacket received = network_manager_->GetMovementState(network_id_);
            
            // Interpolate to new position
            target_position_ = received.position;
            InterpolateToPosition(delta_time);
            
            // Update animations from received state
            if (received.state != current_state_) {
                TransitionTo(received.state);
            }
        }
    }
};
```

---

## STAT SYSTEM INTEGRATION

```cpp
void CharacterController::TakeDamage(float damage, DamageType type) {
    CharacterStats* stats = GetStats();
    
    // Calculate damage reduction from armor & resistances
    float resistance_mult = 1.0f;
    
    if (type == DamageType::Physical) {
        // Armor reduces physical damage
        float armor_reduction = stats->GetArmor() / (stats->GetArmor() + 100.0f);
        resistance_mult = 1.0f - armor_reduction;
    } else if (type == DamageType::Fire) {
        resistance_mult = 1.0f - stats->GetResistance(ElementType::Fire);
    } else if (type == DamageType::Cold) {
        resistance_mult = 1.0f - stats->GetResistance(ElementType::Cold);
    }
    
    float final_damage = damage * resistance_mult;
    stats->ReduceHealth(final_damage);
    
    // Apply knockback if heavy hit
    if (damage > stats->GetMaxHealth() * 0.1f) {
        ApplyKnockback(/* hit direction */, final_damage * 0.5f);
    }
    
    // Play hit reaction animation
    animator_->PlayOnce("HitReaction");
    
    // Fire event for UI updates
    on_damage_taken_.Invoke(final_damage, type);
}
```

---

## FILE STRUCTURE

```
engine/  
├── core/
│   └── character/
│       ├── CMakeLists.txt
│       ├── include/
│       │   ├── character_controller.h
│       │   ├── character_state.h
│       │   ├── character_stats.h
│       │   ├── ground_detector.h
│       │   ├── input_buffer.h
│       │   └── locomotion_system.h
│       └── src/
│           ├── character_controller.cpp
│           ├── character_state.cpp
│           ├── character_stats.cpp
│           ├── ground_detector.cpp
│           ├── input_buffer.cpp
│           └── locomotion_system.cpp
```

---

## TESTING CHECKLIST

- [ ] Walk/Sprint/Idle transitions smooth
- [ ] Jump reaches correct height (15 m/s)
- [ ] Jump can be canceled into dash
- [ ] Dash has 1.5s cooldown
- [ ] Stamina depletes at 20/sec while sprinting
- [ ] Ground detection works on slopes
- [ ] Animation blends smoothly
- [ ] Network movement replicates correctly
- [ ] Damage reduction calculations accurate
- [ ] Knockback applies physics impulse
