#pragma once
// ============================================================================
// editor_extensions — user-written editor extensions (Phase 4.8)
//
// Scripts in a project's editor_scripts/ folder that add commands to the editor
// itself. This is the item the command palette was built for: command_palette.h
// says the registry exists so that "an agent or a script can drive the editor
// without a bespoke hook per feature", and until now nothing could.
//
// WHY THIS IS NOT ScriptSystem. ScriptSystem is per-ENTITY and play-mode-only:
// it instantiates a script when its entity is first seen while playing and tears
// every instance down on Stop. An editor extension is the opposite on both
// counts -- it has no entity and it must be alive in edit mode, which is when
// the editor is being used. The two share the ScriptHost backends (all three
// languages) and share nothing else, so this is a sibling rather than a flag on
// the existing loop.
//
// AN EXTENSION IS AN ORDINARY SCRIPT. It uses the same on_start(e) hook every
// gameplay script uses, called once with entity 0, and registers its commands
// from there. Nothing in the three backends had to learn a new entry point for
// LOADING -- only for calling back in, which is ScriptInstance::invoke.
//
// THE FAILURE THIS FILE IS SHAPED AROUND is reload. Hot reload re-runs on_start,
// which registers the commands again; without removing the previous set first,
// every reload appends a duplicate of each command. Every duplicate still works,
// so nothing appears broken -- the palette just slowly fills with copies. Hence
// CommandRegistry::owner and remove_owned_by, and hence the ordering rule that
// removal happens BEFORE the script is re-run, never after.
// ============================================================================

#include "command_palette.h"
#include "script_system.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace schizo::editor {

struct LoadedExtension {
    std::string name;                    // file stem; doubles as the command owner tag
    std::string path;
    std::string language;                // from the backend, for the panel
    std::string error;                   // empty == healthy
    std::vector<std::string> commands;   // titles this extension registered
    uint64_t    mtime = 0;
    std::unique_ptr<ScriptInstance> inst;
};

class ExtensionSystem {
public:
    /// Map a file extension (".py") to a backend. Same hosts as ScriptSystem;
    /// separate instances, because a VM shared between an editor extension and a
    /// gameplay script would let one clobber the other's globals.
    void register_host(const std::string& ext, std::unique_ptr<ScriptHost> host) {
        hosts_[ext] = std::move(host);
    }
    bool has_host(const std::string& ext) const { return hosts_.count(ext) != 0; }

    /// Load every script in `dir` that has a registered backend. Safe to call
    /// when the folder does not exist -- most projects have no extensions.
    void load_all(const std::filesystem::path& dir, CommandRegistry& cmds, const ScriptApi& api);

    /// Re-run any extension whose file changed on disk. Call once per frame;
    /// the stat is throttled internally.
    void poll(CommandRegistry& cmds, const ScriptApi& api, float dt);

    void reload(size_t index, CommandRegistry& cmds, const ScriptApi& api);
    void reload_all(CommandRegistry& cmds, const ScriptApi& api);

    /// Where register_command lands while an extension's on_start is running.
    /// Returns false when nothing is loading -- a gameplay script that somehow
    /// reached this entry cannot register editor commands.
    bool register_command_from_script(CommandRegistry& cmds, const char* title,
                                      const char* category, const char* token);

    /// Report an error against the extension currently loading (or the one whose
    /// command is running), so a failure shows up in the panel next to the
    /// script that caused it instead of only in the log.
    void note_error(const std::string& message);

    const std::vector<LoadedExtension>& extensions() const { return exts_; }
    const std::filesystem::path&        dir() const { return dir_; }
    size_t total_commands() const;
    size_t failed_count() const;

    /// Write a working starter extension to `path`. Returns false if it exists.
    static bool write_template(const std::filesystem::path& path, std::string* err);

private:
    void load_one(size_t index, CommandRegistry& cmds, const ScriptApi& api);

    std::vector<LoadedExtension> exts_;
    std::unordered_map<std::string, std::unique_ptr<ScriptHost>> hosts_;
    std::filesystem::path dir_;
    float  watch_timer_ = 0.0f;
    size_t loading_     = static_cast<size_t>(-1);   // index whose on_start is running
    size_t running_     = static_cast<size_t>(-1);   // index whose command is running
};

/// Draw the Extensions window: what loaded, what each registered, what failed,
/// and a Reload button. Declared here but implemented in extensions_panel.cpp so
/// this header stays ImGui-free -- extension_check compiles the loader without
/// linking a UI, which is the only reason the loader is testable at all.
void ShowExtensionsPanel(bool* open, ExtensionSystem& sys, CommandRegistry& cmds,
                         const ScriptApi& api);

}  // namespace schizo::editor
