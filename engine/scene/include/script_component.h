#pragma once

#include "entity.h"
#include <map>
#include <string>

namespace schizo::scene {

// ============================================================================
// Script Component (Stage 12 — custom scripts)
// ============================================================================
//
// Attaches a user script file to an entity. The language is inferred from the
// file extension (.py = Python, .cpp = native C++, .cs = C#); the editor's
// ScriptSystem resolves the matching backend, instantiates the script when
// Play starts, and calls its lifecycle hooks:
//     on_start(entity)        — once, when Play begins (and after hot reload)
//     on_update(entity, dt)   — every frame while playing
// Scripts hot-reload: saving the file while playing re-instantiates it live.
//
// `status_` is runtime-only feedback for the Inspector ("ok" / compile or
// runtime error text) — set by the ScriptSystem, never serialized.

class ScriptComponent : public Component {
public:
    ScriptComponent() = default;
    explicit ScriptComponent(const std::string& path) : script_path_(path) {}
    virtual ~ScriptComponent() = default;

    const std::string& GetScriptPath() const { return script_path_; }
    void SetScriptPath(const std::string& p) { script_path_ = p; status_.clear(); }

    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool e) { enabled_ = e; }

    // Runtime status for the Inspector (not serialized).
    const std::string& GetStatus() const { return status_; }
    void SetStatus(const std::string& s) { status_ = s; }

    // ---- Public parameters (Stage 12 script fields) ----
    // Values the Inspector authors for the script's `@param` declarations and
    // the running script reads back via engine.get_param_*. Stored string-typed
    // (parsed on read); only overrides that differ from the declared default
    // are kept here. Keyed by param name.
    const std::map<std::string, std::string>& Params() const { return params_; }
    void SetParam(const std::string& name, const std::string& value) { params_[name] = value; }
    const std::string* FindParam(const std::string& name) const {
        auto it = params_.find(name);
        return it == params_.end() ? nullptr : &it->second;
    }
    void ClearParams() { params_.clear(); }

private:
    std::string script_path_;   // e.g. "assets/scripts/spinner.py"
    bool        enabled_ = true;
    std::string status_;        // runtime-only
    std::map<std::string, std::string> params_;  // authored param overrides
};

} // namespace schizo::scene
