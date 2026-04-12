#include <catch2/catch_test_macros.hpp>
#include <entt/entt.hpp>
#include "character_systems.h"

using namespace engine::character;

// Mock Transform component for testing
struct Transform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
};

// Mock RigidBody component for testing
struct RigidBody {
    glm::vec3 velocity = glm::vec3(0.0f);
    float mass = 1.0f;
};

TEST_CASE("Character Controller - State Machine", "[character][state]") {
    entt::registry registry;
    auto entity = registry.create();
    
    // Add required components
    registry.emplace<Transform>(entity);
    registry.emplace<RigidBody>(entity);

    CharacterController controller(entity);

    SECTION("Initial state is Idle") {
        REQUIRE(controller.IsInState(CharacterStateType::Idle));
        REQUIRE(controller.GetCurrentStateName() == std::string("Idle"));
    }

    SECTION("Can transition Idle -> Walk") {
        InputAction input;
        input.forward = 1.0f;
        controller.ProcessInput(input);
        
        // Verify transition is allowed
        REQUIRE(controller.CanTransitionTo(CharacterStateType::Walk));
    }

    SECTION("Can transition Walk -> Sprint") {
        InputAction input;
        input.forward = 1.0f;
        input.sprint = true;
        controller.ProcessInput(input);
        
        REQUIRE(controller.CanTransitionTo(CharacterStateType::Sprint));
    }

    SECTION("Can transition to Jump from ground") {
        InputAction jump_input;
        jump_input.jump = true;
        controller.ProcessInput(jump_input);
        
        REQUIRE(controller.CanTransitionTo(CharacterStateType::Jump));
    }
}

TEST_CASE("Character Controller - Input Buffering", "[character][input]") {
    InputBuffer buffer;

    SECTION("Buffer stores inputs") {
        InputAction input1;
        input1.forward = 1.0f;
        
        InputAction input2;
        input2.lateral = 1.0f;

        buffer.BufferInput(input1);
        buffer.BufferInput(input2);

        REQUIRE(buffer.GetBufferSize() == 2);
    }

    SECTION("Buffer returns inputs in FIFO order") {
        InputAction in1, in2;
        in1.forward = 1.0f;
        in2.lateral = -1.0f;

        buffer.BufferInput(in1);
        buffer.BufferInput(in2);

        auto out1 = buffer.GetNextInput();
        REQUIRE(out1.forward == 1.0f);
        REQUIRE(out1.lateral == 0.0f);

        auto out2 = buffer.GetNextInput();
        REQUIRE(out2.forward == 0.0f);
        REQUIRE(out2.lateral == -1.0f);
    }

    SECTION("Buffer respects capacity limit") {
        for (int i = 0; i < InputBuffer::BUFFER_SIZE + 5; ++i) {
            InputAction input;
            input.forward = static_cast<float>(i);
            buffer.BufferInput(input);
        }

        REQUIRE(buffer.GetBufferSize() <= InputBuffer::BUFFER_SIZE);
    }
}

TEST_CASE("Character Stats - Damage System", "[character][stats]") {
    entt::registry registry;
    auto entity = registry.create();
    
    CharacterStats stats(entity);

    SECTION("Character starts with full health") {
        REQUIRE(stats.GetHealth() == stats.GetMaxHealth());
        REQUIRE(stats.GetHealthPercent() == Approx(1.0f));
    }

    SECTION("Taking damage reduces health") {
        float initial_health = stats.GetHealth();
        stats.TakeDamage(10.0f);
        
        REQUIRE(stats.GetHealth() == initial_health - 10.0f);
    }

    SECTION("Armor reduces damage taken") {
        stats.SetArmor(50.0f);
        float damage = 100.0f;
        float final_damage = stats.CalculateFinalDamage(damage);
        
        REQUIRE(final_damage < damage);
    }

    SECTION("Resistances reduce elemental damage") {
        stats.SetResistance(ElementType::Fire, 0.5f);
        float fire_damage = stats.CalculateFinalDamage(100.0f, 0.0f, ElementType::Fire);
        float physical_damage = stats.CalculateFinalDamage(100.0f, 0.0f, ElementType::Fire);
        
        REQUIRE(fire_damage < 100.0f);
    }

    SECTION("Character dies when health reaches 0") {
        stats.TakeDamage(stats.GetMaxHealth() + 10.0f);
        REQUIRE(stats.IsDead());
    }

    SECTION("Healing restores health") {
        stats.TakeDamage(50.0f);
        float damaged_health = stats.GetHealth();
        
        stats.Heal(20.0f);
        REQUIRE(stats.GetHealth() == damaged_health + 20.0f);
    }

    SECTION("Stamina system works") {
        REQUIRE(stats.GetStaminaPercent() == Approx(1.0f));
        
        bool consumed = stats.ConsumeStamina(20.0f);
        REQUIRE(consumed);
        REQUIRE(stats.GetStamina() < stats.GetMaxStamina());
    }
}

TEST_CASE("Character Stats - Experience & Leveling", "[character][stats][progression]") {
    entt::registry registry;
    auto entity = registry.create();
    
    CharacterStats stats(entity);

    SECTION("Character starts at level 1") {
        REQUIRE(stats.GetLevel() == 1);
    }

    SECTION("Adding experience can trigger level up") {
        uint8_t initial_level = stats.GetLevel();
        uint64_t xp_needed = stats.GetExperienceForNextLevel() + 1;
        
        bool leveled_up = stats.AddExperience(xp_needed);
        
        REQUIRE(leveled_up);
        REQUIRE(stats.GetLevel() == initial_level + 1);
    }

    SECTION("Level up increases max health") {
        float initial_max_health = stats.GetMaxHealth();
        
        stats.AddExperience(stats.GetExperienceForNextLevel() + 1);
        
        REQUIRE(stats.GetMaxHealth() > initial_max_health);
    }
}

TEST_CASE("Locomotion System - Animation Blending", "[character][animation]") {
    entt::registry registry;
    auto entity = registry.create();
    
    LocomotionSystem locomotion(entity);

    SECTION("Initial parameter is idle (0.0)") {
        REQUIRE(locomotion.GetLocomotionParameter() == Approx(0.0f));
    }

    SECTION("Speed -> Locomotion parameter mapping") {
        // Idle
        locomotion.Update(0.0f, 0.0f, 0.016f);
        REQUIRE(locomotion.GetLocomotionParameter() <= 0.1f);

        // Walk
        locomotion.Update(5.0f, 0.0f, 0.016f);
        float walk_param = locomotion.GetLocomotionParameter();
        REQUIRE(walk_param > 0.0f && walk_param < 1.0f);

        // Sprint
        locomotion.Update(10.0f, 0.0f, 0.016f);
        float sprint_param = locomotion.GetLocomotionParameter();
        REQUIRE(sprint_param > walk_param);
    }

    SECTION("Smooth blending between states") {
        float prev_param = 0.0f;
        
        for (int i = 0; i < 5; ++i) {
            locomotion.Update(10.0f, 0.0f, 0.016f);
            float current_param = locomotion.GetLocomotionParameter();
            
            // Check for smooth transition (no instant jumps)
            float delta = current_param - prev_param;
            REQUIRE(abs(delta) < 0.5f);  // Max change per frame
            
            prev_param = current_param;
        }
    }
}
