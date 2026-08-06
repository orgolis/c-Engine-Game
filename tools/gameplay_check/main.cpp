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

    if (g_fail == 0) { std::cout << "gameplay_check: ALL OK\n"; return 0; }
    std::cout << "gameplay_check: " << g_fail << " FAILED\n";
    return 1;
}
