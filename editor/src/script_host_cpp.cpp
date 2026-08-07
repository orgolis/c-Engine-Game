// ============================================================
// Native C++ script backend — g++ -> DLL hot swap (Stage 12)
// ============================================================
//
// A user .cpp (including assets/scripts/schizo_script.h) is compiled with the
// system g++ into script_cache/<name>_<n>.dll, LoadLibrary'd, and its exported
// on_start/on_update called with the SAME ScriptApi table every backend uses.
// Hot reload: the ScriptSystem re-creates the instance on file change; each
// compile targets a FRESH dll name (Windows locks loaded DLLs), the old module
// is freed and its file best-effort deleted on instance destruction.
//
// Compile errors are captured (g++ stderr) and surfaced in the Inspector.
// Native code runs in-process: a crashing script crashes the editor — that is
// the documented trade-off of the native backend.

#include "script_system.h"

#include <spdlog/spdlog.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Layout guard: the shipped SDK header must stay binary-identical to the
// editor's ScriptApi. A drift breaks user scripts at runtime — fail the
// EDITOR build instead.
#include "../../assets/scripts/schizo_script.h"
#include <cstddef>
static_assert(sizeof(SchizoScriptApi) == sizeof(schizo::editor::ScriptApi),
              "schizo_script.h drifted from script_api.h (size)");
static_assert(offsetof(SchizoScriptApi, time) ==
              offsetof(schizo::editor::ScriptApi, time),
              "schizo_script.h drifted from script_api.h (time)");
static_assert(offsetof(SchizoScriptApi, spawn_primitive) ==
              offsetof(schizo::editor::ScriptApi, spawn_primitive),
              "schizo_script.h drifted from script_api.h (spawn_primitive)");
static_assert(offsetof(SchizoScriptApi, audio_stop) ==
              offsetof(schizo::editor::ScriptApi, audio_stop),
              "schizo_script.h drifted from script_api.h (audio_stop)");
static_assert(offsetof(SchizoScriptApi, use_item) ==
              offsetof(schizo::editor::ScriptApi, use_item),
              "schizo_script.h drifted from script_api.h (use_item)");
static_assert(offsetof(SchizoScriptApi, interact) ==
              offsetof(schizo::editor::ScriptApi, interact),
              "schizo_script.h drifted from script_api.h (interact)");
static_assert(offsetof(SchizoScriptApi, reload_weapon) ==
              offsetof(schizo::editor::ScriptApi, reload_weapon),
              "schizo_script.h drifted from script_api.h (reload_weapon)");
static_assert(offsetof(SchizoScriptApi, drive) ==
              offsetof(schizo::editor::ScriptApi, drive),
              "schizo_script.h drifted from script_api.h (drive)");
static_assert(offsetof(SchizoScriptApi, toggle_flashlight) ==
              offsetof(schizo::editor::ScriptApi, toggle_flashlight),
              "schizo_script.h drifted from script_api.h (toggle_flashlight)");

namespace schizo::editor {

#ifdef _WIN32

namespace {

using StartFn  = void (*)(const SchizoScriptApi*, unsigned);
using UpdateFn = void (*)(const SchizoScriptApi*, unsigned, float);

// Run a command line, capture combined stdout+stderr, return exit code.
int run_capture(const std::string& cmd, std::string& out) {
    out.clear();
    const std::string full = cmd + " 2>&1";
    FILE* p = _popen(full.c_str(), "r");
    if (!p) return -1;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    return _pclose(p);
}

class CppInstance final : public ScriptInstance {
public:
    CppInstance(HMODULE mod, std::string dll_path, const ScriptApi* api,
                StartFn s, UpdateFn u)
        : mod_(mod), dll_path_(std::move(dll_path)), api_(api), start_(s), update_(u) {}

    ~CppInstance() override {
        if (mod_) FreeLibrary(mod_);
        // Best-effort: remove the now-unloaded dll from the cache.
        std::error_code ec;
        std::filesystem::remove(dll_path_, ec);
    }

    bool start(uint32_t entity, std::string& err) override {
        (void)err;
        if (start_) start_(reinterpret_cast<const SchizoScriptApi*>(api_), entity);
        return true;
    }
    bool update(uint32_t entity, float dt, std::string& err) override {
        (void)err;
        if (update_) update_(reinterpret_cast<const SchizoScriptApi*>(api_), entity, dt);
        return true;
    }

private:
    HMODULE          mod_;
    std::string      dll_path_;
    const ScriptApi* api_;
    StartFn          start_;
    UpdateFn         update_;
};

class CppHost final : public ScriptHost {
public:
    const char* language() const override { return "C++ (g++ dll)"; }

    std::unique_ptr<ScriptInstance> create(const std::string& source_path,
                                           const ScriptApi* api,
                                           std::string& err) override {
        namespace fs = std::filesystem;
        if (!fs::exists(source_path)) { err = "cannot open " + source_path; return nullptr; }

        std::error_code ec;
        fs::create_directories("script_cache", ec);

        // Fresh dll per compile — loaded DLLs are locked, so never overwrite.
        static std::atomic<uint32_t> gen{0};
        const std::string stem = fs::path(source_path).stem().string();
        const std::string dll  = "script_cache/" + stem + "_" +
                                 std::to_string(gen++) + ".dll";

        // -I the script's own dir + assets/scripts so `#include
        // "schizo_script.h"` resolves wherever the script lives.
        const std::string src_dir = fs::path(source_path).parent_path().string();
        std::string cmd = "g++ -std=c++17 -O2 -shared "
                          "-o \"" + dll + "\" \"" + source_path + "\" "
                          "-I \"assets/scripts\"";
        if (!src_dir.empty()) cmd += " -I \"" + src_dir + "\"";

        spdlog::info("[script] compiling '{}' -> {}", source_path, dll);
        std::string output;
        const int rc = run_capture(cmd, output);
        if (rc != 0) {
            // First lines of g++'s message are the useful ones.
            err = "compile failed: " + output.substr(0, 600);
            return nullptr;
        }

        HMODULE mod = LoadLibraryA(dll.c_str());
        if (!mod) {
            err = "LoadLibrary failed (err " +
                  std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
            return nullptr;
        }
        auto s = reinterpret_cast<StartFn>(GetProcAddress(mod, "on_start"));
        auto u = reinterpret_cast<UpdateFn>(GetProcAddress(mod, "on_update"));
        if (!s && !u) {
            FreeLibrary(mod);
            err = "script exports neither on_start nor on_update "
                  "(use SCHIZO_SCRIPT from schizo_script.h)";
            return nullptr;
        }
        return std::make_unique<CppInstance>(mod, dll, api, s, u);
    }
};

}  // namespace

std::unique_ptr<ScriptHost> make_cpp_host() { return std::make_unique<CppHost>(); }

#else  // !_WIN32

std::unique_ptr<ScriptHost> make_cpp_host() { return nullptr; }

#endif

}  // namespace schizo::editor
