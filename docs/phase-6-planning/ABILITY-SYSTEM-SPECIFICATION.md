# Ability & Modifier System — Detailed Specification

**Date:** April 12, 2026
**Priority:** Critical (Destiny 2-style modifier pipeline)
**Scope:** Abilities, Skills, Modifiers, Effects, Cooldowns

---

## PHILOSOPHY

The ability system is the **heart of character progression and balancing** in GameWorldshaper.

Key principles:
1. **Modifier Pipeline:** Every stat change flows through unified modifier system
2. **Composable Effects:** Abilities are chains of effects (damage → stun → slow)
3. **Skill Tree Driven:** Unlock abilities and modifiers through progression
4. **Destiny-Style Builds:** Players combine abilities + modifiers for custom playstyles
5. **Networked:** All cooldowns/effects synchronized for multiplayer

---

## CORE ARCHITECTURE

```
Player Input → AbilitySystem → Check Activation
                              ↓
                         Apply Cost (Mana/Stamina)
                              ↓
                         Cast Animation
                              ↓
                         Apply Effects (Damage/Heal/Status)
                              ↓
                         Apply Modifiers (Modify damage/duration)
                              ↓
                         Add Cooldown
                              ↓
                         Update Character Stats
```

---

## ABILITY DEFINITION

### Ability Base Class

```cpp
class Ability {
public:
    // Identity
    std::string name;
    std::string description;
    AbilityType type;  // Active, Passive, Channeled, Toggle
    
    // Cost
    AbilityCost cost;  // Mana, stamina, health
    
    // Execution
    float cast_time;       // 0.0f = instant
    float cooldown;        // Seconds until can cast again
    float channel_duration; // For channeled abilities
    
    // Targeting
    bool requires_target;
    float targeting_range;
    float targeting_radius;  // For AOE
    
    // Content
    std::vector<Effect> effects;
    std::vector<AbilityModifier> modifiers;
    std::string animation_name;
    
    // Events
    on_cast_callback;
    on_complete_callback;
    on_interrupt_callback;
    
public:
    virtual bool CanActivate(const ActivationContext& ctx) const;
    virtual void Activate(ActivationContext& ctx);
    virtual void Update(float delta_time);
    virtual void Deactivate();
    virtual AbilityCost GetCost() const { return cost; }
};
```

### Ability Types

#### 1. Active Abilities

```cpp
class ActiveAbility : public Ability {
    bool can_queue_next_;
    float queue_window_ = 0.2f;  // 200ms window to queue next ability
    
public:
    void Activate(ActivationContext& ctx) override {
        if (cast_time > 0) {
            // Cast time: play animation, wait, then apply effects
            ctx.animator->Play(animation_name);
            state_ = AbilityState::Casting;
            cast_progress_ = 0;
        } else {
            // Instant cast: apply immediately
            ApplyEffects(ctx);
            StartCooldown();
        }
    }
    
    void Update(float delta_time) override {
        if (state_ == AbilityState::Casting) {
            cast_progress_ += delta_time / cast_time;
            
            if (cast_progress_ >= 1.0f) {
                ApplyEffects(current_context_);
                StartCooldown();
                state_ = AbilityState::Idle;
            }
        }
    }
    
    void EnableQueueing() {
        // Allow next ability to be queued during last 200ms of cast
        can_queue_next_ = true;
    }
};
```

#### 2. Passive Abilities

```cpp
class PassiveAbility : public Ability {
    std::function<bool()> trigger_condition_;
    
public:
    PassiveAbility(std::string name, std::function<bool()> condition) 
        : trigger_condition_(condition) {}
    
    void Update(float delta_time) override {
        if (IsActive() && trigger_condition_()) {
            ApplyPassiveEffects();
        }
    }
    
    // Passives apply instantly when conditions met
    // E.g., "Deal 10% more damage while sprinting"
};

// Example passive: "Mystic Shield - Every 5th attack grants 25% damage reduction"
class MysticShield : public PassiveAbility {
    int attack_count_;
    
public:
    MysticShield() : PassiveAbility("Mystic Shield", [this]() {
        return attack_count_ % 5 == 0;
    }) {}
};
```

#### 3. Channeled Abilities

