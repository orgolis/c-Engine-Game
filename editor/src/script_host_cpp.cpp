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
#include "game_export.h"   // prebuilt_key

#include <spdlog/spdlog.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <array>
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
#include <cstdint>
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
static_assert(offsetof(SchizoScriptApi, get_param_string) ==
              offsetof(schizo::editor::ScriptApi, get_param_string),
              "schizo_script.h drifted from script_api.h (get_param_string)");
static_assert(offsetof(SchizoScriptApi, get_forward) ==
              offsetof(schizo::editor::ScriptApi, get_forward),
              "schizo_script.h drifted from script_api.h (get_forward)");

namespace schizo::editor {

#ifdef _WIN32

namespace {

using StartFn  = void (*)(const SchizoScriptApi*, unsigned);
using UpdateFn = void (*)(const SchizoScriptApi*, unsigned, float);
// Phase 4.8: a command entry point. Takes the api so an extension command can
// do the same things on_start can, and nothing else -- a command is a verb, not
// a lifecycle hook.
using CommandFn = void (*)(const SchizoScriptApi*);

// Run a command line, capture combined stdout+stderr, return exit code.
int run_capture(const std::string& cmd, std::string& out) {
    out.clear();
    // cmd.exe strips the outer quote pair. Keep the compiler and the script's
    // own quoted paths intact, including installs/projects containing spaces.
    const std::string full = "\"" + cmd + " 2>&1\"";
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

    // The token is an exported symbol name. Resolved on each call rather than
    // cached at load: the set of commands is decided by the script at run time,
    // so there is no list to resolve against when the DLL is opened.
    bool invoke(const char* token, std::string& err) override {
        if (!token || !*token) { err = "empty command token"; return false; }
        if (!mod_) { err = "script module is not loaded"; return false; }
        auto fn = reinterpret_cast<CommandFn>(GetProcAddress(mod_, token));
        if (!fn) {
            err = std::string("the script exports no function named '") + token +
                  "' (it must be extern \"C\" and __declspec(dllexport))";
            return false;
        }
        fn(reinterpret_cast<const SchizoScriptApi*>(api_));
        return true;
    }

private:
    HMODULE          mod_;
    std::string      dll_path_;
    const ScriptApi* api_;
    StartFn          start_;
    UpdateFn         update_;
};

// A Hub-launched editor does not inherit a developer shell's PATH. Prefer the
// configured compiler, then the project's pinned Windows toolchain, with PATH
// lookup available for other MinGW installations.
std::string script_compiler() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (const char* configured = std::getenv("GWS_SCRIPT_CXX"); configured && fs::is_regular_file(configured, ec))
        return configured;
    if (fs::is_regular_file("C:/Strawberry/c/bin/g++.exe", ec)) return "C:/Strawberry/c/bin/g++.exe";
    return "g++";
}

// The script's path as the export keyed it: project-relative.
std::string project_relative(const std::string& source_path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path sp(source_path);
    if (!sp.is_absolute()) return sp.generic_string();
    const fs::path rel = fs::relative(sp, fs::current_path(ec), ec);
    return ec ? sp.generic_string() : rel.generic_string();
}

bool build_cpp_script(const std::string& source_path, std::string& dll, std::string& err) {
        namespace fs = std::filesystem;
        static std::atomic<uint32_t> gen{0};
        std::error_code ec;
        const std::string stem = fs::path(source_path).stem().string();

        // An exported game: the script was built at export; load a private
        // copy of it (never a compiler on the player's computer).
        if (const PrebuiltScripts& pb = prebuilt_scripts(); !pb.dir.empty()) {
            const std::string rel = project_relative(source_path);
            const fs::path pre = fs::path(pb.dir) / (gexport::prebuilt_key(rel) + ".dll");
            if (!fs::is_regular_file(pre, ec)) {
                err = "this game was exported without the script " + rel + " (export it again)";
                return false;
            }
            fs::create_directories(pb.instance_dir, ec);
            dll = (fs::path(pb.instance_dir) / (stem + "_" + std::to_string(GetCurrentProcessId()) + "_" +
                                                std::to_string(gen++) + ".dll")).string();
            fs::copy_file(pre, dll, fs::copy_options::overwrite_existing, ec);
            if (ec) { err = "cannot prepare " + rel + ": " + ec.message(); return false; }
            spdlog::info("[script] '{}' built for this game", rel);
            return true;
        }

        if (!fs::exists(source_path)) { err = "cannot open " + source_path; return false; }
        fs::create_directories("script_cache", ec);

        // Fresh dll per compile — loaded DLLs are locked, so never overwrite.
        dll = "script_cache/" + stem + "_" +
                                 std::to_string(GetCurrentProcessId()) + "_" + std::to_string(gen++) + ".dll";

        // -I the script's own dir + assets/scripts so `#include
        // "schizo_script.h"` resolves wherever the script lives.
        const std::string src_dir = fs::path(source_path).parent_path().string();
        const std::string compiler = script_compiler();
        // Hash the preprocessed translation unit. GCC resolves nested includes,
        // conditionals and SDK headers exactly as it will for the real build.
        // Each instance still loads a private DLL (scripts can have globals).
        const std::string options = " -std=c++17 -O2 -shared ";
        const std::string prefix = "\"" + compiler + "\"" + options;
        const fs::path preprocessed = dll + ".ii";
        struct TempFile {
            fs::path path;
            ~TempFile() { std::error_code ignored; fs::remove(path, ignored); }
        } cleanup{preprocessed};
        std::string preprocess = prefix + "-E \"" + source_path + "\" -o \"" +
                                 preprocessed.string() + "\" -I \"assets/scripts\"";
        if (!src_dir.empty()) preprocess += " -I \"" + src_dir + "\"";
        std::string output;
        if (run_capture(preprocess, output) != 0) {
            err = "preprocessing failed: " + output.substr(0, 600);
            return false;
        }
        uint64_t digest = 14695981039346656037ull;
        auto hash = [&](const char* data, size_t length) {
            for (size_t i = 0; i < length; ++i)
                digest = (digest ^ static_cast<unsigned char>(data[i])) * 1099511628211ull;
        };
        // SearchPath also resolves an unqualified compiler from PATH.
        char resolved[MAX_PATH]{};
        SearchPathA(nullptr, compiler.c_str(), ".exe", MAX_PATH, resolved, nullptr);
        const fs::path tool = resolved[0] ? resolved : compiler;
        auto stamp = fs::last_write_time(tool, ec).time_since_epoch().count();
        const std::string identity = tool.string() + options + std::to_string(stamp);
        hash(identity.data(), identity.size());
        std::ifstream input(preprocessed, std::ios::binary);
        std::array<char, 16384> buffer;
        while (input) {
            input.read(buffer.data(), buffer.size());
            hash(buffer.data(), static_cast<size_t>(input.gcount()));
        }
        if (!input.eof()) { err = "cannot read preprocessed script"; return false; }
        input.close();
        const fs::path cached = fs::path("script_cache") / ("compiled_" + std::to_string(digest) + ".dll");
        if (fs::copy_file(cached, dll, fs::copy_options::overwrite_existing, ec)) {
            spdlog::info("[script] reused compiled '{}'", source_path);
        } else {
            spdlog::info("[script] compiling '{}' -> {}", source_path, dll);
            const std::string build = prefix + "-o \"" + dll + "\" \"" + preprocessed.string() + "\"";
            if (run_capture(build, output) != 0) {
                err = "compile failed: " + output.substr(0, 600);
                return false;
            }
            // Publish a complete image atomically if two game processes race.
            TempFile stage{dll + ".publishing"};
            if (fs::copy_file(dll, stage.path, fs::copy_options::overwrite_existing, ec))
                fs::rename(stage.path, cached, ec);
        }

        return true;
}

class CppHost final : public ScriptHost {
public:
    const char* language() const override { return "C++ (g++ dll)"; }

