// ============================================================================
// level_tools_panel — arrays, scatter, splines and greybox in the editor (4.7).
//
// The maths lives in level_tools.h and is tested there. This is the surface:
// which entity is being duplicated, where the copies go, and what a copy
// actually consists of.
//
// That last point turned out to matter. The editor's existing Duplicate copies
// the TRANSFORM ONLY -- the copy has no mesh and no renderer, so it is an
// invisible empty entity sitting exactly where you asked for a duplicate. An
// array of forty of those is forty invisible objects, which would have read as
// "the array tool is broken". clone_entity here copies what makes an object
// visible, and the greybox tools create entities that are visible by
// construction.
// ============================================================================
#include "level_tools_panel.h"

#include "level_tools.h"
#include "snapping.h"

#include "collider_component.h"
#include "entity.h"
#include "mesh_component.h"
#include "mesh_renderer_component.h"
#include "scene.h"
#include "transform.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>
#include <string>

namespace schizo::editor {

namespace {

/// Copy the parts of `src` that make it a visible object.
///
/// Not a deep clone of everything: scripts, audio sources and NPC agents are
/// deliberately left off, because forty copies of a patrolling guard or a
/// looping sound is never what "array this fence post" meant.
std::shared_ptr<scene::Entity> clone_entity(const std::shared_ptr<scene::Scene>& scene,
                                            const std::shared_ptr<scene::Entity>& src,
                                            const std::string& name) {
    if (!scene || !src) return nullptr;
    auto dst = scene->CreateEntity(name);
    if (!dst) return nullptr;

    if (auto* mc = src->GetMeshComponent(); mc && !mc->mesh_path.empty())
        dst->SetMesh(mc->mesh_path);

    if (auto mr = src->GetComponent<scene::MeshRendererComponent>()) {
        if (auto out = dst->AddComponent<scene::MeshRendererComponent>(mr->GetMeshType(),
                                                                       mr->GetColor())) {
            out->SetMetallic(mr->GetMetallic());
            out->SetRoughness(mr->GetRoughness());
            out->SetEmissive(mr->GetEmissive());
            out->SetEmissiveIntensity(mr->GetEmissiveIntensity());
        }
    }

    // Colliders come along: a duplicated wall that you can walk through is a
    // bug report waiting to happen.
    if (auto cc = src->GetComponent<scene::ColliderComponent>()) {
        if (auto out = dst->AddComponent<scene::ColliderComponent>()) {
            out->SetShape(cc->GetShape());
            out->SetRadius(cc->GetRadius());
            out->SetHeight(cc->GetHeight());
            out->SetDynamic(cc->IsDynamic());
        }
    }

    if (src->GetParent()) dst->SetParent(src->GetParent());
    return dst;
}

void apply(const std::shared_ptr<scene::Scene>& scene,
           const std::shared_ptr<scene::Entity>& src,
           const std::vector<Placement>& places,
           const SnapSettings& snap,
           bool skip_first,
           int& out_created) {
    if (!scene || !src) return;
    const glm::vec3 base_scale = src->GetTransform()->GetLocalScale();

    for (size_t i = skip_first ? 1u : 0u; i < places.size(); ++i) {
        auto e = clone_entity(scene, src, src->GetName() + "_" + std::to_string(i));
        if (!e) continue;
        auto* t = e->GetTransform();

        glm::vec3 p = places[i].position;
        if (snap.enabled) p = snap_position(p, snap.translate);
        t->SetLocalPosition(p);
        t->SetLocalRotation(places[i].rotation);
        // The placement's scale MULTIPLIES the source's, so scattering a
        // half-size rock at 0.8 gives 0.4 rather than snapping it to 0.8 and
        // quietly discarding what the artist set.
        t->SetLocalScale(base_scale * places[i].scale);
        ++out_created;
    }
}

}  // namespace

void draw_level_tools_panel(bool& open,
                            const std::shared_ptr<scene::Scene>& scene,
                            uint32_t selected_entity_id,
                            const SnapSettings& snap,
                            Spline& spline,
                            bool& out_modified) {
    if (!open) return;

    ImGui::SetNextWindowSize(ImVec2(340.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Level Tools", &open)) { ImGui::End(); return; }

    auto src = scene ? scene->GetEntityById(selected_entity_id) : nullptr;
    if (!src) {
        ImGui::TextDisabled("Select an entity to array, scatter or distribute it.");
    } else {
        ImGui::Text("Source: %s", src->GetName().c_str());
    }
    if (snap.enabled)
        ImGui::TextDisabled("snapping on — placements land on the %.2f grid", snap.translate);
    ImGui::Separator();

    int created = 0;

    // ---- array -------------------------------------------------------------
    if (ImGui::CollapsingHeader("Array", ImGuiTreeNodeFlags_DefaultOpen)) {
        static ArrayParams p;
        ImGui::DragInt3("count x/y/z", &p.count_x, 0.1f, 1, 256);
        ImGui::DragFloat3("step", &p.step.x, 0.05f);
        ImGui::Checkbox("centre on source", &p.center);
        if (ImGui::Button("Build array") && src) {
            const auto places = build_array(src->GetTransform()->GetLocalPosition(), p);
            // skip_first: placement 0 IS the source, and cloning it would leave
            // a duplicate exactly on top of the original.
            apply(scene, src, places, snap, /*skip_first=*/!p.center, created);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d x %d x %d", p.count_x, p.count_y, p.count_z);
    }

    // ---- scatter -----------------------------------------------------------
    if (ImGui::CollapsingHeader("Scatter")) {
        static ScatterParams p;
        ImGui::DragInt("count", &p.count, 0.2f, 1, 2000);
        ImGui::DragFloat("radius", &p.radius, 0.1f, 0.1f, 1000.0f);
        ImGui::DragFloat("min distance", &p.min_distance, 0.05f, 0.0f, 100.0f);
        ImGui::Checkbox("random yaw", &p.random_yaw);
        ImGui::DragFloatRange2("scale", &p.scale_min, &p.scale_max, 0.01f, 0.01f, 10.0f);
        int seed = static_cast<int>(p.seed);
        if (ImGui::DragInt("seed", &seed, 1.0f, 0, 1000000))
            p.seed = static_cast<uint32_t>(seed < 0 ? 0 : seed);
        ImGui::TextDisabled("the same seed always gives the same layout");

        if (ImGui::Button("Scatter") && src) {
            const auto places = build_scatter(src->GetTransform()->GetLocalPosition(), p);
            apply(scene, src, places, snap, /*skip_first=*/false, created);
            if (static_cast<int>(places.size()) < p.count)
                spdlog::info("[level] scatter placed {} of {} — the rest could not "
                             "satisfy min distance {}", places.size(), p.count, p.min_distance);
        }
    }

    // ---- spline ------------------------------------------------------------
    if (ImGui::CollapsingHeader("Spline")) {
        ImGui::Text("%zu control point(s)", spline.size());
        if (ImGui::Button("Add point at selection") && src)
            spline.add_point(src->GetTransform()->GetLocalPosition());
        ImGui::SameLine();
        if (ImGui::Button("Clear")) spline.clear();

        for (size_t i = 0; i < spline.points().size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::DragFloat3("##pt", &spline.points()[i].x, 0.05f);
            ImGui::PopID();
        }

        static int  count = 10;
        static bool align = true;
        ImGui::DragInt("instances", &count, 0.2f, 1, 500);
        ImGui::Checkbox("face along the path", &align);
        if (spline.size() >= 2)
            ImGui::TextDisabled("length ~%.1f units, evenly spaced by arc length",
                                spline.length());
        if (ImGui::Button("Distribute along spline") && src && spline.size() >= 2)
            apply(scene, src, spline.distribute(count, align), snap,
                  /*skip_first=*/false, created);
    }

    // ---- greybox -----------------------------------------------------------
    if (ImGui::CollapsingHeader("Greybox", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("blockout shapes, placed at the origin");
        struct Prim { const char* label; scene::MeshType type; glm::vec3 scale; };
        static const Prim kPrims[] = {
            {"Floor",  scene::MeshType::Cube,     {8.0f, 0.25f, 8.0f}},
            {"Wall",   scene::MeshType::Cube,     {8.0f, 3.0f, 0.25f}},
            {"Block",  scene::MeshType::Cube,     {2.0f, 2.0f, 2.0f}},
            {"Pillar", scene::MeshType::Cylinder, {0.5f, 3.0f, 0.5f}},
            {"Sphere", scene::MeshType::Sphere,   {1.0f, 1.0f, 1.0f}},
        };
        for (size_t i = 0; i < sizeof kPrims / sizeof kPrims[0]; ++i) {
            if (i) ImGui::SameLine();
            if (ImGui::Button(kPrims[i].label) && scene) {
                auto e = scene->CreateEntity(kPrims[i].label);
                if (e) {
                    // A mid-grey unlit-looking block: greybox geometry should
                    // read as placeholder, not as finished art.
                    e->AddComponent<scene::MeshRendererComponent>(
                        kPrims[i].type, glm::vec4(0.62f, 0.62f, 0.64f, 1.0f));
                    e->GetTransform()->SetLocalScale(kPrims[i].scale);
                    ++created;
                }
            }
        }
    }

    if (created > 0) {
        out_modified = true;
        spdlog::info("[level] created {} entities", created);
    }

    ImGui::End();
}

}  // namespace schizo::editor