```cpp
class ChanneledAbility : public Ability {
    float channel_total_time_;
    float current_channel_time_;
    float last_tick_time_;
    float tick_rate_ = 0.1f;  // Tick every 100ms
    
public:
    void Activate(ActivationContext& ctx) override {
        state_ = AbilityState::Channeling;
        current_channel_time_ = 0;
        last_tick_time_ = 0;
        
        // Play channel animation (loops)
        ctx.animator->Play(animation_name, true);
    }
    
    void Update(float delta_time) override {
        if (state_ != AbilityState::Channeling) return;
        
        current_channel_time_ += delta_time;
        last_tick_time_ += delta_time;
        
        // Apply effect on each tick
        if (last_tick_time_ >= tick_rate_) {
            ApplyTickEffect(current_context_);
            last_tick_time_ = 0;
            
            // Drain mana/stamina each tick
            ApplyCostPerTick();
        }
        
        // Can be interrupted or manually stopped
        if (current_channel_time_ >= channel_duration_) {
            Deactivate();
            StartCooldown();
        }
    }
    
    void Interrupt() {
        // Interrupted mid-channel (hit, knocked back, etc)
        state_ = AbilityState::Interrupted;
        current_context_.animator->Stop();
    }
};

// Example: "Beam of Light - Channel to fire continuous beam"
class BeamOfLight : public ChanneledAbility {
    // Damage per tick: 20
    // Tick rate: 0.1s
    // Total: 200 DPS while channeling
};
```

---

## EFFECTS SYSTEM

### Effect Base Class

```cpp
class Effect {
public:
    std::string name;
    EffectType type;  // Damage, Heal, Status, Transform
    DamageType damage_type;  // Physical, Fire, Cold, Lightning, etc
    
    float base_damage;
    float base_healing;
    float duration;
    float radius;  // For AOE effects
    bool is_aoe;
    
    virtual void Apply(Entity* target) = 0;
    virtual void Tick(Entity* target) = 0;
    virtual void Remove(Entity* target) = 0;
};
```

### Effect Types

#### Damage Effect

```cpp
class DamageEffect : public Effect {
    std::vector<Entity*> hit_targets_;
    std::set<uint32_t> hit_target_ids_;  // For multiplayer: track what we hit
    
public:
    void Apply(Entity* target) override {
        // Check if we already hit this target (prevent multi-hit)
        if (hit_target_ids_.count(target->GetID())) {
            return;
        }
        
        auto stats = target->GetComponent<CharacterStats>();
        if (!stats) return;
        
        // Calculate damage with modifiers
        float modified_damage = base_damage;
        
        // Apply ability modifiers
        for (const auto& mod : attached_modifiers_) {
            modified_damage = ApplyModifier(modified_damage, mod);
        }
        
        // Apply character modifiers (bless, curse, buffs)
        auto modifier_stack = target->GetComponent<ModifierStack>();
        if (modifier_stack) {
            modified_damage = modifier_stack->ApplyModifier(
                "damage_multiplier", modified_damage
            );
        }
        
        // Apply resistances
        if (damage_type == DamageType::Fire) {
            modified_damage *= (1.0f - stats->GetResistance(ElementType::Fire));
        }
        
        // Deal damage
        stats->TakeDamage(modified_damage, damage_type);
        
        hit_target_ids_.insert(target->GetID());
    }
};
```

#### Healing Effect

```cpp
class HealingEffect : public Effect {
public:
    void Apply(Entity* target) override {
        auto stats = target->GetComponent<CharacterStats>();
        if (!stats) return;
        
        float modified_healing = base_healing;
        
        // Healing bonuses stack from modifiers
        auto modifier_stack = target->GetComponent<ModifierStack>();
        if (modifier_stack) {
            modified_healing = modifier_stack->ApplyModifier(
                "healing_received", modified_healing
            );
        }
        
        stats->Heal(modified_healing);
    }
};
```

#### Status Effect (Crowd Control)

```cpp
class StatusEffect : public Effect {
    StatusType status_type_;  // Stun, Slow, Burn, Poison, etc
    
public:
    void Apply(Entity* target) override {
        auto stats = target->GetComponent<CharacterStats>();
        if (!stats) return;
        
        // Create modifier for the status effect
        Modifier status_mod;
        status_mod.name = name;
        status_mod.type = ModifierType::StatusEffect;
        status_mod.duration = duration;
        status_mod.value = GetStatusIntensity();
        
        // Different statuses have different effects
        switch (status_type_) {
            case StatusType::Stun:
                status_mod.parameter = "can_move";
                status_mod.operation = ModifierOperation::Set;
                status_mod.value = 0;  // Can't move
                break;
            case StatusType::Slow:
                status_mod.parameter = "movement_speed_multiplier";
                status_mod.operation = ModifierOperation::Multiply;
                status_mod.value = 0.5f;  // 50% speed
                break;
            case StatusType::Burn:
                status_mod.parameter = "damage_over_time";
                status_mod.operation = ModifierOperation::Add;
                status_mod.value = 5.0f;  // 5 DPS
                break;
        }
        
        stats->ApplyModifier(status_mod);
    }
};
```

---

## MODIFIER SYSTEM (Destiny-Style Pipeline)

### Modifier Stack

