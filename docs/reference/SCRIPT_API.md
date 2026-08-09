# Script API reference

**Generated** by `gws docs` from `assets/scripts/schizo_script.h` — do not edit by hand.

One shared C table is exposed identically to all three scripting backends (Python, C++ and C#),
so every verb below is callable from any of them. The table is the contract: its layout is
static_asserted, and adding a verb once exposes it everywhere.

**53 verbs** exposed.

| signature | notes |
|---|---|
| `void (*log)(void* ctx, const char* msg)` |  |
| `unsigned (*find_entity)(void* ctx, const char* name)` |  |
| `bool (*get_position)(void* ctx, unsigned e, float out[3])` |  |
| `void (*set_position)(void* ctx, unsigned e, const float p[3])` |  |
| `bool (*get_rotation_euler)(void* ctx, unsigned e, float out_deg[3])` |  |
| `void (*set_rotation_euler)(void* ctx, unsigned e, const float deg[3])` |  |
| `bool (*get_scale)(void* ctx, unsigned e, float out[3])` |  |
| `void (*set_scale)(void* ctx, unsigned e, const float s[3])` |  |
| `bool (*key_down)(void* ctx, int glfw_key)` |  |
| `bool (*mouse_down)(void* ctx, int button)` |  |
| `void (*mouse_delta)(void* ctx, float out[2])` |  |
| `unsigned (*spawn_primitive)(void* ctx, int shape, const float pos[3],` |  |
| `void (*destroy_entity)(void* ctx, unsigned e)` |  |
| `void (*set_velocity)(void* ctx, unsigned e, const float v[3])` |  |
| `void (*add_impulse)(void* ctx, unsigned e, const float i[3])` |  |
| `bool (*raycast)(void* ctx, const float origin[3], const float dir[3],` |  |
| `void (*set_color)(void* ctx, unsigned e, const float rgba[4])` |  |
| `void (*set_emissive)(void* ctx, unsigned e, const float rgb[3], float intensity)` |  |
| `void (*audio_play)(void* ctx, unsigned e)` |  |
| `void (*audio_stop)(void* ctx, unsigned e)` |  |
| `float (*get_attribute)(void* ctx, unsigned e, const char* name)` |  |
| `void  (*set_attribute)(void* ctx, unsigned e, const char* name, float value)` |  |
| `void  (*adjust_attribute)(void* ctx, unsigned e, const char* name, float delta)` |  |
| `bool  (*has_tag)(void* ctx, unsigned e, const char* tag)` |  |
| `void  (*add_tag)(void* ctx, unsigned e, const char* tag)` |  |
| `void  (*remove_tag)(void* ctx, unsigned e, const char* tag)` |  |
| `float (*apply_damage)(void* ctx, unsigned e, float amount, const char* type)` |  |
| `void  (*apply_heal)(void* ctx, unsigned e, const char* attribute, float amount)` |  |
| `bool  (*activate_ability)(void* ctx, unsigned e, int index, unsigned target)` |  |
| `void  (*grant_xp)(void* ctx, unsigned e, float amount)` |  |
| `int   (*get_level)(void* ctx, unsigned e)` |  |
| `bool  (*unlock_skill)(void* ctx, unsigned e, const char* node_id)` |  |
| `bool  (*send_state_event)(void* ctx, unsigned e, const char* event)` |  |
| `bool  (*in_state)(void* ctx, unsigned e, const char* state)` |  |
| `int   (*add_item)(void* ctx, unsigned e, const char* def_id, int qty)` |  |
| `int   (*item_count)(void* ctx, unsigned e, const char* def_id)` |  |
| `bool  (*equip_item)(void* ctx, unsigned e, const char* def_id)` |  |
| `bool  (*use_item)(void* ctx, unsigned e, const char* def_id)` |  |
| `bool  (*interact)(void* ctx, unsigned e);   /* G7: use the nearest interactable in range */` |  |
| `bool  (*fire_weapon)(void* ctx, unsigned e);   /* G12: fire the entity's Weapon */` |  |
| `bool  (*reload_weapon)(void* ctx, unsigned e); /* G12: reload the entity's Weapon */` |  |
| `void  (*drive)(void* ctx, unsigned e, float throttle, float steer, int brake, int boost); /* G13 */` |  |
| `bool  (*toggle_flashlight)(void* ctx, unsigned e);   /* G15: toggle the entity's Flashlight */` |  |
| `float (*get_param_float)(void* ctx, unsigned e, const char* name, float def)` |  |
| `int   (*get_param_int)(void* ctx, unsigned e, const char* name, int def)` |  |
| `bool  (*get_param_bool)(void* ctx, unsigned e, const char* name, int def)` |  |
| `int   (*get_param_string)(void* ctx, unsigned e, const char* name,` |  |
| `int   (*get_flag)(void* ctx, const char* key)` |  |
| `void  (*set_flag)(void* ctx, const char* key, int value)` |  |
| `void  (*emit_event)(void* ctx, const char* name)` |  |
| `float (*distance)(void* ctx, unsigned a, unsigned b)` |  |
| `void  (*translate)(void* ctx, unsigned e, const float delta[3])` |  |
| `bool  (*get_forward)(void* ctx, unsigned e, float out[3])` |  |

