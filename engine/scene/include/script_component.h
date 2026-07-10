#pragma once

#include "entity.h"
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

private:
    std::string script_path_;   // e.g. "assets/scripts/spinner.py"
    bool        enabled_ = true;
    std::string status_;        // runtime-only
};

} // namespace schizo::scene
