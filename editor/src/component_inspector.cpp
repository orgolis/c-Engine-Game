#include "component_inspector.h"

#include "ecs_bridge.h"
#include "ecs/world.h"
#include "ecs/authorable_components.h"
#include "ecs/gameplay_attributes.h"   // custom drawer for the data-driven AttributeSet
#include "ecs/gameplay_tags.h"         // custom drawer for data-driven GameplayTags
#include "ecs/gameplay_effects.h"      // read-only active-effects view
#include "ecs/gameplay_abilities.h"    // read-only abilities/cooldown view
#include "ecs/gameplay_triggers.h"     // custom drawer for Trigger Volume
#include "ecs/gameplay_state_machine.h" // custom drawer for State Machine
#include "ecs/gameplay_combat.h"        // custom drawer for Combat Actor
#include "ecs/gameplay_progression.h"   // G3 drawers: Derived Stats / Regen / Progression
#include "ecs/gameplay_skills.h"        // G3 drawers: Skill Tree / Unlocked Skills
#include "ecs/gameplay_items.h"         // G4 item registry (names/kinds)
#include "ecs/gameplay_inventory.h"     // G4 drawers: Inventory / Equipment
#include "ecs/gameplay_economy.h"       // G5 drawers: Vendor / Harvest Node
#include "reflection/reflection.h"

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace schizo::editor {
namespace {

// Field type ids, resolved once, to dispatch the right ImGui widget.
const auto TID_FLOAT = gws::reflect::type_id_of<float>();
const auto TID_I32   = gws::reflect::type_id_of<int32_t>();
const auto TID_U32   = gws::reflect::type_id_of<uint32_t>();
const auto TID_U64   = gws::reflect::type_id_of<uint64_t>();
const auto TID_BOOL  = gws::reflect::type_id_of<bool>();
const auto TID_VEC3  = gws::reflect::type_id_of<glm::vec3>();
const auto TID_VEC4  = gws::reflect::type_id_of<glm::vec4>();
const auto TID_QUAT  = gws::reflect::type_id_of<glm::quat>();

// Render one reflected field with a type-appropriate widget. Returns true if edited.
bool draw_field(const gws::reflect::FieldInfo& f, void* p) {
    if (f.attr.editor_hidden) return false;
    const char* label = f.name;

    if (f.type_id == TID_FLOAT) {
        float* v = static_cast<float*>(p);
        if (f.attr.has_range) return ImGui::SliderFloat(label, v, f.attr.range_min, f.attr.range_max);
        return ImGui::DragFloat(label, v, 0.1f);
    }
    if (f.type_id == TID_I32) return ImGui::DragInt(label, static_cast<int*>(p));
    if (f.type_id == TID_U32) {
        int t = static_cast<int>(*static_cast<uint32_t*>(p));
        if (ImGui::DragInt(label, &t, 1.0f, 0, 0)) {
            *static_cast<uint32_t*>(p) = static_cast<uint32_t>(t < 0 ? 0 : t);
            return true;
        }
        return false;
    }
    if (f.type_id == TID_U64) {   // ids: show read-only
        ImGui::Text("%s: %llu", label, static_cast<unsigned long long>(*static_cast<uint64_t*>(p)));
        return false;
    }
    if (f.type_id == TID_BOOL) return ImGui::Checkbox(label, static_cast<bool*>(p));
    if (f.type_id == TID_VEC3) return ImGui::DragFloat3(label, glm::value_ptr(*static_cast<glm::vec3*>(p)), 0.1f);
    if (f.type_id == TID_VEC4) return ImGui::DragFloat4(label, glm::value_ptr(*static_cast<glm::vec4*>(p)), 0.1f);
    if (f.type_id == TID_QUAT) return ImGui::DragFloat4(label, glm::value_ptr(*static_cast<glm::quat*>(p)), 0.05f);

    ImGui::TextDisabled("%s: (unsupported field type)", label);
    return false;
}

// Custom inspector for the data-driven AttributeSet: define ANY attributes per
// game (Health, Stamina, Sanity, Heat…). Each row is editable; add/remove freely.
bool draw_attribute_set(void* comp) {
    auto& s = *static_cast<ecs::AttributeSet*>(comp);
    bool changed = false;
    int remove_idx = -1;
    for (size_t i = 0; i < s.attributes.size(); ++i) {
        auto& a = s.attributes[i];
        ImGui::PushID(static_cast<int>(i));
        char buf[64]; std::snprintf(buf, sizeof(buf), "%s", a.name.c_str());
        if (ImGui::InputText("name", buf, sizeof(buf))) { a.name = buf; changed = true; }
        if (ImGui::SliderFloat("current", &a.current, a.min, a.max)) changed = true;
        if (ImGui::DragFloat("base", &a.base, 0.5f)) changed = true;
        if (ImGui::DragFloat("min", &a.min, 0.5f))   changed = true;
        if (ImGui::DragFloat("max", &a.max, 0.5f))   changed = true;
        if (ImGui::SmallButton("Remove attribute")) remove_idx = static_cast<int>(i);
        ImGui::Separator();
        ImGui::PopID();
    }
    if (remove_idx >= 0) { s.attributes.erase(s.attributes.begin() + remove_idx); changed = true; }

    static char new_name[64] = "Health";
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("##newattr", new_name, sizeof(new_name));
    ImGui::SameLine();
    if (ImGui::SmallButton("Add attribute") && new_name[0]) {
        s.define(new_name, 100.0f, 0.0f, 100.0f);
        changed = true;
    }
    return changed;
}

// Custom inspector for data-driven GameplayTags: add/remove any hierarchical tag.
bool draw_gameplay_tags(void* comp) {
    auto& g = *static_cast<ecs::GameplayTags*>(comp);
    bool changed = false;
    int remove_idx = -1;
    for (size_t i = 0; i < g.tags.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::BulletText("%s", g.tags[i].c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) remove_idx = static_cast<int>(i);
        ImGui::PopID();
    }
    if (remove_idx >= 0) { g.tags.erase(g.tags.begin() + remove_idx); changed = true; }

    static char new_tag[96] = "state.stunned";
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("##newtag", new_tag, sizeof(new_tag));
    ImGui::SameLine();
    if (ImGui::SmallButton("Add tag") && new_tag[0]) { g.add(new_tag); changed = true; }
    return changed;
}

// Custom inspector for the data-driven TriggerVolume: shape + size + the event
// names it publishes on enter/exit (any game gives those events meaning).
bool draw_trigger_volume(void* comp) {
    auto& v = *static_cast<ecs::TriggerVolume*>(comp);
    bool changed = false;

    int shape = static_cast<int>(v.shape);
    if (ImGui::RadioButton("Box", &shape, 0)) changed = true;      // radios, not a combo
    ImGui::SameLine();
    if (ImGui::RadioButton("Sphere", &shape, 1)) changed = true;
    v.shape = static_cast<uint32_t>(shape);

    if (v.shape == 1) {
        if (ImGui::DragFloat("radius", &v.radius, 0.1f, 0.0f, 0.0f)) changed = true;
    } else {
        if (ImGui::DragFloat3("half extents", glm::value_ptr(v.half_extents), 0.1f)) changed = true;
    }

    char enter[96]; std::snprintf(enter, sizeof(enter), "%s", v.enter_event.c_str());
    if (ImGui::InputText("enter event", enter, sizeof(enter))) { v.enter_event = enter; changed = true; }
    char exit[96]; std::snprintf(exit, sizeof(exit), "%s", v.exit_event.c_str());
    if (ImGui::InputText("exit event", exit, sizeof(exit))) { v.exit_event = exit; changed = true; }

    if (ImGui::Checkbox("fire enter once", &v.once)) changed = true;
    ImGui::TextDisabled("currently inside: %d", static_cast<int>(v.inside.size()));
    return changed;
}

// Inspector for the data-driven StateMachine (G1): current state + structure,
// plus a manual event-send box to drive/tune transitions live in the editor.
bool draw_state_machine(ecs::World& w, ecs::Entity e, void* comp) {
    auto& sm = *static_cast<ecs::StateMachine*>(comp);
    bool changed = false;

    ImGui::Text("Current: %s", sm.current.empty() ? "(not started)" : sm.current.c_str());
    ImGui::SameLine(); ImGui::TextDisabled("(%.1fs in state)", sm.time_in_state);

    if (ImGui::TreeNode("States")) {
        for (const auto& s : sm.states) {
            std::string tags;
            for (const auto& t : s.tags) { tags += t; tags += ' '; }
            ImGui::BulletText("%s  [%s]%s", s.name.c_str(), tags.c_str(),
                              s.duration > 0.0f ? "  (timed)" : "");
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Transitions")) {
        for (const auto& t : sm.transitions)
            ImGui::BulletText("%s --%s--> %s", t.from.empty() ? "*" : t.from.c_str(),
                              t.on_event.c_str(), t.to.c_str());
        ImGui::TreePop();
    }

    static char ev[64] = "hit";
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("##smevent", ev, sizeof(ev));
    ImGui::SameLine();
    if (ImGui::SmallButton("Send event") && ev[0])
        if (ecs::send_state_event(w, e, sm, ev)) changed = true;
    return changed;
}

// Inspector for the G2 CombatActor: authored config + live attack state + a
// "test attack" button to start a default light attack (drives combat live).
bool draw_combat_actor(void* comp) {
    auto& a = *static_cast<ecs::CombatActor*>(comp);
    bool changed = false;

    if (ImGui::DragFloat("max poise", &a.max_poise, 1.0f, 0.0f, 0.0f))   changed = true;
    if (ImGui::DragFloat("hurt radius", &a.hurt_radius, 0.05f, 0.0f, 0.0f)) changed = true;
    if (ImGui::DragFloat("hurt height", &a.hurt_height, 0.05f, 0.0f, 0.0f)) changed = true;
    char dt[48]; std::snprintf(dt, sizeof(dt), "%s", a.damage_type.c_str());
    if (ImGui::InputText("damage type", dt, sizeof(dt))) { a.damage_type = dt; changed = true; }

    const char* st = "Idle";
    switch (a.state) {
        case ecs::CombatState::Startup:  st = "Startup";  break;
        case ecs::CombatState::Active:   st = "Active";   break;
        case ecs::CombatState::Recovery: st = "Recovery"; break;
        case ecs::CombatState::Hitstun:  st = "Hitstun";  break;
        default: break;
    }
    ImGui::Text("state: %s  frame: %d  poise: %.0f", st, a.frame, a.poise);
    if (a.iframe_left > 0) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.4f,0.8f,1,1), "[i-frames]"); }
    if (a.parry_left  > 0) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1,0.9f,0.3f,1), "[parry]"); }

    if (ImGui::SmallButton("Test attack")) {
        ecs::AttackFrameData atk;   // default light attack
        if (ecs::combat_start_attack(a, atk)) changed = true;
    }
    return changed;
}

