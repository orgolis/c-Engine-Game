// ============================================================================
// gameplay_check — verifies the F1/F2 ECS gameplay-authoring data path: the
// authorable-component registry + reflection-driven read/write that the editor
// inspector uses (add component -> walk fields -> edit by offset -> persists).
// ============================================================================
#include "ecs/world.h"
#include "ecs/authorable_components.h"
#include "ecs/gameplay_attributes.h"
#include "ecs/gameplay_tags.h"
#include "ecs/gameplay_effects.h"
#include "ecs/gameplay_abilities.h"
#include "ecs/gameplay_ability_cast.h"
#include "ecs/gameplay_life.h"
#include "ecs/gameplay_damage.h"
#include "ecs/gameplay_events.h"
#include "ecs/gameplay_triggers.h"
#include "ecs/gameplay_state_machine.h"
#include "ecs/gameplay_combat.h"
#include "ecs/gameplay_progression.h"
#include "ecs/gameplay_skills.h"
#include "ecs/gameplay_items.h"
#include "ecs/gameplay_inventory.h"
#include "ecs/gameplay_item_file.h"
#include "ecs/gameplay_crafting.h"
#include "ecs/gameplay_economy.h"
#include "ecs/gameplay_interaction.h"
#include "ecs/gameplay_quests.h"
#include "ecs/gameplay_npc.h"
#include "ecs/gameplay_savegame.h"
#include "ecs/gameplay_social.h"
#include "ecs/gameplay_weapons.h"
#include "ecs/gameplay_loadout.h"
#include "ecs/gameplay_vehicles.h"
#include "ecs/gameplay_building.h"
#include "ecs/gameplay_survival.h"
#include "ecs/gameplay_stealth.h"
#include "ecs/gameplay_logic.h"
#include "ecs/prefab.h"
#include "reflection/reflection.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

namespace ecs = schizo::ecs;

static int g_fail = 0;
static void check(const char* what, bool ok) {
    std::cout << (ok ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!ok) ++g_fail;
}

// Two facing combat actors 1.3 apart (attacker at origin, +Z forward); the target
// has a Health attribute so hits route through G0. Default hitbox reaches it.
static void setup_combat_pair(ecs::World& w, ecs::Entity& atk, ecs::Entity& tgt, float tgt_max_poise = 100.0f) {
    atk = w.create(); tgt = w.create();
    w.add<ecs::Transform>(atk, ecs::Transform{});
    ecs::Transform tt; tt.position = glm::vec3(0.0f, 0.0f, 1.3f);
    w.add<ecs::Transform>(tgt, tt);
    w.add<ecs::CombatActor>(atk, ecs::CombatActor{});
    ecs::CombatActor ct; ct.max_poise = tgt_max_poise; ct.poise = tgt_max_poise;
    w.add<ecs::CombatActor>(tgt, ct);
    w.add<ecs::AttributeSet>(tgt, ecs::AttributeSet{});
    w.get<ecs::AttributeSet>(tgt).define("Health", 100, 0, 100);
}

