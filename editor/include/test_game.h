#pragma once

// ============================================================================
// The test game (Road to the Test Game, phase D): Create > Test Game builds a
// small, complete game into the open scene, from the engine's own parts.
// ============================================================================
//
//   - a world (the Rolling hills preset: terrain, erosion, rivers, biomes,
//     forests and grass),
//   - the player: third person, the full movement set, a rifle, three
//     abilities, potions, mortal, saved,
//   - camps of creatures (NPC agents on the navmesh: patrol, chase, claw or
//     spit), mortal, dropping ammunition and potions,
//   - supply caches to open with E, two checkpoints that save, and a beacon,
//   - the rules as logic scripts (GameRules, Checkpoint, Goal .lgraph files,
//     written next to the scene), so the scripting is exercised too:
//     clear every camp, then reach the beacon.
//
// The gameplay components are given once the bridge mirrors the new entities
// (`outfits`, applied by main a frame later), as for the player templates.
// Everything made is ordinary scene content: select it, change it, save it.

#include "ecs/gameplay_abilities.h"
#include "ecs/gameplay_attributes.h"
#include "ecs/gameplay_combat.h"
#include "ecs/gameplay_interaction.h"
#include "ecs/gameplay_life.h"
#include "ecs/gameplay_tags.h"
#include "ecs/gameplay_triggers.h"
#include "entity.h"
#include "collider_component.h"
#include "entity_factory.h"
#include "npc_agent_component.h"
#include "player_templates.h"
#include "scene.h"
#include "script_component.h"
#include "terrain_component.h"
#include "vscript/graph.h"
#include "vscript/io.h"
#include "world_preset.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace schizo::editor {

/// Gameplay components for a scene entity, given once the bridge has it.
struct PendingOutfit {
    uint32_t id = 0;
    std::function<void(schizo::ecs::World&, schizo::ecs::Entity)> fn;
};

struct TestGameOptions {
    uint32_t    seed = 11;
    int         camps = 4;
    int         per_camp = 3;
    int         caches = 8;
    std::string dir = "testgame";   // where its scripts and materials go (relative to the project)
};

struct TestGameBuild {
    std::vector<PendingOutfit>              outfits;
    std::vector<std::shared_ptr<schizo::scene::Entity>> made;
    std::shared_ptr<schizo::scene::Entity>  player, terrain;
    int  enemies = 0;
    bool ok = false;
    std::string error;
};

