// ============================================================
// Python script backend — pocketpy embedding (Stage 12)
// ============================================================
//
// Each ScriptInstance owns its own pocketpy VM: module-level globals are the
// script's per-entity state, and hot reload = throw the VM away and re-exec
// the file. The `engine` module marshals onto the shared ScriptApi table.
//
// pocketpy's native functions are captureless (PyVar(*)(VM*, ArgsView)), so
// the current ScriptApi pointer is published through a file-static before
// every exec/call. The editor is single-threaded, so this is safe.
//
// This is the ONLY translation unit that includes pocketpy.h.

#include "script_system.h"

#include <spdlog/spdlog.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include <pocketpy.h>
#include <cstdint>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace schizo::editor {

namespace {

const ScriptApi* g_api = nullptr;   // published around every VM entry
std::string      g_py_err;          // collects the VM's stderr (error reports)

void stderr_hook(const char* buf, int size) {
    g_py_err.append(buf, static_cast<size_t>(size));
}

// -- small marshalling helpers ------------------------------------------------
pkpy::PyVar vec3_tuple(pkpy::VM* vm, const float v[3]) {
    pkpy::Tuple t(VAR(static_cast<pkpy::f64>(v[0])),
                  VAR(static_cast<pkpy::f64>(v[1])),
                  VAR(static_cast<pkpy::f64>(v[2])));
    return VAR(std::move(t));
}
float farg(pkpy::VM* vm, pkpy::PyVar v) { return static_cast<float>(CAST_F(v)); }
uint32_t earg(pkpy::VM* vm, pkpy::PyVar v) { return static_cast<uint32_t>(CAST(pkpy::i64, v)); }
std::string sarg(pkpy::VM* vm, pkpy::PyVar v) {
    pkpy::Str s = CAST(pkpy::Str&, v);
    return std::string(s.data, static_cast<size_t>(s.size));
}

// -- the `engine` module ------------------------------------------------------
void build_engine_module(pkpy::VM* vm) {
    pkpy::PyVar mod = vm->new_module("engine");

    vm->bind(mod, "log(msg: str) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->log) {
            pkpy::Str s = CAST(pkpy::Str&, args[0]);
            std::string msg(s.data, static_cast<size_t>(s.size));
            g_api->log(g_api->ctx, msg.c_str());
        }
        return vm->None;
    });
    vm->bind(mod, "dt() -> float", [](pkpy::VM* vm, pkpy::ArgsView) {
        return VAR(static_cast<pkpy::f64>(g_api ? g_api->dt : 0.0f));
    });
    vm->bind(mod, "time() -> float", [](pkpy::VM* vm, pkpy::ArgsView) {
        return VAR(static_cast<pkpy::f64>(g_api ? g_api->time : 0.0));
    });

    // ---- entities / transforms ----
    vm->bind(mod, "find(name: str) -> int", [](pkpy::VM* vm, pkpy::ArgsView args) {
        pkpy::i64 id = 0;
        if (g_api && g_api->find_entity) {
            pkpy::Str s = CAST(pkpy::Str&, args[0]);
            std::string name(s.data, static_cast<size_t>(s.size));
            id = g_api->find_entity(g_api->ctx, name.c_str());
        }
        return VAR(id);
    });
    vm->bind(mod, "get_position(e: int) -> tuple", [](pkpy::VM* vm, pkpy::ArgsView args) {
        float v[3] = {0, 0, 0};
        if (g_api && g_api->get_position) g_api->get_position(g_api->ctx, earg(vm, args[0]), v);
        return vec3_tuple(vm, v);
    });
    vm->bind(mod, "set_position(e: int, x: float, y: float, z: float) -> None",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->set_position) {
            const float p[3] = {farg(vm, args[1]), farg(vm, args[2]), farg(vm, args[3])};
            g_api->set_position(g_api->ctx, earg(vm, args[0]), p);
        }
        return vm->None;
    });
    vm->bind(mod, "get_rotation(e: int) -> tuple", [](pkpy::VM* vm, pkpy::ArgsView args) {
        float v[3] = {0, 0, 0};
        if (g_api && g_api->get_rotation_euler) g_api->get_rotation_euler(g_api->ctx, earg(vm, args[0]), v);
        return vec3_tuple(vm, v);
    });
    vm->bind(mod, "set_rotation(e: int, x: float, y: float, z: float) -> None",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->set_rotation_euler) {
            const float r[3] = {farg(vm, args[1]), farg(vm, args[2]), farg(vm, args[3])};
            g_api->set_rotation_euler(g_api->ctx, earg(vm, args[0]), r);
        }
        return vm->None;
    });
    vm->bind(mod, "get_scale(e: int) -> tuple", [](pkpy::VM* vm, pkpy::ArgsView args) {
        float v[3] = {1, 1, 1};
        if (g_api && g_api->get_scale) g_api->get_scale(g_api->ctx, earg(vm, args[0]), v);
        return vec3_tuple(vm, v);
    });
    vm->bind(mod, "set_scale(e: int, x: float, y: float, z: float) -> None",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->set_scale) {
            const float s[3] = {farg(vm, args[1]), farg(vm, args[2]), farg(vm, args[3])};
            g_api->set_scale(g_api->ctx, earg(vm, args[0]), s);
        }
        return vm->None;
    });

    // ---- input ----
    vm->bind(mod, "key_down(key: int) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        const bool d = g_api && g_api->key_down &&
                       g_api->key_down(g_api->ctx, static_cast<int>(CAST(pkpy::i64, args[0])));
        return VAR(d);
    });
    vm->bind(mod, "mouse_down(button: int) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        const bool d = g_api && g_api->mouse_down &&
                       g_api->mouse_down(g_api->ctx, static_cast<int>(CAST(pkpy::i64, args[0])));
        return VAR(d);
    });
    vm->bind(mod, "mouse_delta() -> tuple", [](pkpy::VM* vm, pkpy::ArgsView) {
        float d[2] = {0, 0};
        if (g_api && g_api->mouse_delta) g_api->mouse_delta(g_api->ctx, d);
        pkpy::Tuple t(VAR(static_cast<pkpy::f64>(d[0])), VAR(static_cast<pkpy::f64>(d[1])));
        return VAR(std::move(t));
    });

    // ---- spawn / destroy ----
    vm->bind(mod, "spawn_cube(x: float, y: float, z: float, size: float, r: float, g: float, b: float, dynamic: bool) -> int",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        pkpy::i64 id = 0;
        if (g_api && g_api->spawn_primitive) {
            const float p[3]    = {farg(vm, args[0]), farg(vm, args[1]), farg(vm, args[2])};
            const float rgba[4] = {farg(vm, args[4]), farg(vm, args[5]), farg(vm, args[6]), 1.0f};
            id = g_api->spawn_primitive(g_api->ctx, 0, p, farg(vm, args[3]), rgba,
                                        CAST(bool, args[7]) ? 1 : 0);
        }
        return VAR(id);
    });
    vm->bind(mod, "spawn_sphere(x: float, y: float, z: float, size: float, r: float, g: float, b: float, dynamic: bool) -> int",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        pkpy::i64 id = 0;
        if (g_api && g_api->spawn_primitive) {
            const float p[3]    = {farg(vm, args[0]), farg(vm, args[1]), farg(vm, args[2])};
            const float rgba[4] = {farg(vm, args[4]), farg(vm, args[5]), farg(vm, args[6]), 1.0f};
            id = g_api->spawn_primitive(g_api->ctx, 1, p, farg(vm, args[3]), rgba,
                                        CAST(bool, args[7]) ? 1 : 0);
        }
        return VAR(id);
    });
    vm->bind(mod, "destroy(e: int) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->destroy_entity) g_api->destroy_entity(g_api->ctx, earg(vm, args[0]));
        return vm->None;
    });

    // ---- physics ----
    vm->bind(mod, "set_velocity(e: int, x: float, y: float, z: float) -> None",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->set_velocity) {
            const float v[3] = {farg(vm, args[1]), farg(vm, args[2]), farg(vm, args[3])};
            g_api->set_velocity(g_api->ctx, earg(vm, args[0]), v);
        }
        return vm->None;
    });
    vm->bind(mod, "add_impulse(e: int, x: float, y: float, z: float) -> None",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->add_impulse) {
            const float i[3] = {farg(vm, args[1]), farg(vm, args[2]), farg(vm, args[3])};
            g_api->add_impulse(g_api->ctx, earg(vm, args[0]), i);
        }
        return vm->None;
    });
    vm->bind(mod, "raycast(ox: float, oy: float, oz: float, dx: float, dy: float, dz: float, max_dist: float) -> tuple",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool hit = false; float hp[3] = {0, 0, 0}; uint32_t he = 0;
        if (g_api && g_api->raycast) {
            const float o[3] = {farg(vm, args[0]), farg(vm, args[1]), farg(vm, args[2])};
            const float d[3] = {farg(vm, args[3]), farg(vm, args[4]), farg(vm, args[5])};
            hit = g_api->raycast(g_api->ctx, o, d, farg(vm, args[6]), hp, &he);
        }
        pkpy::Tuple t(5);
        t[0] = VAR(hit);
        t[1] = VAR(static_cast<pkpy::f64>(hp[0]));
        t[2] = VAR(static_cast<pkpy::f64>(hp[1]));
        t[3] = VAR(static_cast<pkpy::f64>(hp[2]));
        t[4] = VAR(static_cast<pkpy::i64>(he));
        return VAR(std::move(t));
    });

    // ---- rendering ----
    vm->bind(mod, "set_color(e: int, r: float, g: float, b: float, a: float) -> None",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->set_color) {
            const float c[4] = {farg(vm, args[1]), farg(vm, args[2]), farg(vm, args[3]), farg(vm, args[4])};
            g_api->set_color(g_api->ctx, earg(vm, args[0]), c);
        }
        return vm->None;
    });
    vm->bind(mod, "set_emissive(e: int, r: float, g: float, b: float, intensity: float) -> None",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->set_emissive) {
            const float c[3] = {farg(vm, args[1]), farg(vm, args[2]), farg(vm, args[3])};
            g_api->set_emissive(g_api->ctx, earg(vm, args[0]), c, farg(vm, args[4]));
        }
        return vm->None;
    });

    // ---- audio ----
    vm->bind(mod, "audio_play(e: int) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->audio_play) g_api->audio_play(g_api->ctx, earg(vm, args[0]));
        return vm->None;
    });
    vm->bind(mod, "audio_stop(e: int) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->audio_stop) g_api->audio_stop(g_api->ctx, earg(vm, args[0]));
        return vm->None;
    });

    // ---- gameplay (G0–G4) ----
    vm->bind(mod, "get_attribute(e: int, name: str) -> float", [](pkpy::VM* vm, pkpy::ArgsView args) {
        float r = 0.0f;
        if (g_api && g_api->get_attribute) r = g_api->get_attribute(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str());
        return VAR(static_cast<pkpy::f64>(r));
    });
    vm->bind(mod, "set_attribute(e: int, name: str, value: float) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->set_attribute) g_api->set_attribute(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str(), farg(vm, args[2]));
        return vm->None;
    });
    vm->bind(mod, "adjust_attribute(e: int, name: str, delta: float) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->adjust_attribute) g_api->adjust_attribute(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str(), farg(vm, args[2]));
        return vm->None;
    });
    vm->bind(mod, "has_tag(e: int, tag: str) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->has_tag && g_api->has_tag(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str());
        return VAR(r);
    });
    vm->bind(mod, "add_tag(e: int, tag: str) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->add_tag) g_api->add_tag(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str());
        return vm->None;
    });
    vm->bind(mod, "remove_tag(e: int, tag: str) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->remove_tag) g_api->remove_tag(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str());
        return vm->None;
    });
    vm->bind(mod, "apply_damage(e: int, amount: float, type: str) -> float", [](pkpy::VM* vm, pkpy::ArgsView args) {
        float r = 0.0f;
        if (g_api && g_api->apply_damage) r = g_api->apply_damage(g_api->ctx, earg(vm, args[0]), farg(vm, args[1]), sarg(vm, args[2]).c_str());
        return VAR(static_cast<pkpy::f64>(r));
    });
    vm->bind(mod, "apply_heal(e: int, attribute: str, amount: float) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->apply_heal) g_api->apply_heal(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str(), farg(vm, args[2]));
        return vm->None;
    });
    vm->bind(mod, "activate_ability(e: int, index: int, target: int) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->activate_ability &&
                 g_api->activate_ability(g_api->ctx, earg(vm, args[0]), static_cast<int>(CAST(pkpy::i64, args[1])), earg(vm, args[2]));
        return VAR(r);
    });
    vm->bind(mod, "grant_xp(e: int, amount: float) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->grant_xp) g_api->grant_xp(g_api->ctx, earg(vm, args[0]), farg(vm, args[1]));
        return vm->None;
    });
    vm->bind(mod, "get_level(e: int) -> int", [](pkpy::VM* vm, pkpy::ArgsView args) {
        pkpy::i64 r = (g_api && g_api->get_level) ? g_api->get_level(g_api->ctx, earg(vm, args[0])) : 0;
        return VAR(r);
    });
    vm->bind(mod, "unlock_skill(e: int, node_id: str) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->unlock_skill && g_api->unlock_skill(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str());
        return VAR(r);
    });
    vm->bind(mod, "send_state_event(e: int, event: str) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->send_state_event && g_api->send_state_event(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str());
        return VAR(r);
    });
    vm->bind(mod, "in_state(e: int, state: str) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->in_state && g_api->in_state(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str());
        return VAR(r);
    });
    vm->bind(mod, "add_item(e: int, def_id: str, qty: int) -> int", [](pkpy::VM* vm, pkpy::ArgsView args) {
        pkpy::i64 r = (g_api && g_api->add_item) ? g_api->add_item(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str(), static_cast<int>(CAST(pkpy::i64, args[2]))) : 0;
        return VAR(r);
    });
    vm->bind(mod, "item_count(e: int, def_id: str) -> int", [](pkpy::VM* vm, pkpy::ArgsView args) {
        pkpy::i64 r = (g_api && g_api->item_count) ? g_api->item_count(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str()) : 0;
        return VAR(r);
    });
    vm->bind(mod, "equip_item(e: int, def_id: str) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->equip_item && g_api->equip_item(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str());
        return VAR(r);
    });
    vm->bind(mod, "use_item(e: int, def_id: str) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->use_item && g_api->use_item(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str());
        return VAR(r);
    });
    vm->bind(mod, "interact(e: int) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->interact && g_api->interact(g_api->ctx, earg(vm, args[0]));
        return VAR(r);
    });
    vm->bind(mod, "fire_weapon(e: int) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->fire_weapon && g_api->fire_weapon(g_api->ctx, earg(vm, args[0]));
        return VAR(r);
    });
    vm->bind(mod, "reload_weapon(e: int) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->reload_weapon && g_api->reload_weapon(g_api->ctx, earg(vm, args[0]));
        return VAR(r);
    });
    vm->bind(mod, "drive(e: int, throttle: float, steer: float, brake: bool, boost: bool) -> None",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->drive)
            g_api->drive(g_api->ctx, earg(vm, args[0]), farg(vm, args[1]), farg(vm, args[2]),
                         CAST(bool, args[3]) ? 1 : 0, CAST(bool, args[4]) ? 1 : 0);
        return vm->None;
    });
    vm->bind(mod, "toggle_flashlight(e: int) -> bool", [](pkpy::VM* vm, pkpy::ArgsView args) {
        bool r = g_api && g_api->toggle_flashlight && g_api->toggle_flashlight(g_api->ctx, earg(vm, args[0]));
        return VAR(r);
    });

    // ---- script public parameters (Stage 12 script fields) ----
    vm->bind(mod, "get_param_float(e: int, name: str, default: float) -> float",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        float def = farg(vm, args[2]);
        float r = (g_api && g_api->get_param_float)
                  ? g_api->get_param_float(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str(), def) : def;
        return VAR(static_cast<pkpy::f64>(r));
    });
    vm->bind(mod, "get_param_int(e: int, name: str, default: int) -> int",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        int def = static_cast<int>(CAST(pkpy::i64, args[2]));
        int r = (g_api && g_api->get_param_int)
                ? g_api->get_param_int(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str(), def) : def;
        return VAR(static_cast<pkpy::i64>(r));
    });
    vm->bind(mod, "get_param_bool(e: int, name: str, default: bool) -> bool",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        int def = CAST(bool, args[2]) ? 1 : 0;
        bool r = (g_api && g_api->get_param_bool)
                 ? g_api->get_param_bool(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str(), def) : (def != 0);
        return VAR(r);
    });
    vm->bind(mod, "get_param_str(e: int, name: str, default: str) -> str",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        std::string def = sarg(vm, args[2]);
        char buf[256];
        if (g_api && g_api->get_param_string)
            g_api->get_param_string(g_api->ctx, earg(vm, args[0]), sarg(vm, args[1]).c_str(),
                                    buf, static_cast<int>(sizeof buf), def.c_str());
        else
            std::snprintf(buf, sizeof buf, "%s", def.c_str());
        return VAR(pkpy::Str(buf));
    });

    // ---- world flags + events (bridge to the scene logic graph) ----
    vm->bind(mod, "get_flag(key: str) -> int", [](pkpy::VM* vm, pkpy::ArgsView args) {
        pkpy::i64 r = (g_api && g_api->get_flag) ? g_api->get_flag(g_api->ctx, sarg(vm, args[0]).c_str()) : 0;
        return VAR(r);
    });
    vm->bind(mod, "set_flag(key: str, value: int) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->set_flag)
            g_api->set_flag(g_api->ctx, sarg(vm, args[0]).c_str(), static_cast<int>(CAST(pkpy::i64, args[1])));
        return vm->None;
    });
    vm->bind(mod, "emit_event(name: str) -> None", [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->emit_event) g_api->emit_event(g_api->ctx, sarg(vm, args[0]).c_str());
        return vm->None;
    });

    // ---- spatial helpers ----
    vm->bind(mod, "distance(a: int, b: int) -> float", [](pkpy::VM* vm, pkpy::ArgsView args) {
        float d = (g_api && g_api->distance) ? g_api->distance(g_api->ctx, earg(vm, args[0]), earg(vm, args[1])) : -1.0f;
        return VAR(static_cast<pkpy::f64>(d));
    });
    vm->bind(mod, "translate(e: int, dx: float, dy: float, dz: float) -> None",
             [](pkpy::VM* vm, pkpy::ArgsView args) {
        if (g_api && g_api->translate) {
            float d[3] = { farg(vm, args[1]), farg(vm, args[2]), farg(vm, args[3]) };
            g_api->translate(g_api->ctx, earg(vm, args[0]), d);
        }
        return vm->None;
    });
    vm->bind(mod, "get_forward(e: int) -> tuple", [](pkpy::VM* vm, pkpy::ArgsView args) {
        float v[3] = {0, 0, -1};
        if (g_api && g_api->get_forward) g_api->get_forward(g_api->ctx, earg(vm, args[0]), v);
        return vec3_tuple(vm, v);
    });

    // Common key / button constants (GLFW codes) so scripts don't hardcode.
    auto set_const = [&](const char* name, pkpy::i64 v) { mod->attr().set(name, VAR(v)); };
    set_const("KEY_SPACE", 32);
    set_const("KEY_A", 65); set_const("KEY_D", 68); set_const("KEY_S", 83); set_const("KEY_W", 87);
    set_const("KEY_E", 69); set_const("KEY_Q", 81); set_const("KEY_F", 70); set_const("KEY_G", 71);
    set_const("KEY_LEFT", 263); set_const("KEY_RIGHT", 262); set_const("KEY_UP", 265); set_const("KEY_DOWN", 264);
    set_const("KEY_LSHIFT", 340); set_const("KEY_LCTRL", 341);
    set_const("MOUSE_LEFT", 0); set_const("MOUSE_RIGHT", 1); set_const("MOUSE_MIDDLE", 2);
}

