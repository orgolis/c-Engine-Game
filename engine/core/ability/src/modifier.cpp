#include "modifier.h"
#include <algorithm>
#include <glm/common.hpp>

namespace engine::ability {

// ============ ModifierStack ============

void ModifierStack::AddModifier(const Modifier& modifier) {
    modifiers_.push_back(modifier);
}

void ModifierStack::RemoveModifierBySource(const std::string& source) {
    auto it = std::remove_if(modifiers_.begin(), modifiers_.end(),
        [&source](const Modifier& m) { return m.source == source; });
    modifiers_.erase(it, modifiers_.end());
}

float ModifierStack::CalculateModifiedValue(float base_value) const {
    float result = base_value;

    // Step 1: Apply all Add modifiers
    float add_total = 0.0f;
    for (const auto& mod : modifiers_) {
        if (mod.operation == ModifierOperation::Add) {
            add_total += mod.value;
        }
    }
    result += add_total;

    // Step 2: Apply all Multiply modifiers
    float multiply_total = 1.0f;
    for (const auto& mod : modifiers_) {
        if (mod.operation == ModifierOperation::Multiply) {
            multiply_total *= (1.0f + mod.value);
        }
    }
    result *= multiply_total;

    // Step 3: Apply Set modifiers (take highest)
    float set_value = result;
    bool has_set = false;
    for (const auto& mod : modifiers_) {
        if (mod.operation == ModifierOperation::Set) {
            if (!has_set || mod.value > set_value) {
                set_value = mod.value;
                has_set = true;
            }
        }
    }
    if (has_set) {
        result = set_value;
    }

    return result;
}

float ModifierStack::GetAddBonus() const {
    float total = 0.0f;
    for (const auto& mod : modifiers_) {
        if (mod.operation == ModifierOperation::Add) {
            total += mod.value;
        }
    }
    return total;
}

float ModifierStack::GetMultiplyBonus() const {
    float total = 0.0f;
    for (const auto& mod : modifiers_) {
        if (mod.operation == ModifierOperation::Multiply) {
            total += mod.value;  // Returns the sum of multipliers
        }
    }
    return total;
}

float ModifierStack::GetSetOverride() const {
    float highest = 0.0f;
    for (const auto& mod : modifiers_) {
        if (mod.operation == ModifierOperation::Set) {
            highest = glm::max(highest, mod.value);
        }
    }
    return highest;
}

bool ModifierStack::HasSetModifier() const {
    for (const auto& mod : modifiers_) {
        if (mod.operation == ModifierOperation::Set) {
            return true;
        }
    }
    return false;
}

void ModifierStack::Update(float dt) {
    // Remove expired temporary modifiers
    auto it = std::remove_if(modifiers_.begin(), modifiers_.end(),
        [dt](Modifier& m) {
            if (m.duration > 0.0f) {
                m.duration -= dt;
                return m.duration <= 0.0f;
            }
            return false;
        });
    modifiers_.erase(it, modifiers_.end());
}

void ModifierStack::Clear() {
    modifiers_.clear();
}

// ============ ModifierDatabase ============

void ModifierDatabase::AddModifier(const std::string& stat_name, const Modifier& modifier) {
    stacks_[stat_name].AddModifier(modifier);
}

float ModifierDatabase::GetModifiedValue(const std::string& stat_name, float base_value) {
    auto it = stacks_.find(stat_name);
    if (it != stacks_.end()) {
        return it->second.CalculateModifiedValue(base_value);
    }
    return base_value;
}

void ModifierDatabase::RemoveModifiersFromSource(const std::string& source) {
    for (auto& [stat_name, stack] : stacks_) {
        stack.RemoveModifierBySource(source);
    }
}

void ModifierDatabase::Update(float dt) {
    for (auto& [stat_name, stack] : stacks_) {
        stack.Update(dt);
    }
}

ModifierStack* ModifierDatabase::GetStack(const std::string& stat_name) {
    auto it = stacks_.find(stat_name);
    if (it != stacks_.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace engine::ability
