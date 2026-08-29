#pragma once

// ============================================================
// ScriptSystem — drives per-entity user scripts (Stage 12)
// ============================================================
//
// Language-agnostic: backends implement ScriptHost (one per language,
// registered by file extension) and produce ScriptInstance objects with the
// two lifecycle hooks. The system:
//   - lazily instantiates a script when its entity is first seen while playing
//   - calls on_start once, then on_update every frame
//   - watches file mtimes (every 0.5 s) and re-instantiates on change — hot
//     reload: state lives in the scene/ECS, so replacing the VM is safe
//   - tears everything down when Play stops
//   - pushes per-script status ("ok" / error text) into ScriptComponent for
//     the Inspector
//
// The system itself has no engine dependencies beyond the scene — the engine
// capabilities scripts call are supplied per-frame via the ScriptApi table
// (built by the editor, or by a test harness in tools/script_check).

#include "script_api.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace schizo::scene { class Scene; }

namespace schizo::editor {

class ScriptInstance {
public:
    virtual ~ScriptInstance() = default;
    // Both return false on script error; `err` gets the message.
    virtual bool start(uint32_t entity, std::string& err) = 0;
    virtual bool update(uint32_t entity, float dt, std::string& err) = 0;

    /// Call a no-argument entry point named `token` (Phase 4.8) -- how an editor
    /// command registered by a script gets back into that script when someone
    /// runs it. Not pure virtual: a backend that cannot do it stays compilable
    /// and simply reports so, rather than every host being forced to grow a stub
    /// on the day this was added.
    virtual bool invoke(const char* token, std::string& err) {
        err = std::string("this script backend cannot invoke '") +
              (token ? token : "") + "'";
        return false;
    }
};

class ScriptHost {
public:
    virtual ~ScriptHost() = default;
    virtual const char* language() const = 0;
    // Load + prepare `source_path`. Returns nullptr with `err` set on failure
    // (syntax error, missing file, compile failure...).
    virtual std::unique_ptr<ScriptInstance> create(const std::string& source_path,
                                                   const ScriptApi* api,
                                                   std::string& err) = 0;
};

/// Factory for the Python (pocketpy) backend — defined in script_host_python.cpp.
std::unique_ptr<ScriptHost> make_python_host();
/// Factory for the native C++ (g++ -> DLL) backend — script_host_cpp.cpp.
/// Returns nullptr on non-Windows platforms.
std::unique_ptr<ScriptHost> make_cpp_host();
/// Factory for the C# (.NET via hostfxr + dotnet build) backend —
/// script_host_cs.cpp. Returns nullptr on non-Windows platforms.
std::unique_ptr<ScriptHost> make_cs_host();

class ScriptSystem {
public:
    // Map a file extension (".py") to a backend.
    void register_host(const std::string& ext, std::unique_ptr<ScriptHost> host) {
        hosts_[ext] = std::move(host);
    }

    // Drive one frame. `playing` false tears down all instances (Play ended).
    // `api` must stay valid for the duration of the call; its dt/time are
    // already filled by the caller.
    void update(const std::shared_ptr<schizo::scene::Scene>& scene,
                bool playing, float dt, const ScriptApi& api);

    // Force one entity's script to re-instantiate next frame (Inspector's
    // Reload button).
    void force_reload(uint32_t entity_id) { slots_.erase(entity_id); }

    void clear() { slots_.clear(); }
    size_t active_count() const { return slots_.size(); }

private:
    struct Slot {
        std::unique_ptr<ScriptInstance> inst;
        std::string path;
        uint64_t    mtime   = 0;       // raw FILETIME ticks (100 ns) on Windows
        bool        started = false;
        bool        failed  = false;   // don't retry a broken script every frame
    };
    std::unordered_map<uint32_t, Slot> slots_;                       // entity id -> script
    std::unordered_map<std::string, std::unique_ptr<ScriptHost>> hosts_;  // ".py" -> backend
    float watch_timer_ = 0.0f;
};

}  // namespace schizo::editor
