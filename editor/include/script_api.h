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
};

}  // namespace schizo::editor
