#pragma once

// ============================================================================
// F5 · Item-definition data files (.items) — author items as DATA, not code.
// ============================================================================
//
// A `.items` file holds one or more item definitions in a simple, human-editable
// text format; the editor scans the project for these at startup and registers
// them into the item catalog (so the Inventory/Equipment inspectors + loot see
// them without any code change). This is the G4 slice of the deferred F5
// "data-def asset types" work; the same pattern extends to skills/loot.
//
//   # comments start with '#'
//   item potion_hp
//     name Health Potion
//     kind consumable          # misc|weapon|armor|consumable|material|currency
//     rarity 0
//     max_stack 10
//     weight 0.1
//     on_use Health 40         # apply an instant effect (attribute, amount) on use
//     about Restores 40 health # a line for the player (inventory screens)
//     casts frag               # used: this ability goes off from the view (grenades)
//   end
//
//   item sword_iron
//     name Iron Sword
//     kind weapon
//     slot weapon              # equip slot ("" = not equippable)
//     weight 3
//     mod AttackPower 12       # equipped modifier (attribute, amount)
//     tag weapon.sword         # granted while equipped
//     affix AttackPower 3 5 of Might   # rollable affix: attribute min max name...
//     set guardian             # set id (for set bonuses)
//   end

#include "gameplay_items.h"
#include "gameplay_weapons.h"
#include "gameplay_ability_cast.h"
#include "gameplay_skills.h"
#include "vfx/effects.h"   // effect blocks: what guns and abilities look like

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace schizo::ecs {

inline ItemKind parse_item_kind(const std::string& s) {
    if (s == "weapon")     return ItemKind::Weapon;
    if (s == "armor")      return ItemKind::Armor;
    if (s == "consumable") return ItemKind::Consumable;
    if (s == "material")   return ItemKind::Material;
    if (s == "currency")   return ItemKind::Currency;
    return ItemKind::Misc;
}
inline const char* item_kind_name(ItemKind k) {
    switch (k) {
        case ItemKind::Weapon:     return "weapon";
        case ItemKind::Armor:      return "armor";
        case ItemKind::Consumable: return "consumable";
        case ItemKind::Material:   return "material";
        case ItemKind::Currency:   return "currency";
        default:                   return "misc";
    }
}

namespace detail {
inline std::string trim(std::string s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
}  // namespace detail

// Parse .items text into a list of ItemDefs (forgiving: unknown keys ignored).
inline std::vector<ItemDef> parse_items_text(const std::string& text) {
    std::vector<ItemDef> out;
    ItemDef cur; bool have = false;
    auto flush = [&] { if (have && !cur.id.empty()) out.push_back(cur); cur = ItemDef{}; have = false; };

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key)) continue;
        if (key.empty() || key[0] == '#') continue;

        if (key == "item")      { flush(); ls >> cur.id; have = true; }
        else if (key == "weapon" || key == "ability" || key == "skill") { flush(); }   // not an item block
        else if (!have)         { continue; }
        else if (key == "end")  { flush(); }
        else if (key == "name")      { std::string rest; std::getline(ls, rest); cur.name = detail::trim(rest); }
        else if (key == "kind")      { std::string v; ls >> v; cur.kind = parse_item_kind(v); }
        else if (key == "rarity")    { ls >> cur.rarity; }
        else if (key == "max_stack") { ls >> cur.max_stack; }
        else if (key == "weight")    { ls >> cur.weight; }
        else if (key == "value")     { ls >> cur.value; }
        else if (key == "slot")      { ls >> cur.equip_slot; }
        else if (key == "set")       { ls >> cur.set_id; }
        else if (key == "tag")       { std::string t; ls >> t; if (!t.empty()) cur.granted_tags.push_back(t); }
        else if (key == "mod")       { ItemModifier m; ls >> m.attribute >> m.amount; if (!m.attribute.empty()) cur.modifiers.push_back(m); }
        else if (key == "on_use")    { std::string a; float amt = 0; ls >> a >> amt; if (!a.empty()) cur.on_use.push_back(make_instant("Use", a, amt)); }
        else if (key == "about")     { std::string rest; std::getline(ls, rest); cur.description = detail::trim(rest); }
        else if (key == "casts")     { ls >> cur.cast_ability; }
        else if (key == "affix")     { ItemAffix af; ls >> af.attribute >> af.min_roll >> af.max_roll; std::string rest; std::getline(ls, rest); af.name = detail::trim(rest); cur.affix_pool.push_back(af); }
    }
    flush();
    return out;
}