namespace test_game_detail {

using schizo::scene::Entity;
using schizo::scene::EntityFactory;
using schizo::scene::Scene;

// ---- the rules, as logic scripts -------------------------------------------------
inline std::string game_rules_script(int enemies) {
    using namespace schizo::vs;
    Script s;
    s.name = "GameRules";
    s.add_variable("Kills", kInt, Value::integer(0));
    s.add_variable("Total", kInt, Value::integer(enemies));
    Graph& g = s.add_graph(GraphKind::Event, "EventGraph");
    const int begin = g.add("event.begin_play").id;
    const int hello = g.add("ui.show_message").id;
    g.find(hello)->set_default("Text", Value::string("Clear the creature camps, then reach the beacon.  (E: open caches and take loot)"));
    g.find(hello)->set_default("Seconds", Value::number(7.0));
    g.link(begin, "Then", hello, "In");

    const int died = g.add("event.gameplay").id;
    g.find(died)->name = "died";
    const int is_enemy = g.add("math.equal").id;
    g.find(is_enemy)->targ = kString;
    g.find(is_enemy)->set_default("B", Value::string("enemy"));
    g.link(died, "Param", is_enemy, "A");
    const int br = g.add("flow.branch").id;
    g.link(died, "Then", br, "In");
    g.link(is_enemy, "Result", br, "Condition");
    const int get_k = g.add("var.get").id;
    g.find(get_k)->name = "Kills";
    const int add1 = g.add("math.add").id;
    g.find(add1)->set_default("B", Value::integer(1));
    g.link(get_k, "Value", add1, "A");
    const int set_k = g.add("var.set").id;
    g.find(set_k)->name = "Kills";
    g.link(br, "True", set_k, "In");
    g.link(add1, "Result", set_k, "Value");
    // "Creatures left: N"
    const int get_t = g.add("var.get").id;
    g.find(get_t)->name = "Total";
    const int left = g.add("math.sub").id;
    g.link(get_t, "Value", left, "A");
    g.link(set_k, "Value", left, "B");
    const int text = g.add("math.add").id;
    g.find(text)->targ = kString;
    g.find(text)->set_default("A", Value::string("Creatures left: "));
    g.link(left, "Result", text, "B");
    const int say = g.add("ui.show_message").id;
    g.find(say)->set_default("Seconds", Value::number(3.0));
    g.link(set_k, "Then", say, "In");
    g.link(text, "Result", say, "Text");
    // All of them: the beacon opens.
    const int done = g.add("math.greater_equal").id;
    g.link(set_k, "Value", done, "A");
    const int get_t2 = g.add("var.get").id;
    g.find(get_t2)->name = "Total";
    g.link(get_t2, "Value", done, "B");
    const int br2 = g.add("flow.branch").id;
    g.link(say, "Then", br2, "In");
    g.link(done, "Result", br2, "Condition");
    const int flag = g.add("world.set_flag").id;
    g.find(flag)->set_default("Key", Value::string("game.cleared"));
    g.find(flag)->set_default("Value", Value::integer(1));
    g.link(br2, "True", flag, "In");
    const int cleared = g.add("ui.show_message").id;
    g.find(cleared)->set_default("Text", Value::string("Every camp is cleared. Head for the beacon!"));
    g.find(cleared)->set_default("Seconds", Value::number(6.0));
    g.link(flag, "Then", cleared, "In");
    return to_text(s);
}

inline std::string checkpoint_script() {
    using namespace schizo::vs;
    Script s;
    s.name = "Checkpoint";
    Graph& g = s.add_graph(GraphKind::Event, "EventGraph");
    const int enter = g.add("event.trigger_enter").id;
    const int save = g.add("world.save_game").id;
    g.find(save)->set_default("Slot", Value::string("quick"));
    g.link(enter, "Then", save, "In");
    const int say = g.add("ui.show_message").id;
    g.find(say)->set_default("Text", Value::string("Checkpoint -- the game is saved"));
    g.find(say)->set_default("Seconds", Value::number(3.0));
    g.link(save, "Then", say, "In");
    return to_text(s);
}

inline std::string goal_script() {
    using namespace schizo::vs;
    Script s;
    s.name = "Goal";
    Graph& g = s.add_graph(GraphKind::Event, "EventGraph");
    const int enter = g.add("event.trigger_enter").id;
    const int flag = g.add("world.get_flag").id;
    g.find(flag)->set_default("Key", Value::string("game.cleared"));
    const int ge = g.add("math.greater_equal").id;
    g.find(ge)->set_default("B", Value::integer(1));
    g.link(flag, "Value", ge, "A");
    const int br = g.add("flow.branch").id;
    g.link(enter, "Then", br, "In");
    g.link(ge, "Result", br, "Condition");
    const int win = g.add("ui.show_message").id;
    g.find(win)->set_default("Text", Value::string("You win! The beacon is yours."));
    g.find(win)->set_default("Seconds", Value::number(10.0));
    g.link(br, "True", win, "In");
    const int wset = g.add("world.set_flag").id;
    g.find(wset)->set_default("Key", Value::string("game.won"));
    g.find(wset)->set_default("Value", Value::integer(1));
    g.link(win, "Then", wset, "In");
    const int not_yet = g.add("ui.show_message").id;
    g.find(not_yet)->set_default("Text", Value::string("The beacon is dark: clear every creature camp first."));
    g.find(not_yet)->set_default("Seconds", Value::number(4.0));
    g.link(br, "False", not_yet, "In");
    return to_text(s);
}

inline bool write_text(const std::filesystem::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}

// A deterministic stream of numbers in [0, 1).
struct Dice {
    uint32_t s;
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; }
};

}  // namespace test_game_detail