// G3 · Progression: level/XP/points + a live "Grant XP" button.
bool draw_progression(ecs::World& w, ecs::Entity e, void* comp) {
    auto& p = *static_cast<ecs::Progression*>(comp);
    bool changed = false;
    ImGui::Text("Level %d   XP %.0f / %.0f   Points %d",
                p.level, p.xp, p.curve.threshold(p.level + 1), p.skill_points);
    if (ImGui::DragInt("points / level", &p.points_per_level, 0.1f, 0, 20)) changed = true;
    ImGui::TextDisabled("curve: %.0f + %.0f*n + %.0f*n^2", p.curve.base, p.curve.linear, p.curve.quadratic);
    if (ImGui::SmallButton("Grant 100 XP")) { ecs::grant_xp(w, e, 100.0f); changed = true; }
    return changed;
}

// G3 · Regeneration: edit per-attribute regen rates.
bool draw_regeneration(void* comp) {
    auto& r = *static_cast<ecs::Regeneration*>(comp);
    bool changed = false;
    int remove = -1;
    for (size_t i = 0; i < r.entries.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        char nm[48]; std::snprintf(nm, sizeof(nm), "%s", r.entries[i].attribute.c_str());
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputText("attr", nm, sizeof(nm))) { r.entries[i].attribute = nm; changed = true; }
        ImGui::SameLine(); ImGui::SetNextItemWidth(90);
        if (ImGui::DragFloat("/s", &r.entries[i].per_second, 0.1f)) changed = true;
        ImGui::SameLine(); if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
        ImGui::PopID();
    }
    if (remove >= 0) { r.entries.erase(r.entries.begin() + remove); changed = true; }
    if (ImGui::SmallButton("Add regen")) { r.entries.push_back({"Health", 1.0f}); changed = true; }
    return changed;
}

