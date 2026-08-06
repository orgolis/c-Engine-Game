#include "component_inspector.h"

#include "ecs_bridge.h"
#include "ecs/world.h"
#include "ecs/authorable_components.h"
#include "ecs/gameplay_attributes.h"   // custom drawer for the data-driven AttributeSet
#include "ecs/gameplay_tags.h"         // custom drawer for data-driven GameplayTags
#include "ecs/gameplay_effects.h"      // read-only active-effects view
#include "ecs/gameplay_abilities.h"    // read-only abilities/cooldown view
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

}  // namespace schizo::editor