// -- instance -----------------------------------------------------------------
class PythonInstance final : public ScriptInstance {
public:
    PythonInstance(std::unique_ptr<pkpy::VM> vm, const ScriptApi* api,
                   pkpy::PyVar on_start, pkpy::PyVar on_update)
        : vm_(std::move(vm)), api_(api), on_start_(on_start), on_update_(on_update) {}

    bool start(uint32_t entity, std::string& err) override {
        inject_params(entity);   // push inspector-authored public-field values into globals
        if (on_start_ == nullptr) return true;   // optional hook
        return call1(on_start_, entity, /*with_dt=*/false, 0.0f, err);
    }
    bool update(uint32_t entity, float dt, std::string& err) override {
        if (on_update_ == nullptr) return true;
        return call1(on_update_, entity, /*with_dt=*/true, dt, err);
    }

private:
    // Unity-style public fields: for every module-level global of a simple type
    // (bool/int/float/str), ask the host for the entity's inspector-authored
    // override (keyed by the variable NAME) and write it back into the global.
    // get_param_* returns the override or the passed default, so un-overridden
    // globals keep their script default. Runs on start() + every hot reload, so
    // a script that just does `speed = 5.0` and uses `speed` respects the value
    // the user typed in the Inspector — no @param / get_param call required.
    void inject_params(uint32_t entity) {
        if (!api_) return;
        auto* vm = vm_.get();
        if (vm->_main == nullptr) return;
        std::vector<std::pair<pkpy::StrName, pkpy::PyVar>> updates;
        vm->_main->attr().apply([&](pkpy::StrName name, pkpy::PyVar val) {
            const std::string_view sv = name.sv();
            if (sv.empty() || sv[0] == '_') return;
            const std::string nm(sv);
            if (pkpy::is_type(val, vm->tp_bool)) {
                if (!api_->get_param_bool) return;
                const int cur = CAST(bool, val) ? 1 : 0;
                updates.emplace_back(name, VAR(api_->get_param_bool(api_->ctx, entity, nm.c_str(), cur)));
            } else if (pkpy::is_int(val)) {
                if (!api_->get_param_int) return;
                const int cur = static_cast<int>(CAST(pkpy::i64, val));
                updates.emplace_back(name, VAR(static_cast<pkpy::i64>(
                    api_->get_param_int(api_->ctx, entity, nm.c_str(), cur))));
            } else if (pkpy::is_float(val)) {
                if (!api_->get_param_float) return;
                const float cur = static_cast<float>(CAST(pkpy::f64, val));
                updates.emplace_back(name, VAR(static_cast<pkpy::f64>(
                    api_->get_param_float(api_->ctx, entity, nm.c_str(), cur))));
            } else if (pkpy::is_type(val, vm->tp_str)) {
                if (!api_->get_param_string) return;
                pkpy::Str s = CAST(pkpy::Str&, val);
                const std::string cur(s.data, static_cast<size_t>(s.size));
                char buf[256];
                api_->get_param_string(api_->ctx, entity, nm.c_str(), buf,
                                       static_cast<int>(sizeof buf), cur.c_str());
                updates.emplace_back(name, VAR(pkpy::Str(buf)));
            }
        });
        for (auto& u : updates) vm->_main->attr().set(u.first, u.second);
    }