// G3 · Derived Stats: read-only formulas + a Recompute button.
bool draw_derived_stats(ecs::World& w, ecs::Entity e, void* comp) {
    const auto& ds = *static_cast<ecs::DerivedStats*>(comp);
    for (const auto& d : ds.stats) {
        std::string f = d.target + (d.set_max ? ".max = " : " = ") + std::to_string(d.base);
        for (const auto& t : d.terms) { f += " + " + std::to_string(t.coeff) + "*" + t.source; }
        ImGui::BulletText("%s", f.c_str());
    }
    if (ImGui::SmallButton("Recompute")) { ecs::recompute_derived(w, e); return true; }
    return false;
}

// G3 · Skill Tree: list nodes with Unlock buttons (spends Progression points).
bool draw_skill_tree(ecs::World& w, ecs::Entity e, void* comp) {
    auto& tree = *static_cast<ecs::SkillTree*>(comp);
    bool changed = false;
    const ecs::UnlockedSkills* un = w.try_get<ecs::UnlockedSkills>(e);
    for (const auto& n : tree.nodes) {
        const bool have = un && ecs::skill_unlocked(*un, n.id);
        ImGui::PushID(n.id.c_str());
        if (have) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "[x] %s (%d)", n.name.c_str(), n.cost);
        } else {
            ImGui::Text("[ ] %s (%d)", n.name.c_str(), n.cost);
            ImGui::SameLine();
            const bool ok = ecs::can_unlock(w, e, n.id);
            if (!ok) ImGui::BeginDisabled();
            if (ImGui::SmallButton("Unlock") && ecs::unlock_skill(w, e, n.id)) changed = true;
            if (!ok) ImGui::EndDisabled();
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("Respec")) { ecs::respec(w, e); changed = true; }
    return changed;
}

