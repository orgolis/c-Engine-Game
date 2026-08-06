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

#include <cctype>
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
        else if (!have)         { continue; }
        else if (key == "end")  { flush(); }
        else if (key == "name")      { std::string rest; std::getline(ls, rest); cur.name = detail::trim(rest); }
        else if (key == "kind")      { std::string v; ls >> v; cur.kind = parse_item_kind(v); }
        else if (key == "rarity")    { ls >> cur.rarity; }
        else if (key == "max_stack") { ls >> cur.max_stack; }
        else if (key == "weight")    { ls >> cur.weight; }
        else if (key == "slot")      { ls >> cur.equip_slot; }
        else if (key == "set")       { ls >> cur.set_id; }
        else if (key == "tag")       { std::string t; ls >> t; if (!t.empty()) cur.granted_tags.push_back(t); }
        else if (key == "mod")       { ItemModifier m; ls >> m.attribute >> m.amount; if (!m.attribute.empty()) cur.modifiers.push_back(m); }
        else if (key == "on_use")    { std::string a; float amt = 0; ls >> a >> amt; if (!a.empty()) cur.on_use.push_back(make_instant("Use", a, amt)); }
        else if (key == "affix")     { ItemAffix af; ls >> af.attribute >> af.min_roll >> af.max_roll; std::string rest; std::getline(ls, rest); af.name = detail::trim(rest); cur.affix_pool.push_back(af); }
    }
    flush();
    return out;
}

// Register every item in the text. Returns the number registered.
inline int register_items_text(const std::string& text) {
    int n = 0;
    for (const auto& d : parse_items_text(text)) { register_item(d); ++n; }
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
