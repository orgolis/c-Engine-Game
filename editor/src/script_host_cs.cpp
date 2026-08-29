// ============================================================
// C# script backend — .NET hosting via hostfxr (Stage 12)
// ============================================================
//
// A user .cs is compiled by `dotnet build` together with the shipped SDK
// (assets/scripts/SchizoScript.cs — the NativeApi mirror + Bridge entry
// points) into a unique assembly under script_cache/, then loaded through
// hostfxr's load_assembly_and_get_function_pointer with the Bridge's
// [UnmanagedCallersOnly] Start/Update methods. The same ScriptApi table every
// backend uses is passed as an IntPtr and wrapped by the C# `Api` class.
//
// Hot reload: each recompile produces a NEW assembly name (the default
// AssemblyLoadContext cannot unload), so the old assembly stays resident —
// a small, dev-time-only memory cost per reload.
//
// Requires the .NET SDK (`dotnet` on PATH) + a .NET 8/9 runtime; both are
// detected at startup and the backend reports a clear error if missing.

#include "script_system.h"

#include <spdlog/spdlog.h>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#endif

namespace schizo::editor {

#ifdef _WIN32

namespace {

// ---- hostfxr C ABI (hosting headers not vendored; declarations are stable) ----
using char_t = wchar_t;
using hostfxr_handle = void*;
using hostfxr_initialize_for_runtime_config_fn =
    int (__cdecl*)(const char_t* runtime_config_path, const void* parameters, hostfxr_handle* host_context_handle);
using hostfxr_get_runtime_delegate_fn =
    int (__cdecl*)(hostfxr_handle host_context_handle, int type, void** delegate);
using hostfxr_close_fn = int (__cdecl*)(hostfxr_handle);
constexpr int hdt_load_assembly_and_get_function_pointer = 5;
using load_assembly_and_get_function_pointer_fn =
    int (__cdecl*)(const char_t* assembly_path, const char_t* type_name,
                   const char_t* method_name, const char_t* delegate_type_name,
                   void* reserved, void** out_delegate);
#define UNMANAGEDCALLERSONLY_METHOD ((const char_t*)-1)

using CsStartFn  = void (__cdecl*)(void* api, uint32_t entity);
using CsUpdateFn = void (__cdecl*)(void* api, uint32_t entity, float dt);

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

int run_capture(const std::string& cmd, std::string& out) {
    out.clear();
    const std::string full = cmd + " 2>&1";
    FILE* p = _popen(full.c_str(), "r");
    if (!p) return -1;
    char buf[512];
    while (fgets(buf, sizeof buf, p)) out += buf;
    return _pclose(p);
}

// Lazily-initialized .NET runtime; shared by all C# script instances.
struct DotnetRuntime {
    load_assembly_and_get_function_pointer_fn load_fn = nullptr;
    bool tried = false;

    bool ensure(const std::string& runtime_config, std::string& err) {
        if (load_fn) return true;
        if (tried) { err = "hostfxr init already failed this session"; return false; }
        tried = true;

        // Locate the newest hostfxr.dll under <dotnet root>/host/fxr/.
        namespace fs = std::filesystem;
        const char* roots[] = { "C:\\Program Files\\dotnet", "C:\\Program Files (x86)\\dotnet" };
        std::string best;
        for (const char* root : roots) {
            const fs::path fxr = fs::path(root) / "host" / "fxr";
            std::error_code ec;
            if (!fs::exists(fxr, ec)) continue;
            for (const auto& d : fs::directory_iterator(fxr, ec)) {
                const std::string cand = (d.path() / "hostfxr.dll").string();
                if (fs::exists(cand) && (best.empty() || d.path().filename().string() >
                                                          fs::path(best).parent_path().filename().string()))
                    best = cand;
            }
        }
        if (best.empty()) { err = ".NET not found (no hostfxr.dll under Program Files/dotnet)"; return false; }

        HMODULE fxr = LoadLibraryA(best.c_str());
        if (!fxr) { err = "failed to load " + best; return false; }
        auto init_fn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
            GetProcAddress(fxr, "hostfxr_initialize_for_runtime_config"));
        auto get_delegate_fn = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
            GetProcAddress(fxr, "hostfxr_get_runtime_delegate"));
        auto close_fn = reinterpret_cast<hostfxr_close_fn>(
            GetProcAddress(fxr, "hostfxr_close"));
        if (!init_fn || !get_delegate_fn || !close_fn) {
            err = "hostfxr exports missing"; return false;
        }

