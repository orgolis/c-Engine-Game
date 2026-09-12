#pragma once

#include <filesystem>
#include <functional>

namespace gws::platform {

// Where the engine keeps per-user files. The kinds have different lifetimes,
// which is why Linux (XDG) gives each its own root and why they must not
// collapse into one folder: deleting a cache must never delete settings.
enum class UserDir {
    Config,       // settings the user chose: render settings
    Cache,        // regenerable: the Vulkan pipeline cache
    Diagnostics,  // logs, crash reports, hang reports
};

enum class HostOs { Windows, Posix };

// Returns the value of an environment variable, or nullptr when it is unset.
using EnvLookup = std::function<const char*(const char*)>;

// Pure resolution: no filesystem access and no real environment, so it can be
// tested for both OSes on either one. Returns an EMPTY path when nothing
// usable is set, and leaves the fallback to the caller.
//
// Windows paths are exactly what the engine has always used, so existing
// settings, caches and crash reports stay where they are. Posix follows the
// XDG base-directory spec, including its rule that a relative or empty XDG_*
// value is invalid and must be ignored.
std::filesystem::path resolve_user_dir(UserDir which, HostOs os, const EnvLookup& env);

// The real thing: this host's OS and real environment. Returns an empty path
// only when no home directory is known at all.
std::filesystem::path user_dir(UserDir which);

}  // namespace gws::platform
