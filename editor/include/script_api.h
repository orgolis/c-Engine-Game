#pragma once

// ============================================================
// ScriptApi — the engine surface exposed to user scripts (Stage 12)
// ============================================================
//
// One plain-C function table shared by EVERY script backend (Python / C++ /
// C#): backends marshal their language's calls onto these entries, so adding
// an engine capability here makes it available to all three languages at once.
// Deliberately C-ABI-friendly (opaque ctx pointer + POD args) because the C++
// backend hands this exact struct to user-compiled DLLs and the C# backend
// marshals it across P/Invoke.
//
// Entities are addressed by their scene id (uint32_t, 0 = invalid). All
// positions/rotations are world-space; rotations are Euler degrees (XYZ).
// Physics entries are live only during Play mode and no-op otherwise.

#include <cstdint>

namespace schizo::editor {

struct ScriptApi {
    void*  ctx  = nullptr;   // editor-side context (opaque to scripts)
    float  dt   = 0.0f;      // seconds since last frame (filled every frame)
    double time = 0.0;       // seconds since play started

    // ---- logging ----
    void (*log)(void* ctx, const char* msg) = nullptr;

    // ---- entities / transforms ----
    uint32_t (*find_entity)(void* ctx, const char* name) = nullptr;   // 0 if absent
    bool (*get_position)(void* ctx, uint32_t e, float out[3]) = nullptr;
    void (*set_position)(void* ctx, uint32_t e, const float p[3]) = nullptr;
    bool (*get_rotation_euler)(void* ctx, uint32_t e, float out_deg[3]) = nullptr;
    void (*set_rotation_euler)(void* ctx, uint32_t e, const float deg[3]) = nullptr;
    bool (*get_scale)(void* ctx, uint32_t e, float out[3]) = nullptr;
    void (*set_scale)(void* ctx, uint32_t e, const float s[3]) = nullptr;

    // ---- input ----
    bool (*key_down)(void* ctx, int glfw_key) = nullptr;
    bool (*mouse_down)(void* ctx, int button) = nullptr;      // 0=L 1=R 2=M
    void (*mouse_delta)(void* ctx, float out[2]) = nullptr;   // px this frame

    // ---- spawn / destroy ----
    // shape: 0=cube 1=sphere. dynamic!=0 also adds a dynamic collider (and a
    // live physics body when spawned during Play).
    uint32_t (*spawn_primitive)(void* ctx, int shape, const float pos[3],
                                float size, const float rgba[4], int dynamic) = nullptr;
    void (*destroy_entity)(void* ctx, uint32_t e) = nullptr;

    // ---- physics (Play mode) ----
    void (*set_velocity)(void* ctx, uint32_t e, const float v[3]) = nullptr;
    void (*add_impulse)(void* ctx, uint32_t e, const float i[3]) = nullptr;
    bool (*raycast)(void* ctx, const float origin[3], const float dir[3],
                    float max_dist, float out_pos[3], uint32_t* out_entity) = nullptr;

    // ---- rendering ----
    void (*set_color)(void* ctx, uint32_t e, const float rgba[4]) = nullptr;
    void (*set_emissive)(void* ctx, uint32_t e, const float rgb[3], float intensity) = nullptr;

    // ---- audio ----
    void (*audio_play)(void* ctx, uint32_t e) = nullptr;   // start entity's AudioSource
    void (*audio_stop)(void* ctx, uint32_t e) = nullptr;

