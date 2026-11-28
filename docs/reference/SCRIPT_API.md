# Script API reference

**Generated** by `gws docs` from `assets/scripts/schizo_script.h` — do not edit by hand.

One shared C table is exposed identically to all three scripting backends (Python, C++ and C#),
so every verb below is callable from any of them. The table is the contract: its layout is
static_asserted, and adding a verb once exposes it everywhere.

**118 verbs** exposed.

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
| `void (*register_command)(void* ctx, const char* title, const char* category,` |  |
| `bool (*run_command)(void* ctx, const char* title)` |  |
| `unsigned (*selected_entity)(void* ctx)` |  |
| `void     (*select_entity)(void* ctx, unsigned e)` |  |
| `int      (*entity_count)(void* ctx)` |  |
| `unsigned (*entity_at)(void* ctx, int index)` |  |
| `int      (*entity_name)(void* ctx, unsigned e, char* out, int out_size)` |  |
| `void     (*set_status)(void* ctx, const char* msg)` |  |
| `int      (*event_count)(void* ctx)` |  |
| `bool     (*event_get)(void* ctx, int index, char* name_out, int name_size,` |  |
| `int      (*biome_at)(void* ctx, unsigned e, char* name_out, int name_size, float* weight)` |  |
| `bool     (*save_game)(void* ctx, const char* slot)` |  |
| `bool     (*load_game)(void* ctx, const char* slot)` |  |
| `void     (*show_message)(void* ctx, const char* text, float seconds)` |  |
| `bool     (*ui_show)(void* ctx, const char* path)` |  |
| `void     (*ui_hide)(void* ctx, const char* path)` |  |
| `bool     (*ui_set_text)(void* ctx, const char* path, const char* id, const char* text)` |  |
| `bool     (*ui_shown)(void* ctx, const char* path)` |  |
| `void     (*set_game_paused)(void* ctx, bool paused)` |  |
| `void     (*quit_game)(void* ctx)` |  |
| `bool     (*action_down)(void* ctx, const char* action)` |  |
| `void     (*rebind_action)(void* ctx, const char* action)` |  |
| `void     (*reset_controls)(void* ctx)` |  |
| `int      (*key_for_action)(void* ctx, const char* action, char* out, int out_size)` |  |
| `bool     (*set_setting)(void* ctx, const char* name, const char* value)` |  |
| `int      (*get_setting)(void* ctx, const char* name, char* out, int out_size)` |  |
| `int      (*next_setting)(void* ctx, const char* name, char* out, int out_size)` |  |
| `bool     (*set_skill_tree)(void* ctx, unsigned e, const char* tree)` |  |
| `void     (*add_skill_points)(void* ctx, unsigned e, int points)` |  |
| `int      (*skill_state)(void* ctx, unsigned e, const char* skill)` |  |
| `int      (*skill_points)(void* ctx, unsigned e)` |  |
| `int      (*ui_selected)(void* ctx, const char* path, const char* id)` |  |
| `bool     (*item_in_slot)(void* ctx, unsigned e, int index, char* id_out, int id_size, char* name_out, int name_size,` |  |
| `bool     (*give_weapon)(void* ctx, unsigned e, const char* weapon)` |  |
| `bool     (*select_weapon)(void* ctx, unsigned e, int index)` |  |
| `int      (*current_weapon)(void* ctx, unsigned e, char* id_out, int id_size)` |  |
| `bool     (*unequip_item)(void* ctx, unsigned e, const char* slot)` |  |
| `bool     (*equipped_item)(void* ctx, unsigned e, const char* slot, char* id_out, int id_size, char* name_out,` |  |
| `bool     (*stow_weapon)(void* ctx, unsigned e, int index)` |  |
| `bool     (*equip_weapon)(void* ctx, unsigned e, const char* weapon, int slot)` |  |
| `void     (*set_weapon_slots)(void* ctx, unsigned e, int count)` |  |
| `bool     (*weapon_in_slot)(void* ctx, unsigned e, int index, char* id_out, int id_size, char* name_out,` |  |
| `bool     (*equip_ability)(void* ctx, unsigned e, int known_index, int slot)` |  |
| `bool     (*play_animation)(void* ctx, unsigned e, const char* clip, bool loop,` |  |
| `bool     (*bone_position)(void* ctx, unsigned e, const char* bone, float out[3])` |  |
| `bool     (*spawn_effect)(void* ctx, const char* effect, const float pos[3], const float dir[3], float scale, float reach)` |  |
| `void     (*set_environment)(void* ctx, float darkness, float whiteout)` |  |
| `bool     (*set_bone_rotation)(void* ctx, unsigned e, const char* bone, const float degrees[3])` |  |
| `unsigned (*spawn_mesh)(void* ctx, const char* path, const float pos[3], float scale)` |  |
| `bool (*set_damage_receiver)(void* ctx, unsigned part, unsigned owner, float multiplier)` |  |
| `bool (*bone_point)(void* ctx, unsigned e, const char* bone, const float model_point[3], float out[3])` |  |
| `bool (*play_montage)(void* ctx, unsigned e, const char* clip, const char* slot, float rate, float blend_in, float blend_out)` |  |
| `bool (*stop_montage)(void* ctx, unsigned e, const char* slot, float blend_out)` |  |
| `bool (*set_anim_param)(void* ctx, unsigned e, const char* name, float value)` |  |
| `int (*anim_state)(void* ctx, unsigned e, int layer, char* out, int out_size)` |  |
| `int  (*apply_status)(void* ctx, unsigned e, const char* status, int stacks, unsigned by)` |  |
| `int  (*status_stacks)(void* ctx, unsigned e, const char* status)` |  |
| `bool (*clear_status)(void* ctx, unsigned e, const char* status)` |  |
| `bool (*add_perk)(void* ctx, unsigned e, const char* perk)` |  |
| `bool (*remove_perk)(void* ctx, unsigned e, const char* perk)` |  |
| `bool (*has_perk)(void* ctx, unsigned e, const char* perk)` |  |
| `bool (*equip_ability_id)(void* ctx, unsigned e, const char* ability, int slot)` |  |
| `int  (*ability_in_slot)(void* ctx, unsigned e, int slot, char* out, int out_size)` |  |
| `int  (*explode)(void* ctx, const float pos[3], float radius, float damage, float force, const char* effect, unsigned by)` |  |
| `void (*shake_camera)(void* ctx, float strength)` |  |