// Weapon blocks in the same files:
//
//   weapon rifle
//     name Assault Rifle
//     fire auto                # semi | auto | burst
//     damage 22 kinetic
//     rpm 650
//     magazine 30
//     reload 2.1
//     spread 0.6               # degrees, hip fire at rest
//     bloom 0.35 5             # per shot, at most
//     recoil 0.8 0.4           # degrees up, degrees sideways (+/-)
//     aim 0.75 0.3             # field of view while aiming, spread while aiming
//     range 150
//     tracer 3                 # every 3rd round
//     impact 0.12              # mark size where it hits the world
//     flash                    # muzzle flash
//   end
inline std::vector<WeaponDef> parse_weapons_text(const std::string& text) {
    std::vector<WeaponDef> out;
    WeaponDef cur; bool have = false;
    auto flush = [&] { if (have && !cur.id.empty()) out.push_back(cur); cur = WeaponDef{}; have = false; };
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        if (key == "weapon")        { flush(); ls >> cur.id; cur.name = cur.id; have = true; }
        else if (key == "item" || key == "ability" || key == "skill") { flush(); }   // not a weapon block
        else if (!have)             { continue; }
        else if (key == "end")      { flush(); }
        else if (key == "name")     { std::string rest; std::getline(ls, rest); cur.name = detail::trim(rest); }
        else if (key == "fire")     { std::string v; ls >> v; cur.fire_mode = v == "auto" ? FireMode::Auto : v == "burst" ? FireMode::Burst : FireMode::Semi; }
        else if (key == "burst")    { ls >> cur.burst; }
        else if (key == "damage")   { ls >> cur.damage; std::string ty; if (ls >> ty) cur.damage_type = ty; }
        else if (key == "rpm")      { ls >> cur.rpm; }
        else if (key == "magazine") { ls >> cur.mag_size; }
        else if (key == "draw")     { ls >> cur.draw_time; }
        else if (key == "pellets")  { ls >> cur.pellets; }
        else if (key == "reload")   { ls >> cur.reload_time; }
        else if (key == "spread")   { ls >> cur.spread_deg; }
        else if (key == "bloom")    { ls >> cur.spread_bloom; float mx; if (ls >> mx) cur.spread_max = mx; float rc; if (ls >> rc) cur.bloom_recovery = rc; }
        else if (key == "recoil")   { ls >> cur.recoil_pitch; float y; if (ls >> y) cur.recoil_yaw = y; }
        else if (key == "aim")      { ls >> cur.ads_fov_scale; float s; if (ls >> s) cur.ads_spread = s; float tm; if (ls >> tm) cur.ads_time = tm; }
        else if (key == "range")    { ls >> cur.range; }
        else if (key == "projectile") { ls >> cur.projectile_speed; }
        else if (key == "tracer")   { ls >> cur.tracer_every; float r, g, b; if (ls >> r >> g >> b) cur.tracer_color = glm::vec3(r, g, b); }
        else if (key == "impact")   { ls >> cur.impact_size; }
        else if (key == "flash")    { cur.muzzle_flash = true; }
        else if (key == "ammo")     { ls >> cur.ammo_item; }
    }
    flush();
    return out;
}

// Skill blocks: one node of a skill tree each.
//
//   skill hunter_frost
//     name Frost Nova
//     tree hunter              # which tree it belongs to (Set Skill Tree)
//     cost 1                   # skill points
//     requires hunter_fire     # learned first (one line each)
//     unlocks frost_nova       # an ability put on the Ability Bar when learned
//     grant Health 25          # a lasting bonus to an attribute
//     tag perk.frost           # a gameplay tag the player gains
//   end
inline std::vector<std::pair<std::string, SkillNode>> parse_skills_text(const std::string& text) {
    std::vector<std::pair<std::string, SkillNode>> out;
    SkillNode cur;
    std::string tree;
    bool have = false;
    auto flush = [&] {
        if (have && !cur.id.empty()) out.push_back({tree.empty() ? std::string("default") : tree, cur});
        cur = SkillNode{};
        tree.clear();
        have = false;
    };
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key) || key[0] == '#') continue;
        if (key == "skill") { flush(); ls >> cur.id; cur.name = cur.id; have = true; }
        else if (key == "item" || key == "weapon" || key == "ability") { flush(); }
        else if (!have) { continue; }
        else if (key == "end") { flush(); }
        else if (key == "name") { std::string r; std::getline(ls, r); cur.name = detail::trim(r); }
        else if (key == "tree") { ls >> tree; }
        else if (key == "cost") { ls >> cur.cost; }
        else if (key == "requires") { std::string r; ls >> r; if (!r.empty()) cur.prerequisites.push_back(r); }
        else if (key == "unlocks") { std::string a; ls >> a; if (!a.empty()) cur.abilities.push_back(a); }
        else if (key == "grant") { StatGrant g; ls >> g.attribute >> g.amount; if (!g.attribute.empty()) cur.grants.push_back(g); }
        else if (key == "tag") { std::string g; ls >> g; if (!g.empty()) cur.granted_tags.push_back(g); }
    }
    flush();
    return out;
}