        hostfxr_handle handle = nullptr;
        const std::wstring wcfg = widen(runtime_config);
        int rc = init_fn(wcfg.c_str(), nullptr, &handle);
        if (rc != 0 && rc != 1 /*Success_HostAlreadyInitialized*/ && rc != 2) {
            err = "hostfxr_initialize_for_runtime_config failed (0x" +
                  std::to_string(rc) + ")";
            return false;
        }
        void* fn = nullptr;
        rc = get_delegate_fn(handle, hdt_load_assembly_and_get_function_pointer, &fn);
        close_fn(handle);
        if (rc != 0 || !fn) { err = "hostfxr_get_runtime_delegate failed"; return false; }
        load_fn = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(fn);
        spdlog::info("[script] .NET runtime hosted via {}", best);
        return true;
    }
};
DotnetRuntime g_dotnet;

// Phase 4.8: an extension command. Same shape as Start minus the entity, and
// it must be [UnmanagedCallersOnly] like the other two.
using CsCommandFn = void (*)(ScriptApi*);

class CsInstance final : public ScriptInstance {
public:
    CsInstance(const ScriptApi* api, CsStartFn s, CsUpdateFn u,
               std::wstring asm_path, std::wstring type_name)
        : api_(api), start_(s), update_(u),
          asm_path_(std::move(asm_path)), type_name_(std::move(type_name)) {}
    bool start(uint32_t entity, std::string& err) override {
        (void)err;
        if (start_) start_(const_cast<ScriptApi*>(api_), entity);
        return true;
    }
    bool update(uint32_t entity, float dt, std::string& err) override {
        (void)err;
        if (update_) update_(const_cast<ScriptApi*>(api_), entity, dt);
        return true;
    }

    // Bound on demand: which methods exist is up to the script, so unlike
    // Start/Update there is no fixed list to resolve when the assembly loads.
    // The binding is cached because load_assembly_and_get_function_pointer is
    // far too slow to run on every click of a menu item.
    bool invoke(const char* token, std::string& err) override {
        if (!token || !*token) { err = "empty command token"; return false; }
        const std::string key(token);
        auto it = commands_.find(key);
        if (it == commands_.end()) {
            void*     fn  = nullptr;
            const int lrc = g_dotnet.load_fn(asm_path_.c_str(), type_name_.c_str(),
                                             widen(key).c_str(),
                                             UNMANAGEDCALLERSONLY_METHOD, nullptr, &fn);
            if (lrc != 0 || !fn) {
                err = "Schizo.Bridge." + key + " not found or not [UnmanagedCallersOnly] (0x" +
                      std::to_string(lrc) + ")";
                return false;
            }
            it = commands_.emplace(key, reinterpret_cast<CsCommandFn>(fn)).first;
        }
        it->second(const_cast<ScriptApi*>(api_));
        return true;
    }
private:
    const ScriptApi* api_;
    CsStartFn        start_;
    CsUpdateFn       update_;
    std::wstring     asm_path_;
    std::wstring     type_name_;
    std::unordered_map<std::string, CsCommandFn> commands_;
};

class CsHost final : public ScriptHost {
public:
    const char* language() const override { return "C# (.NET)"; }

