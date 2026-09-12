#include "gws/platform/user_dirs.h"

#include <cstdlib>
#include <string>

namespace gws::platform {
namespace {

// A variable counts only when it is set and non-empty.
const char* get(const EnvLookup& env, const char* name) {
    const char* v = env(name);
    return (v && *v) ? v : nullptr;
}

// Posix values must be absolute. Checked on the string, not with
// path::is_absolute(), because that asks the HOST's rules and "/home/u" is not
// absolute on Windows, where this is also tested.
const char* get_abs_posix(const EnvLookup& env, const char* name) {
    const char* v = get(env, name);
    return (v && v[0] == '/') ? v : nullptr;
}

std::filesystem::path resolve_windows(UserDir which, const EnvLookup& env) {
    namespace fs = std::filesystem;
    switch (which) {
        case UserDir::Config:
            if (const char* v = get(env, "APPDATA")) return fs::path(v) / "GameWorldshaper";
            if (const char* v = get(env, "USERPROFILE")) return fs::path(v) / ".gameworldshaper";
            return {};
        case UserDir::Cache:
            if (const char* v = get(env, "LOCALAPPDATA")) return fs::path(v) / "GameWorldshaper" / "cache";
            return {};
        case UserDir::Diagnostics:
            if (const char* v = get(env, "LOCALAPPDATA")) return fs::path(v) / "GameWorldshaper" / "diagnostics";
            return {};
    }
    return {};
}

std::filesystem::path resolve_posix(UserDir which, const EnvLookup& env) {
    namespace fs = std::filesystem;
    // The lowercase name matches project.cpp's config_dir(), which already
    // keeps recent projects in ~/.config/gameworldshaper.
    const char* home = get_abs_posix(env, "HOME");
    switch (which) {
        case UserDir::Config:
            if (const char* v = get_abs_posix(env, "XDG_CONFIG_HOME")) return fs::path(v) / "gameworldshaper";
            if (home) return fs::path(home) / ".config" / "gameworldshaper";
            return {};
        case UserDir::Cache:
            if (const char* v = get_abs_posix(env, "XDG_CACHE_HOME")) return fs::path(v) / "gameworldshaper";
            if (home) return fs::path(home) / ".cache" / "gameworldshaper";
            return {};
        case UserDir::Diagnostics:
            // Logs and crash reports are "state" in XDG terms: worth keeping
            // across restarts, not important enough for the config directory.
            if (const char* v = get_abs_posix(env, "XDG_STATE_HOME"))
                return fs::path(v) / "gameworldshaper" / "diagnostics";
            if (home) return fs::path(home) / ".local" / "state" / "gameworldshaper" / "diagnostics";
            return {};
    }
    return {};
}

}  // namespace

std::filesystem::path resolve_user_dir(UserDir which, HostOs os, const EnvLookup& env) {
    return os == HostOs::Windows ? resolve_windows(which, env) : resolve_posix(which, env);
}

std::filesystem::path user_dir(UserDir which) {
#ifdef _WIN32
    constexpr HostOs os = HostOs::Windows;
#else
    constexpr HostOs os = HostOs::Posix;
#endif
    return resolve_user_dir(which, os, [](const char* name) { return std::getenv(name); });
}

}  // namespace gws::platform
