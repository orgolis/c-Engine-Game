#include "editor_extensions.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <cstdint>

namespace fs = std::filesystem;

namespace schizo::editor {
namespace {

// Seconds between disk stats. Same cadence ScriptSystem uses; frequent enough
// that a save feels immediate, rare enough that it is not a stat per frame.
constexpr float kWatchInterval = 0.5f;

uint64_t mtime_of(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    return static_cast<uint64_t>(t.time_since_epoch().count());
}

}  // namespace

void ExtensionSystem::load_all(const fs::path& dir, CommandRegistry& cmds, const ScriptApi& api) {
    dir_ = dir;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;   // no extensions: normal

    std::vector<fs::path> found;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (!has_host(entry.path().extension().string())) continue;
        found.push_back(entry.path());
    }
    // Alphabetical, so load order is the same on every machine. Extensions can
    // call each other's commands, and an order that depends on the filesystem
    // would make that work here and fail somewhere else.
    std::sort(found.begin(), found.end());

    for (const fs::path& p : found) {
        LoadedExtension e;
        e.name = p.stem().string();
        e.path = p.string();
        exts_.push_back(std::move(e));
        load_one(exts_.size() - 1, cmds, api);
    }
    spdlog::info("[ext] {} editor extension(s) from {}", exts_.size(), dir.string());
}

void ExtensionSystem::load_one(size_t index, CommandRegistry& cmds, const ScriptApi& api) {
    if (index >= exts_.size()) return;
    LoadedExtension& e = exts_[index];

    // ORDER MATTERS: drop the previous registration before re-running, or the
    // reload appends a second copy of every command (see the header).
    cmds.remove_owned_by(e.name);
    e.commands.clear();
    e.error.clear();
    e.inst.reset();
    e.mtime = mtime_of(e.path);

    const std::string ext = fs::path(e.path).extension().string();
    auto host = hosts_.find(ext);
    if (host == hosts_.end() || !host->second) {
        e.error = "no backend registered for " + ext;
        return;
    }
    e.language = host->second->language();

    std::string err;
    e.inst = host->second->create(e.path, &api, err);
    if (!e.inst) {
        // A broken extension stays in the list with its error rather than
        // vanishing. An extension that disappears when it fails to compile is
        // indistinguishable from one that was never there.
        e.error = err.empty() ? "failed to load" : err;
        spdlog::warn("[ext] {}: {}", e.name, e.error);
        return;
    }

    loading_ = index;
    const bool ok = e.inst->start(0, err);   // entity 0: an extension has no entity
    loading_ = static_cast<size_t>(-1);
    if (!ok) {
        e.error = err.empty() ? "on_start failed" : err;
        spdlog::warn("[ext] {}: {}", e.name, e.error);
        return;
    }
    spdlog::info("[ext] {} ({}) registered {} command(s)", e.name, e.language, e.commands.size());
}

void ExtensionSystem::reload(size_t index, CommandRegistry& cmds, const ScriptApi& api) {
    load_one(index, cmds, api);
}

void ExtensionSystem::reload_all(CommandRegistry& cmds, const ScriptApi& api) {
    for (size_t i = 0; i < exts_.size(); ++i) load_one(i, cmds, api);
}

void ExtensionSystem::poll(CommandRegistry& cmds, const ScriptApi& api, float dt) {
    watch_timer_ += dt;
    if (watch_timer_ < kWatchInterval) return;
    watch_timer_ = 0.0f;

    for (size_t i = 0; i < exts_.size(); ++i) {
        const uint64_t now = mtime_of(exts_[i].path);
        // 0 means the file is gone. Its commands go, but the entry stays and
        // says so -- silently unregistering would look like the editor losing
        // commands for no reason.
        if (now == 0) {
            if (!exts_[i].inst && exts_[i].error == "file no longer exists") continue;
            cmds.remove_owned_by(exts_[i].name);
            exts_[i].commands.clear();
            exts_[i].inst.reset();
            exts_[i].error = "file no longer exists";
            continue;
        }
        if (now != exts_[i].mtime) {
            spdlog::info("[ext] {} changed on disk - reloading", exts_[i].name);
            load_one(i, cmds, api);
        }
    }
}