    // ---- gameplay (G0–G4) ----
    // Operate on the entity's ECS gameplay components (via the editor's ECS
    // bridge). Entities are the same scene ids used above. Attribute/tag/item
    // names are strings a game defines. No-ops if the entity has no matching
    // component. (Appended at the end so the ABI prefix stays stable.)
    float (*get_attribute)(void* ctx, uint32_t e, const char* name) = nullptr;
    void  (*set_attribute)(void* ctx, uint32_t e, const char* name, float value) = nullptr;
    void  (*adjust_attribute)(void* ctx, uint32_t e, const char* name, float delta) = nullptr;
    bool  (*has_tag)(void* ctx, uint32_t e, const char* tag) = nullptr;
    void  (*add_tag)(void* ctx, uint32_t e, const char* tag) = nullptr;
    void  (*remove_tag)(void* ctx, uint32_t e, const char* tag) = nullptr;
    float (*apply_damage)(void* ctx, uint32_t e, float amount, const char* type) = nullptr;  // -> dealt
    void  (*apply_heal)(void* ctx, uint32_t e, const char* attribute, float amount) = nullptr;
    bool  (*activate_ability)(void* ctx, uint32_t e, int index, uint32_t target) = nullptr;
    void  (*grant_xp)(void* ctx, uint32_t e, float amount) = nullptr;
    int   (*get_level)(void* ctx, uint32_t e) = nullptr;
    bool  (*unlock_skill)(void* ctx, uint32_t e, const char* node_id) = nullptr;
    bool  (*send_state_event)(void* ctx, uint32_t e, const char* event) = nullptr;
    bool  (*in_state)(void* ctx, uint32_t e, const char* state) = nullptr;
    int   (*add_item)(void* ctx, uint32_t e, const char* def_id, int qty) = nullptr;  // -> leftover
    int   (*item_count)(void* ctx, uint32_t e, const char* def_id) = nullptr;
    bool  (*equip_item)(void* ctx, uint32_t e, const char* def_id) = nullptr;
    bool  (*use_item)(void* ctx, uint32_t e, const char* def_id) = nullptr;
    // G7: interact with the nearest interactable in range of entity `e`.
    bool  (*interact)(void* ctx, uint32_t e) = nullptr;
    // G12: fire / reload entity `e`'s Weapon (aims along its Transform forward).
    bool  (*fire_weapon)(void* ctx, uint32_t e) = nullptr;
    bool  (*reload_weapon)(void* ctx, uint32_t e) = nullptr;
    // G13: drive entity `e`'s Vehicle one step (throttle/steer in [-1,1]).
    void  (*drive)(void* ctx, uint32_t e, float throttle, float steer, int brake, int boost) = nullptr;
    // G15: toggle entity `e`'s Flashlight; returns the new on-state.
    bool  (*toggle_flashlight)(void* ctx, uint32_t e) = nullptr;

    // ---- script public parameters (Stage 12 script fields) ----
    // Read the value the Inspector authored for entity `e`'s ScriptComponent
    // parameter `name` (declared in the script via `@param`). Returns `def` when
    // the entity has no override / no ScriptComponent. get_param_string fills
    // `out` (NUL-terminated, capped at out_size) and returns the length written.
    float (*get_param_float)(void* ctx, uint32_t e, const char* name, float def) = nullptr;
    int   (*get_param_int)(void* ctx, uint32_t e, const char* name, int def) = nullptr;
    bool  (*get_param_bool)(void* ctx, uint32_t e, const char* name, int def) = nullptr;
    int   (*get_param_string)(void* ctx, uint32_t e, const char* name,
                              char* out, int out_size, const char* def) = nullptr;

    // ---- world flags + events (bridge to the scene logic graph, G9/G0) ----
    // Flags are a shared key->int store: the visual logic graph's Set/Clear/
    // Toggle/On-Flag nodes and scripts see the same values. emit_event publishes
    // a gameplay event that drives On Event logic nodes, triggers, and quests.
    int  (*get_flag)(void* ctx, const char* key) = nullptr;
    void (*set_flag)(void* ctx, const char* key, int value) = nullptr;
    void (*emit_event)(void* ctx, const char* name) = nullptr;

    // ---- spatial helpers (operate on entity Transforms) ----
    float (*distance)(void* ctx, uint32_t a, uint32_t b) = nullptr;             // world-space
    void  (*translate)(void* ctx, uint32_t e, const float delta[3]) = nullptr;  // move by delta
    bool  (*get_forward)(void* ctx, uint32_t e, float out[3]) = nullptr;        // -Z forward, world