// Register every item and weapon in the text. Returns the number registered.
inline int register_items_text(const std::string& text) {
    int n = 0;
    for (const auto& d : parse_items_text(text)) { register_item(d); ++n; }
    for (const auto& d : parse_weapons_text(text)) { register_weapon(d); ++n; }
    for (const auto& d : parse_abilities_text(text)) { register_ability_def(d); ++n; }
    for (const auto& [tree, node] : parse_skills_text(text)) { register_skill_def(tree, node); ++n; }
    for (const auto& d : schizo::vfx::parse_effects_text(text)) { schizo::vfx::register_effect(d); ++n; }
    return n;
}

// Load + register a single .items file. Returns items registered (0 if unreadable).
inline int load_items_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    std::stringstream ss; ss << f.rdbuf();
    return register_items_text(ss.str());
}

// Recursively scan a directory for *.items and register them all.
inline int load_items_dir(const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return 0;
    int total = 0;
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && it->path().extension() == ".items")
            total += load_items_file(it->path().string());
    }
    return total;
}

// ---- A .items file as a document (the Items editor) ---------------------------
// The blocks in file order, and the comment lines above the first block. A
// file written back from it keeps its blocks and their order; comments between
// blocks are not kept.
struct ItemsDoc {
    enum class Kind : uint8_t { Item, Weapon, Ability, Text };   // Text: a block kept as written (skills)
    struct Entry {
        Kind       kind = Kind::Item;
        ItemDef    item;
        WeaponDef  weapon;
        AbilityDef ability;
        std::string text, text_id;   // Kind::Text: the block as written, and its id
        bool is_weapon() const { return kind == Kind::Weapon; }
        bool is_ability() const { return kind == Kind::Ability; }
        const std::string& id() const {
            return kind == Kind::Weapon ? weapon.id : kind == Kind::Ability ? ability.id : kind == Kind::Text ? text_id : item.id;
        }
        const std::string& name() const {
            return kind == Kind::Weapon ? weapon.name : kind == Kind::Ability ? ability.name : kind == Kind::Text ? text_id : item.name;
        }
        std::string& id_ref() {
            return kind == Kind::Weapon ? weapon.id : kind == Kind::Ability ? ability.id : kind == Kind::Text ? text_id : item.id;
        }
    };
    std::string        header;   // leading '#' lines, kept verbatim
    std::vector<Entry> entries;
};

inline ItemsDoc parse_items_doc(const std::string& text) {
    ItemsDoc doc;
    std::istringstream in(text);
    std::string line, block;
    bool in_header = true, in_block = false;
    std::string block_key;
    auto flush = [&] {
        if (!in_block) return;
        in_block = false;
        if (block_key == "skill" || block_key == "effect") {   // kept as written
            ItemsDoc::Entry e;
            e.kind = ItemsDoc::Kind::Text;
            e.text = block;
            std::istringstream ls(block);
            std::string k;
            ls >> k >> e.text_id;
            doc.entries.push_back(e);
            block.clear();
            return;
        }
        for (const auto& d : parse_items_text(block)) { ItemsDoc::Entry e; e.item = d; doc.entries.push_back(e); }
        for (const auto& d : parse_weapons_text(block)) { ItemsDoc::Entry e; e.kind = ItemsDoc::Kind::Weapon; e.weapon = d; doc.entries.push_back(e); }
        for (const auto& d : parse_abilities_text(block)) { ItemsDoc::Entry e; e.kind = ItemsDoc::Kind::Ability; e.ability = d; doc.entries.push_back(e); }
        block.clear();
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "item" || key == "weapon" || key == "ability" || key == "skill" || (key == "effect" && !in_block)) {
            flush();
            block_key = key;
            in_header = false;
            in_block = true;
            block = line + "\n";
            continue;
        }
        if (in_header) {
            if (key.empty() || key[0] == '#') doc.header += line + "\n";
            continue;
        }
        if (!in_block) continue;
        block += line + "\n";
        if (key == "end") flush();
    }
    flush();
    // Trailing blank lines of the header are the writer's to add.
    while (doc.header.size() >= 2 && doc.header[doc.header.size() - 1] == '\n' && doc.header[doc.header.size() - 2] == '\n')
        doc.header.pop_back();
    return doc;
}

