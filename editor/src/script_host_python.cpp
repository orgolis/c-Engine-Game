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
#include <fstream>
#include <sstream>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include <pocketpy.h>
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
        if (on_start_ == nullptr) return true;   // optional hook
        return call1(on_start_, entity, /*with_dt=*/false, 0.0f, err);
    }
    bool update(uint32_t entity, float dt, std::string& err) override {
        if (on_update_ == nullptr) return true;
        return call1(on_update_, entity, /*with_dt=*/true, dt, err);
    }

private:
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
