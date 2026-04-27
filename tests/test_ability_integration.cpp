#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <entt/entt.hpp>
#include "ability_systems.h"

using namespace engine::ability;
using Catch::Approx;

TEST_CASE("Ability System - Active Ability Casting", "[ability][active]") {
    AbilityConfig config;
    config.name = "Fireball";
    config.type = AbilityType::Active;
    config.cooldown_duration = 5.0f;
    config.mana_cost = 50.0f;

    auto ability = std::make_shared<ActiveAbility>(config);

    SECTION("Active ability has correct type") {
        REQUIRE(ability->GetType() == AbilityType::Active);
        REQUIRE(ability->GetName() == "Fireball");
    }

    SECTION("Can cast when not on cooldown") {
        std::string error = ability->CanCast(entt::null);
        REQUIRE(error.empty());
    }

    SECTION("Ability goes on cooldown after cast") {
        ability->Activate(entt::null, entt::null, glm::vec3(0.0f));
        
        REQUIRE(ability->GetRemainingCooldown() > 0.0f);
        REQUIRE(ability->GetRemainingCooldown() <= config.cooldown_duration);
    }

    SECTION("Cannot cast while on cooldown") {
        ability->Activate(entt::null, entt::null, glm::vec3(0.0f));
        std::string error = ability->CanCast(entt::null);
        
        REQUIRE(!error.empty());
    }

    SECTION("Cooldown decreases over time") {
        ability->Activate(entt::null, entt::null, glm::vec3(0.0f));
        float cooldown_before = ability->GetRemainingCooldown();
        
        ability->Update(1.0f);
        float cooldown_after = ability->GetRemainingCooldown();
        
        REQUIRE(cooldown_after < cooldown_before);
    }

    SECTION("Cooldown expires") {
        ability->Activate(entt::null, entt::null, glm::vec3(0.0f));
        ability->Update(config.cooldown_duration + 1.0f);
        
        REQUIRE(ability->GetRemainingCooldown() <= 0.0f);
    }
}

TEST_CASE("Ability System - Channeled Ability", "[ability][channeled]") {
    AbilityConfig config;
    config.name = "Lightning Storm";
    config.type = AbilityType::Channeled;
    config.channel_duration = 2.0f;
    config.cooldown_duration = 8.0f;

    auto ability = std::make_shared<ChanneledAbility>(config);

    SECTION("Channeled ability has correct type") {
        REQUIRE(ability->GetType() == AbilityType::Channeled);
    }

    SECTION("Can start channeling") {
        bool started = ability->Activate(entt::null, entt::null, glm::vec3(0.0f));
        REQUIRE(started);
        REQUIRE(ability->IsChanneling());
    }

    SECTION("Channel power increases over time") {
        ability->Activate(entt::null, entt::null, glm::vec3(0.0f));
        
        float power_before = ability->UpdateChannel(0.0f);
        ability->UpdateChannel(0.5f);
        float power_after = ability->UpdateChannel(0.5f);
        
        REQUIRE(power_after > power_before);
    }

    SECTION("Channel power maxes at 1.0") {
        ability->Activate(entt::null, entt::null, glm::vec3(0.0f));
        ability->UpdateChannel(config.channel_duration + 1.0f);
        
        REQUIRE(ability->GetChannelPower() <= 1.0f);
    }

    SECTION("Releasing channel goes on cooldown") {
        ability->Activate(entt::null, entt::null, glm::vec3(0.0f));
        ability->UpdateChannel(1.0f);
        ability->ReleaseChannel(1.0f);
        
        REQUIRE(!ability->IsChanneling());
        REQUIRE(ability->GetRemainingCooldown() > 0.0f);
    }
}

TEST_CASE("Ability System - Modifier Pipeline", "[ability][modifiers]") {
    ModifierStack stack;

    SECTION("Add modifiers stack correctly") {
        stack.AddModifier({"damage", 10.0f, ModifierOperation::Add});
        stack.AddModifier({"damage", 15.0f, ModifierOperation::Add});
        
        float result = stack.CalculateModifiedValue(50.0f);
        REQUIRE(result == Approx(75.0f));  // 50 + 10 + 15
    }

    SECTION("Multiply modifiers apply after Add") {
        stack.AddModifier({"damage", 10.0f, ModifierOperation::Add});
        stack.AddModifier({"damage", 0.5f, ModifierOperation::Multiply});  // 1.5x
        
        float result = stack.CalculateModifiedValue(50.0f);
        // (50 + 10) * 1.5 = 90
        REQUIRE(result == Approx(90.0f));
    }

    SECTION("Set modifiers take highest priority") {
        stack.AddModifier({"damage", 10.0f, ModifierOperation::Add});
        stack.AddModifier({"damage", 100.0f, ModifierOperation::Set});
        
        float result = stack.CalculateModifiedValue(50.0f);
        REQUIRE(result == Approx(100.0f));
    }

    SECTION("Multiple operations in order: Add → Multiply → Set") {
        stack.AddModifier({"damage", 20.0f, ModifierOperation::Add});
        stack.AddModifier({"damage", 0.25f, ModifierOperation::Multiply});  // 1.25x
        stack.AddModifier({"damage", 150.0f, ModifierOperation::Set});
        
        float result = stack.CalculateModifiedValue(100.0f);
        // Following pipeline: (100 + 20) * 1.25 = 150, but Set overrides to 150
        REQUIRE(result == Approx(150.0f));
    }

    SECTION("Remove modifiers by source") {
        stack.AddModifier({"damage", 10.0f, ModifierOperation::Add, "Potion", 5.0f});
        stack.AddModifier({"damage", 5.0f, ModifierOperation::Add, "Ability", 0.0f});
        
        REQUIRE(stack.GetModifierCount() == 2);
        
        stack.RemoveModifierBySource("Potion");
        REQUIRE(stack.GetModifierCount() == 1);
    }
}

