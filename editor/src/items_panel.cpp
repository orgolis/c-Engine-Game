#include "items_panel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace schizo::editor {

namespace ecs = schizo::ecs;

bool load_items_doc(const std::string& path, ecs::ItemsDoc& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ecs::parse_items_doc(ss.str());
    return true;
}

bool save_items_doc(const std::string& path, const ecs::ItemsDoc& doc) {
    const std::string text = ecs::items_doc_to_text(doc);
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        if (!f) return false;
    }
    ecs::register_items_text(text);   // Play and the Inspector see it now
    return true;
}

namespace {

// A text field on a std::string.
bool input_str(const char* label, std::string& s, const char* hint = nullptr) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s", s.c_str());
    const bool changed = hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof buf)
                              : ImGui::InputText(label, buf, sizeof buf);
    if (changed) s = buf;
    return changed;
}

// Ids are single words: spaces become underscores.
bool input_id(const char* label, std::string& s) {
    if (!input_str(label, s)) return false;
    for (char& c : s) if (std::isspace(static_cast<unsigned char>(c))) c = '_';
    return true;
}

std::string unique_id(const ecs::ItemsDoc& d, const std::string& base) {
    auto taken = [&](const std::string& id) {
        for (const auto& e : d.entries) if (e.id() == id) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        const std::string id = base + "_" + std::to_string(i);
        if (!taken(id)) return id;
    }
}

bool id_clashes(const ecs::ItemsDoc& d, size_t index) {
    const std::string& id = d.entries[index].id();
    if (id.empty()) return true;
    for (size_t i = 0; i < d.entries.size(); ++i)
        if (i != index && d.entries[i].id() == id) return true;
    return false;
}

