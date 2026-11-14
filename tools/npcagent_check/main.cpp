// ============================================================================
// npcagent_check — perception, decision and movement, connected.
//
// Behaviour trees, the blackboard, the path follower and the vision cone were
// each built and verified separately, and driven by nothing. This covers the
// join: does an agent actually see, decide, and walk on the baked navmesh.
//
// The assertions are about OBSERVABLE behaviour rather than internal state,
// because the failure mode here is an agent that ticks happily and never moves
// — every subsystem reporting success while nothing happens in the world.
// ============================================================================
#include "nav_bake.h"
#include "npc_agents.h"

#include "ecs/gameplay_abilities.h"
#include "ecs/gameplay_ability_cast.h"
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <cstdint>

using namespace schizo;

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; std::printf("  OK   %s\n", what.c_str()); }
    else    { ++g_fail; std::printf("  FAIL %s\n", what.c_str()); }
}

// A scene with walkable ground, an agent, and a target it may or may not see.
struct World {
    std::shared_ptr<scene::Scene>  scene;
    std::shared_ptr<scene::Entity> agent, target;
    ai::NavMesh nav;
};

World make_world(glm::vec3 agent_pos, glm::vec3 target_pos, float fov = 100.0f) {
    World w;
    w.scene = std::make_shared<scene::Scene>("t");

    auto ground = w.scene->CreateEntity("Ground");
    auto tc = ground->AddComponent<scene::TerrainComponent>();
    tc->Resize(16, 160.0f);

    w.agent = w.scene->CreateEntity("Guard");
    w.agent->GetTransform()->SetLocalPosition(agent_pos);
    auto* ag = w.agent->GetNpcAgentComponent();
    ag->enabled       = true;
    ag->target_name   = "Player";
    ag->sight_fov_deg = fov;
    ag->sight_range   = 40.0f;
    ag->move_speed    = 5.0f;

    w.target = w.scene->CreateEntity("Player");
    w.target->GetTransform()->SetLocalPosition(target_pos);

    editor::bake_navmesh_from_scene(w.scene, w.nav);
    return w;
}

void run(editor::EditorNpcAgents& a, World& w, int frames) {
    for (int i = 0; i < frames; ++i) a.update(w.scene, w.nav, 1.0f / 60.0f);
}

float dist_xz(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

}  // namespace

