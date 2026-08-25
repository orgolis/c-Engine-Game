#include "material_editor_panel.h"
#include "material_asset_cache.h"
#include "asset_browser_panel.h"   // drag-drop payload names
#include "entity.h"
#include "scene.h"
#include "mesh_renderer_component.h"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace schizo::editor {

using namespace schizo::scene;
using gws::assets::MaterialAlphaMode;
using gws::assets::MaterialDesc;

namespace {

// Presets are starting points, not products: they set base colour, metallic and
// roughness and leave textures alone, because a preset that cleared the maps
// would silently discard work every time someone browsed the list.
struct Preset {
    const char* name;
    glm::vec3   albedo;
    float       metallic;
    float       roughness;
};
constexpr Preset kPresets[] = {
    {"Gold",     {1.00f, 0.84f, 0.00f}, 1.0f, 0.20f},
    {"Copper",   {0.97f, 0.68f, 0.19f}, 1.0f, 0.30f},
    {"Steel",    {0.77f, 0.78f, 0.78f}, 1.0f, 0.10f},
    {"Aluminum", {0.91f, 0.92f, 0.92f}, 1.0f, 0.20f},
    {"Plastic",  {0.20f, 0.80f, 0.90f}, 0.0f, 0.60f},
    {"Rubber",   {0.10f, 0.10f, 0.10f}, 0.0f, 0.90f},
    {"Wood",     {0.55f, 0.37f, 0.09f}, 0.0f, 0.80f},
    {"Ceramic",  {0.30f, 0.30f, 0.30f}, 0.0f, 0.40f},
    {"Fabric",   {0.40f, 0.40f, 0.40f}, 0.0f, 0.95f},
};

/// One texture slot: path, a TEXTURE_ASSET drop target, and a clear button.
/// `hint` says what the slot is FOR, because "MR" and "AO" mean nothing to
/// someone who has not read a glTF spec, and a mislabelled map is a bug that
/// renders rather than errors.
bool texture_slot(const char* label, const char* hint, std::string& path) {
    bool changed = false;
    ImGui::PushID(label);

    char buf[260];
    std::snprintf(buf, sizeof buf, "%s", path.c_str());
    ImGui::SetNextItemWidth(-90.0f);
    if (ImGui::InputText("##path", buf, sizeof buf)) { /* committed below */ }
    if (ImGui::IsItemDeactivatedAfterEdit() && path != buf) {
        path = buf;
        changed = true;
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl =
                ImGui::AcceptDragDropPayload(AssetBrowserPanel::kPayloadTexture)) {
            const char* p = static_cast<const char*>(pl->Data);
            if (p && p[0]) { path = p; changed = true; }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    if (path.empty()) {
        ImGui::TextDisabled("%s", label);
    } else {
        ImGui::Text("%s", label);
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { path.clear(); changed = true; }
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", hint);
    ImGui::PopID();
    return changed;
}

/// A disabled texture slot, with the reason. Used in inline mode, where the
/// component can only carry a base-colour path.
void texture_slot_disabled(const char* label, const char* why) {
    ImGui::BeginDisabled();
    char empty[2] = {0};
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputText("##path", empty, sizeof empty, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", label);
    ImGui::PopID();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", why);
}

/// Edit the PBR factors shared by both modes. `live` is set while a control is
/// being dragged (viewport should update); `commit` when the gesture ended (the
/// file should be written). Separating them is what keeps a slider drag from
/// writing the file hundreds of times.
void pbr_controls(MaterialDesc& m, bool& live, bool& commit) {
    auto note = [&]() {
        if (ImGui::IsItemActive() || ImGui::IsItemEdited()) live = true;
        if (ImGui::IsItemDeactivatedAfterEdit())            commit = true;
    };

    if (ImGui::ColorEdit4("Base Color", &m.base_color.r,
                          ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview))
        live = true;
    note();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Multiplies the base-colour texture. White = the texture unchanged.");

    if (ImGui::SliderFloat("Metallic", &m.metallic, 0.0f, 1.0f)) live = true;
    note();
    if (ImGui::SliderFloat("Roughness", &m.roughness, 0.04f, 1.0f)) live = true;
    note();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("0.04 is a mirror, 1.0 is fully diffuse. It cannot reach 0:\n"
                          "the BRDF divides by roughness.");
    if (ImGui::SliderFloat("Occlusion", &m.occlusion, 0.0f, 1.0f)) live = true;
    note();
    if (ImGui::SliderFloat("Normal Scale", &m.normal_scale, 0.0f, 4.0f)) live = true;
    note();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Strength of the normal map. No effect without one bound.");

    if (ImGui::ColorEdit3("Emissive", &m.emissive.r,
                          ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
        live = true;
    note();
    if (ImGui::SliderFloat("Emissive Glow", &m.emissive_intensity, 0.0f, 10.0f)) live = true;
    note();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("HDR multiplier on the emissive colour. Bloom kicks in around 1.0,\n"
                          "so push past ~1.5 for a surface that reads as a light source.");
}

}  // namespace

MaterialEditorPanel::MaterialEditorPanel()  = default;
MaterialEditorPanel::~MaterialEditorPanel() = default;

bool MaterialEditorPanel::Render(const std::shared_ptr<Entity>& entity,
                                 MaterialAssetCache* materials,
                                 const std::shared_ptr<Scene>& scene) {
    if (!entity) {
        ImGui::TextDisabled("No entity selected.");
        return false;
    }
    auto mr = entity->GetComponent<MeshRendererComponent>();
    if (!mr) {
        ImGui::TextDisabled("This entity has no Mesh Renderer, so it has no surface\n"
                            "to give a material to. Add one from the Components list.");
        return false;
    }
    if (!materials) {
        ImGui::TextDisabled("Material cache unavailable.");
        return false;
    }

    bool changed = false;

    // ---- The material slot -------------------------------------------------
    ImGui::TextUnformatted("Material Asset");
    ImGui::SameLine();
    ImGui::TextDisabled("(drag a .mat here)");

    {
        char buf[260];
        std::snprintf(buf, sizeof buf, "%s",
                      mr->HasMaterial() ? mr->GetMaterialPath().c_str() : "");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##mat_slot", "none — using this object's inline material",
                                 buf, sizeof buf);
        if (ImGui::IsItemDeactivatedAfterEdit() && mr->GetMaterialPath() != buf) {
            mr->SetMaterialPath(buf);
            edit_path_.clear();          // force a re-sync of the working copy
            changed = true;
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl =
                    ImGui::AcceptDragDropPayload(AssetBrowserPanel::kPayloadMaterial)) {
                const char* p = static_cast<const char*>(pl->Data);
                if (p && p[0]) {
                    mr->SetMaterialPath(p);
                    edit_path_.clear();
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    if (mr->HasMaterial()) {
        if (ImGui::SmallButton("Detach##mat")) {
            // Detach, not delete. The .mat stays on disk — an entity dropping
            // its reference must never be able to destroy an asset other
            // entities may still be using.
            mr->SetMaterialPath("");
            edit_path_.clear();
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Stop using this material asset and fall back to this\n"
                              "object's inline settings. The .mat file is not deleted.");
    }

    ImGui::Separator();

    const bool asset_mode = mr->HasMaterial();
    if (asset_mode) changed |= render_asset_mode(entity, materials, scene);
    else            changed |= render_inline_mode(entity, materials);

    // ---- Alpha mode: always the component's, in both modes ------------------
    // Which pass an object is drawn in is a property of the PLACEMENT, not of
    // the material: the same glass can legitimately be cutout foliage on one
    // object and blended on another. Keeping it here rather than in the .mat is
    // what makes a shared material safe to attach to anything.
    ImGui::Separator();
    ImGui::TextUnformatted("Alpha Mode");
    ImGui::SameLine();
    ImGui::TextDisabled("(per object, not part of the material)");
    int am = static_cast<int>(mr->GetAlphaMode());
    bool am_changed = false;
    am_changed |= ImGui::RadioButton("Opaque##mr_am", &am, 0); ImGui::SameLine();
    am_changed |= ImGui::RadioButton("Cutout##mr_am", &am, 1); ImGui::SameLine();
    am_changed |= ImGui::RadioButton("Blend##mr_am",  &am, 2);
    if (am_changed) {
        mr->SetAlphaMode(static_cast<AlphaMode>(am));
        changed = true;
    }
    ImGui::TextDisabled("Opaque: no transparency.  Cutout: hard discard below cutoff.\n"
                        "Blend: real translucency (forward pass, after lighting).");
    if (mr->GetAlphaMode() == AlphaMode::Cutout) {
        float cutoff = mr->GetAlphaCutoff();
        if (ImGui::SliderFloat("Alpha Cutoff##mr", &cutoff, 0.0f, 1.0f)) {
            mr->SetAlphaCutoff(cutoff);
            changed = true;
        }
    }

    // ---- Imported-model override -------------------------------------------
    // Only meaningful when the entity draws an imported model; a primitive has
    // no asset material to override, so showing the checkbox there would be
    // offering a control that cannot do anything.
    const auto* mc = entity->GetMeshComponent();
    if (mc && !mc->mesh_path.empty()) {
        ImGui::Separator();
        bool ov = mr->GetOverrideAssetMaterial();
        if (ImGui::Checkbox("Override the model's own materials", &ov)) {
            mr->SetOverrideAssetMaterial(ov);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Off: the model keeps the materials it was imported with\n"
                "(glTF ships its own; OBJ uses its .mtl).\n"
                "On: the material above replaces them on EVERY submesh —\n"
                "which is what you want for a model that arrived untextured.");
        if (ov)
            ImGui::TextDisabled("Every submesh of this model uses the material above.");
    }

    return changed;
}

bool MaterialEditorPanel::render_asset_mode(const std::shared_ptr<Entity>& entity,
                                            MaterialAssetCache* materials,
                                            const std::shared_ptr<Scene>& scene) {
    auto mr = entity->GetComponent<MeshRendererComponent>();
    const std::string& path = mr->GetMaterialPath();

    const MaterialDesc* loaded = materials->get(path);
    if (!loaded) {
        // Say what is wrong and what is happening instead. A panel that just
        // shows empty fields here would leave the user editing values that drive
        // nothing while the object renders from its inline settings.
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f),
                           "This material could not be loaded.");
        ImGui::TextWrapped("The object is rendering from its inline settings. Check the "
                           "path above, or detach the material to edit those settings here.");
        if (ImGui::Button("Retry##mat_reload")) materials->invalidate(path);
        return false;
    }

    // Re-sync the working copy when the selection or the file changed under us.
    if (edit_path_ != path || (!dirty_ && edit_.content_hash() != loaded->content_hash())) {
        edit_      = *loaded;
        edit_path_ = path;
        dirty_     = false;
    }

    // How many OTHER entities share this material. Editing a shared asset while
    // believing it is per-object is the one mistake this UI invites, so it is
    // answered before the first slider rather than discovered afterwards.
    int sharers = 0;
    if (scene) {
        for (const auto& e : scene->GetEntities()) {
            if (!e || e == entity) continue;
            if (auto other = e->GetComponent<MeshRendererComponent>())
                if (other->GetMaterialPath() == path) ++sharers;
        }
    }
    if (sharers > 0)
        ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.35f, 1.0f),
                           "Shared: %d other object%s use%s this material.",
                           sharers, sharers == 1 ? "" : "s", sharers == 1 ? "s" : "");

    ImGui::Text("%s%s", edit_.name.empty() ? "(unnamed)" : edit_.name.c_str(),
                dirty_ ? "  *unsaved*" : "");

    // Instance overrides live on the COMPONENT and are written into the scene
    // file, unlike the asset edits below -- so unlike them, they do dirty the
    // scene. Tracked separately for exactly that reason.
    bool instance_changed = false;

    // ---- This object's overrides -------------------------------------------
    // Deliberately OUTSIDE the tabs below. Those edit the shared asset and
    // change it for every object using it; these change only this one. Putting
    // them in the same tab strip would make "am I editing the asset or the
    // object?" a question the UI invites and never answers -- and it sits right
    // under the sharing warning because an override is the answer to it.
    {
        using MR = MeshRendererComponent;
        bool changed = false;
        const uint32_t ov = mr->GetMaterialOverrides();

        if (ImGui::CollapsingHeader("This object overrides",
                                    ov ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            ImGui::TextDisabled("Ticked fields use this object's value instead of\n"
                                "the material's. Unticking restores the material's.");

            auto flag = [&](const char* label, uint32_t bit) {
                bool on = mr->HasMaterialOverride(bit);
                if (ImGui::Checkbox(label, &on)) {
                    mr->SetMaterialOverride(bit, on);
                    changed = true;
                }
                return on;
            };

            if (flag("Tint##ovc", MR::kOverrideBaseColor)) {
                glm::vec4 c = mr->GetColor();
                ImGui::SameLine();
                if (ImGui::ColorEdit4("##ovcv", &c.x, ImGuiColorEditFlags_NoInputs)) {
                    mr->SetColor(c); changed = true;
                }
            }
            if (flag("Metallic##ovm", MR::kOverrideMetallic)) {
                float v = mr->GetMetallic();
                ImGui::SameLine(); ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##ovmv", &v, 0.0f, 1.0f)) { mr->SetMetallic(v); changed = true; }
            }
            if (flag("Roughness##ovr", MR::kOverrideRoughness)) {
                float v = mr->GetRoughness();
                ImGui::SameLine(); ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderFloat("##ovrv", &v, 0.0f, 1.0f)) { mr->SetRoughness(v); changed = true; }
            }
            if (flag("Emissive##ove", MR::kOverrideEmissive)) {
                glm::vec3 e = mr->GetEmissive();
                if (ImGui::ColorEdit3("##ovev", &e.x, ImGuiColorEditFlags_NoInputs)) {
                    mr->SetEmissive(e); changed = true;
                }
                float gi = mr->GetEmissiveIntensity();
                ImGui::SameLine(); ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::DragFloat("##ovegv", &gi, 0.05f, 0.0f, 64.0f, "glow %.2f")) {
                    mr->SetEmissiveIntensity(gi); changed = true;
                }
            }
            if (flag("UV tiling / offset##ovu", MR::kOverrideUv)) {
                glm::vec2 sc = mr->GetUvScale(), of = mr->GetUvOffset();
                if (ImGui::DragFloat2("Tiling##ovuv", &sc.x, 0.01f, 0.01f, 256.0f, "%.2f")) {
                    mr->SetUvScale(sc); changed = true;
                }
                if (ImGui::DragFloat2("Offset##ovuo", &of.x, 0.005f, -16.0f, 16.0f, "%.3f")) {
                    mr->SetUvOffset(of); changed = true;
                }
            }

            if (ov != 0u) {
                if (ImGui::SmallButton("Clear all overrides")) {
                    mr->SetMaterialOverrides(0u);
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(back to the material exactly)");
            }
        }
        // Folded into the panel's return value, which is how every other edit
        // here reports itself; the caller owns marking the scene dirty.
        if (changed) instance_changed = true;
    }

    bool live = false, commit = false;

    if (ImGui::BeginTabBar("##mat_tabs")) {
        if (ImGui::BeginTabItem("Surface")) {
            pbr_controls(edit_, live, commit);
            bool ds = edit_.double_sided;
            if (ImGui::Checkbox("Double sided", &ds)) {
                edit_.double_sided = ds;
                live = commit = true;
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Textures")) {
            ImGui::TextDisabled("Drag textures from the Asset Browser onto a slot.");
            bool t = false;
            t |= texture_slot("Base Color", "Albedo / diffuse. Read as sRGB.",
                              edit_.albedo_map);
            t |= texture_slot("Normal", "Tangent-space normal map. Linear data —\n"
                                        "loaded without sRGB decoding.",
                              edit_.normal_map);
            t |= texture_slot("Metal/Rough",
                              "glTF packing: GREEN = roughness, BLUE = metallic.\n"
                              "Linear data.", edit_.metallic_roughness_map);
            t |= texture_slot("Occlusion", "Ambient occlusion, RED channel. Linear data.",
                              edit_.occlusion_map);
            t |= texture_slot("Emissive", "Self-illumination. Read as sRGB, then scaled\n"
                                          "by Emissive Glow.", edit_.emissive_map);

            // UV transform. Applies to every map above at once -- transforming
            // them individually is how one map ends up unaligned with the rest
            // and the surface looks subtly wrong in a way nobody can point at.
            // Detail maps. Below the five PBR slots because they modify what
            // those produce rather than replacing any of them.
            ImGui::SeparatorText("Detail");
            t |= texture_slot("Detail Color",
                              "Higher-frequency colour blended over Base Color. "
                              "Mid-grey is neutral, which is what detail textures "
                              "are authored around.", edit_.detail_albedo_map);
            t |= texture_slot("Detail Normal",
                              "Higher-frequency normal added to the base normal.",
                              edit_.detail_normal_map);
            if (ImGui::DragFloat("Detail tiling", &edit_.detail_scale, 0.1f, 1.0f, 128.0f, "%.1fx"))
                t = true;
            if (ImGui::SliderFloat("Detail color strength", &edit_.detail_albedo_strength, 0.0f, 1.0f))
                t = true;
            if (ImGui::SliderFloat("Detail normal strength", &edit_.detail_normal_strength, 0.0f, 2.0f))
                t = true;
            if (edit_.detail_albedo_map.empty() && edit_.detail_normal_map.empty())
                ImGui::TextDisabled("  No detail maps - strengths have no effect.");

            ImGui::SeparatorText("UV transform");
            if (ImGui::DragFloat2("Tiling", &edit_.uv_scale.x, 0.01f, 0.01f, 256.0f, "%.2f")) {
                t = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How many times the maps repeat across the mesh's UVs.\n"
                                  "Lets a wall and a floor share one brick material at\n"
                                  "different densities instead of duplicating the .mat.");
            if (ImGui::DragFloat2("Offset", &edit_.uv_offset.x, 0.005f, -16.0f, 16.0f, "%.3f")) {
                t = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Shifts the maps across the surface. Useful to break up\n"
                                  "visible repetition between two objects using one material.");
            if (ImGui::SmallButton("Reset UV")) {
                edit_.uv_scale  = glm::vec2(1.0f);
                edit_.uv_offset = glm::vec2(0.0f);
                t = true;
            }

            if (t) { live = true; commit = true; }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Presets")) {
            if (render_presets(edit_)) { live = true; commit = true; }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // In memory while dragging (the viewport follows the mouse), on disk when
    // the gesture ends.
    if (live) {
        materials->set(path, edit_);
        dirty_ = true;
    }
    if (commit) {
        if (materials->save(path, edit_)) dirty_ = false;
    }

    ImGui::Separator();
    if (dirty_) {
        if (ImGui::Button("Save##mat", ImVec2(-1, 0)))
            if (materials->save(path, edit_)) dirty_ = false;
    } else {
        ImGui::TextDisabled("Saved to %s", path.c_str());
    }

    // A material edit changes the ASSET, not the scene — the scene file has no
    // copy of these values, only the path. Reporting the scene as modified for
    // those would prompt to save a file nothing changed in.
    //
    // Instance overrides are the exception: they are component state and the
    // scene file is the only place they exist, so they must dirty it or an
    // override is silently lost on close.
    return instance_changed;
}

bool MaterialEditorPanel::render_inline_mode(const std::shared_ptr<Entity>& entity,
                                             MaterialAssetCache* materials) {
    auto mr = entity->GetComponent<MeshRendererComponent>();
    bool changed = false;

    ImGui::TextDisabled("Inline material — these settings belong to this object alone.");

    // Read the component into a desc, edit it, write back what a component can
    // actually hold. Going through MaterialDesc keeps one set of controls for
    // both modes rather than two that drift apart.
    MaterialDesc m;
    m.base_color         = mr->GetColor();
    m.metallic           = mr->GetMetallic();
    m.roughness          = mr->GetRoughness();
    m.occlusion          = mr->GetOcclusion();
    m.emissive           = mr->GetEmissive();
    m.emissive_intensity = mr->GetEmissiveIntensity();
    m.albedo_map         = mr->GetAlbedoTexturePath();
    m.name               = entity->GetName();

    bool live = false, commit = false;

    if (ImGui::BeginTabBar("##inline_tabs")) {
        if (ImGui::BeginTabItem("Surface")) {
            // Normal Scale is drawn but inert without a normal map, and inline
            // materials cannot have one; the tooltip on the slot below says so.
            pbr_controls(m, live, commit);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Textures")) {
            if (texture_slot("Base Color", "Albedo / diffuse. Read as sRGB.", m.albedo_map))
                live = true;
            texture_slot_disabled("Normal",
                "An object's inline settings can only carry a base-colour texture.\n"
                "Create a material asset below to use normal, metal/rough,\n"
                "occlusion and emissive maps.");
            texture_slot_disabled("Metal/Rough",  "Create a material asset to use this slot.");
            texture_slot_disabled("Occlusion",    "Create a material asset to use this slot.");
            texture_slot_disabled("Emissive",     "Create a material asset to use this slot.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Presets")) {
            if (render_presets(m)) live = true;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (live || commit) {
        mr->SetColor(m.base_color);
        mr->SetMetallic(m.metallic);
        mr->SetRoughness(m.roughness);
        mr->SetOcclusion(m.occlusion);
        mr->SetEmissive(m.emissive);
        mr->SetEmissiveIntensity(m.emissive_intensity);
        mr->SetAlbedoTexturePath(m.albedo_map);
        changed = true;
    }

    // ---- Promote to an asset ------------------------------------------------
    ImGui::Separator();
    ImGui::TextUnformatted("Create a material asset from these settings");
    if (new_path_buf_[0] == '\0')
        std::snprintf(new_path_buf_, sizeof new_path_buf_, "assets/materials/%s.mat",
                      entity->GetName().c_str());
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputText("##new_mat_path", new_path_buf_, sizeof new_path_buf_);
    ImGui::SameLine();
    if (ImGui::Button("Create")) {
        std::string path = new_path_buf_;
        if (path.empty()) {
            spdlog::warn("[MaterialEditor] give the new material a path first");
        } else {
            if (!gws::assets::is_material_path(path)) path += gws::assets::kMaterialExtension;
            MaterialDesc created = m;
            created.name = std::filesystem::path(path).stem().string();
            if (materials->save(path, created)) {
                // Assign it, so "Create" means the object is now using it —
                // creating a file the object ignores would be a confusing
                // half-action.
                mr->SetMaterialPath(path);
                edit_path_.clear();
                changed = true;
                spdlog::info("[MaterialEditor] created '{}' and assigned it to '{}'",
                             path, entity->GetName());
            }
        }
    }
    ImGui::TextDisabled("The object switches to the new asset, and other objects\n"
                        "can then be pointed at the same file.");

    return changed;
}

bool MaterialEditorPanel::render_presets(MaterialDesc& target) {
    ImGui::TextDisabled("Sets base colour, metallic and roughness. Textures are left alone.");
    ImGui::Separator();

    bool applied = false;
    for (const auto& p : kPresets) {
        ImGui::PushID(p.name);

        // Swatch first, so the list is scannable by colour rather than by
        // reading nine words.
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float  h   = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(
            pos, ImVec2(pos.x + h * 1.6f, pos.y + h),
            ImGui::GetColorU32(ImVec4(p.albedo.r, p.albedo.g, p.albedo.b, 1.0f)), 2.0f);
        ImGui::Dummy(ImVec2(h * 1.6f, h));
        ImGui::SameLine();

        if (ImGui::Selectable(p.name)) {
            target.base_color = glm::vec4(p.albedo, target.base_color.a);
            target.metallic   = p.metallic;
            target.roughness  = p.roughness;
            applied = true;
        }
        ImGui::PopID();
    }
    return applied;
}

}  // namespace schizo::editor