int main() {
    ecs::register_core_components();

    // The registry the inspector iterates.
    const auto& types = ecs::authorable_components();
    check("authorable registry is non-empty", !types.empty());

    const ecs::AuthorableComponent* health = ecs::find_authorable("Health");
    check("Health is authorable", health != nullptr);
    check("Ability State is authorable", ecs::find_authorable("Ability State") != nullptr);
    if (!health) { std::cout << "gameplay_check: " << g_fail << " FAILED\n"; return 1; }

    check("Health reflects fields", health->type && health->type->fields.size() == 3);

    ecs::World w;
    const ecs::Entity e = w.create();

    // add / has / get — the inspector's add + present path.
    check("component absent before add", !health->has(w, e));
    health->add(w, e);
    check("component present after add", health->has(w, e));
    void* comp = health->get(w, e);
    check("get() returns the live component", comp != nullptr);

    // Reflection-driven WRITE by field offset (exactly what draw_field does):
    // set current -> 42, max -> 250 via the reflected fields, then read back the
    // real component to prove the generic edit hit the actual memory.
    bool wrote_current = false, wrote_max = false;
    gws::reflect::for_each_field(comp, *health->type,
        [&](const gws::reflect::FieldInfo& f, void* p) {
            if (std::strcmp(f.name, "current") == 0) { *static_cast<float*>(p) = 42.0f;  wrote_current = true; }
            if (std::strcmp(f.name, "max")     == 0) { *static_cast<float*>(p) = 250.0f; wrote_max     = true; }
        });
    check("walked + wrote 'current' and 'max' generically", wrote_current && wrote_max);

    const ecs::Health& live = w.get<ecs::Health>(e);
    check("generic write reached the real component (current==42)", live.current == 42.0f);
    check("generic write reached the real component (max==250)",    live.max     == 250.0f);

    // Persistence intent: the component stays on the entity across other edits.
    w.add<ecs::Transform>(e, ecs::Transform{});   // simulate the per-frame Transform rewrite
    check("gameplay component survives a Transform rewrite", health->has(w, e) &&
          w.get<ecs::Health>(e).current == 42.0f);

    // remove — the inspector's Remove button.
    health->remove(w, e);
    check("component removed", !health->has(w, e));

    // F3: a component round-trips through reflection bytes (the exact mechanism
    // save_gameplay/load_gameplay use for the .gameplay sidecar).
    {
        struct Sink : gws::reflect::IByteSink {
            std::vector<uint8_t>* b;
            void write(const void* d, size_t n) override {
                auto p = static_cast<const uint8_t*>(d); b->insert(b->end(), p, p + n);
            }
        };
        struct Src : gws::reflect::IByteSource {
            const uint8_t* p; const uint8_t* e;
            bool read(void* o, size_t n) override { if (p + n > e) return false; std::memcpy(o, p, n); p += n; return true; }
            bool skip(size_t n) override { if (p + n > e) return false; p += n; return true; }
        };
        const auto* ti = gws::reflect::reflect<ecs::Health>();
        ecs::Health src_c; src_c.current = 42.0f; src_c.max = 250.0f; src_c.regen = 3.0f;
        std::vector<uint8_t> bytes; Sink sink; sink.b = &bytes;
        for (const auto& f : ti->fields)
            if (f.serializer.write) f.serializer.write(sink, reinterpret_cast<const char*>(&src_c) + f.offset);

        ecs::Health dst_c{};
        Src rd; rd.p = bytes.data(); rd.e = bytes.data() + bytes.size();
        for (const auto& f : ti->fields)
            if (f.serializer.read) f.serializer.read(rd, reinterpret_cast<char*>(&dst_c) + f.offset);

        check("F3: component round-trips through reflection bytes",
              dst_c.current == 42.0f && dst_c.max == 250.0f && dst_c.regen == 3.0f);
    }

    // F4: prefab capture -> text -> from_text -> instantiate round-trip.
    {
        ecs::World pw;
        const ecs::Entity src = pw.create();
        pw.add<ecs::Health>(src, ecs::Health{});      pw.get<ecs::Health>(src).current = 77.0f;
        pw.add<ecs::AbilityState>(src, ecs::AbilityState{}); pw.get<ecs::AbilityState>(src).charges = 5;

        ecs::Prefab pf = ecs::Prefab::capture(pw, src, "Enemy");
        check("prefab captures the present components", pf.components.size() == 2);

        ecs::Prefab pf2 = ecs::Prefab::from_text(pf.to_text());
        check("prefab round-trips through text", pf2.name == "Enemy" && pf2.components.size() == 2);

        const ecs::Entity spawned = pw.create();      // instantiate onto a fresh entity
        pf2.apply(pw, spawned);
        check("prefab instantiation restores components + values",
              pw.has<ecs::Health>(spawned)       && pw.get<ecs::Health>(spawned).current == 77.0f &&
              pw.has<ecs::AbilityState>(spawned) && pw.get<ecs::AbilityState>(spawned).charges == 5u);
    }

    // G0: data-driven AttributeSet — define ARBITRARY stats; custom-serialize;
    // flow through the same authorable/save/prefab path as POD components.
    {
        ecs::register_attribute_component();
        const ecs::AuthorableComponent* attrs = ecs::find_authorable("Attributes");
        check("AttributeSet is authorable via custom hooks",
              attrs && attrs->type == nullptr && attrs->serialize && attrs->deserialize);
        if (attrs) {
            ecs::World aw;
            const ecs::Entity a = aw.create();
            attrs->add(aw, a);
            auto& set = *static_cast<ecs::AttributeSet*>(attrs->get(aw, a));
            set.define("Health", 120, 0, 120);   // any names — a game defines its own
            set.define("Sanity", 80, 0, 100);
            set.set("Health", 42);
            check("define/set arbitrary attributes",
                  set.get("Health") == 42.0f && set.get("Sanity") == 80.0f && set.attributes.size() == 2);

            const auto bytes = ecs::serialize_authorable(*attrs, &set);
            ecs::AttributeSet set2;
            ecs::deserialize_authorable(*attrs, &set2, bytes.data(), bytes.size());
            check("AttributeSet custom serialize round-trips",
                  set2.get("Health") == 42.0f && set2.get("Sanity") == 80.0f && set2.attributes.size() == 2);

            const ecs::Entity spawned2 = aw.create();
            ecs::Prefab::from_text(ecs::Prefab::capture(aw, a, "Monster").to_text()).apply(aw, spawned2);
            const auto* set3 = static_cast<ecs::AttributeSet*>(attrs->get(aw, spawned2));
            check("prefab carries the data-driven attributes",
                  set3 && set3->get("Health") == 42.0f && set3->get("Sanity") == 80.0f);
        }
    }

    // G0: data-driven GameplayTags — hierarchical, user-defined, same path.
    {
        ecs::register_tags_component();
        const ecs::AuthorableComponent* tags = ecs::find_authorable("Tags");
        check("GameplayTags is authorable via custom hooks", tags && tags->serialize && tags->deserialize);

        ecs::GameplayTags g;
        g.add("state.stunned.hard");
        g.add("element.fire");
        g.add("element.fire");                       // dedup
        check("tags add + dedup", g.tags.size() == 2);
        check("hierarchical match: parent query hits child",
              g.has("state") && g.has("state.stunned") && g.has("state.stunned.hard"));
        check("no false match on a different branch", !g.has("state.frozen") && !g.has("element.ice"));
        check("exact vs hierarchical", g.has_exact("element.fire") && !g.has_exact("element"));

        std::vector<uint8_t> tb; tags->serialize(&g, tb);
        ecs::GameplayTags g2; tags->deserialize(&g2, tb.data(), tb.size());
        check("tags round-trip through custom serialize",
              g2.tags.size() == 2 && g2.has("state.stunned") && g2.has_exact("element.fire"));
    }

    // G0: gameplay effects — instant / periodic DoT / duration-tag / buff-revert,
    // all operating on the data-driven attributes + tags by NAME.
    {
        ecs::World ew;
        const ecs::Entity e = ew.create();
        ew.add<ecs::AttributeSet>(e, ecs::AttributeSet{});
        auto& set = ew.get<ecs::AttributeSet>(e);
        set.define("Health", 100, 0, 100);

        ecs::apply_effect(ew, e, ecs::make_instant("Hit", "Health", -30.0f));
        check("instant effect changes attribute (100 -> 70)", set.get("Health") == 70.0f);

        ecs::apply_effect(ew, e, ecs::make_dot("Poison", "Health", -5.0f, 1.0f, 3.0f));
        ecs::tick_effects(ew, 1.0f);
        ecs::tick_effects(ew, 1.0f);
        check("periodic DoT applies each period (70 -> 60)", set.get("Health") == 60.0f);
        ecs::tick_effects(ew, 1.5f);   // final tick then expiry
        check("DoT applies final tick + expires (-> 55, none active)",
              set.get("Health") == 55.0f && ew.get<ecs::ActiveEffects>(e).effects.empty());

        ecs::GameplayEffect stun;
        stun.name = "Stun"; stun.duration_type = ecs::EffectDurationType::Duration; stun.duration = 2.0f;
        stun.granted_tags = {"state.stunned"};
        ecs::apply_effect(ew, e, stun);
        check("duration effect grants a tag while active",
              ew.has<ecs::GameplayTags>(e) && ew.get<ecs::GameplayTags>(e).has("state.stunned"));
        ecs::tick_effects(ew, 2.5f);
        check("granted tag is removed on expiry", !ew.get<ecs::GameplayTags>(e).has("state.stunned"));

        set.set("Health", 40.0f);
        ecs::GameplayEffect buff;
        buff.name = "Fortify"; buff.duration_type = ecs::EffectDurationType::Duration; buff.duration = 1.0f;
        buff.revert_on_end = true;
        buff.modifiers.push_back({"Health", ecs::EffectOp::Add, 50.0f});
        ecs::apply_effect(ew, e, buff);
        check("duration buff applies on start (40 -> 90)", set.get("Health") == 90.0f);
        ecs::tick_effects(ew, 1.5f);
        check("buff reverts on expiry (90 -> 40)", set.get("Health") == 40.0f);
    }

    // G0: ability system — cost (attribute) + cooldown + tag gate, applying
    // effects on activation. All data — a game defines its own abilities.
    {
        ecs::World aw;
        const ecs::Entity caster = aw.create();
        const ecs::Entity target = aw.create();

        aw.add<ecs::AttributeSet>(caster, ecs::AttributeSet{});
        aw.get<ecs::AttributeSet>(caster).define("Stamina", 100, 0, 100);
        aw.add<ecs::AttributeSet>(target, ecs::AttributeSet{});
        aw.get<ecs::AttributeSet>(target).define("Health", 100, 0, 100);

        ecs::AbilitySet set;
        ecs::Ability dash;
        dash.name = "Power Strike";
        dash.cost_attribute = "Stamina"; dash.cost = 30.0f; dash.cooldown = 5.0f;
        dash.block_tags = {"state.stunned"};
        dash.effects_target.push_back(ecs::make_instant("Strike", "Health", -40.0f));
        set.abilities.push_back(ecs::AbilitySlot{dash, 0.0f});
        aw.add<ecs::AbilitySet>(caster, set);

        bool activated = ecs::try_activate_ability(aw, caster, 0, target);
        check("ability activates (cost paid, effect applied)",
              activated && aw.get<ecs::AttributeSet>(caster).get("Stamina") == 70.0f &&
              aw.get<ecs::AttributeSet>(target).get("Health") == 60.0f);
        check("ability is on cooldown after activation",
              aw.get<ecs::AbilitySet>(caster).abilities[0].cooldown_remaining == 5.0f);

        bool again = ecs::try_activate_ability(aw, caster, 0, target);
        check("cannot re-activate while on cooldown", !again &&
              aw.get<ecs::AttributeSet>(caster).get("Stamina") == 70.0f);

        ecs::tick_abilities(aw, 5.0f);
        check("cooldown ticks down to ready",
              aw.get<ecs::AbilitySet>(caster).abilities[0].cooldown_remaining == 0.0f);

        // block tag gates activation
        aw.add<ecs::GameplayTags>(caster, ecs::GameplayTags{});
        aw.get<ecs::GameplayTags>(caster).add("state.stunned");
        check("blocked tag prevents activation", !ecs::try_activate_ability(aw, caster, 0, target));
        aw.get<ecs::GameplayTags>(caster).remove_exact("state.stunned");

        // insufficient cost gates activation
        aw.get<ecs::AttributeSet>(caster).set("Stamina", 10.0f);
        check("insufficient cost prevents activation", !ecs::try_activate_ability(aw, caster, 0, target));
    }

    // Skill trees as data, learning that grants abilities.
    {
        const char* data =
            "ability sk_bolt\n  name Bolt\n  target aimed\nend\n"
            "ability sk_nova\n  name Nova\n  target area\nend\n"
            "skill sk_a\n  name Bolt\n  tree sk_test\n  cost 0\n  unlocks sk_bolt\nend\n"
            "skill sk_b\n  name Toughness\n  tree sk_test\n  cost 1\n  grant Health 25\n  tag perk.tough\nend\n"
            "skill sk_c\n  name Nova\n  tree sk_test\n  cost 1\n  requires sk_a\n  requires sk_b\n  unlocks sk_nova\nend\n"
            "skill sk_other\n  tree other_tree\nend\n";
        ecs::register_items_text(data);
        const ecs::SkillTree tree = ecs::skill_tree_named("sk_test");
        check("skill data: the tree's nodes in the order written, with costs, needs and grants",
              tree.nodes.size() == 3 && tree.nodes[2].id == "sk_c" && tree.nodes[2].prerequisites.size() == 2 &&
              tree.nodes[2].abilities.size() == 1 && tree.nodes[1].grants.size() == 1 && tree.nodes[1].granted_tags.size() == 1 &&
              tree.id == "sk_test");
        ecs::World w;
        const ecs::Entity me = w.create();
        w.add<ecs::AttributeSet>(me, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(me).define("Health", 100, 0, 100);
        check("set skill tree: unknown trees are refused", !ecs::set_skill_tree(w, me, "no_such_tree"));
        const bool set = ecs::set_skill_tree(w, me, "sk_test");
        w.get<ecs::Progression>(me).skill_points = 1;
        const bool locked = ecs::skill_state(w, me, "sk_c") == 0 && !ecs::learn_skill(w, me, "sk_c");
        const bool a = ecs::learn_skill(w, me, "sk_a");
        const bool b = ecs::learn_skill(w, me, "sk_b");
        const bool c_no_points = !ecs::learn_skill(w, me, "sk_c");
        w.get<ecs::Progression>(me).skill_points = 1;
        const bool can_c = ecs::skill_state(w, me, "sk_c") == 1;
        const bool c = ecs::learn_skill(w, me, "sk_c");
        const std::vector<std::string> bar = w.has<ecs::AbilityBar>(me) ? w.get<ecs::AbilityBar>(me).slots : std::vector<std::string>{};
        check("learning: needs come first, points are spent, its abilities go on the bar, bonuses and tags apply",
              set && locked && a && b && c_no_points && can_c && c && ecs::skill_state(w, me, "sk_c") == 2 &&
              bar.size() == 2 && bar[0] == "sk_bolt" && bar[1] == "sk_nova" &&
              w.get<ecs::AttributeSet>(me).find("Health")->max == 125.0f && w.has<ecs::GameplayTags>(me) &&
              w.get<ecs::GameplayTags>(me).has("perk.tough"));
        // The tree saves with its abilities; older data (without them) still reads.
        std::vector<uint8_t> buf;
        ecs::serialize_skill_tree(&tree, buf);
        ecs::SkillTree back;
        ecs::deserialize_skill_tree(&back, buf.data(), buf.size());
        ecs::SkillTree old_tree = tree;
        for (auto& n : old_tree.nodes) n.abilities.clear();
        std::vector<uint8_t> old_buf;
        ecs::serialize_skill_tree(&old_tree, old_buf);
        old_buf.resize(old_buf.size() - 4 - 4 * old_tree.nodes.size() - 4 - old_tree.id.size());   // as an older engine wrote it
        ecs::SkillTree old_back;
        ecs::deserialize_skill_tree(&old_back, old_buf.data(), old_buf.size());
        check("skill tree saving: abilities and id kept; data from before them still reads",
              back.nodes.size() == 3 && back.nodes[2].abilities == tree.nodes[2].abilities && back.id == "sk_test" &&
              old_back.nodes.size() == 3 && old_back.nodes[2].prerequisites.size() == 2 && old_back.nodes[2].abilities.empty());
        // The Items editor keeps skill blocks as written.
        const ecs::ItemsDoc doc = ecs::parse_items_doc(data);
        const std::string written = ecs::items_doc_to_text(doc);
        check("items editor: skill blocks are kept, in place, as written",
              doc.entries.size() == 6 && doc.entries[2].kind == ecs::ItemsDoc::Kind::Text && doc.entries[2].id() == "sk_a" &&
              written.find("skill sk_c\n  name Nova\n  tree sk_test\n  cost 1\n  requires sk_a\n") != std::string::npos);
    }

    // Guns carried: giving, switching (each magazine kept, a draw time),
    // picking one up, saving the loadout.
    {
        ecs::register_items_text(
            "item lo_rounds\n  kind material\n  max_stack 500\nend\n"
            "item lo_shells\n  kind material\n  max_stack 100\nend\n"
            "weapon lo_rifle\n  fire auto\n  damage 20\n  magazine 30\n  ammo lo_rounds\nend\n"
            "weapon lo_shotgun\n  damage 12\n  pellets 8\n  magazine 6\n  draw 0.7\n  ammo lo_shells\nend\n"
            "weapon lo_pistol\n  damage 25\n  magazine 12\n  ammo lo_rounds\nend\n");
        ecs::World w;
        const ecs::Entity me = w.create();
        w.add<ecs::Transform>(me, ecs::Transform{});
        w.add<ecs::Inventory>(me, ecs::Inventory{});
        ecs::inventory_add(w.get<ecs::Inventory>(me), ecs::ItemInstance{"lo_rounds", 100, 0, {}});
        const bool first = ecs::give_weapon(w, me, "lo_rifle") && w.has<ecs::WeaponState>(me) &&
                           w.get<ecs::WeaponState>(me).weapon_id == "lo_rifle" && w.get<ecs::WeaponState>(me).ammo_in_mag == 30;
        const bool second = ecs::give_weapon(w, me, "lo_shotgun") && w.get<ecs::WeaponState>(me).weapon_id == "lo_rifle";
        const bool again = !ecs::give_weapon(w, me, "lo_shotgun") && !ecs::give_weapon(w, me, "no_such_gun");
        check("loadout: the first gun goes into the hand, the next is carried; no gun twice",
              first && second && again && w.get<ecs::WeaponLoadout>(me).slots.size() == 2);
        // Fire 5 rounds' worth, switch away and back: the magazine is as it was left.
        w.get<ecs::WeaponState>(me).ammo_in_mag = 25;
        const bool to_shotgun = ecs::cycle_weapon(w, me, +1);
        const auto& ws = w.get<ecs::WeaponState>(me);
        const bool drawn = ws.weapon_id == "lo_shotgun" && ws.ammo_in_mag == 6 && ws.cooldown > 0.69f;
        const bool shot_while_drawing = ecs::fire_weapon(w, me, glm::vec3(0), glm::vec3(0, 0, -1), w.get<ecs::WeaponState>(me));
        const bool back = ecs::last_weapon(w, me) && w.get<ecs::WeaponState>(me).weapon_id == "lo_rifle" &&
                          w.get<ecs::WeaponState>(me).ammo_in_mag == 25;
        const bool wrap = ecs::cycle_weapon(w, me, -1) && w.get<ecs::WeaponState>(me).weapon_id == "lo_shotgun";
        check("switching: each magazine kept, the drawn gun waits its draw time, round the list, back to the last",
              to_shotgun && drawn && !shot_while_drawing && back && wrap && !ecs::select_weapon(w, me, 7));
        // Rounds loaded but not yet taken from the bag leave it when the gun is put away.
        ecs::select_weapon(w, me, 0);
        auto& rifle = w.get<ecs::WeaponState>(me);
        ecs::sync_ammo_with_inventory(rifle, w.get<ecs::Inventory>(me));
        const int bag_before = ecs::inventory_count(w.get<ecs::Inventory>(me), "lo_rounds");
        rifle.reserve -= 5;
        rifle.ammo_in_mag += 5;
        ecs::select_weapon(w, me, 1);
        check("switching settles the ammunition: no free rounds",
              ecs::inventory_count(w.get<ecs::Inventory>(me), "lo_rounds") == bag_before - 5);
        // A gun on the ground: picked up, carried; picked up again, its ammunition instead.
        auto pickup = [&](const char* id) {
            const ecs::Entity p = w.create();
            w.add<ecs::Transform>(p, ecs::Transform{});
            w.add<ecs::Interactable>(p, ecs::Interactable{});
            w.add<ecs::Pickup>(p, ecs::Pickup{id, 1});
            return ecs::interact(w, me, p);
        };
        const int shells_before = ecs::inventory_count(w.get<ecs::Inventory>(me), "lo_shells");
        const bool got_pistol = pickup("lo_pistol") && w.get<ecs::WeaponLoadout>(me).slots.size() == 3 &&
                                ecs::inventory_count(w.get<ecs::Inventory>(me), "lo_pistol") == 0;
        const bool shells = pickup("lo_shotgun") && w.get<ecs::WeaponLoadout>(me).slots.size() == 3 &&
                            ecs::inventory_count(w.get<ecs::Inventory>(me), "lo_shells") == shells_before + 6;
        check("pickups: a gun joins those carried; one already carried gives a magazine of its ammunition",
              got_pistol && shells);
        // A bare gun in hand (no loadout yet) is the loadout's first; the loadout saves.
        ecs::World w2;
        const ecs::Entity other = w2.create();
        ecs::WeaponState lone; lone.weapon_id = "lo_pistol"; lone.ammo_in_mag = 3;
        w2.add<ecs::WeaponState>(other, lone);
        const bool made = ecs::give_weapon(w2, other, "lo_rifle") && w2.get<ecs::WeaponLoadout>(other).slots[0].weapon_id == "lo_pistol" &&
                          w2.get<ecs::WeaponLoadout>(other).slots[0].ammo_in_mag == 3;
        std::vector<uint8_t> buf;
        ecs::serialize_loadout(&w.get<ecs::WeaponLoadout>(me), buf);
        ecs::WeaponLoadout back_l;
        ecs::deserialize_loadout(&back_l, buf.data(), buf.size());
        const auto& l = w.get<ecs::WeaponLoadout>(me);
        check("loadout: made from the gun in hand; saved and read back",
              made && back_l.slots.size() == l.slots.size() && back_l.current == l.current && back_l.last == l.last &&
              back_l.slots[0].ammo_in_mag == l.slots[0].ammo_in_mag);
    }

    // Weapon slots: a limit on the guns carried; more go into the bag and come
    // out of it into a slot; stowing; rounds neither made nor lost.
    {
        ecs::register_items_text("weapon lo_smg\n  fire auto\n  damage 14\n  magazine 25\n  ammo lo_rounds\nend\n");
        ecs::World w;
        const ecs::Entity me = w.create();
        w.add<ecs::Transform>(me, ecs::Transform{});
        w.add<ecs::Inventory>(me, ecs::Inventory{});
        ecs::inventory_add(w.get<ecs::Inventory>(me), ecs::ItemInstance{"lo_rounds", 200, 0, {}});
        auto L = [&]() -> ecs::WeaponLoadout& { return w.get<ecs::WeaponLoadout>(me); };
        auto bag = [&](const char* id) { return ecs::inventory_count(w.get<ecs::Inventory>(me), id); };
        auto hand = [&]() { return w.has<ecs::WeaponState>(me) ? w.get<ecs::WeaponState>(me).weapon_id : std::string(); };
        auto cell = [&](const char* id) {
            const auto& its = w.get<ecs::Inventory>(me).items;
            for (size_t i = 0; i < its.size(); ++i)
                if (its[i].def_id == id) return i;
            return its.size();
        };
        auto rounds = [&]() {   // lo_rounds in the bag and in every magazine that loads them
            int n = bag("lo_rounds");
            const auto& lo = L();
            for (size_t i = 0; i < lo.slots.size(); ++i) {
                const ecs::WeaponDef* d = ecs::find_weapon(lo.slots[i].weapon_id);
                if (!d || d->ammo_item != "lo_rounds") continue;
                const bool held = static_cast<int>(i) == lo.current && w.has<ecs::WeaponState>(me);
                n += held ? w.get<ecs::WeaponState>(me).ammo_in_mag : lo.slots[i].ammo_in_mag;
            }
            return n;
        };
        ecs::set_weapon_slots(w, me, 3);
        const bool empty_start = L().slots.size() == 3 && L().current == -1 && hand().empty();
        const bool three = ecs::give_weapon(w, me, "lo_rifle") && hand() == "lo_rifle" && ecs::give_weapon(w, me, "lo_shotgun") &&
                           ecs::give_weapon(w, me, "lo_pistol") && hand() == "lo_rifle";
        const bool full = !ecs::give_weapon(w, me, "lo_smg") && ecs::loadout_free_slot(L()) == -1;
        const int r0 = bag("lo_rounds");
        const bool bagged = ecs::receive_weapon(w, me, "lo_smg") == ecs::GunReceived::Bagged && bag("lo_smg") == 1 &&
                            bag("lo_rounds") == r0 + 25 && ecs::loadout_count(L()) == 3;
        const bool had = ecs::receive_weapon(w, me, "lo_smg") == ecs::GunReceived::Ammo && bag("lo_smg") == 1 &&
                         bag("lo_rounds") == r0 + 50;
        check("weapon slots: an empty hand takes the first gun; three carried; a fourth goes into the bag with a magazine",
              empty_start && three && full && bagged && had);

        const int shells0 = bag("lo_shells");
        const bool stowed = ecs::stow_weapon(w, me, 1) && !ecs::slot_filled(L(), 1) && L().slots.size() == 3 &&
                            bag("lo_shotgun") == 1 && bag("lo_shells") == shells0 + 6 && hand() == "lo_rifle";
        const bool skip = ecs::cycle_weapon(w, me, +1) && hand() == "lo_pistol" && ecs::cycle_weapon(w, me, +1) &&
                          hand() == "lo_rifle" && !ecs::select_weapon(w, me, 1);
        check("stowing: the gun goes into the bag with its magazine's rounds; its slot stays, empty, and switching skips it",
              stowed && skip);

        const int before = rounds();
        const bool into_free = ecs::use_item(w, me, w.get<ecs::Inventory>(me), cell("lo_smg")) &&
                               L().slots[1].weapon_id == "lo_smg" && L().slots[1].ammo_in_mag == 25 && hand() == "lo_rifle" &&
                               bag("lo_smg") == 0 && rounds() == before;
        const int before2 = rounds();
        const bool swap = ecs::use_item(w, me, w.get<ecs::Inventory>(me), cell("lo_shotgun")) &&
                          L().slots[0].weapon_id == "lo_shotgun" && hand() == "lo_shotgun" && bag("lo_rifle") == 1 &&
                          w.get<ecs::WeaponState>(me).ammo_in_mag == 6 && rounds() == before2;
        const bool named = ecs::equip_weapon_from_inventory(w, me, "lo_rifle", 2) && L().slots[2].weapon_id == "lo_rifle" &&
                           bag("lo_pistol") == 1 && hand() == "lo_shotgun" && !ecs::equip_weapon_from_inventory(w, me, "lo_rifle", 0) &&
                           !ecs::equip_weapon_from_inventory(w, me, "lo_pistol", 5);
        check("from the bag: into an empty slot; with all full in place of the gun in hand (drawn) or a slot named; "
              "rounds neither made nor lost",
              into_free && swap && named);

        const bool hand_stowed = ecs::stow_weapon(w, me, 0) && hand() == "lo_smg" && ecs::stow_weapon(w, me, 1) &&
                                 hand() == "lo_rifle" && ecs::stow_weapon(w, me, 2) && hand().empty() && L().current == -1 &&
                                 ecs::loadout_count(L()) == 0 && !ecs::cycle_weapon(w, me, 1);
        const bool regive = ecs::use_item(w, me, w.get<ecs::Inventory>(me), cell("lo_pistol")) && hand() == "lo_pistol" &&
                            L().current == 0;
        check("stowing the gun in hand draws another; with none left the hand is empty, and the next gun goes into it",
              hand_stowed && regive);

        w.get<ecs::Inventory>(me).max_slots = static_cast<int>(w.get<ecs::Inventory>(me).items.size());
        const bool refused = !ecs::stow_weapon(w, me, 0) && hand() == "lo_pistol" && ecs::slot_filled(L(), 0);
        w.get<ecs::Inventory>(me).max_slots = 20;
        std::vector<uint8_t> buf;
        ecs::serialize_loadout(&L(), buf);
        ecs::WeaponLoadout back;
        ecs::deserialize_loadout(&back, buf.data(), buf.size());
        std::vector<uint8_t> v1;   // a loadout saved before weapon slots
        ecs::detail::put_i32(v1, 1);
        ecs::detail::put_i32(v1, 1);
        ecs::detail::put_str(v1, "lo_rifle");
        ecs::detail::put_i32(v1, 7);
        ecs::detail::put_i32(v1, 0);
        ecs::detail::put_i32(v1, -1);
        ecs::WeaponLoadout old;
        ecs::deserialize_loadout(&old, v1.data(), v1.size());
        check("a full bag refuses a gun; weapon slots are saved (empty ones too); an older loadout has no limit",
              refused && back.max_slots == 3 && back.slots.size() == 3 && back.slots[0].weapon_id == "lo_pistol" &&
              back.slots[1].weapon_id.empty() && old.max_slots == 0 && old.slots.size() == 1 && old.slots[0].ammo_in_mag == 7);

        ecs::World w3;
        const ecs::Entity e3 = w3.create();
        w3.add<ecs::Inventory>(e3, ecs::Inventory{});
        for (const char* g : {"lo_rifle", "lo_shotgun", "lo_pistol", "lo_smg"}) ecs::give_weapon(w3, e3, g);
        ecs::select_weapon(w3, e3, 3);
        ecs::set_weapon_slots(w3, e3, 2);
        const auto& l3 = w3.get<ecs::WeaponLoadout>(e3);
        check("fewer weapon slots: the guns beyond go into the bag, never the one in hand",
              ecs::loadout_count(l3) == 2 && l3.slots.size() == 2 && l3.slots[0].weapon_id == "lo_rifle" &&
              l3.slots[1].weapon_id == "lo_smg" && l3.current == 1 && w3.get<ecs::WeaponState>(e3).weapon_id == "lo_smg" &&
              ecs::inventory_count(w3.get<ecs::Inventory>(e3), "lo_pistol") == 1 &&
              ecs::inventory_count(w3.get<ecs::Inventory>(e3), "lo_shotgun") == 1);
    }

    // Gear: worn from the bag, swapped back into it, armour that takes damage
    // off, items with a description and an ability.
    {
        const char* data =
            "item gr_helmet\n  name Helmet\n  kind armor\n  slot head\n  mod Health 20\n  mod resist.physical 0.25\n"
            "  about +20 health, a quarter less damage\nend\n"
            "item gr_hood\n  name Hood\n  kind armor\n  slot head\n  mod Mana 40\nend\n"
            "item gr_bomb\n  name Bomb\n  kind consumable\n  max_stack 5\n  casts gr_blast\nend\n";
        ecs::register_items_text(data);
        ecs::World w;
        const ecs::Entity me = w.create();
        w.add<ecs::AttributeSet>(me, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(me).define("Health", 100, 0, 100);
        w.get<ecs::AttributeSet>(me).define("Mana", 50, 0, 50);
        w.add<ecs::Inventory>(me, ecs::Inventory{});
        auto& inv = w.get<ecs::Inventory>(me);
        ecs::inventory_add(inv, ecs::ItemInstance{"gr_helmet", 1, 0, {}});
        ecs::inventory_add(inv, ecs::ItemInstance{"gr_hood", 1, 0, {}});
        ecs::inventory_add(inv, ecs::ItemInstance{"gr_bomb", 2, 0, {}});
        const bool worn = ecs::use_item(w, me, w.get<ecs::Inventory>(me), 0) &&
                          ecs::inventory_count(w.get<ecs::Inventory>(me), "gr_helmet") == 0 &&
                          w.get<ecs::AttributeSet>(me).find("Health")->max == 120.0f;
        const float dealt = ecs::apply_damage(w, me, ecs::DamageInfo{40.0f, "physical"});
        check("gear: Use puts it on from the bag; its bonuses and armour (an attribute it lacked) apply",
              worn && std::fabs(dealt - 30.0f) < 0.01f);
        const bool swap = ecs::equip_from_inventory(w, me, "gr_hood", nullptr) &&
                          ecs::inventory_count(w.get<ecs::Inventory>(me), "gr_helmet") == 1 &&
                          ecs::inventory_count(w.get<ecs::Inventory>(me), "gr_hood") == 0 &&
                          w.get<ecs::AttributeSet>(me).find("Health")->max == 100.0f &&
                          w.get<ecs::AttributeSet>(me).find("Mana")->max == 90.0f &&
                          w.get<ecs::AttributeSet>(me).get("resist.physical") == 0.0f;
        const bool off = ecs::unequip_to_inventory(w, me, "head") && ecs::inventory_count(w.get<ecs::Inventory>(me), "gr_hood") == 1 &&
                         w.get<ecs::AttributeSet>(me).find("Mana")->max == 50.0f && !ecs::unequip_to_inventory(w, me, "head");
        const bool not_owned = !ecs::equip_from_inventory(w, me, "gr_nothing", nullptr);
        check("gear: another in the same slot sends the first back to the bag; taking it off returns it; nothing from nowhere",
              swap && off && not_owned);
        size_t bomb = 0;
        for (size_t i = 0; i < w.get<ecs::Inventory>(me).items.size(); ++i)
            if (w.get<ecs::Inventory>(me).items[i].def_id == "gr_bomb") bomb = i;
        const bool thrown = ecs::use_item(w, me, w.get<ecs::Inventory>(me), bomb) && w.has<ecs::AbilityRequests>(me) &&
                            w.get<ecs::AbilityRequests>(me).ids.size() == 1 && w.get<ecs::AbilityRequests>(me).ids[0] == "gr_blast" &&
                            ecs::inventory_count(w.get<ecs::Inventory>(me), "gr_bomb") == 1;
        const ecs::ItemDef* h = ecs::find_item("gr_helmet");
        const std::string text = ecs::item_def_text(*ecs::find_item("gr_bomb")) + ecs::item_def_text(*h) +
                                 ecs::weapon_def_text(*ecs::find_weapon("lo_shotgun"));
        check("items: an ability asked for when used; about, casts and draw written back",
              thrown && h->description == "+20 health, a quarter less damage" &&
              text.find("  casts gr_blast\n") != std::string::npos && text.find("  about +20 health") != std::string::npos &&
              text.find("  draw 0.7\n") != std::string::npos);
    }

    // D0: life and death.
    {
        ecs::World w;
        ecs::GameplayEventBus bus;
        int died_events = 0;
        bus.subscribe("died", [&died_events](const ecs::GameplayEvent&) { ++died_events; });
        ecs::Rng rng(7);
        auto creature = [&](float hp, bool respawns) {
            const ecs::Entity e = w.create();
            w.add<ecs::AttributeSet>(e, ecs::AttributeSet{});
            w.get<ecs::AttributeSet>(e).define("Health", 100, 0, 100);
            w.get<ecs::AttributeSet>(e).set("Health", hp);
            ecs::Mortal m;
            m.corpse_time = 1.0f;
            m.respawns = respawns;
            m.loot.push_back({"d0_coin", 1.0f, 1.0f, 2, 2, 0});
            w.add<ecs::Mortal>(e, m);
            return e;
        };
        const ecs::Entity foe = creature(10.0f, false);
        const ecs::Entity me = creature(50.0f, true);
        std::vector<ecs::LifeEvent> ev;
        ecs::tick_life(w, 0.1f, &bus, rng, ev);
        bus.flush();
        const bool alive = ev.empty() && !ecs::is_dead(w, foe);
        ecs::apply_damage(w, foe, ecs::DamageInfo{25.0f, "kinetic"});
        ecs::tick_life(w, 0.1f, &bus, rng, ev);
        bus.flush();
        check("life: health at zero is death -- tagged, announced, its loot dropped",
              alive && ev.size() == 1 && ev[0].kind == ecs::LifeEvent::Kind::Died && ev[0].entity == foe &&
              ecs::is_dead(w, foe) && died_events == 1 && ev[0].loot.size() == 1 && ev[0].loot[0].def_id == "d0_coin" &&
              ev[0].loot[0].quantity == 2);
        ev.clear();
        for (int i = 0; i < 8; ++i) ecs::tick_life(w, 0.1f, &bus, rng, ev);
        const bool not_yet = ev.empty();
        for (int i = 0; i < 5; ++i) ecs::tick_life(w, 0.1f, &bus, rng, ev);
        check("life: the body is removed after its corpse time, once", not_yet && ev.size() == 1 &&
              ev[0].kind == ecs::LifeEvent::Kind::Removed && died_events == 1);
        ev.clear();
        w.get<ecs::AttributeSet>(me).set("Health", 0.0f);
        ecs::tick_life(w, 0.1f, &bus, rng, ev);
        const bool me_dead = ecs::is_dead(w, me);
        for (int i = 0; i < 30; ++i) ecs::tick_life(w, 0.1f, &bus, rng, ev);
        bool respawned = false;
        for (const auto& e : ev) respawned |= e.kind == ecs::LifeEvent::Kind::Respawn && e.entity == me;
        check("life: one who respawns comes back with full health, not removed",
              me_dead && respawned && !ecs::is_dead(w, me) && w.get<ecs::AttributeSet>(me).get("Health") == 100.0f);
        std::vector<uint8_t> buf;
        ecs::serialize_mortal(&w.get<ecs::Mortal>(foe), buf);
        ecs::Mortal back;
        ecs::deserialize_mortal(&back, buf.data(), buf.size());
        ecs::register_life_components();
        check("life: Mortal saves its settings, not its death; authorable",
              back.corpse_time == 1.0f && !back.respawns && back.loot.size() == 1 && back.loot[0].min_qty == 2 &&
              !back.dead && ecs::find_authorable("Mortal") != nullptr);
    }

    // C6: a save made in Play -- the player's place and the game state.
    {
        ecs::register_persistence_components();
        ecs::World w;
        const ecs::Entity pl = w.create();
        w.add<ecs::SaveId>(pl, ecs::SaveId{ecs::kSaveKeyPlayer});
        w.add<ecs::AttributeSet>(pl, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(pl).define("Health", 100, 0, 100);
        w.get<ecs::AttributeSet>(pl).set("Health", 35.0f);
        const ecs::Entity fl = w.create();
        w.add<ecs::SaveId>(fl, ecs::SaveId{ecs::kSaveKeyWorldFlags});
        w.add<ecs::WorldFlags>(fl, ecs::WorldFlags{});
        ecs::set_world_flag(w.get<ecs::WorldFlags>(fl), "door.open", 1);
        ecs::PlaySave s;
        s.has_player = true;
        s.position = glm::vec3(1.5f, -2.25f, 1000.125f);
        s.rotation = glm::normalize(glm::quat(0.9f, 0.0f, 0.43f, 0.0f));
        s.camera = glm::normalize(glm::quat(0.98f, 0.2f, 0.0f, 0.0f));
        s.play_time = 12.5;
        s.game = ecs::SaveGame::capture(w);
        ecs::PlaySave r;
        const bool parsed = ecs::PlaySave::from_text(s.to_text(), r);
        check("play save: the player's place and look come back exactly",
              parsed && r.has_player && r.position == s.position && r.rotation == s.rotation && r.camera == s.camera &&
              r.play_time == 12.5);
        // Changed after the save, then loaded: back as it was.
        w.get<ecs::AttributeSet>(pl).set("Health", 90.0f);
        ecs::set_world_flag(w.get<ecs::WorldFlags>(fl), "door.open", 0);
        r.game.apply(w);
        check("play save: loading puts the game state back (health, world flags)",
              w.get<ecs::AttributeSet>(pl).get("Health") == 35.0f && ecs::world_flag(w.get<ecs::WorldFlags>(fl), "door.open") == 1);
        ecs::PlaySave bad;
        check("play save: not a save is refused", !ecs::PlaySave::from_text("SAVEGAME 0\n", bad));
    }

    // C5: casting -- cast time, channel, targeting, the bar, abilities as data.
    {
        ecs::register_items_text(
            "ability c5_bolt\n  name Bolt\n  cost Mana 20\n  cooldown 3\n  cast 0.5\n  target aimed\n  range 30\n"
            "  damage 25 fire\n  vfx bolt\nend\n"
            "ability c5_nova\n  cost Mana 10\n  target area\n  radius 4\n  damage 10\n  effect Speed -2 3\n  self Mana 5\nend\n"
            "ability c5_beam\n  channel 2 0.5\n  target aimed\n  damage 5\n  cooldown 1\nend\n"
            "ability c5_heal\n  cast 1\n  still\n  self Health 30\n  target self\nend\n"
            "ability c5_blast\n  target area_at_aim\n  range 12\n  radius 2\n  damage 50\nend\n");
        ecs::World w;
        ecs::GameplayEventBus bus;
        std::vector<std::string> seen;
        for (const char* n : {"ability.cast", "ability.fire", "ability.hit", "ability.interrupted", "ability.end"})
            bus.subscribe(n, [&seen](const ecs::GameplayEvent& ev) { seen.push_back(ev.name + ":" + ev.param); });
        const ecs::Entity me = w.create();
        w.add<ecs::Transform>(me, ecs::Transform{});
        w.add<ecs::AttributeSet>(me, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(me).define("Mana", 100, 0, 100);
        w.get<ecs::AttributeSet>(me).define("Health", 50, 0, 100);
        auto foe = [&](glm::vec3 pos) {
            const ecs::Entity e = w.create();
            ecs::Transform tf; tf.position = pos;
            w.add<ecs::Transform>(e, tf);
            w.add<ecs::CombatActor>(e, ecs::CombatActor{});
            w.add<ecs::AttributeSet>(e, ecs::AttributeSet{});
            w.get<ecs::AttributeSet>(e).define("Health", 100, 0, 100);
            w.get<ecs::AttributeSet>(e).define("Speed", 5, 0, 10);
            return e;
        };
        const ecs::Entity ahead = foe(glm::vec3(0, 0, -10));
        const ecs::Entity near1 = foe(glm::vec3(2, 0, 1));
        const ecs::Entity far1 = foe(glm::vec3(30, 0, 0));
        const ecs::Entity at_aim = foe(glm::vec3(0, 0, -12) + glm::vec3(1, 0, 0));
        auto hp = [&](ecs::Entity e) { return w.get<ecs::AttributeSet>(e).get("Health"); };
        ecs::AbilityBar bar;
        bar.slots = {"c5_bolt", "c5_nova", "c5_beam", "c5_heal", "c5_blast"};
        const ecs::CombatActor ca{};
        ecs::AbilityCastInput in;
        in.origin = glm::vec3(0, ca.hurt_height, 0);
        in.aim = glm::vec3(0, 0, -1);
        float dt = 0.1f;
        auto frame = [&](int pressed, bool held, bool moving = false) {
            in.pressed = pressed; in.held = held; in.moving = moving;
            std::vector<ecs::AbilityFired> f;
            const int n = ecs::step_ability_bar(w, me, bar, in, nullptr, &bus, dt, &f);
            bus.flush();
            return n;
        };
        // A cast: nothing for 0.5 s, then it goes off at the one aimed at.
        int fired = frame(0, true);
        for (int i = 0; i < 3; ++i) fired += frame(-1, true);
        const bool waited = fired == 0 && hp(ahead) == 100.0f && bar.casting == 0;
        fired += frame(-1, true);
        check("cast: goes off after its cast time at the one aimed at, cost paid",
              waited && fired == 1 && hp(ahead) == 75.0f && w.get<ecs::AttributeSet>(me).get("Mana") == 80.0f);
        check("cast: then cools down", bar.cooldown[0] == 3.0f && !ecs::ability_slot_ready(w, me, bar, 0));
        // Area around the caster: those near, not those far; effects for a while; to the caster too.
        frame(1, true);
        check("area: reaches those near, not those far",
              hp(near1) == 90.0f && hp(far1) == 100.0f && hp(ahead) == 75.0f &&
              w.get<ecs::AttributeSet>(near1).get("Speed") == 3.0f &&
              w.get<ecs::AttributeSet>(me).get("Mana") == 75.0f);
        // Channel: a pulse at the start and every period while held, then cooldown.
        const float before = hp(ahead);
        dt = 0.125f;   // exact in binary: pulses land on their times
        int pulses = frame(2, true);
        for (int i = 0; i < 20; ++i) pulses += frame(-1, true);
        check("channel: pulses while held, for its time", pulses == 5 && hp(ahead) == before - 25.0f && bar.casting < 0 &&
                                                          bar.cooldown[2] > 0.0f);
        bar.cooldown[2] = 0.0f;
        pulses = frame(2, true);
        pulses += frame(-1, true);
        pulses += frame(-1, false);
        check("channel: letting go ends it", pulses == 1 && bar.casting < 0);
        // A cast that needs standing still: moving breaks it, no cooldown, no cost.
        seen.clear();
        frame(3, true);
        frame(-1, true, true);
        bool broke = bar.casting < 0 && bar.cooldown[3] == 0.0f && w.get<ecs::AttributeSet>(me).get("Health") == 50.0f;
        frame(3, true);
        for (int i = 0; i < 10; ++i) frame(-1, true);
        check("still: moving breaks the cast; standing it goes off on the caster",
              broke && w.get<ecs::AttributeSet>(me).get("Health") == 80.0f);
        // Area at the aimed point (no wall: at full range).
        frame(4, true);
        check("area at the aim: those around where it lands", hp(at_aim) == 50.0f && hp(ahead) == before - 30.0f);   // untouched by it: the channels' 25 + 5 only
        // Out of mana: does not start.
        w.get<ecs::AttributeSet>(me).set("Mana", 5.0f);
        bar.cooldown[0] = 0.0f;
        frame(0, true);
        check("too little to pay: does not start", bar.casting < 0);
        bool has_cast = false, has_hit = false, has_int = false, has_end = false;
        for (const auto& s : seen) {
            has_cast |= s == "ability.cast:c5_heal"; has_hit |= s.rfind("ability.hit:", 0) == 0;
            has_int |= s == "ability.interrupted:c5_heal"; has_end |= s == "ability.end:c5_heal";
        }
        check("events: cast, interrupted, end, hit -- with the ability's id", has_cast && has_int && has_end && has_hit);
        // A wall stops an aimed ability.
        bar.cooldown[0] = 0.0f;
        w.get<ecs::AttributeSet>(me).set("Mana", 100.0f);
        const float hp_before = hp(ahead);
        ecs::WorldRayFn wall = [](const glm::vec3&, const glm::vec3&, float, float& t, glm::vec3& n) {
            t = 4.0f; n = glm::vec3(0, 0, 1); return true;
        };
        in.pressed = 0; in.held = true;
        std::vector<ecs::AbilityFired> fl;
        for (int i = 0; i < 6; ++i) { ecs::step_ability_bar(w, me, bar, in, &wall, &bus, dt, &fl); in.pressed = -1; }
        check("a wall stops an aimed ability, and it says where", hp(ahead) == hp_before && fl.size() == 1 &&
                                                                 std::fabs(fl[0].point.z + 4.0f) < 1e-3f && fl[0].vfx == "bolt");
        // Aimed at the sky, an area at the aim comes down on the ground below (a grenade).
        {
            ecs::World sky;
            const ecs::Entity thrower = sky.create();
            ecs::AbilityCastInput up;
            up.origin = glm::vec3(0.0f, 1.6f, 0.0f);
            up.aim = glm::normalize(glm::vec3(0.0f, 1.0f, -1.0f));
            const ecs::WorldRayFn ground = [](const glm::vec3& o, const glm::vec3& d, float max_t, float& t, glm::vec3& n) {
                if (d.y > -0.5f) return false;   // only the ground below
                t = o.y; n = glm::vec3(0.0f, 1.0f, 0.0f);
                return t <= max_t;
            };
            const ecs::AbilityFired f = ecs::ability_detail::fire(sky, thrower, *ecs::find_ability_def("c5_blast"), -1, up, &ground, nullptr);
            check("area at the aim, aimed at the sky: it comes down on the ground below",
                  std::fabs(f.point.y) < 1e-3f && f.point.z < -8.0f);
        }
        // The bar saves its slots.
        std::vector<uint8_t> buf;
        ecs::serialize_ability_bar(&bar, buf);
        ecs::AbilityBar b2;
        ecs::deserialize_ability_bar(&b2, buf.data(), buf.size());
        ecs::register_ability_cast_components();
        check("ability bar: saves its slots; authorable", b2.slots == bar.slots && b2.casting == -1 &&
                                                          ecs::find_authorable("Ability Bar") != nullptr);
        // Written back from the Items editor, an ability reads the same.
        ecs::ItemsDoc doc = ecs::parse_items_doc(
            "ability c5_x\n  name X\n  cost Mana 3\n  cooldown 2\n  cast 0.25\n  still\n  channel 4 0.25\n"
            "  target area_at_aim\n  range 9\n  radius 1.5\n  damage 7 cold\n  self Mana 1 2\n  effect Speed -1\n"
            "  require a.b\n  block c.d\n  vfx frost\nend\n");
        const std::string t1 = ecs::items_doc_to_text(doc);
        const ecs::ItemsDoc doc2 = ecs::parse_items_doc(t1);
        const auto& x = doc2.entries.at(0).ability;
        check("ability data: every setting survives the Items editor",
              doc2.entries.size() == 1 && doc2.entries[0].is_ability() && ecs::items_doc_to_text(doc2) == t1 &&
              x.name == "X" && x.cost == 3.0f && x.cast_time == 0.25f && !x.cast_while_moving && x.channel_time == 4.0f &&
              x.channel_period == 0.25f && x.targeting == ecs::AbilityTargeting::AreaAtAim && x.range == 9.0f &&
              x.radius == 1.5f && x.damage_type == "cold" && x.self_effects.size() == 1 && x.self_effects[0].duration == 2.0f &&
              x.target_effects.size() == 1 && x.require_tags.size() == 1 && x.block_tags.size() == 1 && x.vfx == "frost");
    }

    // G0: damage pipeline — data-driven types, resistance (attribute) + immunity
    // (tag), resolving to an instant effect on the health attribute.
    {
        ecs::World dw;
        const ecs::Entity mob = dw.create();
        dw.add<ecs::AttributeSet>(mob, ecs::AttributeSet{});
        auto& a = dw.get<ecs::AttributeSet>(mob);
        a.define("Health", 100, 0, 100);
        a.define("resist.fire", 0.5f, 0, 1);      // 50% fire resist (any type name works)

        float dealt = ecs::apply_damage(dw, mob, ecs::DamageInfo{40.0f, "fire"});
        check("typed damage reduced by resistance (40 -> 20)",
              dealt == 20.0f && a.get("Health") == 80.0f);

        float untyped = ecs::apply_damage(dw, mob, ecs::DamageInfo{30.0f, ""});
        check("untyped damage ignores resistance (full 30)",
              untyped == 30.0f && a.get("Health") == 50.0f);

        dw.add<ecs::GameplayTags>(mob, ecs::GameplayTags{});
        dw.get<ecs::GameplayTags>(mob).add("immune.fire");
        float blocked = ecs::apply_damage(dw, mob, ecs::DamageInfo{99.0f, "fire"});
        check("immunity tag nullifies typed damage (0, health unchanged)",
              blocked == 0.0f && a.get("Health") == 50.0f);
    }

    // G0: event bus — named + wildcard pub/sub, deferred flush, no re-entrancy.
    {
        ecs::GameplayEventBus bus;
        int dmg_count = 0; float dmg_total = 0.0f; int all_count = 0;
        bus.subscribe("damage.dealt", [&](const ecs::GameplayEvent& e) { ++dmg_count; dmg_total += e.value; });
        bus.subscribe("*",            [&](const ecs::GameplayEvent&)   { ++all_count; });

        ecs::GameplayEvent e1; e1.name = "damage.dealt"; e1.value = 10.0f;
        ecs::GameplayEvent e2; e2.name = "damage.dealt"; e2.value = 25.0f;
        ecs::GameplayEvent e3; e3.name = "item.pickup";
        bus.publish(e1); bus.publish(e2); bus.publish(e3);
        check("events queue until flush", bus.pending() == 3 && dmg_count == 0);
        bus.flush();
        check("named subscriber gets only matching events (2, sum 35)", dmg_count == 2 && dmg_total == 35.0f);
        check("wildcard subscriber gets all events (3)", all_count == 3);
        check("queue drained after flush", bus.pending() == 0);

        ecs::GameplayEventBus bus2;
        int fired = 0;
        bus2.subscribe("chain", [&](const ecs::GameplayEvent&) {
            if (fired == 0) { ecs::GameplayEvent n; n.name = "chain"; bus2.publish(n); }
            ++fired;
        });
        ecs::GameplayEvent c; c.name = "chain"; bus2.publish(c);
        bus2.flush();
        check("event re-published during dispatch is deferred (not re-entrant)", fired == 1 && bus2.pending() == 1);
        bus2.flush();
        check("deferred event dispatched on the next flush", fired == 2 && bus2.pending() == 0);
    }

    // G0: timer manager — one-shot after() + repeating every() + cancel.
    {
        ecs::TimerManager tm;
        int oneshot = 0;
        tm.after(1.0f, [&] { ++oneshot; });
        tm.tick(0.5f);
        check("one-shot timer does not fire early", oneshot == 0);
        tm.tick(0.6f);   // total 1.1
        check("one-shot fires after its delay + is removed", oneshot == 1 && tm.active() == 0);

        int reps = 0;
        const auto id = tm.every(0.5f, [&] { ++reps; });
        tm.tick(1.25f);   // catches up: fires at 0.5 and 1.0
        check("repeating timer fires each interval (2)", reps == 2);
        tm.cancel(id);
        tm.tick(1.0f);
        check("cancelled timer stops firing", reps == 2 && tm.active() == 0);
    }

    // G0: trigger volumes — actor enter/exit publishes events on the bus.
    {
        ecs::World tw;
        ecs::GameplayEventBus bus;
        std::string last_event; ecs::Entity last_actor = ecs::null_entity;
        bus.subscribe("*", [&](const ecs::GameplayEvent& e) { last_event = e.name; last_actor = e.instigator; });

        const ecs::Entity vol = tw.create();
        tw.add<ecs::Transform>(vol, ecs::Transform{});   // volume at origin
        ecs::TriggerVolume tv;
        tv.shape = 1; tv.radius = 2.0f; tv.enter_event = "zone.enter"; tv.exit_event = "zone.exit";
        tw.add<ecs::TriggerVolume>(vol, tv);

        const ecs::Entity actor = tw.create();
        ecs::Transform at; at.position = glm::vec3(10.0f, 0.0f, 0.0f);   // far away
        tw.add<ecs::Transform>(actor, at);
        tw.add<ecs::TriggerActor>(actor, ecs::TriggerActor{});

        ecs::tick_triggers(tw, bus); bus.flush();
        check("no event while actor is outside the volume", last_event.empty());

        tw.get<ecs::Transform>(actor).position = glm::vec3(1.0f, 0.0f, 0.0f);   // inside r=2
        ecs::tick_triggers(tw, bus); bus.flush();
        check("enter event fires when the actor enters", last_event == "zone.enter" && last_actor == actor);

        last_event.clear();
        ecs::tick_triggers(tw, bus); bus.flush();
        check("no repeat enter while still inside", last_event.empty());

        tw.get<ecs::Transform>(actor).position = glm::vec3(10.0f, 0.0f, 0.0f);   // leave
        ecs::tick_triggers(tw, bus); bus.flush();
        check("exit event fires when the actor leaves", last_event == "zone.exit" && last_actor == actor);

        ecs::register_trigger_components();
        const ecs::AuthorableComponent* tvc = ecs::find_authorable("Trigger Volume");
        check("Trigger Volume is authorable via custom hooks", tvc && tvc->serialize && tvc->deserialize);
        if (tvc) {
            const auto bytes = ecs::serialize_authorable(*tvc, &tv);
            ecs::TriggerVolume tv2;
            ecs::deserialize_authorable(*tvc, &tv2, bytes.data(), bytes.size());
            check("Trigger Volume serialize round-trips (shape/size/events)",
                  tv2.shape == 1 && tv2.radius == 2.0f &&
                  tv2.enter_event == "zone.enter" && tv2.exit_event == "zone.exit");
        }
    }

    // G1: gameplay state machine — a player-like FSM (idle/move/dodge/dead).
    // States grant tags; transitions fire on events + are tag-gated; dodge times
    // out back to idle. All data. Enter/exit broadcast on the bus.
    {
        ecs::World sw;
        ecs::GameplayEventBus bus;
        std::string entered;
        bus.subscribe("state.entered", [&](const ecs::GameplayEvent& e) { entered = e.param; });

        const ecs::Entity p = sw.create();
        ecs::StateMachine sm;
        sm.states = {
            {"idle",  {"state.idle"},               0.0f, ""},
            {"move",  {"state.move"},               0.0f, ""},
            {"dodge", {"state.dodge", "state.iframe"}, 0.4f, "dodge.end"},   // times out
            {"dead",  {"state.dead"},               0.0f, ""},
        };
        sm.transitions = {
            {"idle",  "move",  "input.move",  {}, {}},
            {"move",  "idle",  "input.stop",  {}, {}},
            {"",      "dodge", "input.dodge", {}, {"state.dodge"}},  // from any, not while dodging
            {"dodge", "idle",  "dodge.end",   {}, {}},               // fired by the timeout
            {"",      "dead",  "died",        {}, {}},               // from any
        };
        sm.initial = "idle";
        sw.add<ecs::StateMachine>(p, sm);

        ecs::tick_state_machines(sw, &bus, 0.016f);   // first tick starts the machine
        bus.flush();
        auto& live = sw.get<ecs::StateMachine>(p);
        check("FSM starts in the initial state + grants its tags",
              live.current == "idle" && entered == "idle" &&
              sw.has<ecs::GameplayTags>(p) && sw.get<ecs::GameplayTags>(p).has("state.idle"));

        ecs::send_state_event(sw, p, live, "input.move", &bus);
        check("event transition swaps state + tags (idle->move)",
              live.current == "move" &&
              sw.get<ecs::GameplayTags>(p).has("state.move") &&
              !sw.get<ecs::GameplayTags>(p).has("state.idle"));

        ecs::send_state_event(sw, p, live, "input.dodge", &bus);
        check("from-any transition works (move->dodge) + grants iframe",
              live.current == "dodge" && sw.get<ecs::GameplayTags>(p).has("state.iframe"));

        const bool blocked = ecs::send_state_event(sw, p, live, "input.dodge", &bus);
        check("block-tag gate prevents re-dodge while dodging", !blocked && live.current == "dodge");

        ecs::tick_state_machines(sw, &bus, 0.5f);   // dodge (0.4s) times out -> dodge.end -> idle
        check("state times out and auto-transitions (dodge->idle)",
              live.current == "idle" && !sw.get<ecs::GameplayTags>(p).has("state.iframe"));

        ecs::send_state_event(sw, p, live, "died", &bus);
        check("global 'died' transition reaches dead from any state", live.current == "dead");

        ecs::register_state_machine_component();
        const ecs::AuthorableComponent* smc = ecs::find_authorable("State Machine");
        check("State Machine is authorable via custom hooks", smc && smc->serialize && smc->deserialize);
        if (smc) {
            const auto bytes = ecs::serialize_authorable(*smc, &sm);
            ecs::StateMachine sm2;
            ecs::deserialize_authorable(*smc, &sm2, bytes.data(), bytes.size());
            check("State Machine serialize round-trips (states + transitions + initial)",
                  sm2.states.size() == 4 && sm2.transitions.size() == 5 && sm2.initial == "idle" &&
                  sm2.states[2].name == "dodge" && sm2.states[2].tags.size() == 2 &&
                  sm2.states[2].duration == 0.4f && sm2.transitions[2].block_tags.size() == 1);
        }
    }

    // G2: ECS melee combat — frame-data hitboxes resolved through G0 (damage,
    // resist, status), poise/stagger, i-frames, parry, events + FSM drive.
    {
        // basic hit: damage through G0, single-hit dedup, event, drives target FSM.
        ecs::World w; ecs::Entity atk, tgt; setup_combat_pair(w, atk, tgt);
        ecs::StateMachine sm;
        sm.states = { {"idle", {}, 0.0f, ""}, {"stagger", {"state.stagger"}, 0.0f, ""} };
        sm.transitions = { {"", "stagger", "hit", {}, {}} };
        sm.initial = "idle";
        w.add<ecs::StateMachine>(tgt, sm);

        ecs::GameplayEventBus bus;
        int hits = 0; float last_dmg = 0.0f;
        bus.subscribe("combat.hit", [&](const ecs::GameplayEvent& e) { ++hits; last_dmg = e.value; });

        ecs::tick_state_machines(w, &bus, 0.0f); bus.flush();   // start the FSM in idle
        ecs::combat_start_attack(w.get<ecs::CombatActor>(atk), ecs::AttackFrameData{});
        for (int i = 0; i < 14; ++i) ecs::tick_combat(w, bus);
        bus.flush();
        check("melee hit routes damage through G0 (100 -> 75)", w.get<ecs::AttributeSet>(tgt).get("Health") == 75.0f);
        check("single-hit attack lands exactly once (dedup)", hits == 1 && last_dmg == 25.0f);
        check("combat hit drives the target FSM (-> stagger)", w.get<ecs::StateMachine>(tgt).current == "stagger");
    }
    {   // i-frames (dodge) make the hit whiff
        ecs::World w; ecs::Entity atk, tgt; setup_combat_pair(w, atk, tgt);
        ecs::GameplayEventBus bus;
        ecs::combat_start_dodge(w.get<ecs::CombatActor>(tgt), 100);
        ecs::combat_start_attack(w.get<ecs::CombatActor>(atk), ecs::AttackFrameData{});
        for (int i = 0; i < 14; ++i) ecs::tick_combat(w, bus);
        check("i-frames make the hit whiff (no damage)", w.get<ecs::AttributeSet>(tgt).get("Health") == 100.0f);
    }
    {   // parry: no damage + the attacker is punished (staggered)
        ecs::World w; ecs::Entity atk, tgt; setup_combat_pair(w, atk, tgt);
        ecs::GameplayEventBus bus;
        ecs::combat_start_parry(w.get<ecs::CombatActor>(tgt), 100);
        ecs::combat_start_attack(w.get<ecs::CombatActor>(atk), ecs::AttackFrameData{});
        for (int i = 0; i < 14; ++i) ecs::tick_combat(w, bus);
        check("parry deals no damage + staggers the attacker",
              w.get<ecs::AttributeSet>(tgt).get("Health") == 100.0f &&
              w.get<ecs::CombatActor>(atk).state == ecs::CombatState::Hitstun);
    }
    {   // poise break staggers the target
        ecs::World w; ecs::Entity atk, tgt; setup_combat_pair(w, atk, tgt, /*max_poise=*/20.0f);
        ecs::GameplayEventBus bus;
        ecs::combat_start_attack(w.get<ecs::CombatActor>(atk), ecs::AttackFrameData{});   // poise_damage 30 > 20
        for (int i = 0; i < 14; ++i) ecs::tick_combat(w, bus);
        check("poise break staggers the target (Hitstun)", w.get<ecs::CombatActor>(tgt).state == ecs::CombatState::Hitstun);
    }
    {   // on-hit GameplayEffect (bleed DoT) applies a G0 status to the target
        ecs::World w; ecs::Entity atk, tgt; setup_combat_pair(w, atk, tgt);
        ecs::GameplayEventBus bus;
        w.get<ecs::CombatActor>(atk).on_hit_effects.push_back(ecs::make_dot("Bleed", "Health", -2.0f, 1.0f, 3.0f));
        ecs::combat_start_attack(w.get<ecs::CombatActor>(atk), ecs::AttackFrameData{});
        for (int i = 0; i < 14; ++i) ecs::tick_combat(w, bus);
        check("on-hit effect applies a G0 status (bleed active)",
              w.has<ecs::ActiveEffects>(tgt) && !w.get<ecs::ActiveEffects>(tgt).effects.empty());
    }
    {   // combat damage honours G0 resistance by damage type
        ecs::World w; ecs::Entity atk, tgt; setup_combat_pair(w, atk, tgt);
        w.get<ecs::AttributeSet>(tgt).define("resist.physical", 0.5f, 0.0f, 1.0f);
        ecs::GameplayEventBus bus;
        ecs::combat_start_attack(w.get<ecs::CombatActor>(atk), ecs::AttackFrameData{});
        for (int i = 0; i < 14; ++i) ecs::tick_combat(w, bus);
        check("combat damage respects G0 resistance (25 -> 12.5; 100 -> 87.5)",
              w.get<ecs::AttributeSet>(tgt).get("Health") == 87.5f);
    }
    {   // authorable + custom serialize round-trip of the config
        ecs::register_combat_component();
        const ecs::AuthorableComponent* cc = ecs::find_authorable("Combat Actor");
        check("Combat Actor is authorable via custom hooks", cc && cc->serialize && cc->deserialize);
        if (cc) {
            ecs::CombatActor a; a.max_poise = 55.0f; a.hurt_radius = 0.9f; a.hurt_height = 1.4f; a.damage_type = "fire";
            const auto bytes = ecs::serialize_authorable(*cc, &a);
            ecs::CombatActor a2;
            ecs::deserialize_authorable(*cc, &a2, bytes.data(), bytes.size());
            check("Combat Actor serialize round-trips (config + poise reset)",
                  a2.max_poise == 55.0f && a2.hurt_radius == 0.9f && a2.hurt_height == 1.4f &&
                  a2.damage_type == "fire" && a2.poise == 55.0f);
        }
    }

    // G3: derived stats — MaxHealth from Vitality, AttackPower from Strength.
    {
        ecs::World w; const ecs::Entity e = w.create();
        w.add<ecs::AttributeSet>(e, ecs::AttributeSet{});
        auto& a = w.get<ecs::AttributeSet>(e);
        a.define("Vitality", 10, 0, 100);
        a.define("Strength", 5,  0, 100);
        ecs::DerivedStats ds;
        ds.stats.push_back({"Health", 50.0f, {{"Vitality", 10.0f}}, /*set_max=*/true});
        ds.stats.push_back({"AttackPower", 0.0f, {{"Strength", 2.0f}}, false});
        w.add<ecs::DerivedStats>(e, ds);
        ecs::recompute_derived(w, e);
        check("derived MaxHealth = 50 + 10*Vitality (=150)", a.get("Health") == 150.0f && a.find("Health")->max == 150.0f);
        check("derived AttackPower = 2*Strength (=10)", a.get("AttackPower") == 10.0f);
        a.set("Vitality", 20); ecs::recompute_derived(w, e);
        check("derived recompute tracks source change (Health max -> 250)", a.find("Health")->max == 250.0f);
    }
    // G3: resource regeneration over time.
    {
        ecs::World w; const ecs::Entity e = w.create();
        w.add<ecs::AttributeSet>(e, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(e).define("Stamina", 100, 0, 100);
        w.get<ecs::AttributeSet>(e).set("Stamina", 40);
        ecs::Regeneration r; r.entries.push_back({"Stamina", 20.0f});   // 20/s
        w.add<ecs::Regeneration>(e, r);
        ecs::tick_regen(w, 1.0f);
        check("regen adds rate*dt (40 -> 60)", w.get<ecs::AttributeSet>(e).get("Stamina") == 60.0f);
        ecs::tick_regen(w, 10.0f);
        check("regen clamps at the attribute max (100)", w.get<ecs::AttributeSet>(e).get("Stamina") == 100.0f);
    }
    // G3: leveling — XP curve, level-up, points, per-level growth, event.
    {
        ecs::World w; ecs::GameplayEventBus bus;
        const ecs::Entity e = w.create();
        w.add<ecs::AttributeSet>(e, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(e).define("Strength", 5, 0, 999);
        ecs::Progression prog;
        prog.curve = {100.0f, 0.0f, 0.0f};        // flat 100 XP per level
        prog.points_per_level = 2;
        prog.per_level_growth.push_back({"Strength", 1.0f});
        w.add<ecs::Progression>(e, prog);
        int levelups = 0;
        bus.subscribe("level.up", [&](const ecs::GameplayEvent&) { ++levelups; });

        int gained = ecs::grant_xp(w, e, 250.0f, &bus);   // 100+100 -> 2 levels, 50 banked
        bus.flush();
        auto& p = w.get<ecs::Progression>(e);
        check("XP grants the right number of levels (2)", gained == 2 && p.level == 3);
        check("leftover XP is banked (50)", p.xp == 50.0f);
        check("skill points accrue per level (4)", p.skill_points == 4);
        check("per-level growth raised the attribute (Str 5 -> 7)", w.get<ecs::AttributeSet>(e).get("Strength") == 7.0f);
        check("level.up event fired per level (2)", levelups == 2);
    }
    // G3: skill tree — cost/prereq gate, permanent grants, respec reverts exactly.
    {
        ecs::World w; const ecs::Entity e = w.create();
        w.add<ecs::AttributeSet>(e, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(e).define("Strength", 10, 0, 999);
        ecs::Progression prog; prog.skill_points = 3; w.add<ecs::Progression>(e, prog);
        ecs::SkillTree tree;
        tree.nodes.push_back({"might1", "Might I",  1, {},           {{"Strength", 5.0f}}, {}});
        tree.nodes.push_back({"might2", "Might II", 2, {"might1"},   {{"Strength", 5.0f}}, {"ability.power_strike"}});
        w.add<ecs::SkillTree>(e, tree);
        w.add<ecs::UnlockedSkills>(e, ecs::UnlockedSkills{});

        check("cannot unlock a node behind an unmet prerequisite", !ecs::can_unlock(w, e, "might2"));
        check("unlock spends points + applies the grant (Str 10 -> 15)",
              ecs::unlock_skill(w, e, "might1") && w.get<ecs::AttributeSet>(e).get("Strength") == 15.0f &&
              w.get<ecs::Progression>(e).skill_points == 2);
        check("prerequisite now met -> unlock grants tag + stat (Str -> 20)",
              ecs::unlock_skill(w, e, "might2") && w.get<ecs::AttributeSet>(e).get("Strength") == 20.0f &&
              w.get<ecs::GameplayTags>(e).has("ability.power_strike"));

        const int refunded = ecs::respec(w, e);
        check("respec reverts every grant + tag and refunds points",
              refunded == 3 && w.get<ecs::AttributeSet>(e).get("Strength") == 10.0f &&
              w.get<ecs::Progression>(e).skill_points == 3 &&
              w.get<ecs::UnlockedSkills>(e).unlocked.empty() &&
              !w.get<ecs::GameplayTags>(e).has("ability.power_strike"));
    }
    // G3: authorable + serialize round-trips for the new components.
    {
        ecs::register_progression_components();
        ecs::register_skill_components();
        const ecs::AuthorableComponent* pc = ecs::find_authorable("Progression");
        const ecs::AuthorableComponent* tc = ecs::find_authorable("Skill Tree");
        check("Progression + Skill Tree are authorable", pc && pc->serialize && tc && tc->serialize);
        if (pc && tc) {
            ecs::Progression p; p.level = 7; p.xp = 33.0f; p.skill_points = 4;
            p.curve = {120.0f, 40.0f, 5.0f}; p.per_level_growth.push_back({"Vitality", 2.0f});
            auto pb = ecs::serialize_authorable(*pc, &p);
            ecs::Progression p2; ecs::deserialize_authorable(*pc, &p2, pb.data(), pb.size());
            check("Progression serialize round-trips",
                  p2.level == 7 && p2.xp == 33.0f && p2.skill_points == 4 &&
                  p2.curve.quadratic == 5.0f && p2.per_level_growth.size() == 1 &&
                  p2.per_level_growth[0].attribute == "Vitality");

            ecs::SkillTree t;
            t.nodes.push_back({"n1", "Node", 3, {"root"}, {{"Strength", 8.0f}}, {"tag.a", "tag.b"}});
            auto tb = ecs::serialize_authorable(*tc, &t);
            ecs::SkillTree t2; ecs::deserialize_authorable(*tc, &t2, tb.data(), tb.size());
            check("Skill Tree serialize round-trips",
                  t2.nodes.size() == 1 && t2.nodes[0].id == "n1" && t2.nodes[0].cost == 3 &&
                  t2.nodes[0].prerequisites.size() == 1 && t2.nodes[0].grants.size() == 1 &&
                  t2.nodes[0].grants[0].amount == 8.0f && t2.nodes[0].granted_tags.size() == 2);
        }
    }

    // G4: items — registry, stacking inventory, procedural rolls, loot, equip
    // (modifiers/tags through G0), set bonuses, consumables, serialize.
    {
        // seed a small item catalog
        ecs::ItemDef potion; potion.id = "potion_hp"; potion.name = "Health Potion";
        potion.kind = ecs::ItemKind::Consumable; potion.max_stack = 10; potion.weight = 0.1f;
        potion.on_use.push_back(ecs::make_instant("Heal", "Health", +40.0f));
        ecs::register_item(potion);

        ecs::ItemDef sword; sword.id = "sword_iron"; sword.name = "Iron Sword";
        sword.kind = ecs::ItemKind::Weapon; sword.equip_slot = "weapon"; sword.weight = 3.0f;
        sword.modifiers.push_back({"AttackPower", 12.0f});
        sword.affix_pool.push_back({"of Might", "AttackPower", 3.0f, 5.0f});
        ecs::register_item(sword);

        ecs::ItemDef helm; helm.id = "helm_set"; helm.name = "Guardian Helm";
        helm.kind = ecs::ItemKind::Armor; helm.equip_slot = "head"; helm.weight = 2.0f;
        helm.modifiers.push_back({"Armor", 5.0f}); helm.set_id = "guardian";
        ecs::register_item(helm);
        ecs::ItemDef chest; chest.id = "chest_set"; chest.name = "Guardian Chest";
        chest.kind = ecs::ItemKind::Armor; chest.equip_slot = "chest"; chest.weight = 6.0f;
        chest.modifiers.push_back({"Armor", 10.0f}); chest.set_id = "guardian";
        ecs::register_item(chest);
        ecs::register_set_bonus({"guardian", 2, {{"Armor", 20.0f}}, {"set.guardian"}});

        check("item registry stores + finds defs", ecs::find_item("sword_iron") != nullptr &&
              ecs::find_item("sword_iron")->name == "Iron Sword");

        // stacking inventory
        ecs::Inventory inv; inv.max_slots = 5;
        int leftover = ecs::inventory_add(inv, ecs::ItemInstance{"potion_hp", 7, 0, {}});
        leftover += ecs::inventory_add(inv, ecs::ItemInstance{"potion_hp", 5, 0, {}});
        check("stackable items stack up to max_stack (12 -> stacks of 10+2)",
              leftover == 0 && ecs::inventory_count(inv, "potion_hp") == 12 && inv.items.size() == 2);
        check("inventory removal spans stacks", ecs::inventory_remove(inv, "potion_hp", 11) == 11 &&
              ecs::inventory_count(inv, "potion_hp") == 1);

        // slot cap
        ecs::Inventory small; small.max_slots = 1;
        int lo = ecs::inventory_add(small, ecs::ItemInstance{"sword_iron", 3, 0, {}});  // not stackable
        check("slot cap rejects overflow (max_stack 1, 1 slot)", small.items.size() == 1 && lo == 2);

        // procedural roll (deterministic RNG)
        ecs::Rng rng(12345);
        ecs::ItemInstance rolled = ecs::roll_item("sword_iron", 1, rng);
        check("procedural roll adds an affix + bumps rarity",
              rolled.affixes.size() == 1 && rolled.affixes[0].attribute == "AttackPower" &&
              rolled.affixes[0].amount >= 3.0f && rolled.affixes[0].amount <= 5.0f && rolled.rarity == sword.rarity + 1);
        auto mods = ecs::item_modifiers(rolled);
        check("rolled item modifiers = base + affix (2 entries)", mods.size() == 2);

        // loot table (deterministic + repeatable)
        ecs::DropTable table; table.rolls = 3;
        table.entries.push_back({"potion_hp", 1.0f, 1.0f, 1, 3, 0});
        table.entries.push_back({"sword_iron", 1.0f, 1.0f, 1, 1, 1});
        ecs::Rng r1(999), r2(999);
        auto loot1 = ecs::roll_loot(table, r1);
        auto loot2 = ecs::roll_loot(table, r2);
        check("loot table drops items", !loot1.empty() && loot1.size() <= 3);
        check("loot rolls are deterministic for a fixed seed",
              loot1.size() == loot2.size() && (loot1.empty() || loot1[0].def_id == loot2[0].def_id));

        // equip -> modifiers + tags flow to G0; set bonus at 2 pieces
        ecs::World w; const ecs::Entity e = w.create();
        w.add<ecs::AttributeSet>(e, ecs::AttributeSet{});
        auto& a = w.get<ecs::AttributeSet>(e);
        a.define("AttackPower", 0, 0, 999);
        a.define("Armor", 0, 0, 999);
        ecs::equip_item(w, e, ecs::ItemInstance{"sword_iron", 1, 0, {}});
        check("equip applies item modifier to G0 attribute (AttackPower +12)", a.get("AttackPower") == 12.0f);
        auto has_tag = [&](const char* t) { return w.has<ecs::GameplayTags>(e) && w.get<ecs::GameplayTags>(e).has(t); };
        ecs::equip_item(w, e, ecs::ItemInstance{"helm_set", 1, 0, {}});
        check("one set piece: no set bonus yet (Armor 5)", a.get("Armor") == 5.0f && !has_tag("set.guardian"));
        ecs::equip_item(w, e, ecs::ItemInstance{"chest_set", 1, 0, {}});
        check("two set pieces: set bonus applies (Armor 5+10+20) + tag",
              a.get("Armor") == 35.0f && has_tag("set.guardian"));
        ecs::unequip_slot(w, e, "chest");
        check("unequip reverts exactly (set bonus lost -> Armor 5, tag gone)",
              a.get("Armor") == 5.0f && !has_tag("set.guardian"));
        ecs::unequip_slot(w, e, "weapon");
        check("unequip weapon reverts its modifier (AttackPower -> 0)", a.get("AttackPower") == 0.0f);

        // consumable use applies on-use effect + decrements
        ecs::Inventory cinv; ecs::inventory_add(cinv, ecs::ItemInstance{"potion_hp", 2, 0, {}});
        w.get<ecs::AttributeSet>(e).define("Health", 100, 0, 100);
        w.get<ecs::AttributeSet>(e).set("Health", 50);
        check("consumable use heals via G0 + decrements stack",
              ecs::use_item(w, e, cinv, 0) && w.get<ecs::AttributeSet>(e).get("Health") == 90.0f &&
              ecs::inventory_count(cinv, "potion_hp") == 1);

        // authorable + serialize round-trip
        ecs::register_inventory_components();
        const ecs::AuthorableComponent* ic = ecs::find_authorable("Inventory");
        const ecs::AuthorableComponent* ec = ecs::find_authorable("Equipment");
        check("Inventory + Equipment are authorable", ic && ic->serialize && ec && ec->serialize);
        if (ic) {
            ecs::Inventory s; s.max_slots = 8; s.max_weight = 50.0f;
            s.items.push_back({"sword_iron", 1, 2, {{"AttackPower", 4.5f, "of Might"}}});
            s.items.push_back({"potion_hp", 5, 0, {}});
            auto b = ecs::serialize_authorable(*ic, &s);
            ecs::Inventory s2; ecs::deserialize_authorable(*ic, &s2, b.data(), b.size());
            check("Inventory serialize round-trips (items + affixes)",
                  s2.max_slots == 8 && s2.max_weight == 50.0f && s2.items.size() == 2 &&
                  s2.items[0].affixes.size() == 1 && s2.items[0].affixes[0].amount == 4.5f &&
                  s2.items[1].quantity == 5);
        }
    }

    // F5: .items data-file parsing -> item catalog (author items as data).
    {
        const std::string text =
            "# demo items\n"
            "item file_potion\n"
            "  name File Potion\n"
            "  kind consumable\n"
            "  max_stack 20\n"
            "  weight 0.2\n"
            "  on_use Health 55\n"
            "end\n"
            "item file_axe\n"
            "  name Iron Axe\n"
            "  kind weapon\n"
            "  slot weapon\n"
            "  weight 4\n"
            "  mod AttackPower 18\n"
            "  tag weapon.axe\n"
            "  affix AttackPower 2 6 of Fury\n"
            "  set berserker\n"
            "end\n";
        const auto defs = ecs::parse_items_text(text);
        check("parse .items reads all blocks (2)", defs.size() == 2);
        const int n = ecs::register_items_text(text);
        check("register_items_text registers into the catalog", n == 2 && ecs::find_item("file_axe") != nullptr);
        const ecs::ItemDef* axe = ecs::find_item("file_axe");
        check("parsed weapon fields are correct",
              axe && axe->name == "Iron Axe" && axe->kind == ecs::ItemKind::Weapon &&
              axe->equip_slot == "weapon" && axe->weight == 4.0f &&
              axe->modifiers.size() == 1 && axe->modifiers[0].amount == 18.0f &&
              axe->granted_tags.size() == 1 && axe->set_id == "berserker");
        check("parsed affix (attribute min max name...)",
              axe->affix_pool.size() == 1 && axe->affix_pool[0].attribute == "AttackPower" &&
              axe->affix_pool[0].min_roll == 2.0f && axe->affix_pool[0].max_roll == 6.0f &&
              axe->affix_pool[0].name == "of Fury");
        const ecs::ItemDef* pot = ecs::find_item("file_potion");
        check("parsed consumable on_use effect",
              pot && pot->kind == ecs::ItemKind::Consumable && pot->max_stack == 20 &&
              pot->on_use.size() == 1 && pot->on_use[0].modifiers.size() == 1 &&
              pot->on_use[0].modifiers[0].attribute == "Health" && pot->on_use[0].modifiers[0].magnitude == 55.0f);
        // a data-file item is usable end-to-end (equip flows through G0)
        ecs::World w; const ecs::Entity e = w.create();
        w.add<ecs::AttributeSet>(e, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(e).define("AttackPower", 0, 0, 999);
        ecs::equip_item(w, e, ecs::ItemInstance{"file_axe", 1, 0, {}});
        check("data-file item equips + applies its modifier (AttackPower 18)",
              w.get<ecs::AttributeSet>(e).get("AttackPower") == 18.0f);
    }

    // G5: crafting + salvage — recipes over G4 inventory, station/skill gated.
    {
        ecs::register_item(ecs::ItemDef{"g5_ore", "Ore", ecs::ItemKind::Material, 0, 50, 0.5f, 3.0f, "", {}, {}, {}, {}, ""});
        ecs::register_item(ecs::ItemDef{"g5_ingot", "Ingot", ecs::ItemKind::Material, 0, 50, 0.6f, 8.0f, "", {}, {}, {}, {}, ""});
        ecs::register_item(ecs::ItemDef{"g5_blade", "Blade", ecs::ItemKind::Weapon, 0, 1, 3.0f, 60.0f, "weapon", {{"AttackPower", 12.0f}}, {}, {}, {}, ""});
        ecs::register_recipe({"g5_smelt", "Smelt", {{"g5_ore", 2}}, {{"g5_ingot", 1}}, "", ""});
        ecs::register_recipe({"g5_forge", "Forge", {{"g5_ingot", 3}}, {{"g5_blade", 1}}, "station.forge", ""});
        ecs::register_salvage({"g5_blade", {{"g5_ingot", 1}}});

        ecs::World w; const ecs::Entity e = w.create();
        w.add<ecs::Inventory>(e, ecs::Inventory{});
        auto& inv = w.get<ecs::Inventory>(e);
        ecs::inventory_add(inv, ecs::ItemInstance{"g5_ore", 4, 0, {}});

        check("recipe registry finds a recipe", ecs::find_recipe("g5_smelt") != nullptr);
        check("craft consumes inputs + produces outputs (4 ore -> 2 ingot, 0 ore)",
              ecs::craft(w, e, "g5_smelt") && ecs::craft(w, e, "g5_smelt") &&
              ecs::inventory_count(inv, "g5_ingot") == 2 && ecs::inventory_count(inv, "g5_ore") == 0);
        check("craft fails without enough inputs", !ecs::craft(w, e, "g5_smelt"));

        // station-gated recipe: needs the forge tag
        ecs::inventory_add(inv, ecs::ItemInstance{"g5_ingot", 1, 0, {}});   // now 3 ingots
        check("station-gated recipe blocked without the station tag", !ecs::craft(w, e, "g5_forge"));
        w.add<ecs::GameplayTags>(e, ecs::GameplayTags{});
        w.get<ecs::GameplayTags>(e).add("station.forge");
        check("station-gated recipe crafts with the station tag (3 ingot -> 1 blade)",
              ecs::craft(w, e, "g5_forge") && ecs::inventory_count(inv, "g5_blade") == 1 &&
              ecs::inventory_count(inv, "g5_ingot") == 0);
        check("salvage breaks an item into materials (blade -> 1 ingot)",
              ecs::salvage(w, e, "g5_blade") && ecs::inventory_count(inv, "g5_blade") == 0 &&
              ecs::inventory_count(inv, "g5_ingot") == 1);
    }

    // G5: economy — wallet, vendor buy/sell, transfer, harvest.
    {
        ecs::World w;
        ecs::GameplayEventBus bus;
        const ecs::Entity shop  = w.create();
        const ecs::Entity buyer = w.create();

        ecs::Vendor vend;
        vend.entries.push_back({"g5_ingot", 10.0f, 5});   // 10 gold each, 5 in stock
        vend.sell_ratio = 0.5f;
        w.add<ecs::Vendor>(shop, vend);

        w.add<ecs::AttributeSet>(buyer, ecs::AttributeSet{});
        ecs::wallet_add(w, buyer, 100.0f);   // 100 gold
        check("wallet balance reflects deposits", ecs::wallet_balance(w, buyer) == 100.0f);

        check("cannot buy more than affordable", !ecs::vendor_buy(w, shop, buyer, "g5_ingot", 20));
        check("vendor_buy deducts gold + gives item + decrements stock (buy 3 = 30g)",
              ecs::vendor_buy(w, shop, buyer, "g5_ingot", 3, &bus) &&
              ecs::wallet_balance(w, buyer) == 70.0f &&
              ecs::inventory_count(w.get<ecs::Inventory>(buyer), "g5_ingot") == 3 &&
              w.get<ecs::Vendor>(shop).entries[0].stock == 2);
        check("vendor_sell removes item + pays value*ratio (sell 1 ingot: value 8 * 0.5 = 4)",
              ecs::vendor_sell(w, shop, buyer, "g5_ingot", 1, &bus) &&
              ecs::wallet_balance(w, buyer) == 74.0f &&
              ecs::inventory_count(w.get<ecs::Inventory>(buyer), "g5_ingot") == 2);

        // transfer between inventories (container/trade basis)
        const ecs::Entity chest = w.create();
        const int moved = ecs::transfer_item(w, buyer, chest, "g5_ingot", 5);   // only has 2
        check("transfer_item moves what it can between inventories",
              moved == 2 && ecs::inventory_count(w.get<ecs::Inventory>(buyer), "g5_ingot") == 0 &&
              ecs::inventory_count(w.get<ecs::Inventory>(chest), "g5_ingot") == 2);

        // harvest node with respawn cooldown
        const ecs::Entity node = w.create();
        w.add<ecs::HarvestNode>(node, ecs::HarvestNode{"g5_ore", 3, 3, 5.0f, 0.0f});
        check("harvest yields items + then goes on cooldown",
              ecs::harvest(w, node, buyer, &bus) &&
              ecs::inventory_count(w.get<ecs::Inventory>(buyer), "g5_ore") == 3 &&
              w.get<ecs::HarvestNode>(node).cooldown == 5.0f);
        check("cannot harvest again while depleted", !ecs::harvest(w, node, buyer, &bus));
        ecs::tick_harvest(w, 5.0f);
        check("harvest node respawns after its cooldown",
              w.get<ecs::HarvestNode>(node).cooldown == 0.0f && ecs::harvest(w, node, buyer, &bus));

        // authorable + serialize round-trip
        ecs::register_economy_components();
        const ecs::AuthorableComponent* vc = ecs::find_authorable("Vendor");
        check("Vendor is authorable", vc && vc->serialize && vc->deserialize);
        if (vc) {
            const auto b = ecs::serialize_authorable(*vc, &vend);
            ecs::Vendor v2; ecs::deserialize_authorable(*vc, &v2, b.data(), b.size());
            check("Vendor serialize round-trips",
                  v2.entries.size() == 1 && v2.entries[0].item_id == "g5_ingot" &&
                  v2.entries[0].price == 10.0f && v2.sell_ratio == 0.5f && v2.currency == "Gold");
        }
    }

    // G7: world interaction — proximity, pickups, container transfer, events.
    {
        ecs::register_item(ecs::ItemDef{"g7_potion", "Potion", ecs::ItemKind::Consumable, 0, 10, 0.1f, 5.0f, "", {}, {}, {}, {}, ""});
        ecs::World w;
        ecs::GameplayEventBus bus;
        std::string last_event; ecs::Entity last_src = ecs::null_entity;
        bus.subscribe("*", [&](const ecs::GameplayEvent& e) { last_event = e.name; last_src = e.instigator; });

        const ecs::Entity player = w.create();
        w.add<ecs::Transform>(player, ecs::Transform{});   // at origin

        // a ground pickup 1.5m away (in range), consumed on use
        const ecs::Entity pot = w.create();
        ecs::Transform pt; pt.position = glm::vec3(1.5f, 0.0f, 0.0f);
        w.add<ecs::Transform>(pot, pt);
        w.add<ecs::Interactable>(pot, ecs::Interactable{"Pick up", "pickup.potion", 2.0f, true, true});
        w.add<ecs::Pickup>(pot, ecs::Pickup{"g7_potion", 2});

        // a lever far away (out of range)
        const ecs::Entity lever = w.create();
        ecs::Transform lt; lt.position = glm::vec3(20.0f, 0.0f, 0.0f);
        w.add<ecs::Transform>(lever, lt);
        w.add<ecs::Interactable>(lever, ecs::Interactable{"Pull", "lever.pull", 2.0f, true, false});

        check("nearest_interactable finds the in-range one, ignores the far one",
              ecs::nearest_interactable(w, glm::vec3(0)) == pot);

        check("interact grants the pickup + fires events + consumes",
              ecs::try_interact(w, player, &bus) &&
              w.has<ecs::Inventory>(player) &&
              ecs::inventory_count(w.get<ecs::Inventory>(player), "g7_potion") == 2 &&
              !w.get<ecs::Interactable>(pot).enabled);
        bus.flush();
        check("pickup event was published", last_event == "pickup" && last_src == player);

        check("consumed interactable is no longer the nearest",
              ecs::nearest_interactable(w, glm::vec3(0)) == ecs::null_entity);
        check("nothing to interact with in range -> false", !ecs::try_interact(w, player, &bus));

        // container: an entity with an Inventory + an Interactable; transfer works
        const ecs::Entity chest = w.create();
        w.add<ecs::Transform>(chest, ecs::Transform{});
        w.add<ecs::Interactable>(chest, ecs::Interactable{"Open", "container.open", 2.0f, true, false});
        w.add<ecs::Inventory>(chest, ecs::Inventory{});
        ecs::inventory_add(w.get<ecs::Inventory>(chest), ecs::ItemInstance{"g7_potion", 3, 0, {}});
        check("interact with a container fires its open event",
              ecs::interact(w, player, chest, &bus));
        bus.flush();
        const int moved = ecs::transfer_item(w, chest, player, "g7_potion", 3);
        check("loot the container via transfer_item (chest 3 -> player)",
              moved == 3 && ecs::inventory_count(w.get<ecs::Inventory>(player), "g7_potion") == 5 &&
              ecs::inventory_count(w.get<ecs::Inventory>(chest), "g7_potion") == 0);

        // authorable + serialize round-trip
        ecs::register_interaction_components();
        const ecs::AuthorableComponent* ic = ecs::find_authorable("Interactable");
        check("Interactable is authorable", ic && ic->serialize && ic->deserialize);
        if (ic) {
            ecs::Interactable src{"Talk", "npc.talk", 3.5f, true, false};
            const auto b = ecs::serialize_authorable(*ic, &src);
            ecs::Interactable dst; ecs::deserialize_authorable(*ic, &dst, b.data(), b.size());
            check("Interactable serialize round-trips",
                  dst.prompt == "Talk" && dst.event == "npc.talk" && dst.range == 3.5f &&
                  dst.enabled && !dst.consume_on_use);
        }
    }

    // G6: quests — event-driven objectives, stages, rewards, journal.
    {
        ecs::register_item(ecs::ItemDef{"g6_reward", "Reward", ecs::ItemKind::Misc, 0, 10, 0.0f, 0.0f, "", {}, {}, {}, {}, ""});
        ecs::QuestDef q;
        q.id = "hunt"; q.name = "Hunt";
        q.stages.push_back({"Slay wolves", {{"Kill 3 wolves", "combat.hit", "wolf", 3, true}}});
        q.stages.push_back({"Collect pelts", {{"Pick up 2 pelts", "pickup", "pelt", 2, true}}});
        q.reward.items.push_back({"g6_reward", 1});
        q.reward.xp = 100.0f; q.reward.currency = 50.0f; q.reward.tags.push_back("quest.hunt.done");
        ecs::register_quest(q);

        ecs::World w; ecs::GameplayEventBus bus;
        const ecs::Entity player = w.create();
        w.add<ecs::AttributeSet>(player, ecs::AttributeSet{});
        w.add<ecs::Progression>(player, ecs::Progression{});
        bus.subscribe("*", [&](const ecs::GameplayEvent& ev) { ecs::advance_quests(w, ev, &bus); });

        ecs::start_quest(w, player, "hunt", &bus);
        check("quest starts + is in the active log", w.has<ecs::QuestLog>(player) &&
              ecs::find_progress(w.get<ecs::QuestLog>(player), "hunt") != nullptr);

        auto hit_wolf = [&] { ecs::GameplayEvent e; e.name = "combat.hit"; e.instigator = player; e.param = "wolf"; bus.publish(e); bus.flush(); };
        hit_wolf(); hit_wolf();
        check("objective counts matching events (2/3)",
              ecs::find_progress(w.get<ecs::QuestLog>(player), "hunt")->counts[0] == 2);
        { ecs::GameplayEvent e; e.name = "combat.hit"; e.instigator = player; e.param = "rat"; bus.publish(e); bus.flush(); }
        check("objective ignores non-matching param (still 2/3)",
              ecs::find_progress(w.get<ecs::QuestLog>(player), "hunt")->counts[0] == 2);
        hit_wolf();   // 3/3 -> advance to stage 2
        check("completing a stage advances to the next",
              ecs::find_progress(w.get<ecs::QuestLog>(player), "hunt")->stage == 1);

        auto pick_pelt = [&] { ecs::GameplayEvent e; e.name = "pickup"; e.instigator = player; e.param = "pelt"; bus.publish(e); bus.flush(); };
        pick_pelt(); pick_pelt();   // 2/2 -> quest complete + reward
        auto& log = w.get<ecs::QuestLog>(player);
        check("finishing the last stage completes the quest",
              log.active.empty() && log.completed.size() == 1 && log.completed[0] == "hunt");
        check("quest reward granted (item + XP + currency + tag)",
              ecs::inventory_count(w.get<ecs::Inventory>(player), "g6_reward") == 1 &&
              w.get<ecs::Progression>(player).xp == 100.0f &&
              ecs::wallet_balance(w, player) == 50.0f &&
              w.get<ecs::GameplayTags>(player).has("quest.hunt.done"));
    }

    // G8: NPCs — factions, reputation, aggro/threat, spawners.
    {
        ecs::set_faction_standing("bandits", "town", ecs::Standing::Hostile);
        ecs::World w; ecs::GameplayEventBus bus;
        const ecs::Entity guard  = w.create();  w.add<ecs::Faction>(guard,  ecs::Faction{"town"});
        const ecs::Entity bandit = w.create();  w.add<ecs::Faction>(bandit, ecs::Faction{"bandits"});
        const ecs::Entity friend_ = w.create(); w.add<ecs::Faction>(friend_, ecs::Faction{"town"});
        check("hostile factions are hostile", ecs::are_hostile(w, guard, bandit));
        check("same faction is not hostile", !ecs::are_hostile(w, guard, friend_) &&
              ecs::standing_between(w, guard, friend_) == ecs::Standing::Friendly);

        ecs::adjust_reputation(w, guard, "town", 25.0f);
        ecs::adjust_reputation(w, guard, "town", -5.0f);
        check("reputation accumulates per faction", ecs::reputation(w, guard, "town") == 20.0f);

        w.add<ecs::Aggro>(bandit, ecs::Aggro{});
        ecs::add_threat(w, bandit, guard, 10.0f);
        ecs::add_threat(w, bandit, friend_, 25.0f);
        check("aggro targets the highest-threat source", ecs::aggro_target(w, bandit) == friend_);
        ecs::add_threat(w, bandit, guard, 30.0f);   // guard now 40 > 25
        check("aggro retargets when threat shifts", ecs::aggro_target(w, bandit) == guard);
        ecs::clear_threat(w, bandit);
        check("clearing threat drops the target", ecs::aggro_target(w, bandit) == ecs::null_entity);

        // spawner: request every interval up to max_alive, resumes after a death
        const ecs::Entity spawner = w.create();
        w.add<ecs::Spawner>(spawner, ecs::Spawner{"g8_wolf", 2, 1.0f, 5.0f, 0.0f, 0});
        int requests = 0;
        bus.subscribe("spawn.request", [&](const ecs::GameplayEvent&) { ++requests; });
        ecs::tick_spawners(w, 1.0f, &bus); bus.flush();   // alive 0->1, request
        ecs::tick_spawners(w, 1.0f, &bus); bus.flush();   // alive 1->2, request
        ecs::tick_spawners(w, 1.0f, &bus); bus.flush();   // at max -> no request
        check("spawner requests up to max_alive then stops", requests == 2 &&
              w.get<ecs::Spawner>(spawner).alive == 2);
        ecs::spawner_report_death(w, spawner);
        ecs::tick_spawners(w, 1.0f, &bus); bus.flush();   // one died -> request again
        check("spawner resumes after a reported death", requests == 3);
    }

    // G9: persistence — SaveGame captures/restores gameplay state by SaveId.
    {
        ecs::register_persistence_components();
        ecs::register_attribute_component();

        ecs::World src;
        const ecs::Entity p = src.create();
        src.add<ecs::SaveId>(p, ecs::SaveId{1001});
        src.add<ecs::AttributeSet>(p, ecs::AttributeSet{});
        src.get<ecs::AttributeSet>(p).define("Health", 100, 0, 100);
        src.get<ecs::AttributeSet>(p).set("Health", 42);
        src.add<ecs::Inventory>(p, ecs::Inventory{});
        ecs::inventory_add(src.get<ecs::Inventory>(p), ecs::ItemInstance{"g6_reward", 3, 0, {}});

        const std::string text = ecs::SaveGame::capture(src).to_text();
        check("SaveGame captures SaveId'd entities", ecs::SaveGame::from_text(text).entries.size() == 1);

        // restore into a FRESH world (no entities): apply creates + keys them
        ecs::World dst;
        ecs::SaveGame::from_text(text).apply(dst);
        ecs::Entity restored = ecs::null_entity;
        dst.each<ecs::SaveId>([&](ecs::Entity e, ecs::SaveId& s) { if (s.value == 1001) restored = e; });
        check("SaveGame restores an entity by its SaveId", restored != ecs::null_entity);
        check("restored gameplay state matches (Health 42 + 3 items)",
              restored != ecs::null_entity &&
              dst.get<ecs::AttributeSet>(restored).get("Health") == 42.0f &&
              dst.has<ecs::Inventory>(restored) &&
              ecs::inventory_count(dst.get<ecs::Inventory>(restored), "g6_reward") == 3);

        // world flags
        ecs::WorldFlags wf;
        ecs::set_world_flag(wf, "boss.dragon.killed", 1);
        ecs::set_world_flag(wf, "chests.opened", 3);
        ecs::set_world_flag(wf, "chests.opened", 4);   // overwrite
        check("world flags set/get + overwrite",
              ecs::world_flag(wf, "boss.dragon.killed") == 1 && ecs::world_flag(wf, "chests.opened") == 4 &&
              ecs::world_flag(wf, "missing") == 0 && wf.flags.size() == 2);
    }

    // G10: multiplayer social — parties, guilds, chat routing, trade, leaderboard.
    {
        ecs::SocialState s;
        const ecs::PlayerId p1 = 1, p2 = 2, p3 = 3;

        // parties
        const uint64_t party = ecs::create_party(s, p1);
        check("create party puts the leader in it", party != 0 &&
              ecs::party_of(s, p1) && ecs::party_of(s, p1)->leader == p1);
        check("join party adds a member", ecs::join_party(s, party, p2) &&
              ecs::party_of(s, p2) == ecs::party_of(s, p1));
        ecs::join_party(s, party, p3);
        check("kick requires the leader + removes the target",
              !ecs::kick_from_party(s, p2, p3) && ecs::kick_from_party(s, p1, p3) && ecs::party_of(s, p3) == nullptr);
        check("leader leaving promotes another member",
              ecs::leave_party(s, p1) && ecs::party_of(s, p2)->leader == p2);
        ecs::leave_party(s, p2);
        check("last member leaving disbands the party", ecs::find_party(s, party) == nullptr);

        // guilds (ranks)
        const uint64_t guild = ecs::create_guild(s, "Wolves", p1);
        ecs::join_guild(s, guild, p2);
        check("founder is guild leader (rank 2), joiner is member (rank 0)",
              ecs::guild_rank(*ecs::find_guild(s, guild), p1) == 2 && ecs::guild_rank(*ecs::find_guild(s, guild), p2) == 0);
        check("leader can promote a member below their own rank", ecs::set_guild_rank(s, guild, p1, p2, 1));
        check("a member cannot promote at/above the promoter", !ecs::set_guild_rank(s, guild, p2, p1, 2));

        // chat routing
        ecs::World w;
        auto mk = [&](float x) { ecs::Entity e = w.create(); ecs::Transform t; t.position = glm::vec3(x, 0, 0); w.add<ecs::Transform>(e, t); return static_cast<ecs::PlayerId>(e); };
        const ecs::PlayerId a = mk(0.0f), b = mk(3.0f), c = mk(50.0f);
        const std::vector<ecs::PlayerId> online = {a, b, c};
        ecs::create_party(s, a); ecs::join_party(s, ecs::party_of(s, a)->id, b);
        check("party chat reaches only party members",
              ecs::chat_recipients(w, s, a, ecs::ChatChannel::Party, 0, 30.0f, online).size() == 2);
        check("say chat reaches only players in range",
              ecs::chat_recipients(w, s, a, ecs::ChatChannel::Say, 0, 10.0f, online).size() == 2);   // a + b, not c
        check("whisper reaches sender + target", ecs::chat_recipients(w, s, a, ecs::ChatChannel::Whisper, c, 10.0f, online).size() == 2);
        check("global reaches everyone online", ecs::chat_recipients(w, s, a, ecs::ChatChannel::Global, 0, 10.0f, online).size() == 3);

        // trade: swap items + gold once both confirm
        ecs::register_item(ecs::ItemDef{"g10_gem", "Gem", ecs::ItemKind::Material, 0, 99, 0.0f, 100.0f, "", {}, {}, {}, {}, ""});
        const ecs::Entity ea = static_cast<ecs::Entity>(a), eb = static_cast<ecs::Entity>(b);
        w.add<ecs::Inventory>(ea, ecs::Inventory{}); ecs::inventory_add(w.get<ecs::Inventory>(ea), ecs::ItemInstance{"g10_gem", 2, 0, {}});
        w.add<ecs::AttributeSet>(eb, ecs::AttributeSet{}); ecs::wallet_add(w, eb, 500.0f);
        const uint64_t trade = ecs::open_trade(s, a, b);
        ecs::TradeSession* ts = ecs::find_trade(s, trade);
        ecs::trade_offer_item(*ts, a, "g10_gem", 2);
        ecs::trade_set_gold(*ts, b, 300.0f);
        check("trade needs both confirmations", !ecs::trade_complete(w, s, trade));
        ecs::trade_confirm(*ts, a); ecs::trade_confirm(*ts, b);
        check("trade swaps items + gold when both confirm",
              ecs::trade_complete(w, s, trade) &&
              ecs::inventory_count(w.get<ecs::Inventory>(eb), "g10_gem") == 2 &&
              ecs::wallet_balance(w, ea) == 300.0f && ecs::wallet_balance(w, eb) == 200.0f);

        // leaderboard
        ecs::submit_score(s, "Alice", 100); ecs::submit_score(s, "Bob", 250); ecs::submit_score(s, "Cara", 175);
        ecs::submit_score(s, "Alice", 300);   // improve
        check("leaderboard sorts high-to-low + tracks best score",
              ecs::rank_of(s, "Alice") == 1 && ecs::rank_of(s, "Cara") == 3 &&
              ecs::top_scores(s, 2).size() == 2 && ecs::top_scores(s, 2)[0].name == "Alice");
    }

    // G12: shooter — hitscan + projectiles resolved through G0/G2, ammo, reload.
    {
        ecs::WeaponDef rifle; rifle.id = "g12_rifle"; rifle.name = "Rifle"; rifle.damage = 25.0f;
        rifle.damage_type = "kinetic"; rifle.rpm = 600.0f; rifle.mag_size = 5; rifle.spread_deg = 0.0f; rifle.range = 100.0f;
        ecs::register_weapon(rifle);
        ecs::WeaponDef rocket; rocket.id = "g12_rocket"; rocket.name = "Launcher"; rocket.damage = 60.0f;
        rocket.damage_type = "explosive"; rocket.rpm = 60.0f; rocket.mag_size = 1; rocket.projectile_speed = 20.0f; rocket.range = 100.0f;
        ecs::register_weapon(rocket);

        ecs::World w; ecs::GameplayEventBus bus;
        const ecs::Entity shooter = w.create();
        w.add<ecs::Transform>(shooter, ecs::Transform{});                 // at origin
        const ecs::Entity target = w.create();
        ecs::Transform tt; tt.position = glm::vec3(0.0f, 0.0f, 10.0f);     // 10m down +Z
        w.add<ecs::Transform>(target, tt);
        w.add<ecs::CombatActor>(target, ecs::CombatActor{});              // hurtbox at (0,1,10) r=0.6
        w.add<ecs::AttributeSet>(target, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(target).define("Health", 100, 0, 100);

        ecs::WeaponState ws; ws.weapon_id = "g12_rifle"; ws.ammo_in_mag = 5; ws.reserve = 10;
        w.add<ecs::WeaponState>(shooter, ws);
        const glm::vec3 origin(0, 1, 0), aim(0, 0, 1);

        int hits = 0;
        bus.subscribe("weapon.hit", [&](const ecs::GameplayEvent&) { ++hits; });
        auto& live = w.get<ecs::WeaponState>(shooter);
        check("hitscan shot hits the target + damages via G0 (100 -> 75) + spends ammo",
              ecs::fire_weapon(w, shooter, origin, aim, live, &bus) && (bus.flush(), true) &&
              w.get<ecs::AttributeSet>(target).get("Health") == 75.0f && hits == 1 && live.ammo_in_mag == 4);
        check("cannot fire again while on cooldown", !ecs::fire_weapon(w, shooter, origin, aim, live, &bus));
        ecs::tick_weapons(w, 0.2f);   // 600 rpm -> 0.1s cooldown, cleared
        check("fires again after the cooldown elapses", ecs::fire_weapon(w, shooter, origin, aim, live, &bus));

        // empty the mag, then reload from reserve
        live.ammo_in_mag = 0;
        check("cannot fire on an empty magazine", !ecs::fire_weapon(w, shooter, origin, aim, live, &bus));
        ecs::reload_weapon(live);
        check("reload starts", live.reloading > 0.0f);
        ecs::tick_weapons(w, rifle.reload_time + 0.1f);
        check("reload refills the magazine from reserve (mag 5, reserve 5)",
              live.ammo_in_mag == 5 && live.reserve == 5);

        // projectile weapon: spawns a projectile that travels + hits
        w.get<ecs::AttributeSet>(target).set("Health", 100.0f);
        ecs::WeaponState pw; pw.weapon_id = "g12_rocket"; pw.ammo_in_mag = 1;
        const ecs::Entity gunner = w.create();
        w.add<ecs::Transform>(gunner, ecs::Transform{});
        w.add<ecs::WeaponState>(gunner, pw);
        auto& plive = w.get<ecs::WeaponState>(gunner);
        ecs::fire_weapon(w, gunner, origin, aim, plive, &bus);
        check("projectile weapon spawns a projectile", w.count<ecs::Projectile>() == 1);
        ecs::tick_projectiles(w, 0.5f, &bus);   // 20 m/s * 0.5 = 10 m -> reaches the target
        check("projectile travels + hits + applies damage + despawns (100 -> 40)",
              w.get<ecs::AttributeSet>(target).get("Health") == 40.0f && w.count<ecs::Projectile>() == 0);

        // authorable + serialize
        ecs::register_weapon_components();
        const ecs::AuthorableComponent* wc = ecs::find_authorable("Weapon");
        check("Weapon is authorable", wc && wc->serialize && wc->deserialize);
        if (wc) {
            ecs::WeaponState src; src.weapon_id = "g12_rifle"; src.ammo_in_mag = 3; src.reserve = 7;
            const auto b = ecs::serialize_authorable(*wc, &src);
            ecs::WeaponState dst; ecs::deserialize_authorable(*wc, &dst, b.data(), b.size());
            check("Weapon serialize round-trips",
                  dst.weapon_id == "g12_rifle" && dst.ammo_in_mag == 3 && dst.reserve == 7);
        }
    }

    // ---- C3: guns that feel like guns ----
    {
        const char* data =
            "weapon c3_ar\n  name Test Rifle\n  fire auto\n  damage 10 kinetic\n  rpm 600\n  magazine 30\n"
            "  spread 0.5\n  bloom 0.4 3 6\n  recoil 1.0 0.5\n  aim 0.7 0.25 0.2\n  range 100\n  tracer 3\n  impact 0.1\n  flash\nend\n"
            "item c3_ammo\n  name Rounds\n  max_stack 99\nend\n"
            "weapon c3_burst\n  fire burst\n  burst 3\n  rpm 900\n  magazine 7\nend\n";
        const int n = ecs::register_items_text(data);
        const ecs::WeaponDef* ar = ecs::find_weapon("c3_ar");
        check("a .items file registers weapons beside items (3 definitions)", n == 3 && ar && ecs::find_item("c3_ammo"));
        check("every weapon field is read from the file",
              ar && ar->name == "Test Rifle" && ar->fire_mode == ecs::FireMode::Auto && ar->damage == 10.0f && ar->mag_size == 30 &&
                  ar->spread_deg == 0.5f && ar->spread_bloom == 0.4f && ar->spread_max == 3.0f && ar->bloom_recovery == 6.0f &&
                  ar->recoil_pitch == 1.0f && ar->recoil_yaw == 0.5f && ar->ads_fov_scale == 0.7f && ar->ads_spread == 0.25f &&
                  ar->ads_time == 0.2f && ar->tracer_every == 3 && ar->impact_size == 0.1f && ar->muzzle_flash);
        check("an item block after a weapon block is still an item, not swallowed", ecs::find_item("c3_ammo") != nullptr);

        ecs::World w;
        ecs::GameplayEventBus bus;
        const ecs::Entity me = w.create();
        w.add<ecs::Transform>(me, ecs::Transform{});
        ecs::WeaponState ws; ws.weapon_id = "c3_ar"; ws.ammo_in_mag = 30;
        w.add<ecs::WeaponState>(me, ws);
        auto& s = w.get<ecs::WeaponState>(me);
        const glm::vec3 o(0, 1, 0), fwd(0, 0, 1);

        // Spread blooms with each shot, up to its maximum, and settles back.
        const float rest = ecs::current_spread(*ar, s);
        for (int i = 0; i < 20; ++i) { ecs::fire_weapon(w, me, o, fwd, s, &bus, 10 + i); s.cooldown = 0.0f; }
        const float hot = ecs::current_spread(*ar, s);
        ecs::tick_weapons(w, 1.0f);
        check("spread blooms while firing (0.5 -> 3.0 degrees, no further) and settles back",
              rest == 0.5f && std::fabs(hot - 3.0f) < 1e-4f && ecs::current_spread(*ar, s) == 0.5f);

        // Recoil goes to the view, once.
        float kp = 0, ky = 0;
        ecs::take_recoil(s, kp, ky);
        float kp2 = 0, ky2 = 0;
        ecs::take_recoil(s, kp2, ky2);
        check("twenty rounds kick the view up 20 degrees, with some sideways, and taking it clears it",
              std::fabs(kp - 20.0f) < 1e-3f && std::fabs(ky) > 1e-4f && std::fabs(ky) < 10.0f && kp2 == 0.0f && ky2 == 0.0f);

        // Aiming down the sights narrows the spread and takes time.
        s.aiming = true;
        ecs::tick_weapons(w, 0.1f);
        const float half = s.aim;
        ecs::tick_weapons(w, 0.2f);
        check("raising the sights takes the aim time, and narrows the spread to a quarter",
              std::fabs(half - 0.5f) < 1e-4f && s.aim == 1.0f && std::fabs(ecs::current_spread(*ar, s) - 0.125f) < 1e-5f);
        s.reloading = 1.0f;
        ecs::tick_weapons(w, 0.25f);
        check("the sights come down to reload", s.aim == 0.0f);
        s.reloading = 0.0f; s.aiming = false;

        // A wall stops a hitscan round before the target behind it.
        const ecs::Entity target = w.create();
        ecs::Transform tt; tt.position = glm::vec3(0, 0, 20);
        w.add<ecs::Transform>(target, tt);
        w.add<ecs::CombatActor>(target, ecs::CombatActor{});
        w.add<ecs::AttributeSet>(target, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(target).define("Health", 100, 0, 100);
        const ecs::WorldRayFn wall = [](const glm::vec3& ro, const glm::vec3& d, float max_t, float& tt2, glm::vec3& nrm) {
            if (d.z <= 0.0f) return false;
            tt2 = (10.0f - ro.z) / d.z;   // a wall across z = 10
            nrm = glm::vec3(0, 0, -1);
            return tt2 >= 0.0f && tt2 <= max_t;
        };
        std::vector<ecs::ShotTrace> shots;
        s.bloom = 0.0f; s.cooldown = 0.0f;
        ecs::fire_weapon(w, me, o, fwd, s, &bus, 99, &wall, &shots);
        check("a wall stops the round: no damage behind it, the shot ends on the wall with the wall's normal",
              shots.size() == 1 && shots[0].hit == ecs::ShotTrace::Hit::World && std::fabs(shots[0].end.z - 10.0f) < 0.05f &&
                  shots[0].normal.z == -1.0f && w.get<ecs::AttributeSet>(target).get("Health") == 100.0f);
        shots.clear(); s.cooldown = 0.0f;
        ecs::fire_weapon(w, me, o, fwd, s, &bus, 99, nullptr, &shots);
        check("without the wall the same round hits the target and says so",
              shots.size() == 1 && shots[0].hit == ecs::ShotTrace::Hit::Actor && shots[0].target == target &&
                  w.get<ecs::AttributeSet>(target).get("Health") == 90.0f);

        // A tracer every third round.
        int tracers = 0;
        for (int i = 0; i < 9; ++i) {
            shots.clear(); s.cooldown = 0.0f;
            ecs::fire_weapon(w, me, o, glm::vec3(0, 1, 0), s, &bus, 200 + i, nullptr, &shots);
            tracers += !shots.empty() && shots[0].tracer ? 1 : 0;
        }
        check("every third round is a tracer", tracers == 3);

        // Trigger modes.
        ecs::WeaponState b; b.weapon_id = "c3_burst"; b.ammo_in_mag = 7;
        int fired = 0;
        fired += ecs::trigger_weapon(w, me, o, fwd, b, true, true);   // press: a burst starts
        for (int f = 0; f < 20; ++f) { ecs::tick_weapons(w, 0.0f); b.cooldown = 0.0f; fired += ecs::trigger_weapon(w, me, o, fwd, b, false, false); }
        check("one press of a burst weapon fires exactly its burst, even with the button let go", fired == 3 && b.ammo_in_mag == 4);
        ecs::WeaponState semi; semi.weapon_id = "g12_rifle"; semi.ammo_in_mag = 5;
        int sf = ecs::trigger_weapon(w, me, o, fwd, semi, true, true);
        semi.cooldown = 0.0f;
        sf += ecs::trigger_weapon(w, me, o, fwd, semi, false, true);   // held, no new press
        check("a semi-automatic fires once per press, not while held", sf == 1);
        s.ammo_in_mag = 30;   // the earlier checks emptied it
        int af = 0;
        for (int f = 0; f < 5; ++f) { s.cooldown = 0.0f; af += ecs::trigger_weapon(w, me, o, glm::vec3(0, 1, 0), s, false, true); }
        check("an automatic keeps firing while held", af == 5);

        // Ammunition carried as an item: a reload takes rounds out of the inventory.
        ecs::register_items_text("item c3_rounds\n  max_stack 999\nend\nweapon c3_pistol\n  magazine 8\n  reload 0.5\n  ammo c3_rounds\nend\n");
        ecs::Inventory inv;
        ecs::ItemInstance rounds; rounds.def_id = "c3_rounds"; rounds.quantity = 20;
        ecs::inventory_add(inv, rounds);
        ecs::WeaponState p; p.weapon_id = "c3_pistol"; p.ammo_in_mag = 0;
        w.add<ecs::WeaponState>(me, p);
        auto& pl = w.get<ecs::WeaponState>(me);
        ecs::sync_ammo_with_inventory(pl, inv);
        const bool reads = pl.reserve == 20;
        ecs::reload_weapon(pl);
        ecs::tick_weapons(w, 0.6f);
        ecs::sync_ammo_with_inventory(pl, inv);
        check("ammunition is an inventory item: the reserve is what is carried, and a reload takes from it",
              reads && pl.ammo_in_mag == 8 && pl.reserve == 12 && ecs::inventory_count(inv, "c3_rounds") == 12);
        ecs::inventory_add(inv, rounds);
        ecs::sync_ammo_with_inventory(pl, inv);
        check("rounds picked up count at once", pl.reserve == 32);

        // The Items editor: a .items file as a document, written back.
        {
            const std::string src = std::string("# My items\n# for the test\n\n") + ecs::item_file_template() +
                "\nweapon shotgun\n  fire semi\n  damage 9 pellet\n  rpm 70\n  magazine 6\n  pellets 8\n  reload 0.55\n"
                "  spread 6.5\n  tracer 1 0.2 0.9 1\n  projectile 120\n  ammo shells\nend\n";
            const ecs::ItemsDoc d1 = ecs::parse_items_doc(src);
            const std::string t1 = ecs::items_doc_to_text(d1);
            const ecs::ItemsDoc d2 = ecs::parse_items_doc(t1);
            const std::string t2 = ecs::items_doc_to_text(d2);
            check("items doc: blocks in file order, comments above them kept",
                  d1.entries.size() == 5 && !d1.entries[0].is_weapon() && d1.entries[1].is_weapon() &&
                  d1.entries[2].is_ability() && d1.entries[3].id() == "my_sword" && d1.entries[4].id() == "shotgun" &&
                  d1.header == "# My items\n# for the test\n\n# Item definitions (.items). One 'item <id>' block per item; the editor\n"
                               "# loads every .items file under the project at startup / on reload.\n");
            check("items doc: written back and read again is the same file", t1 == t2);
            const auto& r = d2.entries[1].weapon;
            const auto& sg = d2.entries[4].weapon;
            const auto& sw = d2.entries[3].item;
            const auto& po = d2.entries[0].item;
            check("items doc: every weapon setting survives",
                  r.fire_mode == ecs::FireMode::Auto && r.damage == 20.0f && r.rpm == 600.0f && r.mag_size == 30 &&
                  r.spread_deg == 0.6f && r.spread_bloom == 0.35f && r.spread_max == 5.0f && r.recoil_pitch == 0.8f &&
                  r.recoil_yaw == 0.4f && r.ads_fov_scale == 0.75f && r.ads_spread == 0.3f && r.tracer_every == 3 &&
                  r.impact_size == 0.12f && r.muzzle_flash &&
                  sg.pellets == 8 && sg.reload_time == 0.55f && sg.damage_type == "pellet" && sg.projectile_speed == 120.0f &&
                  sg.tracer_color == glm::vec3(0.2f, 0.9f, 1.0f) && sg.ammo_item == "shells" && sg.name == "shotgun");
            check("items doc: every item setting survives",
                  po.kind == ecs::ItemKind::Consumable && po.max_stack == 10 && po.weight == 0.1f && po.on_use.size() == 1 &&
                  po.on_use[0].modifiers[0].attribute == "Health" && po.on_use[0].modifiers[0].magnitude == 25.0f &&
                  sw.kind == ecs::ItemKind::Weapon && sw.equip_slot == "weapon" && sw.modifiers.size() == 1 &&
                  sw.affix_pool.size() == 1 && sw.affix_pool[0].name == "of Power" && sw.affix_pool[0].max_roll == 4.0f);
        }
    }

    // G13: vehicles & racing — arcade drive model + checkpoints/laps/ranking.
    {
        ecs::VehicleDef car; car.id = "g13_car"; car.name = "Car"; car.max_speed = 40.0f; car.accel = 20.0f;
        car.turn_rate = 90.0f; car.drag = 5.0f; car.boost_mult = 1.5f; car.boost_accel = 30.0f;
        ecs::register_vehicle(car);

        ecs::World w; ecs::GameplayEventBus bus;
        const ecs::Entity v = w.create();
        w.add<ecs::Transform>(v, ecs::Transform{});
        w.add<ecs::Vehicle>(v, ecs::Vehicle{"g13_car", 0, 0, 100, 100});
        auto& veh = w.get<ecs::Vehicle>(v);

        // accelerate straight (+Z at heading 0)
        for (int i = 0; i < 60; ++i) ecs::drive_vehicle(w, v, {1.0f, 0.0f, false, false}, 1.0f / 60.0f);
        check("throttle accelerates + moves the vehicle forward (+Z)",
              veh.speed > 15.0f && w.get<ecs::Transform>(v).position.z > 5.0f &&
              std::abs(w.get<ecs::Transform>(v).position.x) < 0.01f);
        const float cruise = veh.speed;

        // boost goes faster + drains the nitro
        ecs::drive_vehicle(w, v, {1.0f, 0.0f, false, true}, 0.1f);
        check("boost accelerates harder + consumes nitro", veh.speed > cruise && veh.boost < 100.0f);

        // steering turns the heading (only while moving)
        const float h0 = veh.heading;
        ecs::drive_vehicle(w, v, {1.0f, 1.0f, false, false}, 0.2f);
        check("steering changes heading while moving", veh.heading != h0);

        // braking sheds speed
        const float pre_brake = veh.speed;
        ecs::drive_vehicle(w, v, {0.0f, 0.0f, true, false}, 0.5f);
        check("braking reduces speed", veh.speed < pre_brake);

        // a parked vehicle can't steer
        ecs::Vehicle parked{"g13_car", 0, 0, 100, 100};
        const ecs::Entity pk = w.create();
        w.add<ecs::Transform>(pk, ecs::Transform{});
        w.add<ecs::Vehicle>(pk, parked);
        ecs::drive_vehicle(w, pk, {0.0f, 1.0f, false, false}, 0.5f);
        check("a stationary vehicle does not turn", w.get<ecs::Vehicle>(pk).heading == 0.0f);

        // racing: a 2-checkpoint / 2-lap track
        ecs::RaceTrack trk; trk.id = "g13_track"; trk.laps = 2;
        trk.checkpoints = { {{0, 0, 10}, 3}, {{0, 0, 0}, 3} };
        ecs::register_track(trk);
        const ecs::Entity racer = w.create();
        w.add<ecs::Transform>(racer, ecs::Transform{});
        w.add<ecs::RaceProgress>(racer, ecs::RaceProgress{"g13_track"});
        int laps = 0; bool finished = false;
        bus.subscribe("race.lap", [&](const ecs::GameplayEvent&) { ++laps; });
        bus.subscribe("race.finish", [&](const ecs::GameplayEvent&) { finished = true; });

        auto teleport = [&](float z) { w.get<ecs::Transform>(racer).position = glm::vec3(0, 0, z); ecs::tick_race(w, 0.1f, &bus); bus.flush(); };
        teleport(10.0f);   // checkpoint 0
        check("crossing a checkpoint advances progress",
              w.get<ecs::RaceProgress>(racer).next_checkpoint == 1);
        teleport(0.0f);    // checkpoint 1 -> lap 1
        teleport(10.0f); teleport(0.0f);   // lap 2 -> finish
        check("completing laps records them + finishes the race",
              laps == 2 && finished && w.get<ecs::RaceProgress>(racer).finished &&
              w.get<ecs::RaceProgress>(racer).best_lap > 0.0f);

        // ranking: a racer further along outranks one behind
        const ecs::Entity behind = w.create();
        w.add<ecs::Transform>(behind, ecs::Transform{});
        w.add<ecs::RaceProgress>(behind, ecs::RaceProgress{"g13_track"});   // lap 0
        const std::vector<ecs::Entity> field = {racer, behind};
        check("race_rank orders by progress", ecs::race_rank(w, racer, field) == 1 && ecs::race_rank(w, behind, field) == 2);

        // authorable serialize
        ecs::register_vehicle_components();
        const ecs::AuthorableComponent* vc = ecs::find_authorable("Vehicle");
        check("Vehicle is authorable", vc && vc->serialize);
        if (vc) {
            ecs::Vehicle src{"g13_car", 10, 1.5f, 40, 80};
            const auto b = ecs::serialize_authorable(*vc, &src);
            ecs::Vehicle dst; ecs::deserialize_authorable(*vc, &dst, b.data(), b.size());
            check("Vehicle serialize round-trips (id + max_boost, runtime reset)",
                  dst.vehicle_id == "g13_car" && dst.max_boost == 80.0f && dst.speed == 0.0f && dst.boost == 80.0f);
        }
    }

    // G14: building & automation — extractor -> conveyor -> machine, power-gated.
    {
        ecs::register_item(ecs::ItemDef{"g14_ore", "Ore", ecs::ItemKind::Material, 0, 999, 0.0f, 0.0f, "", {}, {}, {}, {}, ""});
        ecs::register_item(ecs::ItemDef{"g14_ingot", "Ingot", ecs::ItemKind::Material, 0, 999, 0.0f, 0.0f, "", {}, {}, {}, {}, ""});
        ecs::register_item(ecs::ItemDef{"g14_plate", "Plate", ecs::ItemKind::Material, 0, 999, 0.0f, 0.0f, "", {}, {}, {}, {}, ""});
        ecs::register_machine_recipe({"g14_smelt", {{"g14_ore", 1}}, {{"g14_ingot", 1}}, 2.0f});
        ecs::register_buildable({"g14_smelter", "Smelter", {{"g14_plate", 3}}});

        ecs::World w; ecs::GameplayEventBus bus;

        // extractor mines ore into its own buffer
        const ecs::Entity miner = w.create();
        w.add<ecs::Inventory>(miner, ecs::Inventory{});
        w.add<ecs::Extractor>(miner, ecs::Extractor{"g14_ore", 2.0f, 0.0f});
        ecs::tick_factory(w, 1.0f, &bus);
        check("extractor mines items into its buffer (2 ore/s)",
              ecs::inventory_count(w.get<ecs::Inventory>(miner), "g14_ore") == 2);

        // conveyor moves ore from the miner to a smelter
        const ecs::Entity smelter = w.create();
        w.add<ecs::Inventory>(smelter, ecs::Inventory{});
        w.add<ecs::Machine>(smelter, ecs::Machine{"g14_smelt", 0.0f, false, 10});
        const ecs::Entity belt = w.create();
        w.add<ecs::Conveyor>(belt, ecs::Conveyor{static_cast<uint32_t>(miner), static_cast<uint32_t>(smelter), "g14_ore", 5.0f, 0.0f});
        ecs::tick_factory(w, 1.0f, &bus);   // miner +2 (now 4 total mined, 2 moved), belt moves 2 available
        check("conveyor moves items between building buffers",
              ecs::inventory_count(w.get<ecs::Inventory>(smelter), "g14_ore") >= 1);

        // machine won't run without power
        ecs::tick_factory(w, 3.0f, &bus);
        check("machine idle while unpowered (power < 0)", !ecs::factory_powered(w) &&
              ecs::inventory_count(w.get<ecs::Inventory>(smelter), "g14_ingot") == 0);

        // add a generator -> powered -> machine smelts ore into ingots
        const ecs::Entity gen = w.create();
        w.add<ecs::Generator>(gen, ecs::Generator{20});
        check("adding a generator powers the factory", ecs::factory_powered(w));
        ecs::tick_factory(w, 2.5f, &bus);   // consumes 1 ore, 2s recipe -> 1 ingot
        check("powered machine consumes inputs + produces outputs",
              ecs::inventory_count(w.get<ecs::Inventory>(smelter), "g14_ingot") >= 1);

        // build cost: place a smelter from the builder's inventory
        const ecs::Entity builder = w.create();
        w.add<ecs::Inventory>(builder, ecs::Inventory{});
        ecs::inventory_add(w.get<ecs::Inventory>(builder), ecs::ItemInstance{"g14_plate", 5, 0, {}});
        check("can build when the cost is affordable (5 plates >= 3)", ecs::can_build(w, builder, "g14_smelter"));
        const ecs::Entity placed = ecs::place_building(w, builder, "g14_smelter", glm::vec3(0));
        check("place_building pays the cost + creates a building",
              placed != ecs::null_entity && w.has<ecs::Building>(placed) && w.has<ecs::Inventory>(placed) &&
              ecs::inventory_count(w.get<ecs::Inventory>(builder), "g14_plate") == 2);
        check("cannot build once resources run out", !ecs::can_build(w, builder, "g14_smelter"));

        // authorable serialize
        ecs::register_building_components();
        const ecs::AuthorableComponent* mc = ecs::find_authorable("Machine");
        check("Machine is authorable", mc && mc->serialize);
        if (mc) {
            ecs::Machine src{"g14_smelt", 1.0f, true, 25};
            const auto b = ecs::serialize_authorable(*mc, &src);
            ecs::Machine dst; ecs::deserialize_authorable(*mc, &dst, b.data(), b.size());
            check("Machine serialize round-trips (recipe + power, runtime reset)",
                  dst.recipe_id == "g14_smelt" && dst.power_use == 25 && dst.progress == 0.0f && !dst.crafting);
        }
    }

    // G15: survival & horror — needs decay, sanity in dark/light/fear, flashlight.
    {
        ecs::World w; ecs::GameplayEventBus bus;

        // needs: hunger drains + fires a critical event at the threshold
        const ecs::Entity survivor = w.create();
        w.add<ecs::Transform>(survivor, ecs::Transform{});
        w.add<ecs::AttributeSet>(survivor, ecs::AttributeSet{});
        w.get<ecs::AttributeSet>(survivor).define("Hunger", 100, 0, 100);
        ecs::Needs needs; needs.needs.push_back({"Hunger", 10.0f, 15.0f, "hunger.critical", false});
        w.add<ecs::Needs>(survivor, needs);
        std::string crit;
        bus.subscribe("hunger.critical", [&](const ecs::GameplayEvent&) { crit = "fired"; });

        ecs::tick_survival(w, 5.0f, &bus);   // Hunger 100 -> 50
        check("needs decay over time (Hunger 100 -> 50)", w.get<ecs::AttributeSet>(survivor).get("Hunger") == 50.0f);
        ecs::tick_survival(w, 4.0f, &bus); bus.flush();   // -> 10, crosses critical 15
        check("need fires its critical event at the threshold",
              w.get<ecs::AttributeSet>(survivor).get("Hunger") == 10.0f && crit == "fired");
        // eat: restore hunger via an instant effect (as a food item would)
        ecs::apply_effect(w, survivor, ecs::make_instant("Eat", "Hunger", +60.0f));
        check("consuming food restores the need (10 -> 70)", w.get<ecs::AttributeSet>(survivor).get("Hunger") == 70.0f);

        // sanity: drains in darkness, regenerates in light
        w.add<ecs::Sanity>(survivor, ecs::Sanity{100, 100, 10.0f, 5.0f, false});
        ecs::tick_survival(w, 2.0f, &bus);   // no light -> dark drain 10/s * 2 = 20
        check("sanity drains in darkness (100 -> 80)", w.get<ecs::Sanity>(survivor).value == 80.0f);
        const ecs::Entity lamp = w.create();
        w.add<ecs::Transform>(lamp, ecs::Transform{});         // at origin, near the survivor
        w.add<ecs::LightSource>(lamp, ecs::LightSource{10.0f});
        ecs::tick_survival(w, 2.0f, &bus);   // in light -> regen 5/s * 2 = 10
        check("sanity regenerates in light (80 -> 90)", w.get<ecs::Sanity>(survivor).value == 90.0f);

        // fear source drains sanity even in light
        const ecs::Entity ghost = w.create();
        w.add<ecs::Transform>(ghost, ecs::Transform{});
        w.add<ecs::FearSource>(ghost, ecs::FearSource{10.0f, 30.0f});
        int low = 0;
        bus.subscribe("sanity.low", [&](const ecs::GameplayEvent&) { ++low; });
        ecs::tick_survival(w, 3.0f, &bus); bus.flush();   // fear 30/s * 3 = 90 -> below 20 -> sanity.low
        check("a fear source drains sanity + fires sanity.low",
              w.get<ecs::Sanity>(survivor).value == 0.0f && low == 1);

        // flashlight: on -> battery drains; provides its own light; toggle
        const ecs::Entity explorer = w.create();
        w.add<ecs::Transform>(explorer, ecs::Transform{glm::vec3(100, 0, 0)});   // far from any light
        w.add<ecs::Sanity>(explorer, ecs::Sanity{100, 100, 10.0f, 5.0f, false});
        w.add<ecs::Flashlight>(explorer, ecs::Flashlight{20.0f, 100.0f, 10.0f, true, 6.0f});
        ecs::tick_survival(w, 1.0f, &bus);   // flashlight on -> in own light -> regen; battery 20 -> 10
        check("flashlight lights the holder (sanity regens) + drains battery",
              w.get<ecs::Sanity>(explorer).value == 100.0f && w.get<ecs::Flashlight>(explorer).battery == 10.0f);
        ecs::tick_survival(w, 2.0f, &bus);   // battery hits 0 -> off -> now in the dark
        check("flashlight dies when the battery empties",
              w.get<ecs::Flashlight>(explorer).battery == 0.0f && !w.get<ecs::Flashlight>(explorer).on);
        check("toggle_flashlight refuses a dead battery", !ecs::toggle_flashlight(w, explorer));

        // authorable serialize
        ecs::register_survival_components();
        const ecs::AuthorableComponent* sc = ecs::find_authorable("Sanity");
        check("Sanity is authorable", sc && sc->serialize);
        if (sc) {
            ecs::Sanity src{60, 120, 8, 4, false};
            const auto b = ecs::serialize_authorable(*sc, &src);
            ecs::Sanity dst; ecs::deserialize_authorable(*sc, &dst, b.data(), b.size());
            check("Sanity serialize round-trips",
                  dst.value == 60.0f && dst.max == 120.0f && dst.dark_drain == 8.0f && dst.light_regen == 4.0f);
        }
    }

    // G16: stealth & detection — vision cones, awareness states, hearing.
    {
        ecs::World w; ecs::GameplayEventBus bus;
        // a guard at origin facing +Z (identity rotation)
        const ecs::Entity guard = w.create();
        w.add<ecs::Transform>(guard, ecs::Transform{});
        w.add<ecs::VisionCone>(guard, ecs::VisionCone{90.0f, 15.0f});
        w.add<ecs::Awareness>(guard, ecs::Awareness{0, ecs::Alertness::Unaware, 0xFFFFFFFFu, 1.0f, 0.5f});
        w.add<ecs::Hearing>(guard, ecs::Hearing{20.0f});

        // vision geometry
        check("target in front + in range is visible",
              ecs::can_see(glm::vec3(0), glm::vec3(0, 0, 1), 90.0f, 15.0f, glm::vec3(0, 0, 5)));
        check("target behind is not visible",
              !ecs::can_see(glm::vec3(0), glm::vec3(0, 0, 1), 90.0f, 15.0f, glm::vec3(0, 0, -5)));
        check("target beyond range is not visible",
              !ecs::can_see(glm::vec3(0), glm::vec3(0, 0, 1), 90.0f, 15.0f, glm::vec3(0, 0, 40)));

        // a normal-detectability intruder in the cone -> awareness rises to Alert
        const ecs::Entity intruder = w.create();
        w.add<ecs::Transform>(intruder, ecs::Transform{glm::vec3(0, 0, 4)});
        w.add<ecs::Stealth>(intruder, ecs::Stealth{1.0f});
        std::string ev;
        bus.subscribe("stealth.suspicious", [&](const ecs::GameplayEvent&) { ev = "suspicious"; });
        bus.subscribe("stealth.alert", [&](const ecs::GameplayEvent& e) { ev = "alert"; });

        ecs::tick_stealth(w, 0.2f, &bus); bus.flush();
        check("seeing an intruder raises awareness + targets them",
              w.get<ecs::Awareness>(guard).level > 0.0f && w.get<ecs::Awareness>(guard).target == static_cast<uint32_t>(intruder));
        for (int i = 0; i < 20; ++i) { ecs::tick_stealth(w, 0.2f, &bus); bus.flush(); }
        check("sustained sight escalates to ALERT (+ event)",
              w.get<ecs::Awareness>(guard).state == ecs::Alertness::Alert && ev == "alert");

        // intruder leaves the cone -> awareness decays back down
        w.get<ecs::Transform>(intruder).position = glm::vec3(0, 0, -10);   // behind the guard
        for (int i = 0; i < 20; ++i) ecs::tick_stealth(w, 0.2f, &bus);
        check("losing sight decays awareness back to unaware",
              w.get<ecs::Awareness>(guard).state == ecs::Alertness::Unaware);

        // a crouched (low-detectability) target raises awareness slower than a normal one
        auto rise_for = [&](float det) {
            ecs::World ww; const ecs::Entity g = ww.create();
            ww.add<ecs::Transform>(g, ecs::Transform{});
            ww.add<ecs::VisionCone>(g, ecs::VisionCone{90.0f, 15.0f});
            ww.add<ecs::Awareness>(g, ecs::Awareness{0, ecs::Alertness::Unaware, 0xFFFFFFFFu, 1.0f, 0.5f});
            const ecs::Entity tg = ww.create();
            ww.add<ecs::Transform>(tg, ecs::Transform{glm::vec3(0, 0, 4)});
            ww.add<ecs::Stealth>(tg, ecs::Stealth{det});
            ecs::tick_stealth(ww, 0.5f, nullptr);
            return ww.get<ecs::Awareness>(g).level;
        };
        check("lower detectability raises awareness more slowly", rise_for(0.25f) < rise_for(1.0f));

        // hearing: a noise near the guard bumps its awareness even if it can't see
        ecs::Awareness& ga = w.get<ecs::Awareness>(guard);
        ga.level = 0.0f; ga.state = ecs::Alertness::Unaware;
        int heard = 0;
        bus.subscribe("stealth.noise", [&](const ecs::GameplayEvent&) { ++heard; });
        ecs::emit_noise(w, glm::vec3(0, 0, 2), 25.0f, &bus);   // within the guard's hearing radius
        bus.flush();
        check("a nearby noise is heard + bumps awareness", heard == 1 && ga.level > 0.0f);
        ecs::emit_noise(w, glm::vec3(0, 0, 500), 25.0f, &bus);   // far away
        bus.flush();
        check("a distant noise is not heard", heard == 1);

        // authorable serialize
        ecs::register_stealth_components();
        const ecs::AuthorableComponent* vc = ecs::find_authorable("Vision Cone");
        check("Vision Cone is authorable", vc && vc->serialize);
        if (vc) {
            ecs::VisionCone src{110.0f, 22.0f};
            const auto b = ecs::serialize_authorable(*vc, &src);
            ecs::VisionCone dst; ecs::deserialize_authorable(*vc, &dst, b.data(), b.size());
            check("Vision Cone serialize round-trips", dst.fov_deg == 110.0f && dst.range == 22.0f);
        }
    }

    // Scene logic graph — On Start / On Event nodes drive Emit / Set Flag actions.
    {
        ecs::World w; ecs::GameplayEventBus bus;
        const ecs::Entity world_ent = w.create();
        w.add<ecs::WorldFlags>(world_ent, ecs::WorldFlags{});

        ecs::LogicGraph g;
        // On Start -> Set Flag "started"=1
        const int start = g.add_node(ecs::LogicNodeKind::OnStart, 0, 0);
        const int f1 = g.add_node(ecs::LogicNodeKind::SetFlag, 0, 0); g.find(f1)->param = "started"; g.find(f1)->param2 = "1";
        g.link(start, f1);
        // On Event "ping" -> Emit "pong" + Set Flag "pinged"=1
        const int onping = g.add_node(ecs::LogicNodeKind::OnEvent, 0, 0); g.find(onping)->param = "ping";
        const int emit = g.add_node(ecs::LogicNodeKind::EmitEvent, 0, 0); g.find(emit)->param = "pong";
        const int f2 = g.add_node(ecs::LogicNodeKind::SetFlag, 0, 0); g.find(f2)->param = "pinged"; g.find(f2)->param2 = "1";
        g.link(onping, emit); g.link(onping, f2);

        int pong = 0;
        bus.subscribe("pong", [&](const ecs::GameplayEvent&) { ++pong; });

        ecs::LogicRuntime rt;
        rt.start(g, w, bus, world_ent);
        check("On Start action runs when the graph starts (flag set)",
              ecs::world_flag(w.get<ecs::WorldFlags>(world_ent), "started") == 1);

        { ecs::GameplayEvent e; e.name = "ping"; bus.publish(e); }
        bus.flush();   // dispatch "ping" -> OnEvent fires -> emits "pong" (deferred) + sets flag
        check("On Event action runs on a matching bus event (flag set)",
              ecs::world_flag(w.get<ecs::WorldFlags>(world_ent), "pinged") == 1);
        bus.flush();   // dispatch the deferred "pong"
        check("Emit Event action publishes a new event", pong == 1);

        // On Key node fired by the editor
        const int onkey = g.add_node(ecs::LogicNodeKind::OnKey, 0, 0); g.find(onkey)->param = "70";   // 'F'
        const int f3 = g.add_node(ecs::LogicNodeKind::SetFlag, 0, 0); g.find(f3)->param = "keyed"; g.find(f3)->param2 = "1";
        g.link(onkey, f3);
        rt.stop(); rt.start(g, w, bus, world_ent);   // re-arm with the new nodes
        rt.on_key(70);
        check("On Key action runs when the editor feeds the key",
              ecs::world_flag(w.get<ecs::WorldFlags>(world_ent), "keyed") == 1);
        rt.stop();

        // ---- depth: action chaining, Branch, Clear/Toggle Flag, On Tick, On Flag ----
        {
            ecs::World w2; ecs::GameplayEventBus bus2;
            const ecs::Entity fe = w2.create();
            w2.add<ecs::WorldFlags>(fe, ecs::WorldFlags{});
            auto flag = [&](const char* k) { return ecs::world_flag(w2.get<ecs::WorldFlags>(fe), k); };

            ecs::LogicGraph cg;
            // On Start -> Set Flag "a"=2 -> Toggle Flag "b"  (an action chains from an action)
            const int s  = cg.add_node(ecs::LogicNodeKind::OnStart, 0, 0);
            const int sa = cg.add_node(ecs::LogicNodeKind::SetFlag, 0, 0);    cg.find(sa)->param = "a"; cg.find(sa)->param2 = "2";
            const int tb = cg.add_node(ecs::LogicNodeKind::ToggleFlag, 0, 0); cg.find(tb)->param = "b";
            cg.link(s, sa); cg.link(sa, tb);
            // On Event "go" -> Branch(a>=2 TRUE) -> Set Flag "passed"=1
            const int og = cg.add_node(ecs::LogicNodeKind::OnEvent, 0, 0);    cg.find(og)->param = "go";
            const int br = cg.add_node(ecs::LogicNodeKind::Branch, 0, 0);     cg.find(br)->param = "a"; cg.find(br)->param2 = ">=2";
            const int sp = cg.add_node(ecs::LogicNodeKind::SetFlag, 0, 0);    cg.find(sp)->param = "passed"; cg.find(sp)->param2 = "1";
            cg.link(og, br); cg.link(br, sp);
            // On Event "no" -> Branch(a>=99 FALSE) -> Set Flag "nope"=1
            const int on2 = cg.add_node(ecs::LogicNodeKind::OnEvent, 0, 0);   cg.find(on2)->param = "no";
            const int br2 = cg.add_node(ecs::LogicNodeKind::Branch, 0, 0);    cg.find(br2)->param = "a"; cg.find(br2)->param2 = ">=99";
            const int sn = cg.add_node(ecs::LogicNodeKind::SetFlag, 0, 0);    cg.find(sn)->param = "nope"; cg.find(sn)->param2 = "1";
            cg.link(on2, br2); cg.link(br2, sn);

            ecs::LogicRuntime rt2;
            rt2.start(cg, w2, bus2, fe);
            check("action chains from an action (Set Flag -> Toggle Flag)", flag("a") == 2 && flag("b") == 1);
            { ecs::GameplayEvent e; e.name = "go"; bus2.publish(e); } bus2.flush();
            check("Branch propagates when its condition is TRUE",  flag("passed") == 1);
            { ecs::GameplayEvent e; e.name = "no"; bus2.publish(e); } bus2.flush();
            check("Branch blocks when its condition is FALSE",     flag("nope") == 0);

            // Clear Flag zeroes a flag.
            ecs::LogicGraph cg2;
            const int s2 = cg2.add_node(ecs::LogicNodeKind::OnStart, 0, 0);
            const int cf = cg2.add_node(ecs::LogicNodeKind::ClearFlag, 0, 0); cg2.find(cf)->param = "a";
            cg2.link(s2, cf);
            rt2.stop(); rt2.start(cg2, w2, bus2, fe);
            check("Clear Flag zeroes a flag", flag("a") == 0);
            rt2.stop();

            // On Tick fires only after its interval elapses.
            ecs::World w3; ecs::GameplayEventBus bus3;
            const ecs::Entity fe3 = w3.create(); w3.add<ecs::WorldFlags>(fe3, ecs::WorldFlags{});
            ecs::LogicGraph tg;
            const int ot = tg.add_node(ecs::LogicNodeKind::OnTick, 0, 0);     tg.find(ot)->param = "0.1";
            const int ti = tg.add_node(ecs::LogicNodeKind::ToggleFlag, 0, 0); tg.find(ti)->param = "tick";
            tg.link(ot, ti);
            ecs::LogicRuntime rt3; rt3.start(tg, w3, bus3, fe3);
            rt3.tick(0.05f);
            const bool tick_not_yet = ecs::world_flag(w3.get<ecs::WorldFlags>(fe3), "tick") == 0;
            rt3.tick(0.06f);   // cumulative 0.11 >= 0.1 -> fires once
            const bool tick_fired  = ecs::world_flag(w3.get<ecs::WorldFlags>(fe3), "tick") == 1;
            check("On Tick fires only after its interval elapses", tick_not_yet && tick_fired);
            rt3.stop();

            // On Flag fires on the RISING edge of its condition (no spurious fire at start).
            ecs::World w4; ecs::GameplayEventBus bus4;
            const ecs::Entity fe4 = w4.create(); w4.add<ecs::WorldFlags>(fe4, ecs::WorldFlags{});
            ecs::LogicGraph fg;
            const int of = fg.add_node(ecs::LogicNodeKind::OnFlag, 0, 0);  fg.find(of)->param = "hp"; fg.find(of)->param2 = "<=0";
            const int fx = fg.add_node(ecs::LogicNodeKind::SetFlag, 0, 0); fg.find(fx)->param = "dead"; fg.find(fx)->param2 = "1";
            fg.link(of, fx);
            ecs::LogicRuntime rt4; rt4.start(fg, w4, bus4, fe4);   // hp=0 at start: <=0 already true (baseline, not an edge)
            rt4.tick(0.016f);
            const bool no_spurious = ecs::world_flag(w4.get<ecs::WorldFlags>(fe4), "dead") == 0;
            ecs::set_world_flag(w4.get<ecs::WorldFlags>(fe4), "hp", 5); rt4.tick(0.016f);  // condition false
            ecs::set_world_flag(w4.get<ecs::WorldFlags>(fe4), "hp", 0); rt4.tick(0.016f);  // true again -> edge
            const bool edge_fired = ecs::world_flag(w4.get<ecs::WorldFlags>(fe4), "dead") == 1;
            check("On Flag fires on the rising edge of its condition", no_spurious && edge_fired);
            rt4.stop();

            // Do Once: only the first activation of a run propagates.
            ecs::World w5; ecs::GameplayEventBus bus5;
            const ecs::Entity fe5 = w5.create(); w5.add<ecs::WorldFlags>(fe5, ecs::WorldFlags{});
            ecs::LogicGraph og5;
            const int n_oe  = og5.add_node(ecs::LogicNodeKind::OnEvent, 0, 0);    og5.find(n_oe)->param = "go";
            const int n_do  = og5.add_node(ecs::LogicNodeKind::DoOnce, 0, 0);
            const int n_tog = og5.add_node(ecs::LogicNodeKind::ToggleFlag, 0, 0); og5.find(n_tog)->param = "t";
            og5.link(n_oe, n_do); og5.link(n_do, n_tog);
            ecs::LogicRuntime rt5; rt5.start(og5, w5, bus5, fe5);
            { ecs::GameplayEvent e; e.name = "go"; bus5.publish(e); } bus5.flush();   // 1st -> toggles to 1
            { ecs::GameplayEvent e; e.name = "go"; bus5.publish(e); } bus5.flush();   // 2nd -> Do Once blocks
            check("Do Once fires once then blocks", ecs::world_flag(w5.get<ecs::WorldFlags>(fe5), "t") == 1);
            rt5.stop();

            // Delay: propagates downstream only after the interval elapses (via tick).
            ecs::World w6; ecs::GameplayEventBus bus6;
            const ecs::Entity fe6 = w6.create(); w6.add<ecs::WorldFlags>(fe6, ecs::WorldFlags{});
            ecs::LogicGraph dg6;
            const int n_ds = dg6.add_node(ecs::LogicNodeKind::OnStart, 0, 0);
            const int n_dl = dg6.add_node(ecs::LogicNodeKind::Delay, 0, 0);   dg6.find(n_dl)->param = "0.1";
            const int n_df = dg6.add_node(ecs::LogicNodeKind::SetFlag, 0, 0); dg6.find(n_df)->param = "delayed"; dg6.find(n_df)->param2 = "1";
            dg6.link(n_ds, n_dl); dg6.link(n_dl, n_df);
            ecs::LogicRuntime rt6; rt6.start(dg6, w6, bus6, fe6);   // OnStart hits Delay -> scheduled, not fired
            const bool delay_pending = ecs::world_flag(w6.get<ecs::WorldFlags>(fe6), "delayed") == 0;
            rt6.tick(0.05f);
            const bool delay_notyet  = ecs::world_flag(w6.get<ecs::WorldFlags>(fe6), "delayed") == 0;
            rt6.tick(0.06f);   // 0.11 >= 0.1 -> elapses, propagates
            const bool delay_fired   = ecs::world_flag(w6.get<ecs::WorldFlags>(fe6), "delayed") == 1;
            check("Delay defers downstream until its timer elapses", delay_pending && delay_notyet && delay_fired);
            rt6.stop();

            // ---- data pins + variables (Blueprint-style value flow) ----
            ecs::World wd; ecs::GameplayEventBus busd;
            const ecs::Entity fed = wd.create(); wd.add<ecs::WorldFlags>(fed, ecs::WorldFlags{});
            auto dflag = [&](const char* k) { return ecs::world_flag(wd.get<ecs::WorldFlags>(fed), k); };

            // A literal wired into an action's value pin overrides its authored param.
            {
                ecs::LogicGraph pg;
                const int ps  = pg.add_node(ecs::LogicNodeKind::OnStart, 0, 0);
                const int psf = pg.add_node(ecs::LogicNodeKind::SetFlag, 0, 0); pg.find(psf)->param = "score"; pg.find(psf)->param2 = "1";
                const int lit = pg.add_node(ecs::LogicNodeKind::LiteralInt, 0, 0); pg.find(lit)->param = "42";
                pg.link(ps, psf);
                pg.link_data(lit, 0, psf, 1);                 // literal 42 -> SetFlag value pin (not the "1" param)
                ecs::LogicRuntime rtp; rtp.start(pg, wd, busd, fed);
                check("data pin overrides authored param (SetFlag value = literal 42)", dflag("score") == 42);
                rtp.stop();
            }

            // GetVar + Add compute a value into a data pin.
            ecs::LogicGraph ag;
            {
                ag.variables.push_back({"gold", ecs::LogicValue::make_int(100)});
                const int as  = ag.add_node(ecs::LogicNodeKind::OnStart, 0, 0);
                const int asf = ag.add_node(ecs::LogicNodeKind::SetFlag, 0, 0); ag.find(asf)->param = "total";
                const int gv  = ag.add_node(ecs::LogicNodeKind::GetVar, 0, 0);  ag.find(gv)->param = "gold";
                const int l5  = ag.add_node(ecs::LogicNodeKind::LiteralInt, 0, 0); ag.find(l5)->param = "5";
                const int add = ag.add_node(ecs::LogicNodeKind::Add, 0, 0);
                ag.link(as, asf);
                ag.link_data(gv, 0, add, 1);
                ag.link_data(l5, 0, add, 2);
                ag.link_data(add, 0, asf, 1);                 // (gold + 5) -> SetFlag value
                ecs::LogicRuntime rta; rta.start(ag, wd, busd, fed);
                check("GetVar + Add compute into a data pin (100 + 5 = 105)", dflag("total") == 105);
                rta.stop();
            }

            // A single data source feeding two inputs (diamond) — exercises the
            // by-value cycle guard: g+g must be 100, not 50 (2nd input zeroed).
            {
                ecs::LogicGraph dg;
                dg.variables.push_back({"g", ecs::LogicValue::make_int(50)});
                const int s   = dg.add_node(ecs::LogicNodeKind::OnStart, 0, 0);
                const int sf  = dg.add_node(ecs::LogicNodeKind::SetFlag, 0, 0); dg.find(sf)->param = "dbl";
                const int gv  = dg.add_node(ecs::LogicNodeKind::GetVar, 0, 0);  dg.find(gv)->param = "g";
                const int add = dg.add_node(ecs::LogicNodeKind::Add, 0, 0);
                dg.link(s, sf);
                dg.link_data(gv, 0, add, 1);
                dg.link_data(gv, 0, add, 2);                  // SAME source into both inputs
                dg.link_data(add, 0, sf, 1);
                ecs::LogicRuntime rtd; rtd.start(dg, wd, busd, fed);
                check("shared data source feeds two inputs (diamond g+g = 100)", dflag("dbl") == 100);
                rtd.stop();
            }

            // SetVar writes a runtime variable; a later GetVar in the chain reads it.
            {
                ecs::LogicGraph sg;
                sg.variables.push_back({"counter", ecs::LogicValue::make_int(0)});
                const int s    = sg.add_node(ecs::LogicNodeKind::OnStart, 0, 0);
                const int setv = sg.add_node(ecs::LogicNodeKind::SetVar, 0, 0);  sg.find(setv)->param = "counter";
                const int l7   = sg.add_node(ecs::LogicNodeKind::LiteralInt, 0, 0); sg.find(l7)->param = "7";
                const int ssf  = sg.add_node(ecs::LogicNodeKind::SetFlag, 0, 0); sg.find(ssf)->param = "out";
                const int gv2  = sg.add_node(ecs::LogicNodeKind::GetVar, 0, 0);  sg.find(gv2)->param = "counter";
                sg.link(s, setv); sg.link(setv, ssf);         // exec: SetVar THEN SetFlag
                sg.link_data(l7, 0, setv, 1);                 // SetVar counter = 7
                sg.link_data(gv2, 0, ssf, 1);                 // SetFlag out = counter (7 after SetVar)
                ecs::LogicRuntime rts; rts.start(sg, wd, busd, fed);
                check("SetVar writes a variable; downstream GetVar reads it (out = 7)", dflag("out") == 7);
                check("runtime exposes the live variable value", rts.variable("counter") && rts.variable("counter")->as_int() == 7);
                rts.stop();
            }

            // Compare -> Branch: a data-driven boolean condition (Blueprint style).
            {
                ecs::LogicGraph cg3;
                cg3.variables.push_back({"hp", ecs::LogicValue::make_int(10)});
                const int s    = cg3.add_node(ecs::LogicNodeKind::OnStart, 0, 0);
                const int br   = cg3.add_node(ecs::LogicNodeKind::Branch, 0, 0);
                const int pass = cg3.add_node(ecs::LogicNodeKind::SetFlag, 0, 0); cg3.find(pass)->param = "alive"; cg3.find(pass)->param2 = "1";
                const int gv   = cg3.add_node(ecs::LogicNodeKind::GetVar, 0, 0);  cg3.find(gv)->param = "hp";
                const int zero = cg3.add_node(ecs::LogicNodeKind::LiteralInt, 0, 0); cg3.find(zero)->param = "0";
                const int cmp  = cg3.add_node(ecs::LogicNodeKind::Compare, 0, 0); cg3.find(cmp)->param2 = ">";
                cg3.link(s, br); cg3.link(br, pass);
                cg3.link_data(gv, 0, cmp, 1);
                cg3.link_data(zero, 0, cmp, 2);
                cg3.link_data(cmp, 0, br, 1);                 // Branch condition = (hp > 0)
                ecs::LogicRuntime rtc; rtc.start(cg3, wd, busd, fed);
                const bool true_prop = dflag("alive") == 1;
                rtc.stop();
                cg3.find_var("hp")->value = ecs::LogicValue::make_int(0);   // now hp==0 -> hp>0 false
                ecs::set_world_flag(wd.get<ecs::WorldFlags>(fed), "alive", 0);
                ecs::LogicRuntime rtc2; rtc2.start(cg3, wd, busd, fed);
                const bool false_block = dflag("alive") == 0;
                rtc2.stop();
                check("Compare feeds Branch condition (hp>0 TRUE propagates, FALSE blocks)", true_prop && false_block);
            }

            // Serialize round-trip carrying variables + data links (pin fields).
            {
                const std::string dtext = ecs::logic_to_text(ag);
                ecs::LogicGraph ag2 = ecs::logic_from_text(dtext);
                bool has_data_link = false;
                for (const auto& l : ag2.links) if (ecs::logic_link_is_data(l)) has_data_link = true;
                check("data-pin graph serialize round-trips (variables + data links)",
                      ag2.variables.size() == 1 && ag2.variables[0].name == "gold" &&
                      ag2.variables[0].value.as_int() == 100 &&
                      ag2.links.size() == ag.links.size() && has_data_link);
            }
        }

        // text round-trip (scene .logic sidecar)
        const std::string text = ecs::logic_to_text(g);
        ecs::LogicGraph g2 = ecs::logic_from_text(text);
        check("logic graph serialize round-trips (nodes + links)",
              g2.nodes.size() == g.nodes.size() && g2.links.size() == g.links.size() &&
              g2.find(onping) && g2.find(onping)->param == "ping");
    }

    if (g_fail == 0) { std::cout << "gameplay_check: ALL OK\n"; return 0; }
    std::cout << "gameplay_check: " << g_fail << " FAILED\n";
    return 1;
}
