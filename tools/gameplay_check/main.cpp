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

    if (g_fail == 0) { std::cout << "gameplay_check: ALL OK\n"; return 0; }
    std::cout << "gameplay_check: " << g_fail << " FAILED\n";
    return 1;
}
