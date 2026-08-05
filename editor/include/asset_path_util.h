#pragma once

// Shared asset-path helpers (used by the render mesh loader AND the physics
// mesh-collider loader — both must resolve paths identically or a model
// renders while its collider silently fails, or vice-versa).

#include <algorithm>
#include <cctype>
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

/// Import an asset into the CURRENT PROJECT's `assets/<subdir>/` folder and return
/// a project-relative path ("assets/<subdir>/<file>"). Keeps assets PER-PROJECT
/// and portable: dropping a model that lives outside the project copies it in, so
/// the scene stores a stable project-relative path that always resolves (this is
/// what fixes "applying an OBJ does nothing" — an external file that didn't
/// resolve is now inside the project). If the source already lives inside the
/// project it is referenced in place (no copy). Returns "" if the source can't be
/// found. `source` may be absolute or relative.
inline std::string import_asset_into_project(const std::string& source,
                                             const std::string& subdir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path src = utf8_path(resolve_asset_path(source));
    if (source.empty() || !fs::exists(src, ec)) return {};       // source not found
    src = fs::absolute(src, ec);
    const fs::path src_canon = fs::weakly_canonical(src, ec);

    const fs::path cwd = fs::current_path(ec);                   // = project root (sandbox chdir)

    // Already inside the project tree? Reference it in place (project-relative).
    {
        const fs::path rel = fs::relative(src_canon, cwd, ec);
        const std::string rels = rel.generic_string();
        if (!ec && !rels.empty() && rels.rfind("..", 0) != 0)   // under cwd, not "../..."
            return rels;
    }

    // Outside the project: copy it into assets/<subdir>/<filename>.
    const fs::path dstdir = cwd / "assets" / subdir;
    fs::create_directories(dstdir, ec);
    const fs::path dst = dstdir / src.filename();
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        spdlog::error("[assets] import copy failed '{}' -> '{}': {}",
                      src.string(), dst.string(), ec.message());
        return {};
    }
    // Bring an OBJ's .mtl sidecar along (kept as a pair for future material import).
    std::string ext = src.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
    if (ext == ".obj") {
        fs::path mtl = src; mtl.replace_extension(".mtl");
        if (fs::exists(mtl, ec))
            fs::copy_file(mtl, dstdir / mtl.filename(), fs::copy_options::overwrite_existing, ec);
    }
    spdlog::info("[assets] imported '{}' into project assets/{}/", src.filename().string(), subdir);
    return (fs::path("assets") / subdir / src.filename()).generic_string();
}

} // namespace schizo::editor