```cpp
class ModifierStack : public Component {
    std::vector<Modifier> active_modifiers_;
    std::map<std::string, std::vector<Modifier>> modifier_groups_;
    
public:
    void AddModifier(const Modifier& mod) {
        // Check stacking behavior
        auto existing = FindModifiers(mod.name);
        
        switch (mod.stacking_behavior) {
            case StackingBehavior::Replace:
                existing.clear();  // Remove old ones
                break;
            
            case StackingBehavior::Stack:
                // Multiple instances allowed
                break;
            
            case StackingBehavior::Refresh:
                if (!existing.empty()) {
                    existing[0].remaining_time = mod.duration;
                    return;
                }
                break;
        }
        
        active_modifiers_.push_back(mod);
        modifier_groups_[mod.source].push_back(mod);
        
        // Apply immediately
        ApplyModifierToStats(mod);
    }
    
    float CalculateModifiedValue(const std::string& stat_name, float base_value) {
        float result = base_value;
        
        // Apply modifiers in order: Add→Multiply→Set
        
        // 1. Add modifiers (flat bonuses)
        for (const auto& mod : FindModifiers("damage_add")) {
            result += mod.value;
        }
        
        // 2. Multiply modifiers (percentage)
        for (const auto& mod : FindModifiers("damage_multiplier")) {
            result *= (1.0f + mod.value);
        }
        
        // 3. Set modifiers (override)
        for (const auto& mod : FindModifiers("damage_set")) {
            result = mod.value;
        }
        
        return std::max(result, 0.0f);  // Clamp to 0
    }
    
    void Update(float delta_time) {
        // Update all modifiers, remove expired ones
        for (auto it = active_modifiers_.begin(); it != active_modifiers_.end();) {
            it->remaining_time -= delta_time;
            
            if (it->remaining_time <= 0) {
                RemoveModifierFromStats(*it);
                it = active_modifiers_.erase(it);
            } else {
                ++it;
            }
        }
    }
};

struct Modifier {
    std::string name;
    std::string source;  // "FireDebuff", "SprintBonus", etc
    ModifierType type;   // StatModifier, StatusEffect, Movement, etc
    ModifierOperation operation;  // Add, Multiply, Set, Override
    float value;
    float duration;
    float remaining_time;
    int max_stacks = 1;
    StackingBehavior stacking_behavior;
};
```

### Modifier Application Example

```
Base Damage: 50

Modifiers Applied:
1. "Strength Buff" (Add): +10 damage
   → 50 + 10 = 60

2. "Fire Vulnerability" (Multiply: 1.5): 50% more damage
   → 60 * 1.5 = 90

3. "Armor Reduction" (Multiply: 0.8): Target has 20% armor
   → 90 * 0.8 = 72

Final Damage: 72 damage dealt
```

---

## SKILL TREE SYSTEM

### Skill Node

```cpp
struct SkillNode {
    uint32_t id;
    std::string name;
    std::string description;
    
    // Position in tree for UI
    glm::vec2 position;
    
    // Progression
    uint32_t required_points;
    std::vector<uint32_t> prerequisite_nodes;
    
    // Grants
    std::vector<std::string> abilities_granted;
    std::vector<Modifier> modifiers_granted;
    std::vector<std::string> passive_effects;
    
    // State
    bool unlocked;
    int level;  // Some nodes can be leveled multiple times
};

// Example skill tree structure
/*
        [Level 30 Unlock]
         /        |        \
      [Fireball]  [Lightning]  [Ice Nova]
         |           |            |
     [Fire Buff] [Lightning Buff] [Ice Buff]
         \            |            /
          [Elemental Mastery] ← Requires 1 of 3 above
                     |
          [Ultimate: Prism Burst]
*/
```

### Skill Tree Navigation

```cpp
class SkillTree {
    std::map<uint32_t, SkillNode> nodes_;
    std::set<uint32_t> unlocked_nodes_;
    uint32_t skill_points_available_;
    
public:
    bool UnlockNode(uint32_t node_id) {
        const auto& node = nodes_[node_id];
        
        // Check prerequisites
        for (uint32_t prereq : node.prerequisite_nodes) {
            if (!unlocked_nodes_.count(prereq)) {
                return false;  // Missing prerequisite
            }
        }
        
        // Check skill points
        if (skill_points_available_ < node.required_points) {
            return false;
        }
        
        // Unlock and grant abilities
        unlocked_nodes_.insert(node_id);
        skill_points_available_ -= node.required_points;
        
        ability_system_->RegisterAbilities(node.abilities_granted);
        modifier_stack_->AddPermanentModifiers(node.modifiers_granted);
        
        return true;
    }
    
    void LevelUpSkillPoint() {
        skill_points_available_++;
        on_skill_points_changed_.Invoke(skill_points_available_);
    }
};
```

---

## COOLDOWN MANAGEMENT