/// Build the test game into `scene`. The player is the scene's "Player" when
/// it has one (moved to the start), else a new one.
inline TestGameBuild build_test_game(const std::shared_ptr<schizo::scene::Scene>& scene, const TestGameOptions& opt = {}) {
    namespace ecs = schizo::ecs;
    namespace fs = std::filesystem;
    using namespace test_game_detail;
    TestGameBuild out;
    if (!scene) { out.error = "no scene"; return out; }
    std::error_code ec;
    fs::create_directories(fs::path(opt.dir), ec);
    ensure_starter_data();

    // ---- the world ------------------------------------------------------------------
    const auto presets = schizo::scene::world_presets();
    const schizo::scene::BuiltWorld bw = schizo::scene::build_world(*scene, presets[0], opt.seed, (fs::path(opt.dir) / "materials").generic_string());
    if (!bw.terrain) { out.error = "the world could not be built"; return out; }
    out.terrain = bw.terrain;
    for (const auto& e : bw.made) out.made.push_back(e);
    auto tc = bw.terrain->GetComponent<schizo::scene::TerrainComponent>();
    const glm::vec3 tp = bw.terrain->GetTransform()->GetWorldPosition();
    const float half = tc->GetSize() * 0.5f - 24.0f;
    auto height = [&](float x, float z) { return tp.y + tc->SampleHeightLocal(x - tp.x, z - tp.z); };
    // The low ground holds the rivers: keep the game above it, and off slopes.
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i <= 32; ++i)
        for (int j = 0; j <= 32; ++j) {
            const float h = height(tp.x - half + 2.0f * half * i / 32.0f, tp.z - half + 2.0f * half * j / 32.0f);
            lo = std::min(lo, h); hi = std::max(hi, h);
        }
    const float dry = lo + (hi - lo) * 0.28f;
    auto good = [&](float x, float z) {
        if (std::fabs(x - tp.x) > half || std::fabs(z - tp.z) > half) return false;
        const float h = height(x, z);
        if (h < dry) return false;
        const float s = std::max(std::fabs(height(x + 2.0f, z) - height(x - 2.0f, z)), std::fabs(height(x, z + 2.0f) - height(x, z - 2.0f)));
        return s < 1.6f;   // under ~22 degrees
    };
    Dice dice{opt.seed * 2654435761u + 7u};
    auto find_spot = [&](glm::vec2 around, float radius, glm::vec2& out_p) {
        for (int tries = 0; tries < 400; ++tries) {
            const float a = dice.next() * 6.2831853f, r = std::sqrt(dice.next()) * radius;
            const glm::vec2 p = around + glm::vec2(std::cos(a), std::sin(a)) * r;
            if (good(p.x, p.y)) { out_p = p; return true; }
        }
        return false;
    };
    auto ground = [&](glm::vec2 p, float up) { return glm::vec3(p.x, height(p.x, p.y) + up, p.y); };

    // ---- start and goal: across the map from each other --------------------------------
    glm::vec2 start, goal;
    if (!find_spot(glm::vec2(tp.x - half * 0.75f, tp.z), half * 0.3f, start) ||
        !find_spot(glm::vec2(tp.x + half * 0.75f, tp.z), half * 0.3f, goal)) {
        out.error = "no dry, level ground for the start or the goal";
        return out;
    }

    // ---- the player ---------------------------------------------------------------------
    auto player = scene->GetEntityByName("Player");
    if (!player) {
        player = EntityFactory::CreatePlayer(scene, "Player", ground(start, 1.5f));
        if (player) out.made.push_back(player);
    } else {
        player->GetTransform()->SetWorldPosition(ground(start, 1.5f));
    }
    if (!player) { out.error = "no player"; return out; }
    out.player = player;
    {
        // Face the goal.
        const glm::vec2 d = glm::normalize(goal - start);
        player->GetTransform()->SetWorldRotation(glm::angleAxis(std::atan2(-d.x, -d.y), glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    out.outfits.push_back({player->GetId(), [](ecs::World& w, ecs::Entity e) {
        outfit_player(w, e, PlayerTemplate::ActionRpg);
        ecs::WeaponState ws;
        ws.weapon_id = "starter_rifle";
        ws.ammo_in_mag = 30;
        if (w.has<ecs::WeaponState>(e)) w.get<ecs::WeaponState>(e) = ws; else w.add<ecs::WeaponState>(e, ws);
        if (auto* inv = w.try_get<ecs::Inventory>(e)) {
            ecs::ItemInstance rounds; rounds.def_id = "rifle_rounds"; rounds.quantity = 120;
            ecs::inventory_add(*inv, rounds);
        }
    }});

    // ---- the rules ------------------------------------------------------------------------
    const fs::path dir(opt.dir);
    // GameRules is written last, when the number of creatures is known.
    if (!write_text(dir / "Checkpoint.lgraph", checkpoint_script()) || !write_text(dir / "Goal.lgraph", goal_script())) {
        out.error = "could not write the scripts to " + opt.dir;
        return out;
    }
    if (auto rules = scene->CreateEntity("GameRules")) {
        rules->AddComponent<schizo::scene::ScriptComponent>((dir / "GameRules.lgraph").generic_string());
        out.made.push_back(rules);
    }

    // ---- the beacon and the checkpoints -----------------------------------------------------
    auto marker = [&](const std::string& name, glm::vec2 at, glm::vec3 size, glm::vec4 colour) {
        auto e = EntityFactory::CreateCube(scene, name, 1.0f, colour);
        if (!e) return e;
        e->GetTransform()->SetWorldPosition(ground(at, size.y * 0.5f));
        e->GetTransform()->SetLocalScale(size);
        out.made.push_back(e);
        return e;
    };
    auto trigger = [&](const std::string& name, glm::vec2 at, glm::vec3 half_extents, const fs::path& script) {
        auto e = scene->CreateEntity(name);
        if (!e) return;
        e->GetTransform()->SetWorldPosition(ground(at, half_extents.y));
        e->AddComponent<schizo::scene::ScriptComponent>(script.generic_string());
        out.made.push_back(e);
        out.outfits.push_back({e->GetId(), [half_extents](ecs::World& w, ecs::Entity en) {
            ecs::TriggerVolume tv;
            tv.shape = 0;
            tv.half_extents = half_extents;
            if (w.has<ecs::TriggerVolume>(en)) w.get<ecs::TriggerVolume>(en) = tv; else w.add<ecs::TriggerVolume>(en, tv);
        }});
    };
    marker("Beacon", goal, glm::vec3(0.8f, 9.0f, 0.8f), glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));
    trigger("Beacon Zone", goal, glm::vec3(4.0f, 4.0f, 4.0f), dir / "Goal.lgraph");
    for (int c = 1; c <= 2; ++c) {
        glm::vec2 at;
        if (!find_spot(glm::mix(start, goal, c / 3.0f), 20.0f, at)) continue;
        marker("Checkpoint " + std::to_string(c), at, glm::vec3(0.4f, 2.5f, 0.4f), glm::vec4(0.3f, 0.6f, 1.0f, 1.0f));
        trigger("Checkpoint Zone " + std::to_string(c), at, glm::vec3(3.0f, 3.0f, 3.0f), dir / "Checkpoint.lgraph");
    }

    // ---- creature camps ------------------------------------------------------------------------
    std::vector<glm::vec2> camps;
    for (int c = 0; c < opt.camps; ++c) {
        // Spread along the way, off to either side of the straight line.
        const float t = (c + 1.0f) / (opt.camps + 1.0f);
        const glm::vec2 line = glm::mix(start, goal, t);
        const glm::vec2 side = glm::normalize(glm::vec2(-(goal - start).y, (goal - start).x)) * ((c % 2) ? 1.0f : -1.0f) * 25.0f;
        glm::vec2 at;
        if (!find_spot(line + side, 25.0f, at) && !find_spot(line, 40.0f, at)) continue;
        camps.push_back(at);
        marker("Camp " + std::to_string(c + 1), at, glm::vec3(1.2f, 0.4f, 1.2f), glm::vec4(0.35f, 0.2f, 0.1f, 1.0f));
        const bool spitters = (c % 3) == 2;   // every third camp fights from range
        for (int k = 0; k < opt.per_camp; ++k) {
            glm::vec2 p;
            if (!find_spot(at, 5.0f, p)) p = at;
            auto cr = EntityFactory::CreateCapsule(scene, spitters ? "Spitter" : "Creature", 0.4f, 1.8f,
                                                   spitters ? glm::vec4(0.55f, 0.25f, 0.6f, 1.0f) : glm::vec4(0.3f, 0.55f, 0.2f, 1.0f));
            if (!cr) continue;
            // Agents walk by their transform: a collider would stay behind as a
            // wall where they stood.
            if (auto col = cr->GetComponent<schizo::scene::ColliderComponent>()) cr->RemoveComponent(col);
            cr->GetTransform()->SetWorldPosition(ground(p, 0.9f));
            auto* ag = cr->GetNpcAgentComponent();
            ag->enabled = true;
            ag->target_name = "Player";
            ag->sight_range = spitters ? 26.0f : 20.0f;
            ag->sight_fov_deg = 140.0f;
            ag->move_speed = spitters ? 2.6f : 3.4f;
            ag->patrol_radius = 6.0f;
            ag->attack_range = spitters ? 12.0f : 2.0f;
            ag->ability_index = 0;
            out.made.push_back(cr);
            ++out.enemies;
            out.outfits.push_back({cr->GetId(), [spitters](ecs::World& w, ecs::Entity e) {
                ecs::AttributeSet a;
                a.define("Health", spitters ? 40.0f : 60.0f, 0.0f, spitters ? 40.0f : 60.0f);
                a.define("Speed", 5, 0, 20);
                w.add<ecs::AttributeSet>(e, a);
                ecs::CombatActor ca;
                ca.hurt_height = 0.2f;
                ca.hurt_radius = 0.75f;
                w.add<ecs::CombatActor>(e, ca);
                ecs::Mortal m;
                m.corpse_time = 4.0f;
                m.loot = {{"rifle_rounds", 3.0f, 0.8f, 15, 30, 0}, {"health_potion", 1.0f, 0.6f, 1, 1, 0}};
                w.add<ecs::Mortal>(e, m);
                ecs::Ability atk;
                atk.name = spitters ? "Spit" : "Claw";
                atk.cooldown = spitters ? 2.2f : 1.2f;
                atk.effects_target.push_back(ecs::make_instant(atk.name, "Health", spitters ? -6.0f : -9.0f));
                ecs::AbilitySet set;
                set.abilities.push_back({atk, 0.0f});
                w.add<ecs::AbilitySet>(e, set);
                ecs::GameplayTags tags;
                tags.add("team.enemy");
                w.add<ecs::GameplayTags>(e, tags);
            }});
        }
    }

    // ---- supply caches --------------------------------------------------------------------------
    for (int k = 0; k < opt.caches; ++k) {
        const glm::vec2 around = glm::mix(start, goal, (k + 0.5f) / opt.caches);
        glm::vec2 at;
        if (!find_spot(around, 18.0f, at)) continue;
        auto box = marker("Supply Cache", at, glm::vec3(0.7f, 0.5f, 0.5f), glm::vec4(0.55f, 0.4f, 0.2f, 1.0f));
        if (!box) continue;
        const bool ammo = (k % 2) == 0;
        out.outfits.push_back({box->GetId(), [ammo](ecs::World& w, ecs::Entity e) {
            ecs::Interactable it;
            it.prompt = ammo ? "Open supply cache (40 rounds)" : "Open supply cache (a health potion)";
            it.event = "cache.opened";
            it.range = 2.2f;
            it.consume_on_use = true;
            w.add<ecs::Interactable>(e, it);
            w.add<ecs::Pickup>(e, ecs::Pickup{ammo ? "rifle_rounds" : "health_potion", ammo ? 40 : 1});
        }});
    }

    if (!write_text(dir / "GameRules.lgraph", game_rules_script(std::max(1, out.enemies)))) {
        out.error = "could not write the scripts to " + opt.dir;
        return out;
    }
    out.ok = true;
    return out;
}

}  // namespace schizo::editor