void draw_item(ecs::ItemDef& it, const ecs::ItemsDoc& d) {
    (void)d;
    input_id("id", it.id);
    input_str("name", it.name);
    const char* kinds[] = {"misc", "weapon", "armor", "consumable", "material", "currency"};
    const ecs::ItemKind kind_of[] = {ecs::ItemKind::Misc, ecs::ItemKind::Weapon, ecs::ItemKind::Armor,
                                     ecs::ItemKind::Consumable, ecs::ItemKind::Material, ecs::ItemKind::Currency};
    int cur = 0;
    for (int i = 0; i < 6; ++i) if (kind_of[i] == it.kind) cur = i;
    if (ImGui::Combo("kind", &cur, kinds, 6)) it.kind = kind_of[cur];
    ImGui::DragInt("rarity", &it.rarity, 0.05f, 0, 20);
    ImGui::DragInt("max stack", &it.max_stack, 0.2f, 1, 100000);
    ImGui::DragFloat("weight", &it.weight, 0.01f, 0.0f, 10000.0f, "%.2f");
    ImGui::DragFloat("value", &it.value, 0.1f, 0.0f, 1e7f, "%.1f");
    input_str("equip slot", it.equip_slot, "none (weapon, head, chest...)");
    input_str("set", it.set_id, "not part of a set");

    if (ImGui::TreeNodeEx("Bonuses while equipped", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        for (size_t i = 0; i < it.modifiers.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(140.0f);
            input_str("##attr", it.modifiers[i].attribute, "attribute");
            ImGui::SameLine(); ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("##amt", &it.modifiers[i].amount, 0.1f);
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove >= 0) it.modifiers.erase(it.modifiers.begin() + remove);
        if (ImGui::SmallButton("Add bonus")) it.modifiers.push_back({"AttackPower", 1.0f});
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Tags while equipped", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        for (size_t i = 0; i < it.granted_tags.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(230.0f);
            input_id("##tag", it.granted_tags[i]);
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove >= 0) it.granted_tags.erase(it.granted_tags.begin() + remove);
        if (ImGui::SmallButton("Add tag")) it.granted_tags.push_back("item.tag");
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("When used", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        for (size_t i = 0; i < it.on_use.size(); ++i) {
            if (it.on_use[i].modifiers.empty()) it.on_use[i].modifiers.push_back({"Health", ecs::EffectOp::Add, 0.0f});
            auto& m = it.on_use[i].modifiers[0];
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(140.0f);
            input_str("##attr", m.attribute, "attribute");
            ImGui::SameLine(); ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("##amt", &m.magnitude, 0.5f);
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove >= 0) it.on_use.erase(it.on_use.begin() + remove);
        if (ImGui::SmallButton("Add effect")) it.on_use.push_back(ecs::make_instant("Use", "Health", 25.0f));
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Random bonuses (affixes)")) {
        int remove = -1;
        for (size_t i = 0; i < it.affix_pool.size(); ++i) {
            auto& a = it.affix_pool[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(110.0f);
            input_str("##attr", a.attribute, "attribute");
            ImGui::SameLine(); ImGui::SetNextItemWidth(55.0f);
            ImGui::DragFloat("##min", &a.min_roll, 0.1f);
            ImGui::SameLine(); ImGui::SetNextItemWidth(55.0f);
            ImGui::DragFloat("##max", &a.max_roll, 0.1f);
            ImGui::SameLine(); ImGui::SetNextItemWidth(100.0f);
            input_str("##name", a.name, "name");
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove >= 0) it.affix_pool.erase(it.affix_pool.begin() + remove);
        if (ImGui::SmallButton("Add affix")) it.affix_pool.push_back({"of Power", "AttackPower", 1.0f, 3.0f});
        ImGui::TreePop();
    }
}

void draw_weapon(ecs::WeaponDef& w, const ecs::ItemsDoc& d) {
    input_id("id", w.id);
    input_str("name", w.name);
    int mode = w.fire_mode == ecs::FireMode::Auto ? 1 : w.fire_mode == ecs::FireMode::Burst ? 2 : 0;
    const char* modes[] = {"single shots", "automatic", "burst"};
    if (ImGui::Combo("fire", &mode, modes, 3))
        w.fire_mode = mode == 1 ? ecs::FireMode::Auto : mode == 2 ? ecs::FireMode::Burst : ecs::FireMode::Semi;
    if (w.fire_mode == ecs::FireMode::Burst) ImGui::DragInt("rounds per burst", &w.burst, 0.05f, 1, 20);

    if (ImGui::TreeNodeEx("Damage and ammunition", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("damage", &w.damage, 0.1f, 0.0f, 100000.0f, "%.1f");
        input_id("damage type", w.damage_type);
        ImGui::DragFloat("rounds per minute", &w.rpm, 1.0f, 1.0f, 6000.0f, "%.0f");
        ImGui::DragInt("magazine", &w.mag_size, 0.2f, 1, 10000);
        ImGui::DragInt("pellets", &w.pellets, 0.05f, 1, 64);
        ImGui::SetItemTooltip("More than 1: a shotgun.");
        ImGui::DragFloat("reload", &w.reload_time, 0.01f, 0.0f, 30.0f, "%.2f s");
        ImGui::DragFloat("range", &w.range, 0.5f, 0.1f, 100000.0f, "%.0f m");
        ImGui::DragFloat("projectile speed", &w.projectile_speed, 1.0f, 0.0f, 10000.0f, "%.0f m/s");
        ImGui::SetItemTooltip("0: hits at once (hitscan).");
        // Ammunition: an item in the inventory, picked from the file's and the
        // registered items.
        const char* none = "(own reserve)";
        if (ImGui::BeginCombo("ammunition item", w.ammo_item.empty() ? none : w.ammo_item.c_str())) {
            if (ImGui::Selectable(none, w.ammo_item.empty())) w.ammo_item.clear();
            std::vector<std::string> ids;
            for (const auto& e : d.entries) if (!e.is_weapon && !e.item.id.empty()) ids.push_back(e.item.id);
            for (const auto& it : ecs::item_registry())
                if (std::find(ids.begin(), ids.end(), it.id) == ids.end()) ids.push_back(it.id);
            for (const auto& id : ids)
                if (ImGui::Selectable(id.c_str(), id == w.ammo_item)) w.ammo_item = id;
            ImGui::EndCombo();
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Accuracy and recoil", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("spread", &w.spread_deg, 0.01f, 0.0f, 45.0f, "%.2f deg");
        ImGui::DragFloat("bloom per shot", &w.spread_bloom, 0.01f, 0.0f, 20.0f, "%.2f deg");
        ImGui::DragFloat("bloom at most", &w.spread_max, 0.05f, 0.0f, 90.0f, "%.1f deg");
        ImGui::DragFloat("bloom recovery", &w.bloom_recovery, 0.05f, 0.0f, 200.0f, "%.1f deg/s");
        ImGui::DragFloat("recoil up", &w.recoil_pitch, 0.01f, 0.0f, 30.0f, "%.2f deg");
        ImGui::DragFloat("recoil sideways", &w.recoil_yaw, 0.01f, 0.0f, 30.0f, "%.2f deg");
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Aiming", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("view while aiming", &w.ads_fov_scale, 0.01f, 0.1f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("Field of view as a share of normal: 0.5 zooms in twice.");
        ImGui::DragFloat("spread while aiming", &w.ads_spread, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("time to aim", &w.ads_time, 0.005f, 0.0f, 2.0f, "%.3f s");
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("muzzle flash", &w.muzzle_flash);
        ImGui::DragInt("tracer every", &w.tracer_every, 0.05f, 0, 100);
        ImGui::SetItemTooltip("A visible tracer every Nth round; 0 for none.");
        if (w.tracer_every > 0) ImGui::ColorEdit3("tracer colour", &w.tracer_color.x);
        ImGui::DragFloat("impact mark", &w.impact_size, 0.005f, 0.0f, 5.0f, "%.3f m");
        ImGui::TreePop();
    }
    // What it adds up to.
    const float per_s = w.rpm / 60.0f;
    ImGui::TextDisabled("%.0f damage a second, a magazine lasts %.1f s",
                        w.damage * std::max(1, w.pellets) * per_s,
                        per_s > 0.0f ? static_cast<float>(w.mag_size) / per_s : 0.0f);
}

}  // namespace

void draw_items_panel(bool& open, ItemsPanelState& st, DocumentFile* doc) {
    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Items", &open)) { ImGui::End(); return; }
    if (doc) draw_document_bar(*doc, "items file");
    ecs::ItemsDoc& d = st.doc;
    if (d.entries.empty()) st.selected = 0;
    else st.selected = std::clamp(st.selected, 0, static_cast<int>(d.entries.size()) - 1);

    // ---- the list ----------------------------------------------------------
    ImGui::BeginChild("##items_list", ImVec2(250.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter", st.filter, sizeof st.filter);
    if (ImGui::Button("+ Item")) {
        ecs::ItemsDoc::Entry e;
        e.item.id = unique_id(d, "new_item");
        e.item.name = "New Item";
        d.entries.push_back(e);
        st.selected = static_cast<int>(d.entries.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Weapon")) {
        ecs::ItemsDoc::Entry e;
        e.is_weapon = true;
        e.weapon.id = unique_id(d, "new_weapon");
        e.weapon.name = "New Weapon";
        d.entries.push_back(e);
        st.selected = static_cast<int>(d.entries.size()) - 1;
    }
    const bool has_sel = !d.entries.empty();
    ImGui::BeginDisabled(!has_sel);
    if (ImGui::Button("Duplicate") && has_sel) {
        ecs::ItemsDoc::Entry e = d.entries[st.selected];
        std::string& id = e.is_weapon ? e.weapon.id : e.item.id;
        id = unique_id(d, id + "_copy");
        d.entries.insert(d.entries.begin() + st.selected + 1, e);
        ++st.selected;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && has_sel) {
        d.entries.erase(d.entries.begin() + st.selected);
        st.selected = std::max(0, st.selected - 1);
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##up", ImGuiDir_Up) && has_sel && st.selected > 0) {
        std::swap(d.entries[st.selected], d.entries[st.selected - 1]);
        --st.selected;
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##down", ImGuiDir_Down) && has_sel && st.selected + 1 < static_cast<int>(d.entries.size())) {
        std::swap(d.entries[st.selected], d.entries[st.selected + 1]);
        ++st.selected;
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    std::string f = st.filter;
    std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (size_t i = 0; i < d.entries.size(); ++i) {
        const auto& e = d.entries[i];
        std::string label = (e.name().empty() ? e.id() : e.name());
        if (!f.empty()) {
            std::string hay = label + " " + e.id();
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (hay.find(f) == std::string::npos) continue;
        }
        ImGui::PushID(static_cast<int>(i));
        const bool clash = id_clashes(d, i);
        if (clash) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        const std::string row = std::string(e.is_weapon ? "[weapon] " : "") + label;
        if (ImGui::Selectable(row.c_str(), st.selected == static_cast<int>(i))) st.selected = static_cast<int>(i);
        if (clash) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s", e.id().c_str(), clash ? "  (id empty or used twice)" : "");
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- the selected one --------------------------------------------------
    ImGui::BeginChild("##items_form", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (d.entries.empty()) {
        ImGui::TextDisabled("No items or weapons yet. Add one with + Item or + Weapon.");
    } else {
        auto& e = d.entries[st.selected];
        ImGui::PushID(st.selected);
        ImGui::PushItemWidth(-150.0f);
        if (e.is_weapon) draw_weapon(e.weapon, d);
        else             draw_item(e.item, d);
        ImGui::PopItemWidth();
        if (id_clashes(d, static_cast<size_t>(st.selected)))
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "The id is empty or used by another entry in this file.");
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Saving rewrites the file: the comments at the top stay, comments between entries do not.");
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace schizo::editor