// G4 · Inventory: list stacks (name x qty, weight), use/equip/drop buttons.
bool draw_inventory(ecs::World& w, ecs::Entity e, void* comp) {
    auto& inv = *static_cast<ecs::Inventory*>(comp);
    bool changed = false;
    ImGui::Text("%d/%s slots   %.1f%s kg",
                static_cast<int>(inv.items.size()),
                inv.max_slots > 0 ? std::to_string(inv.max_slots).c_str() : "inf.",
                ecs::inventory_weight(inv),
                inv.max_weight > 0.0f ? ("/" + std::to_string(static_cast<int>(inv.max_weight))).c_str() : "");
    int use = -1, drop = -1, equip = -1;
    for (size_t i = 0; i < inv.items.size(); ++i) {
        const ecs::ItemInstance& it = inv.items[i];
        const ecs::ItemDef* d = ecs::find_item(it.def_id);
        ImGui::PushID(static_cast<int>(i));
        ImGui::BulletText("%s x%d%s", d ? d->name.c_str() : it.def_id.c_str(), it.quantity,
                          it.affixes.empty() ? "" : "  (rolled)");
        if (d && d->kind == ecs::ItemKind::Consumable) { ImGui::SameLine(); if (ImGui::SmallButton("Use")) use = static_cast<int>(i); }
        if (d && !d->equip_slot.empty()) { ImGui::SameLine(); if (ImGui::SmallButton("Equip")) equip = static_cast<int>(i); }
        ImGui::SameLine(); if (ImGui::SmallButton("Drop")) drop = static_cast<int>(i);
        ImGui::PopID();
    }
    if (equip >= 0) { ecs::equip_item(w, e, inv.items[equip]); ecs::inventory_remove(inv, inv.items[equip].def_id, 1); changed = true; }
    else if (use >= 0)  { ecs::use_item(w, e, inv, static_cast<size_t>(use)); changed = true; }
    else if (drop >= 0) { inv.items.erase(inv.items.begin() + drop); changed = true; }
    return changed;
}

