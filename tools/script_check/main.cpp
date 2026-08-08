// ====================
// script_check — Stage 12 scripting verification CLI
// ====================
//
// Drives the editor's ScriptSystem + Python (pocketpy) backend headlessly with
// a minimal ScriptApi over a real OOP scene. Verifies:
//   1. load + on_start + on_update move the entity (engine.set_position)
//   2. scripts can spawn entities (engine.spawn_cube via the mini api)
//   3. HOT RELOAD: rewriting the file re-instantiates the script (fresh
//      globals + on_start re-runs) and the new behavior takes effect
//   4. a broken script reports an error status without crashing the system

#include "script_system.h"
#include "script_api.h"
#include "script_params.h"

#include "scene.h"
#include "entity.h"
#include "transform.h"
#include "entity_factory.h"
#include "script_component.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

using namespace schizo;

namespace {
struct MiniCtx {
    std::shared_ptr<scene::Scene> scn;
    int spawn_counter = 0;
};

void mini_log(void*, const char* msg) { std::printf("  [py] %s\n", msg); }
uint32_t mini_find(void* c, const char* name) {
    auto* m = static_cast<MiniCtx*>(c);
    auto e = m->scn->GetEntityByName(name);
    return e ? e->GetId() : 0;
}
bool mini_get_pos(void* c, uint32_t e, float out[3]) {
    auto* m = static_cast<MiniCtx*>(c);
    auto ent = m->scn->GetEntityById(e);
    if (!ent) return false;
    const glm::vec3 p = ent->GetTransform()->GetWorldPosition();
    out[0] = p.x; out[1] = p.y; out[2] = p.z;
    return true;
}
void mini_set_pos(void* c, uint32_t e, const float p[3]) {
    auto* m = static_cast<MiniCtx*>(c);
    if (auto ent = m->scn->GetEntityById(e))
        ent->GetTransform()->SetWorldPosition(glm::vec3(p[0], p[1], p[2]));
}
uint32_t mini_spawn(void* c, int, const float pos[3], float size, const float rgba[4], int) {
    auto* m = static_cast<MiniCtx*>(c);
    auto ent = scene::EntityFactory::CreateCube(
        m->scn, "spawned_" + std::to_string(m->spawn_counter++), size,
        glm::vec4(rgba[0], rgba[1], rgba[2], rgba[3]));
    if (!ent) return 0;
    ent->GetTransform()->SetWorldPosition(glm::vec3(pos[0], pos[1], pos[2]));
    return ent->GetId();
}
const std::string* mini_param(void* c, uint32_t e, const char* name) {
    auto* m = static_cast<MiniCtx*>(c);
    auto ent = name ? m->scn->GetEntityById(e) : nullptr;
    auto sc  = ent ? ent->GetComponent<scene::ScriptComponent>() : nullptr;
    return sc ? sc->FindParam(name) : nullptr;
}
float mini_get_param_float(void* c, uint32_t e, const char* name, float def) {
    const std::string* v = mini_param(c, e, name);
    return v ? std::strtof(v->c_str(), nullptr) : def;
}
bool mini_get_param_bool(void* c, uint32_t e, const char* name, int def) {
    const std::string* v = mini_param(c, e, name);
    if (!v) return def != 0;
    return (*v == "1" || *v == "true" || *v == "True");
}
}  // namespace

