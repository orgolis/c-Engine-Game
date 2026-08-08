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
};

}  // namespace schizo::editor