TEST_CASE("Ability System - Cooldown Manager", "[ability][cooldown]") {
    CooldownManager manager;

    SECTION("Ability ready initially") {
        REQUIRE(manager.IsReady("Fireball"));
    }

    SECTION("Start cooldown") {
        manager.StartCooldown("Fireball", 5.0f);
        
        REQUIRE(!manager.IsReady("Fireball"));
        REQUIRE(manager.GetRemainingCooldown("Fireball") > 0.0f);
    }

    SECTION("Cooldown decreases") {
        manager.StartCooldown("Fireball", 5.0f);
        float before = manager.GetRemainingCooldown("Fireball");
        
        manager.Update(1.0f);
        float after = manager.GetRemainingCooldown("Fireball");
        
        REQUIRE(after < before);
    }

    SECTION("Multiple cooldowns") {
        manager.StartCooldown("Fireball", 5.0f);
        manager.StartCooldown("Frostbolt", 3.0f);
        
        REQUIRE(!manager.IsReady("Fireball"));
        REQUIRE(!manager.IsReady("Frostbolt"));
        
        manager.Update(4.0f);
        
        REQUIRE(!manager.IsReady("Fireball"));
        REQUIRE(manager.IsReady("Frostbolt"));
    }

    SECTION("Get cooldown info for UI") {
        manager.StartCooldown("Fireball", 5.0f);
        CooldownInfo info = manager.GetCooldownInfo("Fireball");
        
        REQUIRE(info.total_duration == Approx(5.0f));
        REQUIRE(info.GetProgress() >= 0.0f);
        REQUIRE(info.GetProgress() <= 1.0f);
    }
}

TEST_CASE("Ability System - Central Manager", "[ability][system]") {
    entt::registry registry;
    auto entity = registry.create();

    AbilitySystem system(entity);

    SECTION("Add ability to system") {
        auto ability = std::make_shared<ActiveAbility>(
            AbilityConfig{"Fireball", "...", AbilityType::Active, 5.0f}
        );
        
        bool added = system.AddAbility(ability);
        REQUIRE(added);
        REQUIRE(system.HasAbility("Fireball"));
    }

    SECTION("Cannot add duplicate ability") {
        auto ability1 = std::make_shared<ActiveAbility>(
            AbilityConfig{"Fireball", "...", AbilityType::Active, 5.0f}
        );
        auto ability2 = std::make_shared<ActiveAbility>(
            AbilityConfig{"Fireball", "...", AbilityType::Active, 5.0f}
        );
        
        system.AddAbility(ability1);
        bool added = system.AddAbility(ability2);
        REQUIRE(!added);
    }

    SECTION("Activate ability from system") {
        auto ability = std::make_shared<ActiveAbility>(
            AbilityConfig{"Fireball", "...", AbilityType::Active, 5.0f}
        );
        system.AddAbility(ability);
        
        bool activated = system.ActivateAbility("Fireball");
        REQUIRE(activated);
    }

    SECTION("System updates cooldowns") {
        auto ability = std::make_shared<ActiveAbility>(
            AbilityConfig{"Fireball", "...", AbilityType::Active, 5.0f}
        );
        system.AddAbility(ability);
        system.ActivateAbility("Fireball");
        
        system.Update(1.0f);
        
        float cooldown = system.GetAbilityCooldown("Fireball");
        REQUIRE(cooldown < 5.0f);
        REQUIRE(cooldown > 0.0f);
    }
}

TEST_CASE("Ability System - Skill Tree", "[ability][skill_tree]") {
    SkillTree tree;

    SkillNode node1;
    node1.id = 1;
    node1.name = "Fireball";
    node1.abilities_granted = {"Fireball"};

    SkillNode node2;
    node2.id = 2;
    node2.name = "Inferno";
    node2.prerequisites = {1};  // Require Fireball first
    node2.abilities_granted = {"Inferno"};

    tree.AddNode(node1);
    tree.AddNode(node2);

    SECTION("Can unlock initial node") {
        bool unlocked = tree.UnlockNode(1);
        REQUIRE(unlocked);
        REQUIRE(tree.IsNodeUnlocked(1));
    }

    SECTION("Cannot unlock without prerequisites") {
        std::string error = tree.CanUnlockNode(2);
        REQUIRE(!error.empty());
        
        bool unlocked = tree.UnlockNode(2);
        REQUIRE(!unlocked);
    }

    SECTION("Can unlock with prerequisites met") {
        tree.UnlockNode(1);
        
        std::string error = tree.CanUnlockNode(2);
        REQUIRE(error.empty());
        
        bool unlocked = tree.UnlockNode(2);
        REQUIRE(unlocked);
    }

    SECTION("Granted abilities from unlocked nodes") {
        tree.UnlockNode(1);
        tree.UnlockNode(2);
        
        auto abilities = tree.GetGrantedAbilities();
        REQUIRE(abilities.size() == 2);
        REQUIRE(std::find(abilities.begin(), abilities.end(), "Fireball") != abilities.end());
        REQUIRE(std::find(abilities.begin(), abilities.end(), "Inferno") != abilities.end());
    }
}