namespace detail {
// Shortest text that reads back as the same float.
inline std::string num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v));
    for (int prec = 1; prec <= 9; ++prec) {
        char t[32];
        std::snprintf(t, sizeof t, "%.*g", prec, static_cast<double>(v));
        if (std::strtof(t, nullptr) == v) return t;
    }
    return buf;
}
}  // namespace detail

inline std::string item_def_text(const ItemDef& d) {
    using detail::num;
    std::string s = "item " + d.id + "\n";
    if (!d.name.empty()) s += "  name " + d.name + "\n";
    if (d.kind != ItemKind::Misc) s += std::string("  kind ") + item_kind_name(d.kind) + "\n";
    if (d.rarity != 0) s += "  rarity " + std::to_string(d.rarity) + "\n";
    if (d.max_stack != 1) s += "  max_stack " + std::to_string(d.max_stack) + "\n";
    if (d.weight != 0.0f) s += "  weight " + num(d.weight) + "\n";
    if (d.value != 0.0f) s += "  value " + num(d.value) + "\n";
    if (!d.equip_slot.empty()) s += "  slot " + d.equip_slot + "\n";
    if (!d.set_id.empty()) s += "  set " + d.set_id + "\n";
    for (const auto& t : d.granted_tags) s += "  tag " + t + "\n";
    for (const auto& m : d.modifiers) s += "  mod " + m.attribute + " " + num(m.amount) + "\n";
    for (const auto& fx : d.on_use)
        for (const auto& m : fx.modifiers) s += "  on_use " + m.attribute + " " + num(m.magnitude) + "\n";
    if (!d.cast_ability.empty()) s += "  casts " + d.cast_ability + "\n";
    if (!d.description.empty()) s += "  about " + d.description + "\n";
    for (const auto& a : d.affix_pool) {
        s += "  affix " + a.attribute + " " + num(a.min_roll) + " " + num(a.max_roll);
        if (!a.name.empty()) s += " " + a.name;
        s += "\n";
    }
    return s + "end\n";
}

inline std::string weapon_def_text(const WeaponDef& w) {
    using detail::num;
    const WeaponDef def;
    std::string s = "weapon " + w.id + "\n";
    if (!w.name.empty() && w.name != w.id) s += "  name " + w.name + "\n";
    s += std::string("  fire ") + (w.fire_mode == FireMode::Auto ? "auto" : w.fire_mode == FireMode::Burst ? "burst" : "semi") + "\n";
    if (w.fire_mode == FireMode::Burst || w.burst != def.burst) s += "  burst " + std::to_string(w.burst) + "\n";
    s += "  damage " + num(w.damage) + (w.damage_type.empty() ? "" : " " + w.damage_type) + "\n";
    s += "  rpm " + num(w.rpm) + "\n";
    s += "  magazine " + std::to_string(w.mag_size) + "\n";
    if (w.draw_time != def.draw_time) s += "  draw " + num(w.draw_time) + "\n";
    if (w.reload_time != def.reload_time) s += "  reload " + num(w.reload_time) + "\n";
    if (w.pellets != def.pellets) s += "  pellets " + std::to_string(w.pellets) + "\n";
    s += "  spread " + num(w.spread_deg) + "\n";
    if (w.spread_bloom != def.spread_bloom || w.spread_max != def.spread_max || w.bloom_recovery != def.bloom_recovery)
        s += "  bloom " + num(w.spread_bloom) + " " + num(w.spread_max) + " " + num(w.bloom_recovery) + "\n";
    if (w.recoil_pitch != def.recoil_pitch || w.recoil_yaw != def.recoil_yaw)
        s += "  recoil " + num(w.recoil_pitch) + " " + num(w.recoil_yaw) + "\n";
    if (w.ads_fov_scale != def.ads_fov_scale || w.ads_spread != def.ads_spread || w.ads_time != def.ads_time)
        s += "  aim " + num(w.ads_fov_scale) + " " + num(w.ads_spread) + " " + num(w.ads_time) + "\n";
    if (w.range != def.range) s += "  range " + num(w.range) + "\n";
    if (w.projectile_speed != def.projectile_speed) s += "  projectile " + num(w.projectile_speed) + "\n";
    if (w.tracer_every != 0) {
        s += "  tracer " + std::to_string(w.tracer_every);
        if (w.tracer_color != def.tracer_color)
            s += " " + num(w.tracer_color.r) + " " + num(w.tracer_color.g) + " " + num(w.tracer_color.b);
        s += "\n";
    }
    if (w.impact_size != 0.0f) s += "  impact " + num(w.impact_size) + "\n";
    if (w.muzzle_flash) s += "  flash\n";
    if (!w.ammo_item.empty()) s += "  ammo " + w.ammo_item + "\n";
    return s + "end\n";
}

