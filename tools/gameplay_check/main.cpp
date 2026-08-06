// ============================================================================
// gameplay_check — verifies the F1/F2 ECS gameplay-authoring data path: the
// authorable-component registry + reflection-driven read/write that the editor
// inspector uses (add component -> walk fields -> edit by offset -> persists).
// ============================================================================
#include "ecs/world.h"
#include "ecs/authorable_components.h"
#include "ecs/gameplay_attributes.h"
#include "ecs/gameplay_tags.h"
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

    if (g_fail == 0) { std::cout << "gameplay_check: ALL OK\n"; return 0; }
    std::cout << "gameplay_check: " << g_fail << " FAILED\n";
    return 1;
}