// G4 · Equipment: show each slot + unequip.
bool draw_equipment(ecs::World& w, ecs::Entity e, void* comp) {
    auto& eq = *static_cast<ecs::Equipment*>(comp);
    bool changed = false;
    int unequip = -1;
    for (size_t i = 0; i < eq.slots.size(); ++i) {
        const ecs::ItemDef* d = ecs::find_item(eq.slots[i].item.def_id);
        ImGui::PushID(static_cast<int>(i));
        ImGui::BulletText("%s: %s", eq.slots[i].slot.c_str(), d ? d->name.c_str() : eq.slots[i].item.def_id.c_str());
        ImGui::SameLine(); if (ImGui::SmallButton("Unequip")) unequip = static_cast<int>(i);
        ImGui::PopID();
    }
    if (unequip >= 0) { ecs::unequip_slot(w, e, eq.slots[unequip].slot); changed = true; }
    ImGui::TextDisabled("applied: %d modifiers, %d tags",
                        static_cast<int>(eq.applied.size()), static_cast<int>(eq.applied_tags.size()));
    return changed;
}

// G5 · Vendor: edit the stock list (item id / price / stock), sell ratio, currency.
bool draw_vendor(void* comp) {
    auto& v = *static_cast<ecs::Vendor*>(comp);
    bool changed = false;
    int remove = -1;
    for (size_t i = 0; i < v.entries.size(); ++i) {
        auto& en = v.entries[i];
        ImGui::PushID(static_cast<int>(i));
        char id[64]; std::snprintf(id, sizeof(id), "%s", en.item_id.c_str());
        ImGui::SetNextItemWidth(140);
        if (ImGui::InputText("item", id, sizeof(id))) { en.item_id = id; changed = true; }
        ImGui::SameLine(); ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("price", &en.price, 1.0f, 0.0f, 0.0f)) changed = true;
        ImGui::SameLine(); ImGui::SetNextItemWidth(80);
        if (ImGui::DragInt("stock", &en.stock, 1.0f, -1, 0)) changed = true;   // -1 = unlimited
        ImGui::SameLine(); if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
        ImGui::PopID();
    }
    if (remove >= 0) { v.entries.erase(v.entries.begin() + remove); changed = true; }
    if (ImGui::SmallButton("Add stock item")) { v.entries.push_back({"potion_hp", 20.0f, -1}); changed = true; }
    if (ImGui::DragFloat("sell ratio", &v.sell_ratio, 0.01f, 0.0f, 1.0f)) changed = true;
    char cur[32]; std::snprintf(cur, sizeof(cur), "%s", v.currency.c_str());
    if (ImGui::InputText("currency", cur, sizeof(cur))) { v.currency = cur; changed = true; }
    return changed;
}

// G5 · Harvest Node: what it yields + respawn time (+ live cooldown).
bool draw_harvest_node(void* comp) {
    auto& h = *static_cast<ecs::HarvestNode*>(comp);
    bool changed = false;
    char id[64]; std::snprintf(id, sizeof(id), "%s", h.item_id.c_str());
    if (ImGui::InputText("yields item", id, sizeof(id))) { h.item_id = id; changed = true; }
    if (ImGui::DragInt("min qty", &h.min_qty, 0.1f, 0, 999)) changed = true;
    if (ImGui::DragInt("max qty", &h.max_qty, 0.1f, 0, 999)) changed = true;
    if (ImGui::DragFloat("respawn (s)", &h.respawn, 0.1f, 0.0f, 0.0f)) changed = true;
    if (h.cooldown > 0.0f) ImGui::TextDisabled("depleted (%.1fs to respawn)", h.cooldown);
    else                   ImGui::TextDisabled("ready");
    return changed;
}

}  // namespace