inline std::string ability_def_text(const AbilityDef& a) {
    using detail::num;
    const AbilityDef def;
    std::string s = "ability " + a.id + "\n";
    if (!a.name.empty() && a.name != a.id) s += "  name " + a.name + "\n";
    if (!a.cost_attribute.empty()) s += "  cost " + a.cost_attribute + " " + num(a.cost) + "\n";
    if (a.cooldown != 0.0f) s += "  cooldown " + num(a.cooldown) + "\n";
    if (a.cast_time != 0.0f) s += "  cast " + num(a.cast_time) + "\n";
    if (!a.cast_while_moving) s += "  still\n";
    if (a.channel_time > 0.0f) s += "  channel " + num(a.channel_time) + " " + num(a.channel_period) + "\n";
    s += std::string("  target ") + ability_targeting_name(a.targeting) + "\n";
    if (a.range != def.range) s += "  range " + num(a.range) + "\n";
    if (a.radius != def.radius) s += "  radius " + num(a.radius) + "\n";
    if (a.damage != 0.0f || a.damage_type != def.damage_type)
        s += "  damage " + num(a.damage) + (a.damage_type.empty() ? "" : " " + a.damage_type) + "\n";
    auto eff = [&](const char* key, const AbilityEffect& e) {
        s += std::string("  ") + key + " " + e.attribute + " " + num(e.amount);
        if (e.duration > 0.0f) s += " " + num(e.duration);
        s += "\n";
    };
    for (const auto& e : a.self_effects) eff("self", e);
    for (const auto& e : a.target_effects) eff("effect", e);
    for (const auto& t : a.require_tags) s += "  require " + t + "\n";
    for (const auto& t : a.block_tags) s += "  block " + t + "\n";
    if (!a.vfx.empty()) s += "  vfx " + a.vfx + "\n";
    if (a.speed > 0.0f) s += "  speed " + num(a.speed) + "\n";
    if (a.arc > 0.0f) s += "  arc " + num(a.arc) + "\n";
    return s + "end\n";
}

inline std::string items_doc_to_text(const ItemsDoc& doc) {
    std::string s = doc.header;
    for (const auto& e : doc.entries) {
        if (!s.empty()) s += "\n";
        s += e.kind == ItemsDoc::Kind::Weapon ? weapon_def_text(e.weapon)
           : e.kind == ItemsDoc::Kind::Ability ? ability_def_text(e.ability)
           : e.kind == ItemsDoc::Kind::Text ? e.text
           : item_def_text(e.item);
    }
    return s;
}

// A starter .items file for the "New Item Definitions" editor action.
inline const char* item_file_template() {
    return "# Item definitions (.items). One 'item <id>' block per item; the editor\n"
           "# loads every .items file under the project at startup / on reload.\n"
           "\n"
           "item my_potion\n"
           "  name My Potion\n"
           "  kind consumable\n"
           "  max_stack 10\n"
           "  weight 0.1\n"
           "  on_use Health 25\n"
           "end\n"
           "\n"
           "weapon my_rifle\n"
           "  name My Rifle\n"
           "  fire auto\n"
           "  damage 20 kinetic\n"
           "  rpm 600\n"
           "  magazine 30\n"
           "  spread 0.6\n"
           "  bloom 0.35 5\n"
           "  recoil 0.8 0.4\n"
           "  aim 0.75 0.3\n"
           "  tracer 3\n"
           "  impact 0.12\n"
           "  flash\n"
           "end\n"
           "\n"
           "ability my_firebolt\n"
           "  name Firebolt\n"
           "  cost Mana 15\n"
           "  cooldown 2\n"
           "  cast 0.4\n"
           "  target aimed\n"
           "  range 30\n"
           "  damage 35 fire\n"
           "  vfx firebolt\n"
           "end\n"
           "\n"
           "item my_sword\n"
           "  name My Sword\n"
           "  kind weapon\n"
           "  slot weapon\n"
           "  weight 3\n"
           "  mod AttackPower 10\n"
           "  affix AttackPower 2 4 of Power\n"
           "end\n";
}

}  // namespace schizo::ecs