    bool call1(pkpy::PyVar fn, uint32_t entity, bool with_dt, float dt, std::string& err) {
        auto* vm = vm_.get();
        g_api = api_;
        g_py_err.clear();
        try {
            if (with_dt)
                vm->call(fn, VAR(static_cast<pkpy::i64>(entity)), VAR(static_cast<pkpy::f64>(dt)));
            else
                vm->call(fn, VAR(static_cast<pkpy::i64>(entity)));
            return true;
        } catch (pkpy::Exception& e) {
            pkpy::Str s = e.summary();
            err.assign(s.data, static_cast<size_t>(s.size));
        } catch (const std::exception& e) {
            err = e.what();
        } catch (...) {
            err = "unknown script error";
        }
        if (err.empty() && !g_py_err.empty()) err = g_py_err;
        return false;
    }

    std::unique_ptr<pkpy::VM> vm_;
    const ScriptApi*          api_;
    pkpy::PyVar               on_start_;
    pkpy::PyVar               on_update_;
};

// -- host ---------------------------------------------------------------------
class PythonHost final : public ScriptHost {
public:
    const char* language() const override { return "Python (pocketpy)"; }

    std::unique_ptr<ScriptInstance> create(const std::string& source_path,
                                           const ScriptApi* api,
                                           std::string& err) override {
        std::ifstream f(source_path);
        if (!f.is_open()) { err = "cannot open " + source_path; return nullptr; }
        std::stringstream ss; ss << f.rdbuf();
        const std::string source = ss.str();

        auto vm = std::make_unique<pkpy::VM>();
        vm->_stderr = &stderr_hook;
        build_engine_module(vm.get());

        g_api = api;
        g_py_err.clear();
        pkpy::PyVar ok = nullptr;
        try {
            ok = vm->exec(source, source_path, pkpy::EXEC_MODE);
        } catch (pkpy::Exception& e) {
            pkpy::Str s = e.summary();
            err.assign(s.data, static_cast<size_t>(s.size));
            return nullptr;
        } catch (const std::exception& e) { err = e.what(); return nullptr; }
        if (ok == nullptr) {
            err = g_py_err.empty() ? "script failed to execute" : g_py_err;
            return nullptr;
        }

        auto* vmp = vm.get();
        pkpy::PyVar on_start  = vmp->_main->attr().try_get(pkpy::StrName("on_start"));
        pkpy::PyVar on_update = vmp->_main->attr().try_get(pkpy::StrName("on_update"));
        if (on_start == nullptr && on_update == nullptr) {
            err = "script defines neither on_start(e) nor on_update(e, dt)";
            return nullptr;
        }
        return std::make_unique<PythonInstance>(std::move(vm), api, on_start, on_update);
    }
};

}  // namespace

std::unique_ptr<ScriptHost> make_python_host() {
    return std::make_unique<PythonHost>();
}

}  // namespace schizo::editor