bool draw_ecs_component_inspector(EcsSceneBridge& bridge, schizo::scene::Transform* tf) {
    if (!tf) return false;
    const uint32_t id = bridge.ecs_entity_id(tf);
    if (id == kNoEcsEntity) {
        ImGui::TextDisabled("(ECS entity syncs next frame — components appear then)");
        return false;
    }
    ecs::World&  w = bridge.world();
    ecs::Entity  e = static_cast<ecs::Entity>(id);
    bool changed = false;

    for (const auto& ct : ecs::authorable_components()) {
        ImGui::PushID(ct.name);
        if (ct.has(w, e)) {
            if (ImGui::CollapsingHeader(ct.name, ImGuiTreeNodeFlags_DefaultOpen)) {
                if (void* comp = ct.get(w, e)) {
                    if (ct.type) {   // POD component -> generic reflection fields
                        gws::reflect::for_each_field(
                            comp, *ct.type,
                            [&](const gws::reflect::FieldInfo& f, void* fp) {
                                if (draw_field(f, fp)) changed = true;
                            });
                    } else if (std::strcmp(ct.name, "Attributes") == 0) {
                        if (draw_attribute_set(comp)) changed = true;   // data-driven
                    } else if (std::strcmp(ct.name, "Tags") == 0) {
                        if (draw_gameplay_tags(comp)) changed = true;   // data-driven
                    } else if (std::strcmp(ct.name, "Trigger Volume") == 0) {
                        if (draw_trigger_volume(comp)) changed = true;  // data-driven
                    } else if (std::strcmp(ct.name, "Trigger Actor") == 0) {
                        auto& a = *static_cast<ecs::TriggerActor*>(comp);
                        if (ImGui::Checkbox("enabled (triggers volumes)", &a.enabled)) changed = true;
                    } else if (std::strcmp(ct.name, "State Machine") == 0) {
                        if (draw_state_machine(w, e, comp)) changed = true;
                    } else if (std::strcmp(ct.name, "Combat Actor") == 0) {
                        if (draw_combat_actor(comp)) changed = true;
                    } else if (std::strcmp(ct.name, "Progression") == 0) {
                        if (draw_progression(w, e, comp)) changed = true;
                    } else if (std::strcmp(ct.name, "Regeneration") == 0) {
                        if (draw_regeneration(comp)) changed = true;
                    } else if (std::strcmp(ct.name, "Derived Stats") == 0) {
                        if (draw_derived_stats(w, e, comp)) changed = true;
                    } else if (std::strcmp(ct.name, "Skill Tree") == 0) {
                        if (draw_skill_tree(w, e, comp)) changed = true;
                    } else if (std::strcmp(ct.name, "Unlocked Skills") == 0) {
                        const auto& un = *static_cast<ecs::UnlockedSkills*>(comp);
                        ImGui::TextDisabled("%d unlocked", static_cast<int>(un.unlocked.size()));
                    } else if (std::strcmp(ct.name, "Inventory") == 0) {
                        if (draw_inventory(w, e, comp)) changed = true;
                    } else if (std::strcmp(ct.name, "Equipment") == 0) {
                        if (draw_equipment(w, e, comp)) changed = true;
                    } else if (std::strcmp(ct.name, "Vendor") == 0) {
                        if (draw_vendor(comp)) changed = true;
                    } else if (std::strcmp(ct.name, "Harvest Node") == 0) {
                        if (draw_harvest_node(comp)) changed = true;
                    } else {
                        ImGui::TextDisabled("(no inspector for this component)");
                    }
                }
                if (ImGui::SmallButton("Remove component")) { ct.remove(w, e); changed = true; }
            }
        } else {
            if (ImGui::SmallButton((std::string("Add ") + ct.name).c_str())) { ct.add(w, e); changed = true; }
        }
        ImGui::PopID();
    }

    // Read-only view of the entity's abilities + live cooldowns (G0).
    if (w.has<ecs::AbilitySet>(e)) {
        const auto& set = w.get<ecs::AbilitySet>(e);
        if (!set.abilities.empty() && ImGui::CollapsingHeader("Abilities (runtime)")) {
            for (const auto& s : set.abilities) {
                if (s.cooldown_remaining > 0.0f)
                    ImGui::BulletText("%s  (cooldown %.1fs)", s.ability.name.c_str(), s.cooldown_remaining);
                else
                    ImGui::BulletText("%s  (ready)", s.ability.name.c_str());
            }
        }
    }

    // Read-only view of runtime effects currently active on the entity (G0).
    if (w.has<ecs::ActiveEffects>(e)) {
        const auto& ae = w.get<ecs::ActiveEffects>(e);
        if (!ae.effects.empty() && ImGui::CollapsingHeader("Active Effects (runtime)")) {
            for (const auto& fx : ae.effects) {
                if (fx.def.duration_type == ecs::EffectDurationType::Duration)
                    ImGui::BulletText("%s  (%.1fs left)", fx.def.name.c_str(), fx.remaining);
                else
                    ImGui::BulletText("%s", fx.def.name.c_str());
            }
        }
    }
    return changed;
}