bool ExtensionSystem::register_command_from_script(CommandRegistry& cmds, const char* title,
                                                   const char* category, const char* token) {
    if (loading_ >= exts_.size()) return false;      // not inside an extension load
    if (!title || !*title || !token || !*token) {
        note_error("register_command needs a non-empty title and token");
        return false;
    }

    LoadedExtension& e = exts_[loading_];
    const size_t     idx = loading_;
    const std::string tok(token);
    const std::string cat = (category && *category) ? category : "Script";

    cmds.add_owned(title, cat, "", [this, idx, tok] {
        if (idx >= exts_.size()) return;
        LoadedExtension& owner = exts_[idx];
        if (!owner.inst) return;
        std::string err;
        running_ = idx;
        const bool ok = owner.inst->invoke(tok.c_str(), err);
        running_ = static_cast<size_t>(-1);
        if (!ok) {
            // Surfaced on the extension AND in the log. A command that fails
            // quietly is the worst outcome here: the user clicks, nothing
            // happens, and there is nowhere to look.
            owner.error = err;
            spdlog::warn("[ext] {} command '{}' failed: {}", owner.name, tok, err);
        }
    }, e.name);

    e.commands.push_back(title);
    return true;
}

void ExtensionSystem::note_error(const std::string& message) {
    const size_t idx = loading_ < exts_.size() ? loading_ : running_;
    if (idx < exts_.size()) exts_[idx].error = message;
    spdlog::warn("[ext] {}", message);
}

size_t ExtensionSystem::total_commands() const {
    size_t n = 0;
    for (const LoadedExtension& e : exts_) n += e.commands.size();
    return n;
}

size_t ExtensionSystem::failed_count() const {
    size_t n = 0;
    for (const LoadedExtension& e : exts_) if (!e.error.empty()) ++n;
    return n;
}

bool ExtensionSystem::write_template(const fs::path& path, std::string* err) {
    std::error_code ec;
    if (fs::exists(path, ec)) {
        if (err) *err = "already exists: " + path.string();
        return false;
    }
    fs::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (err) *err = "cannot write " + path.string();
        return false;
    }
    // A template that actually does something. An empty stub teaches nobody the
    // two things that are not guessable: that on_start is where you register,
    // and that the third argument is the NAME of the function to call back.
    out <<
        "# A GameWorldshaper editor extension (Phase 4.8).\n"
        "#\n"
        "# Dropped in <project>/editor_scripts/, this loads when the editor starts and\n"
        "# reloads whenever you save the file. Its commands appear in the command\n"
        "# palette (Ctrl+P) and in Window > Extensions.\n"
        "\n"
        "import engine\n"
        "\n"
        "\n"
        "def on_start(e):\n"
        "    # (title, category, token) -- the token is the NAME of the function below\n"
        "    # to run when the command is chosen.\n"
        "    engine.register_command(\"Count Entities\", \"Script\", \"count_entities\")\n"
        "    engine.register_command(\"Select First Entity\", \"Script\", \"select_first\")\n"
        "\n"
        "\n"
        "def count_entities():\n"
        "    engine.set_status(\"This scene has \" + str(engine.entity_count()) + \" entities\")\n"
        "\n"
        "\n"
        "def select_first():\n"
        "    if engine.entity_count() > 0:\n"
        "        engine.select_entity(engine.entity_at(0))\n"
        "        engine.set_status(\"Selected \" + engine.entity_name(engine.selected_entity()))\n"
        "    else:\n"
        "        engine.set_status(\"The scene is empty\")\n"
        "\n"
        "\n"
        "# Anything the editor can do is reachable by name, including built-ins:\n"
        "#     engine.run_command(\"Save Scene\")\n";
    return true;
}

}  // namespace schizo::editor