    std::unique_ptr<ScriptInstance> create(const std::string& source_path,
                                           const ScriptApi* api,
                                           std::string& err) override {
        std::string dll;
        if (!build_cpp_script(source_path,dll,err)) return nullptr;
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

bool export_cpp_script(const std::string& rel_source, const std::string& out_dll, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(rel_source, ec)) { error = "cannot open " + rel_source; return false; }
    fs::create_directories(fs::path(out_dll).parent_path(), ec);
    // -static: the C++ runtime inside the DLL, so it depends on nothing but
    // Windows (the compiler's own runtime DLLs are not the engine's). -s: no
    // symbols in a shipped game.
    const std::string src_dir = fs::path(rel_source).parent_path().string();
    std::string cmd = "\"" + script_compiler() + "\" -std=c++17 -O2 -shared -static -s -o \"" + out_dll + "\" \"" +
                      rel_source + "\" -I \"assets/scripts\"";
    if (!src_dir.empty()) cmd += " -I \"" + src_dir + "\"";
    std::string output;
    if (run_capture(cmd, output) != 0 || !fs::is_regular_file(out_dll, ec)) {
        error = output.empty() ? std::string("the compiler did not run (is g++ installed? GWS_SCRIPT_CXX names it)")
                               : output.substr(0, 1500);
        return false;
    }
    return true;
}

bool prepare_cpp_script(const std::string& path,std::string& error) {
    std::string instance;
    bool ok=build_cpp_script(path,instance,error);
    std::error_code ignored;
    if (!instance.empty()) std::filesystem::remove(instance,ignored);
    return ok;
}

std::unique_ptr<ScriptHost> make_cpp_host() { return std::make_unique<CppHost>(); }

#else  // !_WIN32

bool prepare_cpp_script(const std::string&,std::string&) { return true; }
bool export_cpp_script(const std::string&, const std::string&, std::string& error) {
    error = "C++ scripts are built for Windows games only";
    return false;
}
std::unique_ptr<ScriptHost> make_cpp_host() { return nullptr; }

#endif

}  // namespace schizo::editor
