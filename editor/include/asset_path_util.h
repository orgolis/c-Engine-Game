#pragma once

// Shared asset-path helpers (used by the render mesh loader AND the physics
// mesh-collider loader — both must resolve paths identically or a model
// renders while its collider silently fails, or vice-versa).

#include <filesystem>
#include <string>
#include <system_error>

#include <spdlog/spdlog.h>

namespace schizo::editor {

/// Editor paths arrive as UTF-8 (ImGui / the asset browser; scene files store
/// the bytes verbatim). A narrow-string std::ifstream / fopen goes through the
/// ANSI codepage on Windows, so non-ASCII filenames (e.g. Cyrillic) fail to
/// open. Constructing the path from UTF-8 routes through the wide Win32 API.
inline std::filesystem::path utf8_path(const std::string& s) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

/// Resolve an asset path independent of the process working directory. Scene
/// files store repo-root-relative paths ("assets/models/X.obj"), but the editor
/// is often launched from build-editor/bin — which used to fail every model
/// load (render → primitive cube; physics → missing collider). Try the path as
/// given, then walk up a few levels.
inline std::string resolve_asset_path(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || fs::exists(utf8_path(path), ec)) return path;
    if (utf8_path(path).is_absolute()) return path;
    std::string prefix;
    for (int up = 1; up <= 8; ++up) {   // deeper trees (project sandbox, build/bin) need more hops
        prefix += "../";
        const std::string candidate = prefix + path;
        if (fs::exists(utf8_path(candidate), ec)) {
            spdlog::info("[assets] resolved '{}' -> '{}' (cwd-independent)", path, candidate);
            return candidate;
        }
    }
    return path;   // let the caller report the failure with the original path
}

} // namespace schizo::editor