    // ---- editor extensions (Phase 4.8) ----
    // Filled only for EDITOR scripts -- the ones loaded from the project's
    // editor_scripts/ folder, which run in edit mode with no entity. A gameplay
    // script sees these as null and cannot reach into the editor.
    //
    // Appended at the very end, like the gameplay block above, so the ABI prefix
    // the C++ backend hands to user-compiled DLLs stays byte-identical.
    //
    // register_command takes a TOKEN, not a callback: this table crosses a C ABI
    // into three languages, and a function pointer back into a Python VM or a
    // .NET assembly is not something all three can express. The token is the
    // name of a no-argument entry point in the script itself, which every
    // backend already knows how to look up -- pocketpy by module global, the C++
    // DLL by GetProcAddress, C# by method name.
    void (*register_command)(void* ctx, const char* title, const char* category,
                             const char* token) = nullptr;
    /// Run any editor command by its exact title -- built-in or another
    /// extension's. This is the half that makes the palette a real automation
    /// surface rather than a menu: a script can drive anything the editor does.
    bool (*run_command)(void* ctx, const char* title) = nullptr;

    uint32_t (*selected_entity)(void* ctx) = nullptr;              // 0 when nothing is selected
    void     (*select_entity)(void* ctx, uint32_t e) = nullptr;
    int      (*entity_count)(void* ctx) = nullptr;
    uint32_t (*entity_at)(void* ctx, int index) = nullptr;         // 0 when out of range
    int      (*entity_name)(void* ctx, uint32_t e, char* out, int out_size) = nullptr;
    /// Show a message on the editor's status line -- an extension needs a way to
    /// report what it did that is not the log file.
    void (*set_status)(void* ctx, const char* msg) = nullptr;

    // ---- gameplay event feed (added v0.8.17; new entries go at the end) ----
    // Gameplay events published since the previous frame (trigger enter/exit,
    // damage, pickups, anything on the bus). Lets a script REACT to an event
    // instead of polling flags. Index 0 .. event_count()-1; entity ids are
    // scene ids, 0 when the event names none.
    int  (*event_count)(void* ctx) = nullptr;
    bool (*event_get)(void* ctx, int index, char* name_out, int name_size,
                      uint32_t* instigator, uint32_t* target, float* value,
                      char* param_out, int param_size) = nullptr;

    // ---- biomes (added v0.8.29; new entries go at the end) ----
    // The biome entity `e` stands in: the terrain under it with a Biome
    // component, the biome with the most weight there. Writes its name and
    // returns its index, or -1 (name "") where there is none. `weight`, when
    // given, is how much of that biome is there (0..1).
    int  (*biome_at)(void* ctx, uint32_t e, char* name_out, int name_size, float* weight) = nullptr;

    // ---- saving in Play (added v0.8.36; new entries go at the end) ----
    // Save the game to a named slot (saves/<slot>.sav: the player's place and
    // every entity with a SaveId), or load one. False outside Play or when
    // the file cannot be written / read.
    bool (*save_game)(void* ctx, const char* slot) = nullptr;
    bool (*load_game)(void* ctx, const char* slot) = nullptr;

    // ---- messages in the game view (added v0.8.38; new entries go at the end) ----
    // Text shown across the game view for `seconds` (the newest replaces the last).
    void (*show_message)(void* ctx, const char* text, float seconds) = nullptr;

    // ---- the project's UI documents (added v0.8.39; new entries go at the end) ----
    // Show / hide a .ui file over the game; set a widget's text; whether one
    // is shown. A button click arrives as the gameplay event "ui.click" with
    // the button's id as its param (logic scripts: On UI Button).
    bool (*ui_show)(void* ctx, const char* path) = nullptr;
    void (*ui_hide)(void* ctx, const char* path) = nullptr;
    bool (*ui_set_text)(void* ctx, const char* path, const char* id, const char* text) = nullptr;
    bool (*ui_shown)(void* ctx, const char* path) = nullptr;
    // The game stands still (scripts and UI go on); leave Play / the game.
    void (*set_game_paused)(void* ctx, bool paused) = nullptr;
    void (*quit_game)(void* ctx) = nullptr;

