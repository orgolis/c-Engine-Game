#include "component_inspector.h"

#include "ecs_bridge.h"
#include "ecs/world.h"
#include "ecs/authorable_components.h"
#include "reflection/reflection.h"

#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdint>
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
        if (!ct.type) continue;
        ImGui::PushID(ct.name);
        if (ct.has(w, e)) {
            if (ImGui::CollapsingHeader(ct.name, ImGuiTreeNodeFlags_DefaultOpen)) {
                if (void* comp = ct.get(w, e)) {
                    gws::reflect::for_each_field(
                        comp, *ct.type,
                        [&](const gws::reflect::FieldInfo& f, void* fp) {
                            if (draw_field(f, fp)) changed = true;
                        });
                }
                if (ImGui::SmallButton("Remove component")) { ct.remove(w, e); changed = true; }
            }
        } else {
            if (ImGui::SmallButton((std::string("Add ") + ct.name).c_str())) { ct.add(w, e); changed = true; }
        }
        ImGui::PopID();
    }
    return changed;
}

}  // namespace schizo::editor
