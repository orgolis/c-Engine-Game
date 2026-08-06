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
#include "ecs/prefab.h"
#include "reflection/reflection.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

    if (g_fail == 0) { std::cout << "gameplay_check: ALL OK\n"; return 0; }
    std::cout << "gameplay_check: " << g_fail << " FAILED\n";
    return 1;
}