    std::unique_ptr<ScriptInstance> create(const std::string& source_path,
                                           const ScriptApi* api,
                                           std::string& err) override {
        namespace fs = std::filesystem;
        if (!fs::exists(source_path)) { err = "cannot open " + source_path; return nullptr; }

        // Locate the shipped SDK source to compile alongside the user script.
        std::string sdk;
        for (const char* c : { "assets/scripts/SchizoScript.cs",
                               "../../assets/scripts/SchizoScript.cs" })
            if (fs::exists(c)) { sdk = c; break; }
        if (sdk.empty()) { err = "SchizoScript.cs SDK not found under assets/scripts"; return nullptr; }

        // Fresh project per compile — unique assembly name because the default
        // AssemblyLoadContext cannot unload (hot reload = load a new assembly).
        static std::atomic<uint32_t> gen{0};
        const uint32_t n = gen++;
        const std::string asm_name = "schizo_script_" + std::to_string(n);
        const fs::path dir = fs::path("script_cache") / ("cs_" + std::to_string(n));
        std::error_code ec;
        fs::create_directories(dir, ec);

        {
            std::ofstream proj(dir / "script.csproj", std::ios::trunc);
            proj << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
                    "  <PropertyGroup>\n"
                    "    <TargetFramework>net9.0</TargetFramework>\n"
                    "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
                    "    <Nullable>enable</Nullable>\n"
                    "    <AssemblyName>" << asm_name << "</AssemblyName>\n"
                    "    <GenerateRuntimeConfigurationFiles>true</GenerateRuntimeConfigurationFiles>\n"
                    "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n"
                    "  </PropertyGroup>\n"
                    "</Project>\n";
        }
        // Manual copy: fs::copy_file(overwrite_existing) unreliably reports
        // "File exists" on this MinGW libstdc++ when the target is present.
        auto stage = [](const std::string& from, const fs::path& to) -> bool {
            std::ifstream in(from, std::ios::binary);
            if (!in.is_open()) return false;
            std::ofstream out(to, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return false;
            out << in.rdbuf();
            return true;
        };
        if (!stage(sdk, dir / "Bridge.cs") || !stage(source_path, dir / "User.cs")) {
            err = "failed to stage sources into " + dir.string();
            return nullptr;
        }

        const std::string out_dir = (dir / "out").string();
        spdlog::info("[script] dotnet build '{}' -> {}", source_path, out_dir);
        std::string output;
        const int rc = run_capture(
            "dotnet build \"" + (dir / "script.csproj").string() +
            "\" -c Release -o \"" + out_dir + "\" --nologo -v q", output);
        if (rc != 0) {
            err = "C# compile failed: " + output.substr(0, 600);
            return nullptr;
        }

        const std::string asm_path = out_dir + "\\" + asm_name + ".dll";
        const std::string cfg_path =
            fs::absolute(out_dir + "\\" + asm_name + ".runtimeconfig.json").string();
        if (!fs::exists(asm_path)) {
            err = "dotnet build reported success but no assembly at " + asm_path;
            return nullptr;
        }
        if (!g_dotnet.ensure(cfg_path, err)) return nullptr;

        const std::wstring wasm  = widen(fs::absolute(asm_path).string());
        const std::wstring wtype = widen("Schizo.Bridge, " + asm_name);
        void* start_fn = nullptr;
        void* update_fn = nullptr;
        int lrc = g_dotnet.load_fn(wasm.c_str(), wtype.c_str(), L"Start",
                                   UNMANAGEDCALLERSONLY_METHOD, nullptr, &start_fn);
        if (lrc != 0) { err = "failed to bind Schizo.Bridge.Start (0x" + std::to_string(lrc) + ")"; return nullptr; }
        lrc = g_dotnet.load_fn(wasm.c_str(), wtype.c_str(), L"Update",
                               UNMANAGEDCALLERSONLY_METHOD, nullptr, &update_fn);
        if (lrc != 0) { err = "failed to bind Schizo.Bridge.Update (0x" + std::to_string(lrc) + ")"; return nullptr; }

        return std::make_unique<CsInstance>(api,
                                            reinterpret_cast<CsStartFn>(start_fn),
                                            reinterpret_cast<CsUpdateFn>(update_fn),
                                            wasm, wtype);
    }
};

}  // namespace

std::unique_ptr<ScriptHost> make_cs_host() { return std::make_unique<CsHost>(); }

#else  // !_WIN32

std::unique_ptr<ScriptHost> make_cs_host() { return nullptr; }

#endif

}  // namespace schizo::editor