    // ---- controls and settings (added v0.8.40; new entries go at the end) ----
    // An action of the player's controls is held (move_forward, fire,
    // ability_1, or a project's own from input.cfg).
    bool (*action_down)(void* ctx, const char* action) = nullptr;
    // The next key or mouse button pressed becomes the action's (Esc
    // cancels); the gameplay event "input.rebound" follows with the action.
    void (*rebind_action)(void* ctx, const char* action) = nullptr;
    void (*reset_controls)(void* ctx) = nullptr;
    // The key's name for an action ("Space", "Mouse Left"). Returns its length.
    int  (*key_for_action)(void* ctx, const char* action, char* out, int out_size) = nullptr;
    // Settings by name (quality, ambient_occlusion, shadow_quality, fov,
    // mouse_sensitivity, invert_y, ...): set; read; step to the next choice.
    bool (*set_setting)(void* ctx, const char* name, const char* value) = nullptr;
    int  (*get_setting)(void* ctx, const char* name, char* out, int out_size) = nullptr;
    int  (*next_setting)(void* ctx, const char* name, char* out, int out_size) = nullptr;

    // ---- skill trees and inventory screens (added v0.8.41; new entries go at the end) ----
    // Give an entity a skill tree from the project's data (skill blocks).
    bool (*set_skill_tree)(void* ctx, uint32_t e, const char* tree) = nullptr;
    void (*add_skill_points)(void* ctx, uint32_t e, int points) = nullptr;
    // 2 learned, 1 can be learned now, 0 not yet.
    int  (*skill_state)(void* ctx, uint32_t e, const char* skill) = nullptr;
    int  (*skill_points)(void* ctx, uint32_t e) = nullptr;
    // The selected cell of a slots grid in a shown UI document (-1 none).
    int  (*ui_selected)(void* ctx, const char* path, const char* id) = nullptr;
    // What is in an entity's inventory at `index`: its item id, name, count.
    bool (*item_in_slot)(void* ctx, uint32_t e, int index, char* id_out, int id_size, char* name_out, int name_size,
                         int* count) = nullptr;

    // ---- guns carried and gear worn (added v0.8.42; new entries go at the end) ----
    bool (*give_weapon)(void* ctx, uint32_t e, const char* weapon) = nullptr;
    bool (*select_weapon)(void* ctx, uint32_t e, int index) = nullptr;
    // The gun in hand: its place among those carried (-1 none) and its id.
    int  (*current_weapon)(void* ctx, uint32_t e, char* id_out, int id_size) = nullptr;
    // Take off what is in a gear slot, into the bag.
    bool (*unequip_item)(void* ctx, uint32_t e, const char* slot) = nullptr;
    // What is worn in a gear slot: item id and name.
    bool (*equipped_item)(void* ctx, uint32_t e, const char* slot, char* id_out, int id_size, char* name_out,
                          int name_size) = nullptr;

    // ---- weapon slots (added v0.8.43; new entries go at the end) ----
    // The gun at a place (from 0) into the bag.
    bool (*stow_weapon)(void* ctx, uint32_t e, int index) = nullptr;
    // A gun from the bag into a weapon slot (-1: the first empty one, else the gun in hand's).
    bool (*equip_weapon)(void* ctx, uint32_t e, const char* weapon, int slot) = nullptr;
    // How many guns can be carried (0: any number).
    void (*set_weapon_slots)(void* ctx, uint32_t e, int count) = nullptr;
    // The gun at a place: its id and name (false: an empty slot).
    bool (*weapon_in_slot)(void* ctx, uint32_t e, int index, char* id_out, int id_size, char* name_out,
                           int name_size) = nullptr;
};

}  // namespace schizo::editor
