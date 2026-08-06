#pragma once

// ============================================================
// Project Schizo — native C++ script SDK (Stage 12)
// ============================================================
//
// Write a .cpp next to this header, implement one or both hooks, attach the
// file to an entity via Inspector -> Add Component -> Script. The editor
// compiles it with g++ into a DLL, loads it live, and recompiles + reloads
// whenever you save the file during Play.
//
//   #include "schizo_script.h"
//   SCHIZO_SCRIPT void on_start(const SchizoScriptApi* api, unsigned entity) { ... }
//   SCHIZO_SCRIPT void on_update(const SchizoScriptApi* api, unsigned entity, float dt) { ... }
//
// The api table is the same surface Python scripts see. Entities are scene
// ids (0 = invalid); rotations are world-space Euler degrees. Physics entries
// only act during Play. NOTE: native scripts run in-process — a crash in your
// script crashes the editor. Keep your file-scope state simple: it is REPLACED
// on every hot reload (the DLL is swapped).
//
// This struct MUST stay binary-identical to editor/include/script_api.h —
// the editor build static_asserts the layouts against each other.

extern "C" {

typedef struct SchizoScriptApi {
    void*  ctx;
    float  dt;      // seconds since last frame
    double time;    // seconds since Play started

    void (*log)(void* ctx, const char* msg);

    unsigned (*find_entity)(void* ctx, const char* name);
    bool (*get_position)(void* ctx, unsigned e, float out[3]);
    void (*set_position)(void* ctx, unsigned e, const float p[3]);
    bool (*get_rotation_euler)(void* ctx, unsigned e, float out_deg[3]);
    void (*set_rotation_euler)(void* ctx, unsigned e, const float deg[3]);
    bool (*get_scale)(void* ctx, unsigned e, float out[3]);
    void (*set_scale)(void* ctx, unsigned e, const float s[3]);

    bool (*key_down)(void* ctx, int glfw_key);
    bool (*mouse_down)(void* ctx, int button);
    void (*mouse_delta)(void* ctx, float out[2]);

    unsigned (*spawn_primitive)(void* ctx, int shape, const float pos[3],
                                float size, const float rgba[4], int dynamic);
    void (*destroy_entity)(void* ctx, unsigned e);

    void (*set_velocity)(void* ctx, unsigned e, const float v[3]);
    void (*add_impulse)(void* ctx, unsigned e, const float i[3]);
    bool (*raycast)(void* ctx, const float origin[3], const float dir[3],
                    float max_dist, float out_pos[3], unsigned* out_entity);

    void (*set_color)(void* ctx, unsigned e, const float rgba[4]);
    void (*set_emissive)(void* ctx, unsigned e, const float rgb[3], float intensity);

    void (*audio_play)(void* ctx, unsigned e);
    void (*audio_stop)(void* ctx, unsigned e);

    /* ---- gameplay (G0-G4); appended at the end so the ABI prefix is stable ---- */
    float (*get_attribute)(void* ctx, unsigned e, const char* name);
    void  (*set_attribute)(void* ctx, unsigned e, const char* name, float value);
    void  (*adjust_attribute)(void* ctx, unsigned e, const char* name, float delta);
    bool  (*has_tag)(void* ctx, unsigned e, const char* tag);
    void  (*add_tag)(void* ctx, unsigned e, const char* tag);
    void  (*remove_tag)(void* ctx, unsigned e, const char* tag);
    float (*apply_damage)(void* ctx, unsigned e, float amount, const char* type);
    void  (*apply_heal)(void* ctx, unsigned e, const char* attribute, float amount);
    bool  (*activate_ability)(void* ctx, unsigned e, int index, unsigned target);
    void  (*grant_xp)(void* ctx, unsigned e, float amount);
    int   (*get_level)(void* ctx, unsigned e);
    bool  (*unlock_skill)(void* ctx, unsigned e, const char* node_id);
    bool  (*send_state_event)(void* ctx, unsigned e, const char* event);
    bool  (*in_state)(void* ctx, unsigned e, const char* state);
    int   (*add_item)(void* ctx, unsigned e, const char* def_id, int qty);
    int   (*item_count)(void* ctx, unsigned e, const char* def_id);
    bool  (*equip_item)(void* ctx, unsigned e, const char* def_id);
    bool  (*use_item)(void* ctx, unsigned e, const char* def_id);
} SchizoScriptApi;

}  // extern "C"

// Common GLFW key codes for key_down().
enum {
    SCHIZO_KEY_SPACE = 32,
    SCHIZO_KEY_A = 65, SCHIZO_KEY_D = 68, SCHIZO_KEY_E = 69, SCHIZO_KEY_F = 70,
    SCHIZO_KEY_G = 71, SCHIZO_KEY_Q = 81, SCHIZO_KEY_S = 83, SCHIZO_KEY_W = 87,
    SCHIZO_KEY_RIGHT = 262, SCHIZO_KEY_LEFT = 263, SCHIZO_KEY_DOWN = 264, SCHIZO_KEY_UP = 265,
    SCHIZO_KEY_LSHIFT = 340, SCHIZO_KEY_LCTRL = 341,
    SCHIZO_MOUSE_LEFT = 0, SCHIZO_MOUSE_RIGHT = 1, SCHIZO_MOUSE_MIDDLE = 2,
};

#if defined(_WIN32)
#define SCHIZO_SCRIPT extern "C" __declspec(dllexport)
#else
#define SCHIZO_SCRIPT extern "C" __attribute__((visibility("default")))
#endif