int main() {
    std::printf("=== npcagent_check ===\n\n");

    // ---- 1. an agent with a visible target closes on it --------------------
    // The headline behaviour, and the one that proves all four subsystems are
    // actually joined rather than merely present.
    {
        // Target directly ahead (+Z is the entity's forward).
        World w = make_world({0, 0, 0}, {0, 0, 25});
        editor::EditorNpcAgents agents;

        const float before = dist_xz(w.agent->GetTransform()->GetWorldPosition(),
                                     w.target->GetTransform()->GetWorldPosition());
        run(agents, w, 120);
        const float after = dist_xz(w.agent->GetTransform()->GetWorldPosition(),
                                    w.target->GetTransform()->GetWorldPosition());

        check(agents.agent_count() == 1, "the agent entity gets a behaviour tree");
        check(agents.chasing() == 1, "it sees the target and chooses to chase");
        check(after < before - 1.0f, "and actually CLOSES the distance");
    }

    // ---- 2. a target behind it is not seen ---------------------------------
    // Without this, "chasing" could just mean "a target exists somewhere".
    {
        World w = make_world({0, 0, 0}, {0, 0, -25}, /*fov=*/60.0f);
        editor::EditorNpcAgents agents;
        run(agents, w, 30);
        check(agents.chasing() == 0, "a target behind the agent is NOT seen");
    }

    // ---- 3. a target out of range is not seen ------------------------------
    {
        World w = make_world({0, 0, 0}, {0, 0, 25});
        w.agent->GetNpcAgentComponent()->sight_range = 5.0f;
        editor::EditorNpcAgents agents;
        run(agents, w, 30);
        check(agents.chasing() == 0, "a target beyond sight range is not seen");
    }

    // ---- 4. with nothing visible it patrols rather than standing still -----
    // A stationary agent and a broken agent look identical.
    {
        World w = make_world({0, 0, 0}, {0, 0, -25}, /*fov=*/60.0f);
        editor::EditorNpcAgents agents;
        const glm::vec3 start = w.agent->GetTransform()->GetWorldPosition();
        run(agents, w, 120);
        const glm::vec3 end = w.agent->GetTransform()->GetWorldPosition();
        check(dist_xz(start, end) > 1.0f, "an agent with nothing to chase still patrols");
    }

    // ---- 5. NO NAVMESH means it does not move ------------------------------
    // The important negative: movement must come from the baked geometry. An
    // agent that slides toward its target without a navmesh would look correct
    // and prove nothing.
    {
        World w = make_world({0, 0, 0}, {0, 0, 25});
        ai::NavMesh empty;
        editor::EditorNpcAgents agents;
        const glm::vec3 start = w.agent->GetTransform()->GetWorldPosition();
        for (int i = 0; i < 60; ++i) agents.update(w.scene, empty, 1.0f / 60.0f);
        const glm::vec3 end = w.agent->GetTransform()->GetWorldPosition();
        check(dist_xz(start, end) < 0.001f,
              "without a baked navmesh the agent does not move at all");
    }

    // ---- 6. movement stays on the navmesh ----------------------------------
    // Following a path means the agent's positions are on walkable ground, not
    // a straight line through wherever the target happens to be.
    {
        World w = make_world({-40, 0, -40}, {40, 0, 40});
        editor::EditorNpcAgents agents;
        bool stayed_on_ground = true;
        for (int i = 0; i < 200; ++i) {
            agents.update(w.scene, w.nav, 1.0f / 60.0f);
            const glm::vec3 p = w.agent->GetTransform()->GetWorldPosition();
            // The terrain spans ±80; leaving it means the follower ignored the path.
            if (std::abs(p.x) > 80.0f || std::abs(p.z) > 80.0f) stayed_on_ground = false;
        }
        check(stayed_on_ground, "the agent stays within the walkable region while pathing");
    }

    // ---- 7. disabling releases the agent -----------------------------------
    {
        World w = make_world({0, 0, 0}, {0, 0, 25});
        editor::EditorNpcAgents agents;
        run(agents, w, 20);
        check(agents.agent_count() == 1, "running while enabled");
        w.agent->GetNpcAgentComponent()->enabled = false;
        run(agents, w, 2);
        check(agents.agent_count() == 0, "disabling releases its tree and path state");
    }

    // ---- 8. a missing target is survivable ---------------------------------
    // Agents are authored before the thing they hunt exists.
    {
        World w = make_world({0, 0, 0}, {0, 0, 25});
        w.agent->GetNpcAgentComponent()->target_name = "NobodyHere";
        editor::EditorNpcAgents agents;
        run(agents, w, 30);
        check(agents.chasing() == 0, "an agent whose target does not exist chases nothing");
        check(agents.agent_count() == 1, "and keeps running rather than dropping out");
    }


    // ---- 9. COMBAT: in range, the agent uses its ability -------------------
    // The AI decides WHEN; the ability data decides WHAT. These assertions are
    // about the decision, not about damage numbers.
    {
        World w = make_world({0, 0, 0}, {0, 0, 3});     // target already in range
        w.agent->GetNpcAgentComponent()->attack_range = 6.0f;

        editor::EcsSceneBridge bridge;
        bridge.sync_and_run(w.scene);

        // One free, instant ability, so activation is gated only by the AI's
        // decision rather than by cost or cooldown.
        const uint32_t cid = bridge.ecs_entity_id(w.agent->GetTransform());
        ecs::AbilitySet set;
        ecs::AbilitySlot slot;
        slot.ability.name = "Strike";
        set.abilities.push_back(slot);
        bridge.world().add<ecs::AbilitySet>(static_cast<ecs::Entity>(cid), set);

        editor::EditorNpcAgents agents;
        size_t total = 0;
        for (int i = 0; i < 30; ++i) {
            agents.update(w.scene, w.nav, 1.0f / 60.0f, &bridge);
            total += agents.attacks_this_frame();
        }
        check(total > 0, "an agent in range of a visible target uses its ability");
        check(agents.chasing() == 0, "and stops chasing while attacking (attack outranks chase)");
    }

    // ---- 10. it does NOT attack out of range -------------------------------
    // Without this, "attacks" could just mean "a target exists somewhere".
    {
        World w = make_world({0, 0, 0}, {0, 0, 30});
        w.agent->GetNpcAgentComponent()->attack_range = 3.0f;

        editor::EcsSceneBridge bridge;
        bridge.sync_and_run(w.scene);
        const uint32_t cid = bridge.ecs_entity_id(w.agent->GetTransform());
        ecs::AbilitySet set; set.abilities.push_back(ecs::AbilitySlot{});
        bridge.world().add<ecs::AbilitySet>(static_cast<ecs::Entity>(cid), set);

        editor::EditorNpcAgents agents;
        size_t total = 0;
        for (int i = 0; i < 5; ++i) {   // too few frames to close the gap
            agents.update(w.scene, w.nav, 1.0f / 60.0f, &bridge);
            total += agents.attacks_this_frame();
        }
        check(total == 0, "a target beyond attack range is chased, not attacked");
        check(agents.chasing() == 1, "and the agent closes instead");
    }

    // ---- 11. attack_range 0 means "not a combatant" ------------------------
    // A patroller must not acquire combat behaviour by accident.
    {
        World w = make_world({0, 0, 0}, {0, 0, 2});
        // attack_range left at its 0 default
        editor::EcsSceneBridge bridge;
        bridge.sync_and_run(w.scene);
        const uint32_t cid = bridge.ecs_entity_id(w.agent->GetTransform());
        ecs::AbilitySet set; set.abilities.push_back(ecs::AbilitySlot{});
        bridge.world().add<ecs::AbilitySet>(static_cast<ecs::Entity>(cid), set);

        editor::EditorNpcAgents agents;
        size_t total = 0;
        for (int i = 0; i < 20; ++i) {
            agents.update(w.scene, w.nav, 1.0f / 60.0f, &bridge);
            total += agents.attacks_this_frame();
        }
        check(total == 0, "attack_range 0 keeps a patroller out of combat entirely");
    }

    // ---- 12. cooldown gates the rate ---------------------------------------
    // The ability system owns cost and cooldown. If the agent bypassed it, an
    // ability would fire every frame -- 60 attacks a second, the classic
    // symptom of AI calling activation directly instead of asking.
    {
        World w = make_world({0, 0, 0}, {0, 0, 3});
        w.agent->GetNpcAgentComponent()->attack_range = 6.0f;

        editor::EcsSceneBridge bridge;
        bridge.sync_and_run(w.scene);
        const uint32_t cid = bridge.ecs_entity_id(w.agent->GetTransform());
        ecs::AbilitySet set;
        ecs::AbilitySlot slot;
        slot.ability.name     = "Slow Strike";
        slot.ability.cooldown = 1.0f;              // one per second
        set.abilities.push_back(slot);
        bridge.world().add<ecs::AbilitySet>(static_cast<ecs::Entity>(cid), set);

        editor::EditorNpcAgents agents;
        size_t total = 0;
        for (int i = 0; i < 30; ++i) {             // half a second of frames
            agents.update(w.scene, w.nav, 1.0f / 60.0f, &bridge);
            total += agents.attacks_this_frame();
        }
        check(total == 1, "a one-second cooldown fires once in half a second, not every frame");
    }

    // ---- 13. no bridge means no attacks, and no crash ----------------------
    // Headless callers pass nullptr; the agent must degrade to movement only.
    {
        World w = make_world({0, 0, 0}, {0, 0, 3});
        w.agent->GetNpcAgentComponent()->attack_range = 6.0f;
        editor::EditorNpcAgents agents;
        for (int i = 0; i < 10; ++i) agents.update(w.scene, w.nav, 1.0f / 60.0f, nullptr);
        check(agents.attacks_this_frame() == 0, "without an ECS bridge the agent does not attack");
        check(agents.agent_count() == 1, "and keeps running");
    }


    // ---- a floating-origin rebase is invisible to an agent -----------------
    // Streaming and agents were built separately and each verified alone; this
    // is the seam between them. A rebase moves every entity but not the
    // navmesh, nor the world-space numbers an agent remembers -- its patrol
    // goal, the last goal it pathed to, and the waypoints it is steering
    // along. The agent has to come out of it still walking, rather than frozen
    // off-mesh or heading for a place that no longer exists.
    {
        const glm::vec3 kShift(-512.0f, 0.0f, 256.0f);
        World w = make_world({0.0f, 0.0f, 0.0f}, {200.0f, 0.0f, 200.0f});  // target far: patrols
        editor::EditorNpcAgents agents;
        run(agents, w, 90);

        const glm::vec3 before = w.agent->GetTransform()->GetLocalPosition();
        check(dist_xz(before, {0.0f, 0.0f, 0.0f}) > 0.5f,
              "an agent is patrolling before the rebase");

        // Rebase in the editor's order: entities, then mesh, then agent state.
        for (const auto& e : w.scene->GetEntities())
            if (e && !e->GetParent())
                if (auto* tf = e->GetTransform())
                    tf->SetLocalPosition(tf->GetLocalPosition() + kShift);
        w.nav.translate(kShift);
        agents.apply_origin_shift(kShift);

        const glm::vec3 at_rebase = w.agent->GetTransform()->GetLocalPosition();
        run(agents, w, 90);
        const glm::vec3 after = w.agent->GetTransform()->GetLocalPosition();

        check(dist_xz(after, at_rebase) > 0.5f,
              "and is still walking 90 frames later -- not frozen off-mesh");
        check(w.nav.contains(after),
              "on the navmesh, where the world now is");
        check(dist_xz(after, before) > 100.0f,
              "in the new frame of reference, not dragged back toward the old origin");
        check(agents.agent_count() == 1,
              "with the agent still registered across the rebase");
    }

    // ---- an attack as data: a wind-up, then the ability goes off -----------
    // `ability` names an ability block: its cast time is a wind-up the agent
    // stands through, then it reaches the target through the ability system.
    {
        ecs::AbilityDef claw;
        claw.id = "chk_claw"; claw.name = "Claw";
        claw.targeting = ecs::AbilityTargeting::Aimed;
        claw.range = 4.0f; claw.cast_time = 0.3f; claw.cooldown = 1.0f;
        claw.damage = 10.0f; claw.damage_type = "physical";
        ecs::register_ability_def(claw);
        World w = make_world({0, 0, 0}, {0, 0, 2});
        auto* ag = w.agent->GetNpcAgentComponent();
        ag->attack_range = 2.5f;
        ag->ability = "chk_claw";
        editor::EcsSceneBridge bridge;
        bridge.sync_and_run(w.scene);
        auto& ew = bridge.world();
        const auto tid = static_cast<ecs::Entity>(bridge.ecs_entity_id(w.target->GetTransform()));
        ecs::AttributeSet a; a.define("Health", 100.0f, 0.0f, 100.0f);
        ew.add<ecs::AttributeSet>(tid, a);
        ecs::CombatActor ca; ca.hurt_radius = 0.6f; ca.hurt_height = 0.0f;
        ew.add<ecs::CombatActor>(tid, ca);
        editor::EditorNpcAgents agents;
        float early = 0.0f;
        size_t attacks = 0;
        bool sourced = false;
        for (int i = 0; i < 60; ++i) {
            agents.update(w.scene, w.nav, 1.0f / 60.0f, &bridge);
            attacks += agents.attacks_this_frame();
            if (agents.attacks_this_frame() > 0)
                sourced = agents.attack_sources().size() == 1 && dist_xz(agents.attack_sources()[0], {0, 0, 0}) < 1.0f;
            if (i == 10) early = ew.get<ecs::AttributeSet>(tid).get("Health");
        }
        check(early == 100.0f, "an attack with a cast time winds up first: no damage after 1/6 s");
        check(attacks == 1 && ew.get<ecs::AttributeSet>(tid).get("Health") == 90.0f,
              "then it goes off once (its own cooldown after) and the target takes its damage");
        check(sourced, "and the hit reports where it came from (the HUD's hit direction)");
    }

    // ---- line of sight: a wall between them hides the target ----------------
    {
        World w = make_world({0, 0, 0}, {0, 0, 20});
        // Everything is behind a wall 5 m ahead.
        const ecs::WorldRayFn wall = [](const glm::vec3& o, const glm::vec3& d, float max_t, float& t, glm::vec3& n) {
            if (d.z <= 0.1f || o.z >= 5.0f) return false;
            const float hit = (5.0f - o.z) / d.z;
            if (hit > max_t) return false;
            t = hit; n = glm::vec3(0, 0, -1);
            return true;
        };
        editor::EditorNpcAgents agents;
        for (int i = 0; i < 10; ++i) agents.update(w.scene, w.nav, 1.0f / 60.0f, nullptr, &wall);
        check(agents.chasing() == 0, "a target behind a wall is not seen");
        World w2 = make_world({0, 0, 0}, {0, 0, 20});
        editor::EditorNpcAgents agents2;
        const ecs::WorldRayFn open = [](const glm::vec3&, const glm::vec3&, float, float&, glm::vec3&) { return false; };
        for (int i = 0; i < 10; ++i) agents2.update(w2.scene, w2.nav, 1.0f / 60.0f, nullptr, &open);
        check(agents2.chasing() == 1, "the same target in the open is");
    }

    // ---- a camp fights together; gunfire is heard ---------------------------
    {
        World w = make_world({0, 0, 0}, {0, 0, 20});
        auto second = w.scene->CreateEntity("Guard2");
        second->GetTransform()->SetLocalPosition({6, 0, 0});
        second->GetTransform()->SetWorldRotation(glm::angleAxis(3.14159f, glm::vec3(0, 1, 0)));   // facing away
        auto* ag2 = second->GetNpcAgentComponent();
        *ag2 = *w.agent->GetNpcAgentComponent();
        editor::EditorNpcAgents agents;
        for (int i = 0; i < 3; ++i) agents.update(w.scene, w.nav, 1.0f / 60.0f);
        check(agents.mode_of(second->GetId()) == editor::EditorNpcAgents::Mode::Chase,
              "one that sees the target calls the others near it (alert radius)");

        World q = make_world({0, 0, 0}, {0, 0, -30});   // behind it, beyond hearing it
        editor::EditorNpcAgents quiet;
        for (int i = 0; i < 3; ++i) quiet.update(q.scene, q.nav, 1.0f / 60.0f);
        const bool calm = quiet.mode_of(q.agent->GetId()) == editor::EditorNpcAgents::Mode::Patrol;
        quiet.notify_noise({0, 0, -30});
        quiet.update(q.scene, q.nav, 1.0f / 60.0f);
        check(calm && quiet.mode_of(q.agent->GetId()) == editor::EditorNpcAgents::Mode::Chase,
              "a shot within its hearing range brings it");
    }

    // ---- a leash: chased too far from home, it gives up and goes back -------
    {
        World w = make_world({0, 0, 0}, {0, 0, 30});
        w.agent->GetNpcAgentComponent()->leash_radius = 4.0f;
        editor::EditorNpcAgents agents;
        bool chased = false;
        for (int i = 0; i < 120; ++i) {
            agents.update(w.scene, w.nav, 1.0f / 60.0f);
            chased = chased || agents.mode_of(w.agent->GetId()) == editor::EditorNpcAgents::Mode::Chase;
        }
        check(chased && agents.mode_of(w.agent->GetId()) != editor::EditorNpcAgents::Mode::Chase &&
                  dist_xz(w.agent->GetTransform()->GetLocalPosition(), {0, 0, 0}) < 6.0f,
              "past its leash radius it stops chasing and heads home");
    }

    // ---- out of sight: to where it was last seen, then it searches ----------
    {
        World w = make_world({0, 0, 0}, {0, 0, 10});
        editor::EditorNpcAgents agents;
        for (int i = 0; i < 5; ++i) agents.update(w.scene, w.nav, 1.0f / 60.0f);
        w.target->SetActive(false);   // gone
        bool searched = false;
        for (int i = 0; i < 400 && !searched; ++i) {
            agents.update(w.scene, w.nav, 1.0f / 60.0f);
            searched = agents.mode_of(w.agent->GetId()) == editor::EditorNpcAgents::Mode::Search;
        }
        check(searched && dist_xz(w.agent->GetTransform()->GetLocalPosition(), {0, 0, 10}) < 3.0f,
              "a target lost from sight is looked for where it was last seen");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("FAIL npcagent_check\n");
    return g_fail ? 1 : 0;
}