void draw_inventory_ui(EcsSceneBridge& bridge) {
    ecs::World& w = bridge.world();
    w.each<ecs::Inventory>([&](ecs::Entity e, ecs::Inventory& inv) {
        auto* tags = w.try_get<ecs::GameplayTags>(e);
        if (!tags || !tags->has("ui.inventory_open")) return;

        char title[64];
        std::snprintf(title, sizeof(title), "Inventory###inv_%u", static_cast<unsigned>(e));
        bool open = true;
        ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, &open)) {
            ImGui::Text("%d/%s slots    %.1f kg",
                        static_cast<int>(inv.items.size()),
                        inv.max_slots > 0 ? std::to_string(inv.max_slots).c_str() : "inf.",
                        ecs::inventory_weight(inv));
            ImGui::Separator();

            int use = -1, drop = -1, equip = -1;
            if (inv.items.empty()) ImGui::TextDisabled("(empty)");
            for (size_t i = 0; i < inv.items.size(); ++i) {
                const ecs::ItemInstance& it = inv.items[i];
                const ecs::ItemDef* d = ecs::find_item(it.def_id);
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%s  x%d%s", d ? d->name.c_str() : it.def_id.c_str(), it.quantity,
                            it.affixes.empty() ? "" : "  (rolled)");
                ImGui::SameLine(200.0f);
                if (d && d->kind == ecs::ItemKind::Consumable) { if (ImGui::SmallButton("Use")) use = static_cast<int>(i); ImGui::SameLine(); }
                if (d && !d->equip_slot.empty())              { if (ImGui::SmallButton("Equip")) equip = static_cast<int>(i); ImGui::SameLine(); }
                if (ImGui::SmallButton("Drop")) drop = static_cast<int>(i);
                ImGui::PopID();
            }
            if (equip >= 0) { ecs::equip_item(w, e, inv.items[equip], &bridge.events()); ecs::inventory_remove(inv, inv.items[equip].def_id, 1); }
            else if (use >= 0)  ecs::use_item(w, e, inv, static_cast<size_t>(use), &bridge.events());
            else if (drop >= 0) inv.items.erase(inv.items.begin() + drop);

            if (auto* eq = w.try_get<ecs::Equipment>(e); eq && !eq->slots.empty()) {
                ImGui::Separator();
                ImGui::TextUnformatted("Equipped");
                int unequip = -1;
                for (size_t i = 0; i < eq->slots.size(); ++i) {
                    const ecs::ItemDef* d = ecs::find_item(eq->slots[i].item.def_id);
                    ImGui::PushID(1000 + static_cast<int>(i));
                    ImGui::BulletText("%s: %s", eq->slots[i].slot.c_str(),
                                      d ? d->name.c_str() : eq->slots[i].item.def_id.c_str());
                    ImGui::SameLine(); if (ImGui::SmallButton("Unequip")) unequip = static_cast<int>(i);
                    ImGui::PopID();
                }
                if (unequip >= 0) ecs::unequip_slot(w, e, eq->slots[unequip].slot, &bridge.events());
            }

            if (auto* a = w.try_get<ecs::AttributeSet>(e)) {
                ImGui::Separator();
                ImGui::Text("Health %.0f    AttackPower %.0f    Armor %.0f",
                            a->get("Health"), a->get("AttackPower"), a->get("Armor"));
            }
            ImGui::Separator();
            if (ImGui::Button("Close")) open = false;
        }
        ImGui::End();
        if (!open) tags->remove_exact("ui.inventory_open");   // window 'X' or Close button
    });
}

}  // namespace schizo::editor