```cpp
class CooldownManager {
    std::map<std::string, Cooldown> cooldowns_;
    
public:
    bool IsReady(const std::string& ability_name) const {
        auto it = cooldowns_.find(ability_name);
        if (it == cooldowns_.end()) return true;  // No cooldown set
        
        return it->second.remaining_time <= 0;
    }
    
    float GetRemainingCooldown(const std::string& ability_name) const {
        auto it = cooldowns_.find(ability_name);
        if (it == cooldowns_.end()) return 0;
        
        return std::max(0.0f, it->second.remaining_time);
    }
    
    void StartCooldown(const std::string& ability_name, float duration) {
        cooldowns_[ability_name].remaining_time = duration;
        cooldowns_[ability_name].total_duration = duration;
        
        // For charge-based abilities
        if (cooldowns_[ability_name].charges_max > 0) {
            cooldowns_[ability_name].charges--;
        }
    }
    
    void Update(float delta_time) {
        for (auto& pair : cooldowns_) {
            pair.second.remaining_time -= delta_time;
            
            // Recharge system (regain charges over time)
            if (pair.second.charges < pair.second.charges_max) {
                pair.second.charge_recharge_time -= delta_time;
                
                if (pair.second.charge_recharge_time <= 0) {
                    pair.second.charges++;
                    pair.second.charge_recharge_time = pair.second.charge_cooldown;
                }
            }
        }
    }
};

struct Cooldown {
    float total_duration;
    float remaining_time;
    
    // Charge system (e.g., dash has 2 charges)
    uint32_t charges;
    uint32_t charges_max;
    float charge_cooldown;      // Seconds between charge recovery
    float charge_recharge_time; // Time until next charge
};
```

---

## ACTIVATION CONTEXT

```cpp
struct ActivationContext {
    CharacterController* caster;
    Entity* caster_entity;
    Entity* target_entity;
    glm::vec3 target_position;
    Animator* animator;
    CharacterStats* caster_stats;
    ModifierStack* modifier_stack;
    CooldownManager* cooldown_manager;
    PhysicsWorld* physics_world;
    RigidBody* caster_rigidbody;
    AbilitySystem* ability_system;
};
```

---

## ABILITY ACTIVATION FLOW

```cpp
void AbilitySystem::ActivateAbility(const std::string& ability_name, 
                                    const ActivationContext& ctx) {
    auto it = abilities_.find(ability_name);
    if (it == abilities_.end()) {
        SPDLOG_WARN("Ability not found: {}", ability_name);
        return;
    }
    
    Ability* ability = it->second.get();
    
    // 1. Check can cast
    if (!ability->CanActivate(ctx)) {
        SPDLOG_WARN("Cannot cast: {}", ability_name);
        return;
    }
    
    // 2. Apply cost
    if (!ApplyCost(ability->GetCost(), ctx)) {
        SPDLOG_WARN("Insufficient resources for: {}", ability_name);
        return;
    }
    
    // 3. Activate ability (plays animation, etc)
    ability->Activate(ctx);
    
    // 4. Fire callbacks for UI/VFX
    on_ability_activated_.Invoke(ability_name);
}

void AbilitySystem::Update(float delta_time) {
    // Update all active abilities (casting, channeling, etc)
    for (auto& pair : abilities_) {
        pair.second->Update(delta_time);
    }
    
    // Update cooldowns
    cooldown_manager_.Update(delta_time);
    
    // Update active effects
    for (auto it = active_effects_.begin(); it != active_effects_.end();) {
        it->Update(delta_time);
        
        if (it->IsExpired()) {
            it = active_effects_.erase(it);
        } else {
            ++it;
        }
    }
}
```

---

## FILE STRUCTURE

```
engine/
├── core/
│   └── ability/
│       ├── CMakeLists.txt
│       ├── include/
│       │   ├── ability.h
│       │   ├── ability_system.h
│       │   ├── effect.h
│       │   ├── modifier.h
│       │   ├── cooldown_manager.h
│       │   ├── skill_tree.h
│       │   └── ability_types.h
│       └── src/
│           ├── ability.cpp
│           ├── ability_system.cpp
│           ├── effect.cpp
│           ├── modifier.cpp
│           ├── cooldown_manager.cpp
│           └── skill_tree.cpp
```

---

## TESTING SCENARIOS

- [ ] Basic ability activation (instant cast)
- [ ] Cast time with animation
- [ ] Channeled ability (interruptible)
- [ ] Cooldown reduction modifier (50% cooldown)
- [ ] Damage modifier stacking (multiple sources)
- [ ] Skill tree unlock with prerequisites
- [ ] Ability chaining (queue next ability)
- [ ] Status effect application and duration
- [ ] Charge-based ability (2 charges, 5s recovery)
- [ ] Network replication of ability state