static void write_file(const char* path, const char* text) {
    std::ofstream f(path, std::ios::trunc);
    f << text;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("script_check: Stage 12 scripting (Python / C++ / C#)\n");
    // Fresh compile cache so reruns are deterministic.
    { std::error_code ec; std::filesystem::remove_all("script_cache", ec); }

    auto scn = std::make_shared<scene::Scene>("script_test");
    auto obj = scn->CreateEntity("Obj");
    auto sc  = obj->AddComponent<scene::ScriptComponent>(std::string("script_check_tmp.py"));

    // v1: on_start marks, on_update moves to (1,2,3) and spawns one cube once.
    write_file("script_check_tmp.py",
               "import engine\n"
               "spawned = False\n"
               "def on_start(e):\n"
               "    engine.log('v1 start on ' + str(e))\n"
               "def on_update(e, dt):\n"
               "    global spawned\n"
               "    engine.set_position(e, 1.0, 2.0, 3.0)\n"
               "    if not spawned:\n"
               "        spawned = True\n"
               "        engine.spawn_cube(9.0, 9.0, 9.0, 1.0, 1.0, 0.0, 0.0, False)\n");

    editor::ScriptSystem sys;
    sys.register_host(".py", editor::make_python_host());

    MiniCtx ctx{scn, 0};
    editor::ScriptApi api;          // persistent table (address captured)
    api.ctx             = &ctx;
    api.log             = &mini_log;
    api.find_entity     = &mini_find;
    api.get_position    = &mini_get_pos;
    api.set_position    = &mini_set_pos;
    api.spawn_primitive = &mini_spawn;
    api.get_param_float = &mini_get_param_float;
    api.get_param_bool  = &mini_get_param_bool;

    for (int i = 0; i < 3; ++i) { api.dt = 0.016f; sys.update(scn, true, 0.016f, api); }

    const glm::vec3 p1  = obj->GetTransform()->GetWorldPosition();
    const bool moved    = std::fabs(p1.x - 1) < 1e-4f && std::fabs(p1.y - 2) < 1e-4f &&
                          std::fabs(p1.z - 3) < 1e-4f;
    const bool spawned  = scn->GetEntityByName("spawned_0") != nullptr;
    const bool running  = sc->GetStatus() == "running";
    std::printf("  v1: moved %s | spawned %s | status '%s' %s\n",
                moved ? "OK" : "FAIL", spawned ? "OK" : "FAIL",
                sc->GetStatus().c_str(), running ? "OK" : "FAIL");

    // ---- hot reload: v2 moves elsewhere; on_start must re-run (fresh VM) ----
    namespace fs = std::filesystem;
    std::error_code mec;
    const auto mt_before = fs::last_write_time("script_check_tmp.py", mec);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // distinct mtime
    write_file("script_check_tmp.py",
               "import engine\n"
               "def on_start(e):\n"
               "    engine.set_position(e, 7.0, 42.0, 7.0)\n"
               "    engine.log('v2 start (hot reloaded)')\n");
    const auto mt_after = fs::last_write_time("script_check_tmp.py", mec);
    std::printf("  [diag] mtime changed on disk: %s (ec=%s)\n",
                mt_after != mt_before ? "yes" : "NO",
                mec ? mec.message().c_str() : "ok");
    // Cross the 0.5 s mtime-poll threshold, then run a frame.
    sys.update(scn, true, 0.6f, api);
    sys.update(scn, true, 0.016f, api);
    const glm::vec3 p2 = obj->GetTransform()->GetWorldPosition();
    const bool reloaded = std::fabs(p2.y - 42.0f) < 1e-4f;
    std::printf("  hot reload: pos (%.1f,%.1f,%.1f) -> %s\n",
                p2.x, p2.y, p2.z, reloaded ? "OK" : "FAIL");

    // ---- broken script: error status, no crash ----
    auto bad   = scn->CreateEntity("Bad");
    auto badsc = bad->AddComponent<scene::ScriptComponent>(std::string("script_check_bad.py"));
    write_file("script_check_bad.py", "def on_update(e, dt)\n    syntax error here\n");
    sys.update(scn, true, 0.016f, api);
    const bool errored = !badsc->GetStatus().empty() && badsc->GetStatus() != "running";
    std::printf("  broken script: status '%.60s' -> %s\n",
                badsc->GetStatus().c_str(), errored ? "OK (reported)" : "FAIL");

    // ---- teardown on play stop ----
    sys.update(scn, false, 0.016f, api);
    const bool torn_down = sys.active_count() == 0;

    // ================= native C++ backend (g++ -> DLL) =================
    // Uses the REAL compile pipeline; needs g++ on PATH (present on the dev
    // machine — the engine itself builds with it). The SDK header is copied
    // next to the script so `#include "schizo_script.h"` resolves.
    bool cpp_ok = true;
    if (auto cpp_host = editor::make_cpp_host()) {
        std::error_code cec;
        std::filesystem::copy_file("../../assets/scripts/schizo_script.h",
                                   "schizo_script.h",
                                   std::filesystem::copy_options::overwrite_existing, cec);
        auto cobj = scn->CreateEntity("CppObj");
        cobj->AddComponent<scene::ScriptComponent>(std::string("script_check_tmp.cpp"));
        write_file("script_check_tmp.cpp",
                   "#include \"schizo_script.h\"\n"
                   "SCHIZO_SCRIPT void on_update(const SchizoScriptApi* api, unsigned e, float dt) {\n"
                   "    const float p[3] = {5.0f, 6.0f, 7.0f};\n"
                   "    api->set_position(api->ctx, e, p);\n"
                   "}\n");
        editor::ScriptSystem csys;
        csys.register_host(".cpp", editor::make_cpp_host());
        std::printf("  [cpp] compiling v1 (takes a moment)...\n");
        for (int i = 0; i < 2; ++i) csys.update(scn, true, 0.016f, api);
        const glm::vec3 cp1 = cobj->GetTransform()->GetWorldPosition();
        const bool cpp_moved = std::fabs(cp1.x - 5) < 1e-4f && std::fabs(cp1.y - 6) < 1e-4f;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        write_file("script_check_tmp.cpp",
                   "#include \"schizo_script.h\"\n"
                   "SCHIZO_SCRIPT void on_start(const SchizoScriptApi* api, unsigned e) {\n"
                   "    const float p[3] = {11.0f, 22.0f, 33.0f};\n"
                   "    api->set_position(api->ctx, e, p);\n"
                   "    api->log(api->ctx, \"C++ v2 hot reloaded\");\n"
                   "}\n");
        std::printf("  [cpp] recompiling v2 (hot reload)...\n");
        csys.update(scn, true, 0.6f, api);
        csys.update(scn, true, 0.016f, api);
        const glm::vec3 cp2 = cobj->GetTransform()->GetWorldPosition();
        const bool cpp_reloaded = std::fabs(cp2.y - 22.0f) < 1e-4f;
        csys.update(scn, false, 0.016f, api);   // frees + deletes the dlls
        std::printf("  cpp: moved %s | hot reload %s\n",
                    cpp_moved ? "OK" : "FAIL", cpp_reloaded ? "OK" : "FAIL");
        cpp_ok = cpp_moved && cpp_reloaded;
        std::remove("script_check_tmp.cpp");
        std::remove("schizo_script.h");
    } else {
        std::printf("  cpp: backend unavailable on this platform — skipped\n");
    }

    // ================= C# backend (.NET via hostfxr) =================
    // Real pipeline: dotnet build + hostfxr load. Needs the .NET SDK; the
    // first build can take several seconds (SDK warmup).
    bool cs_ok = true;
    if (auto cs_probe = editor::make_cs_host()) {
        auto csobj = scn->CreateEntity("CsObj");
        csobj->AddComponent<scene::ScriptComponent>(std::string("script_check_tmp.cs"));
        write_file("script_check_tmp.cs",
                   "using Schizo;\n"
                   "public static class Script {\n"
                   "    public static void OnStart(Api api, uint e) {\n"
                   "        api.Log(\"C# v1 start\");\n"
                   "    }\n"
                   "    public static void OnUpdate(Api api, uint e, float dt) {\n"
                   "        api.SetPosition(e, 13.0f, 14.0f, 15.0f);\n"
                   "    }\n"
                   "}\n");
        editor::ScriptSystem cssys;
        cssys.register_host(".cs", editor::make_cs_host());
        std::printf("  [cs] dotnet build v1 (can take a while on first run)...\n");
        for (int i = 0; i < 2; ++i) cssys.update(scn, true, 0.016f, api);
        const glm::vec3 sp1 = csobj->GetTransform()->GetWorldPosition();
        const bool cs_moved = std::fabs(sp1.x - 13) < 1e-4f && std::fabs(sp1.y - 14) < 1e-4f;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        write_file("script_check_tmp.cs",
                   "using Schizo;\n"
                   "public static class Script {\n"
                   "    public static void OnStart(Api api, uint e) {\n"
                   "        api.SetPosition(e, 44.0f, 55.0f, 66.0f);\n"
                   "        api.Log(\"C# v2 hot reloaded\");\n"
                   "    }\n"
                   "}\n");
        std::printf("  [cs] dotnet build v2 (hot reload)...\n");
        cssys.update(scn, true, 0.6f, api);
        cssys.update(scn, true, 0.016f, api);
        const glm::vec3 sp2 = csobj->GetTransform()->GetWorldPosition();
        const bool cs_reloaded = std::fabs(sp2.y - 55.0f) < 1e-4f;
        cssys.update(scn, false, 0.016f, api);
        std::printf("  cs: moved %s | hot reload %s\n",
                    cs_moved ? "OK" : "FAIL", cs_reloaded ? "OK" : "FAIL");
        cs_ok = cs_moved && cs_reloaded;
        std::remove("script_check_tmp.cs");
    } else {
        std::printf("  cs: backend unavailable on this platform — skipped\n");
    }

    // ============== script public parameters (Stage 12 script fields) ==============
    // (a) the @param scanner reads declarations straight from source (no execution);
    // (b) get_param_* returns the entity's Inspector-authored override at runtime.
    bool params_ok = false;
    {
        write_file("script_check_params.py",
                   "# @param speed float 90.0 Spin Speed\n"
                   "# @param glow bool true\n"
                   "import engine\n"
                   "def on_update(e, dt):\n"
                   "    s = engine.get_param_float(e, 'speed', 90.0)\n"
                   "    engine.set_position(e, s, 0.0, 0.0)\n");
        auto decls = editor::scan_script_params("script_check_params.py");
        const bool scan_ok = decls.size() == 2 &&
                             decls[0].name == "speed" && decls[0].type == "float" &&
                             decls[0].def == "90.0" && decls[0].label == "Spin Speed" &&
                             decls[1].name == "glow" && decls[1].type == "bool";

        auto pobj = scn->CreateEntity("ParamObj");
        auto psc  = pobj->AddComponent<scene::ScriptComponent>(std::string("script_check_params.py"));
        psc->SetParam("speed", "250");          // stand-in for an Inspector edit
        editor::ScriptSystem psys;
        psys.register_host(".py", editor::make_python_host());
        for (int i = 0; i < 2; ++i) psys.update(scn, true, 0.016f, api);
        const glm::vec3 pp = pobj->GetTransform()->GetWorldPosition();
        const bool runtime_ok = std::fabs(pp.x - 250.0f) < 1e-3f;   // override, not the 90 default
        psys.update(scn, false, 0.016f, api);
        params_ok = scan_ok && runtime_ok;
        std::printf("  params: scan %s | runtime override %s (x=%.1f, expected 250)\n",
                    scan_ok ? "OK" : "FAIL", runtime_ok ? "OK" : "FAIL", pp.x);
        std::remove("script_check_params.py");
    }

    std::remove("script_check_tmp.py");
    std::remove("script_check_bad.py");
    const bool ok = moved && spawned && running && reloaded && errored && torn_down &&
                    cpp_ok && cs_ok && params_ok;
    std::printf("script_check: %s\n", ok ? "ALL OK" : "FAIL");
    return ok ? 0 : 1;
}
